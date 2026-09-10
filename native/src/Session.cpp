#include "Session.h"

namespace theta
{
namespace
{
struct PresetNote { int step, pitch, length; };

struct PresetPattern
{
    const PresetNote* notes = nullptr;
    int count = 0;
    juce::String name;
    bool useDrums = false;
};

PresetPattern presetPattern(Session::PatternPreset preset)
{
    static constexpr PresetNote warmPulse[] {{0, 48, 2}, {4, 55, 2}, {8, 60, 2}, {12, 55, 2}};
    static constexpr PresetNote acidSteps[] {{0, 48, 1}, {3, 51, 1}, {6, 55, 1}, {7, 58, 1}, {10, 55, 1}, {13, 63, 1}, {15, 58, 1}};
    static constexpr PresetNote houseKit[] {{0, 48, 1}, {4, 48, 1}, {8, 48, 1}, {12, 48, 1}, {4, 53, 1}, {12, 53, 1},
                                            {2, 58, 1}, {6, 58, 1}, {10, 58, 1}, {14, 58, 1}};
    static constexpr PresetNote breakKit[] {{0, 48, 1}, {3, 48, 1}, {8, 48, 1}, {11, 48, 1}, {4, 53, 1}, {10, 53, 1},
                                            {1, 58, 1}, {3, 58, 1}, {6, 58, 1}, {9, 58, 1}, {12, 58, 1}, {15, 58, 1}};
    static constexpr PresetNote minimalKit[] {{0, 48, 1}, {7, 48, 1}, {12, 48, 1}, {4, 53, 1}, {12, 53, 1}, {2, 58, 1}, {10, 58, 1}, {14, 58, 1}};

    switch (preset)
    {
        case Session::PatternPreset::WarmPulse:  return {warmPulse,  static_cast<int>(std::size(warmPulse)),  "Warm pulse", false};
        case Session::PatternPreset::AcidSteps:  return {acidSteps,  static_cast<int>(std::size(acidSteps)),  "Acid steps", false};
        case Session::PatternPreset::HouseKit:   return {houseKit,   static_cast<int>(std::size(houseKit)),   "House kit", true};
        case Session::PatternPreset::BreakKit:   return {breakKit,   static_cast<int>(std::size(breakKit)),   "Break kit", true};
        case Session::PatternPreset::MinimalKit: return {minimalKit, static_cast<int>(std::size(minimalKit)), "Minimal kit", true};
    }
    return {};
}

void fillMidiClip(te::MidiClip& clip, const PresetPattern& preset, juce::UndoManager& undoManager)
{
    auto& sequence = clip.getSequence();
    sequence.removeAllNotes(&undoManager);
    for (int i = 0; i < preset.count; ++i)
        sequence.addNote(preset.notes[i].pitch, tracktion::core::BeatPosition::fromBeats(preset.notes[i].step * 0.25),
                         tracktion::core::BeatDuration::fromBeats(std::max(1, preset.notes[i].length) * 0.225), 100, 0,
                         &undoManager);
    clip.setName(preset.name);
}

te::Plugin* findPlugin(te::AudioTrack& track, const juce::String& type)
{
    for (auto* plugin : track.pluginList)
        if (plugin != nullptr && plugin->getPluginType() == type)
            return plugin;
    return nullptr;
}

bool trackHasPlugin(te::AudioTrack& track, const juce::String& type)
{
    return findPlugin(track, type) != nullptr;
}

DrumDevice* findDrumDevice(te::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (auto* drums = dynamic_cast<DrumDevice*>(plugin))
            return drums;
    return nullptr;
}

te::AutomatableParameter* activeParameterAt(te::Plugin& plugin, int index)
{
    int active = 0;
    for (auto* parameter : plugin.getAutomatableParameters())
        if (parameter != nullptr && parameter->isParameterActive())
        {
            if (active == index)
                return parameter;
            ++active;
        }
    return nullptr;
}

tracktion::core::TimeRange firstFreeDuplicateRange(te::Clip& source)
{
    const auto old = source.getPosition().time;
    const auto length = old.getLength();
    auto start = old.getEnd();
    auto* owner = source.getClipTrack();
    if (owner == nullptr)
        return {start, start + length};

    bool moved = false;
    do
    {
        moved = false;
        const tracktion::core::TimeRange candidate {start, start + length};
        for (auto* clip : owner->getClips())
        {
            if (clip == nullptr || clip == &source)
                continue;
            const auto occupied = clip->getPosition().time;
            if (candidate.overlaps(occupied))
            {
                start = occupied.getEnd();
                moved = true;
                break;
            }
        }
    }
    while (moved);

    return {start, start + length};
}

juce::Result ensurePlugin(te::Edit& edit, te::AudioTrack& track, const juce::String& type,
                          int insertIndex, te::Plugin*& plugin, bool& changed)
{
    if ((plugin = findPlugin(track, type)) != nullptr)
        return juce::Result::ok();

    auto created = edit.getPluginCache().createNewPlugin(type, {});
    if (created == nullptr)
        return juce::Result::fail("The target track device could not be created.");

    plugin = created.get();
    track.pluginList.insertPlugin(created, juce::jlimit(0, track.pluginList.size(), insertIndex), nullptr);
    changed = true;
    return juce::Result::ok();
}

juce::Result switchTrackInstrument(te::Edit& edit, te::AudioTrack& track, bool useDrums, bool& changed)
{
    te::Plugin* selected = nullptr;
    const auto selectedType = useDrums ? juce::String(DrumDevice::xmlTypeName)
                                      : juce::String(te::FourOscPlugin::xmlTypeName);
    auto result = ensurePlugin(edit, track, selectedType, 0, selected, changed);
    if (result.failed())
        return result;

    if (auto* fourOsc = findPlugin(track, te::FourOscPlugin::xmlTypeName))
        if (fourOsc->isEnabled() == useDrums)
        {
            fourOsc->setEnabled(!useDrums);
            changed = true;
        }

    if (auto* drumDevice = findDrumDevice(track))
        if (drumDevice->isEnabled() != useDrums)
        {
            drumDevice->setEnabled(useDrums);
            changed = true;
        }

    if (selected != nullptr && !selected->isEnabled())
    {
        selected->setEnabled(true);
        changed = true;
    }

    return juce::Result::ok();
}
}

Session::Session()
{
    engine.getPluginManager().createBuiltInType<UtilityDevice>();
    engine.getPluginManager().createBuiltInType<DrumDevice>();
    engine.getPluginManager().createBuiltInType<ThetaSpaceDevice>();
    edit = te::createEmptyEdit(engine, {});
    edit->state.setProperty("thetaFormatVersion", 1, nullptr);
    edit->tempoSequence.getTempo(0)->setBpm(120.0);
    edit->ensureNumberOfAudioTracks(2);
    auto* track = te::getAudioTracks(*edit)[0];
    track->setName("Pattern synth");
    auto synthPlugin = edit->getPluginCache().createNewPlugin(te::FourOscPlugin::xmlTypeName, {});
    synth = dynamic_cast<te::FourOscPlugin*>(synthPlugin.get());
    track->pluginList.insertPlugin(synthPlugin, 0, nullptr);
    auto drumPlugin = edit->getPluginCache().createNewPlugin(DrumDevice::xmlTypeName, {});
    drums = dynamic_cast<DrumDevice*>(drumPlugin.get());
    drums->setEnabled(false);
    track->pluginList.insertPlugin(drumPlugin, 1, nullptr);
    auto device = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    utility = dynamic_cast<UtilityDevice*>(device.get());
    track->pluginList.insertPlugin(device, 2, nullptr);
    utility->gain().setParameter(-12.0f, juce::dontSendNotification);
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
    patternClip = track->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
    patternClipID = patternClip->itemID;
    auto* audioTrack = te::getAudioTracks(*edit)[1];
    audioTrack->setName("Audio 1");
    auto audioDevice = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    audioUtility = dynamic_cast<UtilityDevice*>(audioDevice.get());
    audioTrack->pluginList.insertPlugin(audioDevice, 0, nullptr);
    refreshLoop();
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
}

juce::Result Session::importAudio(const juce::File& file)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    te::AudioFile audio(engine, file);
    const auto duration = audio.getLength();
    if (duration <= 0.0)
        return juce::Result::fail("This file could not be read as audio.");

