#include "Session.h"

namespace theta
{
namespace
{
const juce::Identifier starterPlaceholderID {"thetaStarterPlaceholder"};

struct PresetNote { int step, pitch, length; };

struct PresetPattern
{
    const PresetNote* notes = nullptr;
    int count = 0;
    juce::String name;
    bool useDrums = false;
    bool useSustainPatch = false;
};

PresetPattern presetPattern(Session::PatternPreset preset)
{
    static constexpr PresetNote warmPulse[] {{0, 48, 2}, {4, 55, 2}, {8, 60, 2}, {12, 55, 2}};
    static constexpr PresetNote acidSteps[] {{0, 48, 1}, {3, 51, 1}, {6, 55, 1}, {7, 58, 1}, {10, 55, 1}, {13, 63, 1}, {15, 58, 1}};
    static constexpr PresetNote arpRun[] {{0, 48, 16}, {0, 52, 16}, {0, 55, 16}, {0, 60, 16}};
    static constexpr PresetNote chordPad[] {{0, 48, 7}, {0, 55, 7}, {0, 60, 7}, {0, 64, 7},
                                            {8, 50, 7}, {8, 57, 7}, {8, 62, 7}, {8, 65, 7}};
    static constexpr PresetNote sirenLead[] {{0, 48, 1}, {1, 55, 1}, {2, 60, 1}, {3, 67, 1}, {4, 72, 2}, {7, 67, 1},
                                             {8, 60, 1}, {9, 55, 1}, {10, 48, 1}, {12, 60, 1}, {14, 67, 1}, {15, 72, 1}};
    static constexpr PresetNote houseKit[] {{0, 48, 1}, {4, 48, 1}, {8, 48, 1}, {12, 48, 1}, {4, 53, 1}, {12, 53, 1},
                                            {2, 58, 1}, {6, 58, 1}, {10, 58, 1}, {14, 58, 1}};
    static constexpr PresetNote breakKit[] {{0, 48, 1}, {3, 48, 1}, {8, 48, 1}, {11, 48, 1}, {4, 53, 1}, {10, 53, 1},
                                            {1, 58, 1}, {3, 58, 1}, {6, 58, 1}, {9, 58, 1}, {12, 58, 1}, {15, 58, 1}};
    static constexpr PresetNote minimalKit[] {{0, 48, 1}, {7, 48, 1}, {12, 48, 1}, {4, 53, 1}, {12, 53, 1}, {2, 58, 1}, {10, 58, 1}, {14, 58, 1}};
    static constexpr PresetNote clapKit[] {{0, 48, 1}, {4, 56, 1}, {8, 48, 1}, {12, 56, 1}, {2, 58, 1}, {6, 58, 1}, {10, 58, 1}, {14, 58, 1}};

    switch (preset)
    {
        case Session::PatternPreset::WarmPulse:  return {warmPulse,  static_cast<int>(std::size(warmPulse)),  "Warm pulse", false, false};
        case Session::PatternPreset::AcidSteps:  return {acidSteps,  static_cast<int>(std::size(acidSteps)),  "Acid steps", false, false};
        case Session::PatternPreset::ArpRun:     return {arpRun,     static_cast<int>(std::size(arpRun)),     "Arp run", false, false};
        case Session::PatternPreset::ChordPad:   return {chordPad,   static_cast<int>(std::size(chordPad)),   "Chord pad", false, true};
        case Session::PatternPreset::SirenLead:  return {sirenLead,  static_cast<int>(std::size(sirenLead)),  "Siren lead", false, false};
        case Session::PatternPreset::HouseKit:   return {houseKit,   static_cast<int>(std::size(houseKit)),   "House kit", true, false};
        case Session::PatternPreset::BreakKit:   return {breakKit,   static_cast<int>(std::size(breakKit)),   "Break kit", true, false};
        case Session::PatternPreset::MinimalKit: return {minimalKit, static_cast<int>(std::size(minimalKit)), "Minimal kit", true, false};
        case Session::PatternPreset::ClapKit:    return {clapKit,    static_cast<int>(std::size(clapKit)),    "Clap kit", true, false};
    }
    return {};
}

juce::Colour presetColour(Session::PatternPreset preset)
{
    switch (preset)
    {
        case Session::PatternPreset::WarmPulse:  return juce::Colour(0xff4f7d8f);
        case Session::PatternPreset::AcidSteps:  return juce::Colour(0xff2f6e78);
        case Session::PatternPreset::ArpRun:     return juce::Colour(0xff77659a);
        case Session::PatternPreset::ChordPad:   return juce::Colour(0xff5f718f);
        case Session::PatternPreset::SirenLead:  return juce::Colour(0xff8f4f67);
        case Session::PatternPreset::HouseKit:   return juce::Colour(0xff657844);
        case Session::PatternPreset::BreakKit:   return juce::Colour(0xff6f7f43);
        case Session::PatternPreset::MinimalKit: return juce::Colour(0xff506d45);
        case Session::PatternPreset::ClapKit:    return juce::Colour(0xff8a7a42);
    }
    return juce::Colour(0xff4b6671);
}

juce::Colour instrumentColour(Session::Instrument instrument)
{
    switch (instrument)
    {
        case Session::Instrument::FourOsc: return juce::Colour(0xff3d6f8b);
        case Session::Instrument::Drums:   return juce::Colour(0xff738044);
        case Session::Instrument::Utility: return juce::Colour(0xff56636c);
    }
    return juce::Colour(0xff4b6671);
}

juce::Colour nextClipColour(juce::Colour current)
{
    static constexpr juce::uint32 palette[] {
        0xff3d6f8b, 0xff738044, 0xff8d5f42, 0xff7a5b8f,
        0xff9b4f67, 0xff4b7f68, 0xff8a7a42, 0xff56636c
    };
    int closest = -1;
    for (int i = 0; i < static_cast<int>(std::size(palette)); ++i)
        if (current == juce::Colour(palette[i]))
        {
            closest = i;
            break;
        }
    return juce::Colour(palette[static_cast<size_t>((closest + 1) % static_cast<int>(std::size(palette)))]);
}

bool effectTypeAndName(Session::AudioEffect effect, const char*& type, juce::String& name)
{
    switch (effect)
    {
        case Session::AudioEffect::Equaliser:  type = te::EqualiserPlugin::xmlTypeName;  name = "EQ"; break;
        case Session::AudioEffect::Reverb:     type = te::ReverbPlugin::xmlTypeName;     name = "Reverb"; break;
        case Session::AudioEffect::Delay:      type = te::DelayPlugin::xmlTypeName;      name = "Delay"; break;
        case Session::AudioEffect::Compressor: type = te::CompressorPlugin::xmlTypeName; name = "Compressor"; break;
        case Session::AudioEffect::ThetaSpace: type = ThetaSpaceDevice::xmlTypeName;     name = "Theta Space"; break;
        case Session::AudioEffect::ThetaBloom: type = ThetaBloomDevice::xmlTypeName;     name = "Theta Bloom"; break;
    }
    return type != nullptr;
}

void resetPluginList(te::PluginList* list)
{
    if (list == nullptr)
        return;
    for (auto* plugin : *list)
        if (plugin != nullptr)
        {
            plugin->midiPanic();
            plugin->reset();
        }
}

void fillMidiClip(te::MidiClip& clip, const PresetPattern& preset, juce::UndoManager& undoManager)
{
    clip.state.removeProperty(starterPlaceholderID, &undoManager);
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

te::FourOscPlugin* findFourOsc(te::AudioTrack& track)
{
    return dynamic_cast<te::FourOscPlugin*>(findPlugin(track, te::FourOscPlugin::xmlTypeName));
}

void setPluginParameter(te::AutomatableParameter::Ptr parameter, float value)
{
    if (parameter == nullptr)
        return;
    const auto range = parameter->getValueRange();
    parameter->setParameter(juce::jlimit(range.getStart(), range.getEnd(), value), juce::sendNotification);
}

void applyChordPadPatch(te::FourOscPlugin& synth, juce::UndoManager& undoManager)
{
    setPluginParameter(synth.ampAttack, 0.18f);
    setPluginParameter(synth.ampDecay, 0.55f);
    setPluginParameter(synth.ampSustain, 78.0f);
    setPluginParameter(synth.ampRelease, 0.85f);
    setPluginParameter(synth.ampVelocity, 55.0f);
    setPluginParameter(synth.filterAttack, 0.12f);
    setPluginParameter(synth.filterDecay, 0.45f);
    setPluginParameter(synth.filterSustain, 62.0f);
    setPluginParameter(synth.filterRelease, 0.75f);
    setPluginParameter(synth.filterFreq, 78.0f);
    setPluginParameter(synth.filterResonance, 12.0f);
    setPluginParameter(synth.filterAmount, 0.08f);
    setPluginParameter(synth.chorusSpeed, 0.65f);
    setPluginParameter(synth.chorusDepth, 7.5f);
    setPluginParameter(synth.chorusWidth, 0.9f);
    setPluginParameter(synth.chorusMix, 0.24f);
    setPluginParameter(synth.reverbSize, 0.48f);
    setPluginParameter(synth.reverbDamping, 0.62f);
    setPluginParameter(synth.reverbWidth, 0.95f);
    setPluginParameter(synth.reverbMix, 0.11f);
    setPluginParameter(synth.masterLevel, -9.0f);

    synth.state.setProperty("voiceMode", 2, &undoManager);
    synth.state.setProperty("voices", 32, &undoManager);
    synth.state.setProperty("filterType", 1, &undoManager);
    synth.state.setProperty("filterSlope", 12, &undoManager);
    synth.state.setProperty("chorusOn", true, &undoManager);
    synth.state.setProperty("reverbOn", true, &undoManager);
    synth.state.setProperty("distortionOn", false, &undoManager);

    if (synth.oscParams.size() >= 4)
    {
        setPluginParameter(synth.oscParams[0]->level, -9.0f);
        setPluginParameter(synth.oscParams[0]->detune, 0.02f);
        setPluginParameter(synth.oscParams[0]->spread, 45.0f);
        setPluginParameter(synth.oscParams[1]->level, -12.0f);
        setPluginParameter(synth.oscParams[1]->fineTune, -8.0f);
        setPluginParameter(synth.oscParams[1]->detune, 0.04f);
        setPluginParameter(synth.oscParams[1]->spread, -55.0f);
        setPluginParameter(synth.oscParams[2]->level, -16.0f);
        setPluginParameter(synth.oscParams[2]->tune, 12.0f);
        setPluginParameter(synth.oscParams[2]->fineTune, 5.0f);
        setPluginParameter(synth.oscParams[2]->spread, 70.0f);
        setPluginParameter(synth.oscParams[3]->level, -100.0f);
    }
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
    engine.getPluginManager().createBuiltInType<ThetaBloomDevice>();
    engine.getPluginManager().createBuiltInType<ThetaArpDevice>();
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
    if (patternClip != nullptr)
    {
        patternClip->setColour(presetColour(PatternPreset::WarmPulse));
        patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
    }
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
    clip->setColour(juce::Colour(0xff4d6975));
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

void Session::panicReset()
{
    te::TransportControl::stopAllTransports(engine, false, true);
    auto& transport = edit->getTransport();
    transport.stop(false, true);
    transport.setPosition({});

    for (auto* track : te::getAudioTracks(*edit))
    {
        resetPluginList(&track->pluginList);
        for (auto* clip : track->getClips())
            resetPluginList(clip->getPluginList());
    }

    transport.freePlaybackContext();
    engine.getDeviceManager().deviceManager.closeAudioDevice();
    engine.getDeviceManager().deviceManager.restartLastAudioDevice();
    transport.ensureContextAllocated(true);
    sendSynchronousChangeMessage();
}

bool Session::hasNote(int step, int pitch) const
{
    for (auto* note : pattern().getSequence().getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - step * 0.25) < 0.0001)
            return true;
    return false;
}

void Session::beginNoteGesture(juce::String actionName) { edit->getUndoManager().beginNewTransaction(actionName); }
void Session::endNoteGesture() { edit->getUndoManager().beginNewTransaction(); }

void Session::setNote(int step, int pitch, bool enabled)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (step < 0 || step >= steps || pitch < 0 || pitch > 127)
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
        pattern().state.removeProperty(starterPlaceholderID, undoManager);
        sequence.addNote(pitch, tracktion::core::BeatPosition::fromBeats(step * 0.25),
                         tracktion::core::BeatDuration::fromBeats(0.225), 100, 0, undoManager);
        markModified();
    }
    sendSynchronousChangeMessage();
}

juce::Result Session::moveNote(int sourceStep, int sourcePitch, int targetStep, int targetPitch)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (sourceStep < 0 || sourceStep >= steps || targetStep < 0 || targetStep >= steps
        || sourcePitch < 0 || sourcePitch > 127
        || targetPitch < 0 || targetPitch > 127)
        return juce::Result::fail("Move notes inside the visible pitch grid.");
    if (sourceStep == targetStep && sourcePitch == targetPitch)
        return juce::Result::ok();

    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    auto* moving = static_cast<te::MidiNote*>(nullptr);
    for (auto* note : sequence.getNotes())
    {
        const auto step = juce::roundToInt(note->getStartBeat().inBeats() * 4.0);
        if (step == targetStep && note->getNoteNumber() == targetPitch)
            return juce::Result::fail("That note cell is already occupied.");
        if (step == sourceStep && note->getNoteNumber() == sourcePitch)
            moving = note;
    }
    if (moving == nullptr)
        return juce::Result::fail("Select a note to move.");