    auto* track = te::getAudioTracks(*edit)[1];
    tracktion::core::TimePosition start;
    for (auto* existing : track->getClips())
        start = std::max(start, existing->getPosition().time.getEnd());
    return importAudioAt(file, 1, start.inSeconds());
}

juce::Result Session::importAudioAt(const juce::File& file, int trackIndex, double startSeconds)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!std::isfinite(startSeconds) || startSeconds < 0.0)
        return juce::Result::fail("Invalid audio drop position.");
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop audio on an audio track.");

    te::AudioFile audio(engine, file);
    const auto duration = audio.getLength();
    if (duration <= 0.0)
        return juce::Result::fail("This file could not be read as audio.");

    auto* track = tracks[trackIndex];
    const auto start = tracktion::core::TimePosition::fromSeconds(startSeconds);
    edit->getUndoManager().beginNewTransaction("Import audio");
    auto clip = track->insertWaveClip(file.getFileNameWithoutExtension(), file,
        {{start, start + tracktion::core::TimeDuration::fromSeconds(duration)}, {}}, false);
    if (clip == nullptr)
        return juce::Result::fail("The audio clip could not be added.");
    refreshLoop();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::togglePlayback()
{
    auto& transport = edit->getTransport();
    if (transport.isPlaying()) transport.stop(false, false);
    else transport.play(false);
}

void Session::stop()
{
    edit->getTransport().stop(false, false);
    edit->getTransport().setPosition({});
}

bool Session::hasNote(int step, int pitch) const
{
    for (auto* note : pattern().getSequence().getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - step * 0.25) < 0.0001)
            return true;
    return false;
}

void Session::beginNoteGesture() { edit->getUndoManager().beginNewTransaction("Draw notes"); }
void Session::endNoteGesture() { edit->getUndoManager().beginNewTransaction(); }

void Session::setNote(int step, int pitch, bool enabled)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (step < 0 || step >= steps || pitch < lowestNote || pitch >= lowestNote + pitches)
        return;
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - step * 0.25) < 0.0001)
        {
            if (!enabled)
            {
                sequence.removeNote(*note, undoManager);
                markModified();
            }
            sendSynchronousChangeMessage();
            return;
        }
    if (enabled)
    {
        sequence.addNote(pitch, tracktion::core::BeatPosition::fromBeats(step * 0.25),
                         tracktion::core::BeatDuration::fromBeats(0.225), 100, 0, undoManager);
        markModified();
    }
    sendSynchronousChangeMessage();
}

void Session::clearPattern()
{
    if (pattern().getSequence().getNumNotes() == 0) return;
    edit->getUndoManager().beginNewTransaction("Clear pattern");
    pattern().getSequence().removeAllNotes(&edit->getUndoManager());
    markModified();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
}

void Session::applyPatternPreset(PatternPreset preset)
{
    const auto data = presetPattern(preset);
    edit->getUndoManager().beginNewTransaction("Load " + data.name);
    setPatternInstrument(data.useDrums);
    fillMidiClip(pattern(), data, edit->getUndoManager());
    markModified();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
}