    const auto targetStart = tracktion::core::BeatPosition::fromBeats(targetStep * 0.25);
    const auto maximumLength = tracktion::core::BeatDuration::fromBeats(4.0 - targetStart.inBeats());
    if (maximumLength <= tracktion::core::BeatDuration())
        return juce::Result::fail("Move notes inside the clip.");
    moving->setStartAndLength(targetStart, std::min(moving->getLengthBeats(), maximumLength), undoManager);
    moving->setNoteNumber(targetPitch, undoManager);
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::clearPattern()
{
    if (pattern().getSequence().getNumNotes() == 0) return;
    edit->getUndoManager().beginNewTransaction("Clear pattern");
    pattern().getSequence().removeAllNotes(&edit->getUndoManager());
    pattern().state.setProperty(starterPlaceholderID, true, &edit->getUndoManager());
    markModified();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
}

void Session::applyPatternPreset(PatternPreset preset)
{
    const auto data = presetPattern(preset);
    edit->getUndoManager().beginNewTransaction("Load " + data.name);
    setPatternInstrument(data.useDrums);
    if (data.useSustainPatch)
        if (auto* fourOsc = findFourOsc(*te::getAudioTracks(*edit)[0]))
            applyChordPadPatch(*fourOsc, edit->getUndoManager());
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
    if (data.useSustainPatch)
        if (auto* fourOsc = findFourOsc(*track))
            applyChordPadPatch(*fourOsc, edit->getUndoManager());
    auto clip = track->insertMIDIClip(data.name, {start, end}, nullptr);
    if (clip == nullptr)
        return juce::Result::fail("The pattern clip could not be added.");
    clip->setColour(presetColour(preset));
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
    clip->setColour(instrumentColour(instrument));
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
    if (!effectTypeAndName(effect, type, name))
        return juce::Result::fail("That audio effect could not be created.");

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

juce::Result Session::addClipAudioEffect(AudioEffect effect, te::EditItemID clipID)
{
    const char* type = nullptr;
    juce::String name;
    if (!effectTypeAndName(effect, type, name))
        return juce::Result::fail("That audio effect could not be created.");

    auto* clip = dynamic_cast<te::AudioClipBase*>(findClip(clipID));
    if (clip == nullptr)
        return juce::Result::fail("Clip effects can be dropped on audio clips.");
    if (!clip->canHaveEffects())
        return juce::Result::fail("This audio clip cannot host effects while warped or reversed.");

    auto plugin = edit->getPluginCache().createNewPlugin(type, {});
    if (plugin == nullptr)
        return juce::Result::fail(name + " could not be created.");
    if (!plugin->canBeAddedToClip())
        return juce::Result::fail(name + " cannot be added to a clip.");

    edit->getUndoManager().beginNewTransaction("Add " + name + " to clip");
    clip->getPluginList()->insertPlugin(plugin, clip->getPluginList()->size(), nullptr);
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

juce::Result Session::addMidiEffect(MidiEffect effect, int trackIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop MIDI FX on an instrument track.");

    const char* type = nullptr;
    juce::String name;
    switch (effect)
    {
        case MidiEffect::ThetaArp: type = ThetaArpDevice::xmlTypeName; name = "Theta Arp"; break;
    }

    auto* track = tracks[trackIndex];
    edit->getUndoManager().beginNewTransaction("Add " + name);
    auto plugin = edit->getPluginCache().createNewPlugin(type, {});
    if (plugin == nullptr)
        return juce::Result::fail(name + " could not be created.");

    int insertIndex = 0;
    for (int i = 0; i < track->pluginList.size(); ++i)
    {
        auto* existing = track->pluginList[i];
        if (existing != nullptr && (existing->getPluginType() == te::FourOscPlugin::xmlTypeName
                                    || existing->getPluginType() == DrumDevice::xmlTypeName))
        {
            insertIndex = i;
            break;
        }
        insertIndex = i + 1;
    }

    track->pluginList.insertPlugin(plugin, juce::jlimit(0, track->pluginList.size(), insertIndex), nullptr);
    edit->getUndoManager().beginNewTransaction();
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
        {
            patternClip->setColour(presetColour(PatternPreset::WarmPulse));
            patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
            patternClipID = patternClip->itemID;
        }
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

bool Session::shouldShowClipInArrangement(te::Clip& clip) const
{
    auto* midi = dynamic_cast<te::MidiClip*>(&clip);
    if (midi == nullptr)
        return true;
    return !static_cast<bool>(clip.state.getProperty(starterPlaceholderID, false))
        || midi->getSequence().getNumNotes() > 0;
}

juce::Result Session::editClip(te::EditItemID id, ClipGeometry next, ClipGesture gesture, int targetTrack)
{
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip first.");
    if (!std::isfinite(next.start) || !std::isfinite(next.end) || !std::isfinite(next.offset)
        || next.start < 0.0 || next.end <= next.start || next.offset < -1.0e-8
        || next.end > te::Edit::getMaximumEditEnd().inSeconds())
        return juce::Result::fail("Invalid clip position.");
    const auto tracks = te::getAudioTracks(*edit);
    auto* oldTrack = clip->getClipTrack();
    const auto oldTrackIndex = tracks.indexOf(dynamic_cast<te::AudioTrack*>(oldTrack));
    if (targetTrack < 0 || gesture != ClipGesture::move)
        targetTrack = oldTrackIndex;
    if (targetTrack < 0 || targetTrack > tracks.size())
        return juce::Result::fail("Drop the clip on a track lane.");
    const auto old = clip->getPosition();
    if (std::abs(old.time.getStart().inSeconds() - next.start) < 1.0e-8
        && std::abs(old.time.getEnd().inSeconds() - next.end) < 1.0e-8
        && std::abs(old.offset.inSeconds() - next.offset) < 1.0e-8
        && targetTrack == oldTrackIndex)
        return juce::Result::ok();
    edit->getUndoManager().beginNewTransaction(gesture == ClipGesture::move ? "Move clip" : "Trim clip");
    if (targetTrack == tracks.size())
    {
        auto newTrack = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr, false);
        if (newTrack == nullptr)
            return juce::Result::fail("Could not create a track for the moved clip.");
        newTrack->setName("Audio " + juce::String(tracks.size()));
        auto audioDevice = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
        newTrack->pluginList.insertPlugin(audioDevice, 0, nullptr);
        targetTrack = tracks.size();
    }
    const auto refreshedTracks = te::getAudioTracks(*edit);
    if (targetTrack != oldTrackIndex)
    {
        auto* target = refreshedTracks[targetTrack];
        te::Clip::Ptr clipRef(clip);
        if (!target->addClip(clipRef))
            return juce::Result::fail("The clip could not be moved to that track.");
    }
    clip->setPosition({{tracktion::core::TimePosition::fromSeconds(next.start),
                       tracktion::core::TimePosition::fromSeconds(next.end)},
                       tracktion::core::TimeDuration::fromSeconds(std::max(0.0, next.offset))});
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
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
    {
        copy = track->insertWaveClip(audio->getName() + " copy", audio->getSourceFileReference().getFile(),
            {duplicateRange, old.offset}, false).get();
        if (copy != nullptr)
            copy->setColour(audio->getColour());
    }
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
                    if (patternClip != nullptr)
                    {
                        patternClip->setColour(presetColour(PatternPreset::WarmPulse));
                        patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
                    }
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

juce::Result Session::cycleClipColour(te::EditItemID id)
{
    auto* clip = findClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select a clip first.");
    edit->getUndoManager().beginNewTransaction("Color clip");
    clip->setColour(nextClipColour(clip->getColour()));
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

int Session::clipPluginCount(te::EditItemID id) const
{
    auto* clip = dynamic_cast<te::AudioClipBase*>(findClip(id));
    if (clip == nullptr || clip->getPluginList() == nullptr)
        return 0;
    return clip->getPluginList()->size();
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