juce::Result Session::insertPatternPreset(PatternPreset preset, int trackIndex, double startSeconds)
{
    if (!std::isfinite(startSeconds) || startSeconds < 0.0)
        return juce::Result::fail("Invalid pattern drop position.");
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop clips on a track lane.");
    const auto data = presetPattern(preset);
    const auto start = tracktion::core::TimePosition::fromSeconds(startSeconds);
    const auto end = start + tracktion::core::TimeDuration::fromSeconds(
        edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0)).inSeconds());
    edit->getUndoManager().beginNewTransaction("Add " + data.name);
    auto* track = tracks[trackIndex];
    if (trackIndex == 0)
        setPatternInstrument(data.useDrums);
    else
    {
        bool instrumentChanged = false;
        const auto result = switchTrackInstrument(*edit, *track, data.useDrums, instrumentChanged);
        if (result.failed())
            return result;
    }
    auto clip = track->insertMIDIClip(data.name, {start, end}, nullptr);
    if (clip == nullptr)
        return juce::Result::fail("The pattern clip could not be added.");
    fillMidiClip(*clip, data, edit->getUndoManager());
    refreshLoop();
    markModified();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::insertInstrumentClip(Instrument instrument, int trackIndex, double startSeconds)
{
    if (!std::isfinite(startSeconds) || startSeconds < 0.0)
        return juce::Result::fail("Invalid instrument drop position.");
    if (instrument == Instrument::Utility)
        return addInstrument(instrument, trackIndex);

    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop instruments on a track lane.");

    const auto useDrums = instrument == Instrument::Drums;
    const auto name = useDrums ? juce::String("Theta Drums") : juce::String("4OSC synth");
    const auto start = tracktion::core::TimePosition::fromSeconds(startSeconds);
    const auto end = start + tracktion::core::TimeDuration::fromSeconds(
        edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0)).inSeconds());
    auto* track = tracks[trackIndex];

    edit->getUndoManager().beginNewTransaction("Add " + name);
    if (trackIndex == 0)
        setPatternInstrument(useDrums);
    else
    {
        bool instrumentChanged = false;
        const auto result = switchTrackInstrument(*edit, *track, useDrums, instrumentChanged);
        if (result.failed())
            return result;
    }

    auto clip = track->insertMIDIClip(name, {start, end}, nullptr);
    if (clip == nullptr)
        return juce::Result::fail("The instrument clip could not be added.");
    patternClip = clip.get();
    patternClipID = patternClip->itemID;
    refreshLoop();
    markModified();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::setPatternInstrument(bool useDrums)
{
    if (synth) synth->setEnabled(!useDrums);
    if (drums) drums->setEnabled(useDrums);
    edit->state.setProperty("thetaPatternInstrument", useDrums ? "drums" : "synth", &edit->getUndoManager());
}

bool Session::isPatternDrums() const
{
    auto* track = patternClip != nullptr ? patternClip->getClipTrack() : nullptr;
    if (track == nullptr) return drums != nullptr && drums->isEnabled();
    auto* audioTrack = dynamic_cast<te::AudioTrack*>(track);
    if (audioTrack == nullptr) return false;
    if (auto* drumDevice = findDrumDevice(*audioTrack))
        return drumDevice->isEnabled();
    return false;
}

juce::Result Session::selectPatternClip(te::EditItemID id)
{
    auto* midi = dynamic_cast<te::MidiClip*>(findClip(id));
    if (midi == nullptr) return juce::Result::fail("Select a MIDI clip to edit notes.");
    patternClip = midi;
    patternClipID = id;
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::addAudioEffect(AudioEffect effect, int trackIndex)
{
    const char* type = nullptr;
    juce::String name;
    switch (effect)
    {
        case AudioEffect::Equaliser:  type = te::EqualiserPlugin::xmlTypeName;  name = "EQ"; break;
        case AudioEffect::Reverb:     type = te::ReverbPlugin::xmlTypeName;     name = "Reverb"; break;
        case AudioEffect::Delay:      type = te::DelayPlugin::xmlTypeName;      name = "Delay"; break;
        case AudioEffect::Compressor: type = te::CompressorPlugin::xmlTypeName; name = "Compressor"; break;
        case AudioEffect::ThetaSpace: type = ThetaSpaceDevice::xmlTypeName;     name = "Theta Space"; break;
    }

    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop audio effects on a track.");

    auto* track = tracks[trackIndex];
    edit->getUndoManager().beginNewTransaction("Add " + name);
    auto plugin = edit->getPluginCache().createNewPlugin(type, {});
    if (plugin == nullptr)
        return juce::Result::fail(name + " could not be created.");
    track->pluginList.insertPlugin(plugin, track->pluginList.size(), nullptr);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::addInstrument(Instrument instrument, int trackIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop instruments on a track.");

    auto* track = tracks[trackIndex];
    const char* type = nullptr;
    juce::String name;
    switch (instrument)
    {
        case Instrument::FourOsc: type = te::FourOscPlugin::xmlTypeName; name = "4OSC"; break;
        case Instrument::Drums:   type = DrumDevice::xmlTypeName;        name = "Theta Drums"; break;
        case Instrument::Utility: type = UtilityDevice::xmlTypeName;     name = "Utility"; break;
    }

    edit->getUndoManager().beginNewTransaction("Add " + name);
    bool changed = false;
    if (instrument == Instrument::Utility)
    {
        te::Plugin* plugin = nullptr;
        const auto result = ensurePlugin(*edit, *track, type, track->pluginList.size(), plugin, changed);
        if (result.failed())
            return juce::Result::fail(name + " could not be created.");
    }
    else
    {
        const auto result = switchTrackInstrument(*edit, *track, instrument == Instrument::Drums, changed);
        if (result.failed())
            return juce::Result::fail(name + " could not be created.");
    }
    edit->getUndoManager().beginNewTransaction();
    if (changed)
        markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

int Session::trackCount() const
{
    return te::getAudioTracks(*edit).size();
}

juce::String Session::trackName(int track) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return {};
    return tracks[track]->getName();
}

juce::Result Session::addAudioTrack()
{
    const auto tracks = te::getAudioTracks(*edit);
    edit->getUndoManager().beginNewTransaction("Add audio track");
    auto newTrack = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr, false);
    if (newTrack == nullptr)
        return juce::Result::fail("Could not create audio track.");
    newTrack->setName("Audio " + juce::String(tracks.size()));
    auto audioDevice = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    newTrack->pluginList.insertPlugin(audioDevice, 0, nullptr);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::removeAudioTrack(int track)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (track <= 0 || !juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("Select an audio track to remove.");
    if (tracks.size() <= 2)
        return juce::Result::fail("Keep at least one audio track.");
    const auto removingEditedPatternTrack = patternClip != nullptr && patternClip->getClipTrack() == tracks[track];
    edit->getUndoManager().beginNewTransaction("Remove audio track");
    edit->deleteTrack(tracks[track]);
    if (removingEditedPatternTrack)
    {
        patternClip = nullptr;
        patternClipID = {};
        ensureEditablePatternClip();
    }
    if (track == 1)
        audioUtility = nullptr;
    const auto refreshed = te::getAudioTracks(*edit);
    if (audioUtility == nullptr && refreshed.size() > 1)
        for (auto plugin : refreshed[1]->pluginList)
            if (auto* device = dynamic_cast<UtilityDevice*>(plugin))
                audioUtility = device;
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

std::vector<Session::DeviceSlot> Session::deviceSlots(int track) const
{
    std::vector<DeviceSlot> slots;
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return slots;
    for (auto* plugin : tracks[track]->pluginList)
    {
        if (plugin == nullptr) continue;
        const auto type = plugin->getPluginType();
        const auto coreStarterDevice = track == 0 && (type == UtilityDevice::xmlTypeName
            || type == te::FourOscPlugin::xmlTypeName || type == DrumDevice::xmlTypeName);
        slots.push_back({plugin->getDisplayName(), type, plugin->isEnabled(),
                         !coreStarterDevice && type != UtilityDevice::xmlTypeName});
    }
    return slots;
}

std::vector<Session::DeviceParameter> Session::deviceParameters(int track, int slot) const
{
    std::vector<DeviceParameter> parameters;
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return parameters;
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return parameters;

    for (auto* parameter : plugin->getAutomatableParameters())
    {
        if (parameter == nullptr || !parameter->isParameterActive())
            continue;
        const auto range = parameter->getValueRange();
        if (!std::isfinite(range.getStart()) || !std::isfinite(range.getEnd()) || range.getLength() <= 0.0f)
            continue;
        parameters.push_back({parameter->getParameterShortName(18),
                              parameter->getCurrentValueAsStringWithLabel(),
                              parameter->getCurrentValue(),
                              range.getStart(),
                              range.getEnd(),
                              parameter->isDiscrete()});
    }
    return parameters;
}

juce::Result Session::beginDeviceParameterGesture(int track, int slot, int parameterIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return juce::Result::fail("Select a device first.");
    auto* parameter = activeParameterAt(*plugin, parameterIndex);
    if (parameter == nullptr) return juce::Result::fail("Select a parameter first.");
    parameter->parameterChangeGestureBegin();
    return juce::Result::ok();
}

juce::Result Session::setDeviceParameter(int track, int slot, int parameterIndex, float value)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return juce::Result::fail("Select a device first.");
    auto* parameter = activeParameterAt(*plugin, parameterIndex);
    if (parameter == nullptr) return juce::Result::fail("Select a parameter first.");
    const auto range = parameter->getValueRange();
    const auto next = juce::jlimit(range.getStart(), range.getEnd(), value);
    parameter->setParameter(next, juce::sendNotification);
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::endDeviceParameterGesture(int track, int slot, int parameterIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return juce::Result::fail("Select a device first.");
    auto* parameter = activeParameterAt(*plugin, parameterIndex);
    if (parameter == nullptr) return juce::Result::fail("Select a parameter first.");
    parameter->parameterChangeGestureEnd();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::toggleDeviceEnabled(int track, int slot)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return juce::Result::fail("Select a device first.");
    edit->getUndoManager().beginNewTransaction(plugin->isEnabled() ? "Bypass device" : "Enable device");
    plugin->setEnabled(!plugin->isEnabled());
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::deleteDevice(int track, int slot)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a removable device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    const auto type = plugin->getPluginType();
    const auto coreStarterDevice = track == 0 && (type == UtilityDevice::xmlTypeName
        || type == te::FourOscPlugin::xmlTypeName || type == DrumDevice::xmlTypeName);
    if (plugin == nullptr || coreStarterDevice || type == UtilityDevice::xmlTypeName)
        return juce::Result::fail("Core devices stay in the starter track chain.");
    edit->getUndoManager().beginNewTransaction("Delete device");
    plugin->removeFromParent();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

double Session::tempo() const { return edit->tempoSequence.getTempo(0)->getBpm(); }

void Session::setTempo(double bpm)
{
    if (!std::isfinite(bpm)) return;
    bpm = juce::jlimit(40.0, 240.0, bpm);
    if (bpm == tempo()) return;
    edit->getUndoManager().beginNewTransaction("Change tempo");
    edit->tempoSequence.getTempo(0)->setBpm(juce::jlimit(40.0, 240.0, bpm));
    markModified();
    edit->tempoSequence.updateTempoData();
    // Keep this initial editor exactly one bar; MIDI positions remain in beats.
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
    pattern().setLength(end - tracktion::core::TimePosition{}, true);
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
}

void Session::refreshLoop()
{
    auto end = tracktion::core::TimePosition::fromSeconds(0.0);
    const auto tracks = te::getAudioTracks(*edit);
    for (auto* track : tracks)
        for (auto* clip : track->getClips())
            end = std::max(end, clip->getPosition().time.getEnd());
    if (end <= tracktion::core::TimePosition::fromSeconds(0.0))
        end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
    edit->getTransport().setLoopRange({{}, end});
    edit->getTransport().looping = true;
}

void Session::undo()
{
    refreshAfterUndoRedo(edit->getUndoManager().undo());
}

void Session::redo()
{
    refreshAfterUndoRedo(edit->getUndoManager().redo());
}

void Session::refreshAfterUndoRedo(bool changed)
{
    if (changed) markModified();
    ensureEditablePatternClip();
    edit->tempoSequence.updateTempoData();
    refreshLoop();
    if (changed && edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
}

void Session::ensureEditablePatternClip()
{
    if (auto* midi = dynamic_cast<te::MidiClip*>(findClip(patternClipID)))
    {
        patternClip = midi;
        return;
    }

    const auto tracks = te::getAudioTracks(*edit);
    if (!tracks.isEmpty())
        for (auto* clip : tracks[0]->getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
            {
                patternClip = midi;
                patternClipID = midi->itemID;
                return;
            }

    if (!tracks.isEmpty())
    {
        const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
        patternClip = tracks[0]->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
        if (patternClip != nullptr)
            patternClipID = patternClip->itemID;
    }
}

juce::ValueTree Session::projectSnapshot()
{
    edit->flushState();
    auto snapshot = edit->state.createCopy();
    snapshot.setProperty("thetaSnapshotRevision", changeRevision, nullptr);
    return snapshot;
}

void Session::markModified()
{
    ++changeRevision;
    edit->markAsChanged();
}

juce::Result Session::restoreProject(const juce::ValueTree& state, const juce::File& file)
{
    if (!state.hasType(te::IDs::EDIT) || static_cast<int>(state.getProperty("thetaFormatVersion")) != 1)
        return juce::Result::fail("This is not a supported Theta native project.");
    auto candidate = te::loadEditFromState(engine, state.createCopy());
    if (!candidate) return juce::Result::fail("The project could not be loaded.");
    candidate->editFileRetriever = [file] { return file; };
    const auto tracks = te::getAudioTracks(*candidate);
    if (tracks.size() < 2)
        return juce::Result::fail("This editor requires a pattern track and at least one audio track.");
    te::MidiClip* nextPattern = nullptr;
    UtilityDevice* nextUtility = nullptr;
    UtilityDevice* nextAudioUtility = nullptr;
    te::FourOscPlugin* nextSynth = nullptr;
    DrumDevice* nextDrums = nullptr;
    for (auto* clip : tracks[0]->getClips())
        if (auto* midi = dynamic_cast<te::MidiClip*>(clip)) nextPattern = midi;
    for (auto plugin : tracks[0]->pluginList)
    {
        if (auto* device = dynamic_cast<UtilityDevice*>(plugin)) nextUtility = device;
        if (auto* device = dynamic_cast<te::FourOscPlugin*>(plugin)) nextSynth = device;
        if (auto* device = dynamic_cast<DrumDevice*>(plugin)) nextDrums = device;
    }
    for (auto plugin : tracks[1]->pluginList)
        if (auto* device = dynamic_cast<UtilityDevice*>(plugin)) nextAudioUtility = device;
    if (!nextPattern || !nextUtility || !nextSynth)
        return juce::Result::fail("The project is missing its pattern or synth devices.");
    if (!nextDrums)
    {
        auto device = candidate->getPluginCache().createNewPlugin(DrumDevice::xmlTypeName, {});
        nextDrums = dynamic_cast<DrumDevice*>(device.get());
        if (!nextDrums) return juce::Result::fail("The drum device could not be created.");
        nextDrums->setEnabled(false);
        tracks[0]->pluginList.insertPlugin(device, 1, nullptr);
    }
    if (!nextAudioUtility)
    {
        auto device = candidate->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
        nextAudioUtility = dynamic_cast<UtilityDevice*>(device.get());
        tracks[1]->pluginList.insertPlugin(device, 0, nullptr);
    }
    listeners.call(&Listener::editWillChange);
    stop();
    edit = std::move(candidate);
    patternClip = nextPattern;
    patternClipID = patternClip->itemID;
    utility = nextUtility;
    audioUtility = nextAudioUtility;
    synth = nextSynth;
    drums = nextDrums;
    setPatternInstrument(edit->state.getProperty("thetaPatternInstrument").toString() == "drums");
    projectFile = file;
    savedRevision = ++changeRevision;
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
    refreshLoop();
    listeners.call(&Listener::editDidChange);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::projectSaved(const juce::ValueTree& snapshot, const juce::File& file)
{
    projectFile = file;
    // Edits made while the worker wrote the snapshot must remain unsaved.
    if (static_cast<juce::int64>(snapshot.getProperty("thetaSnapshotRevision")) == changeRevision)
    {
        savedRevision = changeRevision;
        edit->resetChangedStatus();
    }
    sendSynchronousChangeMessage();
}

te::Clip* Session::findClip(te::EditItemID id) const
{
    for (auto* track : te::getAudioTracks(*edit))
        if (auto* clip = track->findClipForID(id))
            return clip;
    return nullptr;
}

te::WaveAudioClip* Session::findAudioClip(te::EditItemID id) const
{
    return dynamic_cast<te::WaveAudioClip*>(findClip(id));
}

juce::Result Session::editClip(te::EditItemID id, ClipGeometry next, ClipGesture gesture)
{
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip first.");
    if (!std::isfinite(next.start) || !std::isfinite(next.end) || !std::isfinite(next.offset)
        || next.start < 0.0 || next.end <= next.start || next.offset < -1.0e-8
        || next.end > te::Edit::getMaximumEditEnd().inSeconds())
        return juce::Result::fail("Invalid clip position.");
    const auto old = clip->getPosition();
    if (std::abs(old.time.getStart().inSeconds() - next.start) < 1.0e-8
        && std::abs(old.time.getEnd().inSeconds() - next.end) < 1.0e-8
        && std::abs(old.offset.inSeconds() - next.offset) < 1.0e-8)
        return juce::Result::ok();
    edit->getUndoManager().beginNewTransaction(gesture == ClipGesture::move ? "Move audio clip" : "Trim audio clip");
    clip->setPosition({{tracktion::core::TimePosition::fromSeconds(next.start),
                       tracktion::core::TimePosition::fromSeconds(next.end)},
                       tracktion::core::TimeDuration::fromSeconds(std::max(0.0, next.offset))});
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::splitClip(te::EditItemID id, double splitTimeSeconds)
{
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip to split.");
    if (!std::isfinite(splitTimeSeconds)) return juce::Result::fail("Invalid split position.");

    const auto old = clip->getPosition();
    const auto split = tracktion::core::TimePosition::fromSeconds(splitTimeSeconds);
    constexpr double minimumSeconds = 0.01;
    if (split <= old.time.getStart() + tracktion::core::TimeDuration::fromSeconds(minimumSeconds)
        || split >= old.time.getEnd() - tracktion::core::TimeDuration::fromSeconds(minimumSeconds))
        return juce::Result::fail("Move the playhead inside the selected clip before splitting.");

    edit->getUndoManager().beginNewTransaction("Split audio clip");
    auto* track = clip->getClipTrack();
    auto* right = track != nullptr ? track->splitClip(*clip, split) : nullptr;
    if (right == nullptr)
        return juce::Result::fail("The right-hand split clip could not be created.");
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::duplicateClip(te::EditItemID id)
{
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip to duplicate.");
    const auto old = clip->getPosition();
    auto* track = clip->getClipTrack();
    if (track == nullptr) return juce::Result::fail("The selected clip is not on a track.");
    edit->getUndoManager().beginNewTransaction("Duplicate clip");
    const auto duplicateRange = firstFreeDuplicateRange(*clip);
    te::Clip* copy = nullptr;
    if (auto* audio = dynamic_cast<te::WaveAudioClip*>(clip))
        copy = track->insertWaveClip(audio->getName() + " copy", audio->getSourceFileReference().getFile(),
            {duplicateRange, old.offset}, false).get();
    else if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
        if (auto midiCopy = track->insertMIDIClip(midi->getName() + " copy",
            duplicateRange, nullptr))
        {
            midiCopy->cloneFrom(midi);
            midiCopy->setPosition({duplicateRange, old.offset});
            copy = midiCopy.get();
        }
    if (copy == nullptr)
        return juce::Result::fail("The duplicate clip could not be created.");
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::deleteClip(te::EditItemID id)
{
    if (auto* clip = findClip(id))
    {
        const auto deletingPattern = clip == patternClip;
        edit->getUndoManager().beginNewTransaction("Delete audio clip");
        clip->removeFromParent();
        if (deletingPattern)
        {
            patternClip = nullptr;
            const auto tracks = te::getAudioTracks(*edit);
            if (!tracks.isEmpty())
            {
                for (auto* existing : tracks[0]->getClips())
                    if (auto* midi = dynamic_cast<te::MidiClip*>(existing))
                    {
                        patternClip = midi;
                        break;
                    }
                if (patternClip == nullptr)
                {
                    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
                    patternClip = tracks[0]->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
                }
                if (patternClip != nullptr)
                    patternClipID = patternClip->itemID;
            }
        }
        refreshLoop();
        edit->getUndoManager().beginNewTransaction();
        markModified();
        sendSynchronousChangeMessage();
    }
}

void Session::toggleTrackMute(int track)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return;
    edit->getUndoManager().beginNewTransaction("Track mute");
    tracks[track]->state.setProperty(te::IDs::mute, !tracks[track]->isMuted(false), &edit->getUndoManager());
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
}

void Session::toggleTrackSolo(int track)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return;
    edit->getUndoManager().beginNewTransaction("Track solo");
    tracks[track]->state.setProperty(te::IDs::solo, !tracks[track]->isSolo(false), &edit->getUndoManager());
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
}
}
