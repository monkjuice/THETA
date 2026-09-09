#include "Session.h"

namespace theta
{
Session::Session()
{
    engine.getPluginManager().createBuiltInType<UtilityDevice>();
    engine.getPluginManager().createBuiltInType<DrumDevice>();
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
    struct Note { int step, pitch, length; };
    const Note* notes = nullptr;
    int count = 0;
    juce::String name;

    static constexpr Note warmPulse[] {{0, 48, 2}, {4, 55, 2}, {8, 60, 2}, {12, 55, 2}};
    static constexpr Note acidSteps[] {{0, 48, 1}, {3, 51, 1}, {6, 55, 1}, {7, 58, 1}, {10, 55, 1}, {13, 63, 1}, {15, 58, 1}};
    static constexpr Note houseKit[] {{0, 48, 1}, {4, 48, 1}, {8, 48, 1}, {12, 48, 1}, {4, 53, 1}, {12, 53, 1},
                                      {2, 58, 1}, {6, 58, 1}, {10, 58, 1}, {14, 58, 1}};
    static constexpr Note breakKit[] {{0, 48, 1}, {3, 48, 1}, {8, 48, 1}, {11, 48, 1}, {4, 53, 1}, {10, 53, 1},
                                      {1, 58, 1}, {3, 58, 1}, {6, 58, 1}, {9, 58, 1}, {12, 58, 1}, {15, 58, 1}};
    static constexpr Note minimalKit[] {{0, 48, 1}, {7, 48, 1}, {12, 48, 1}, {4, 53, 1}, {12, 53, 1}, {2, 58, 1}, {10, 58, 1}, {14, 58, 1}};

    auto useDrums = false;
    switch (preset)
    {
        case PatternPreset::WarmPulse:  notes = warmPulse;  count = static_cast<int>(std::size(warmPulse));  name = "Warm pulse"; break;
        case PatternPreset::AcidSteps:  notes = acidSteps;  count = static_cast<int>(std::size(acidSteps));  name = "Acid steps"; break;
        case PatternPreset::HouseKit:   notes = houseKit;   count = static_cast<int>(std::size(houseKit));   name = "House kit"; useDrums = true; break;
        case PatternPreset::BreakKit:   notes = breakKit;   count = static_cast<int>(std::size(breakKit));   name = "Break kit"; useDrums = true; break;
        case PatternPreset::MinimalKit: notes = minimalKit; count = static_cast<int>(std::size(minimalKit)); name = "Minimal kit"; useDrums = true; break;
    }

    edit->getUndoManager().beginNewTransaction("Load " + name);
    setPatternInstrument(useDrums);
    auto& sequence = pattern().getSequence();
    sequence.removeAllNotes(&edit->getUndoManager());
    for (int i = 0; i < count; ++i)
        sequence.addNote(notes[i].pitch, tracktion::core::BeatPosition::fromBeats(notes[i].step * 0.25),
                         tracktion::core::BeatDuration::fromBeats(std::max(1, notes[i].length) * 0.225), 100, 0,
                         &edit->getUndoManager());
    pattern().setName(name);
    markModified();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
}

void Session::setPatternInstrument(bool useDrums)
{
    if (synth) synth->setEnabled(!useDrums);
    if (drums) drums->setEnabled(useDrums);
    edit->state.setProperty("thetaPatternInstrument", useDrums ? "drums" : "synth", &edit->getUndoManager());
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
    auto end = pattern().getPosition().time.getEnd();
    for (auto* clip : te::getAudioTracks(*edit)[1]->getClips())
        end = std::max(end, clip->getPosition().time.getEnd());
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
    edit->tempoSequence.updateTempoData();
    refreshLoop();
    if (changed && edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
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
    if (tracks.size() != 2)
        return juce::Result::fail("This editor requires a pattern track and an audio track.");
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
    utility = nextUtility;
    audioUtility = nextAudioUtility;
    synth = nextSynth;
    drums = nextDrums;
    setPatternInstrument(edit->state.getProperty("thetaPatternInstrument").toString() == "drums");
    projectFile = file;
    savedRevision = ++changeRevision;
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
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

te::WaveAudioClip* Session::findAudioClip(te::EditItemID id) const
{
    for (auto* clip : te::getAudioTracks(*edit)[1]->getClips())
        if (clip->itemID == id) return dynamic_cast<te::WaveAudioClip*>(clip);
    return nullptr;
}

juce::Result Session::editAudioClip(te::EditItemID id, ClipGeometry next, ClipGesture gesture)
{
    auto* clip = findAudioClip(id);
    if (!clip) return juce::Result::fail("The audio clip no longer exists.");
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

juce::Result Session::splitAudioClip(te::EditItemID id, double splitTimeSeconds)
{
    auto* clip = findAudioClip(id);
    if (!clip) return juce::Result::fail("Select an audio clip to split.");
    if (!std::isfinite(splitTimeSeconds)) return juce::Result::fail("Invalid split position.");

    const auto old = clip->getPosition();
    const auto split = tracktion::core::TimePosition::fromSeconds(splitTimeSeconds);
    constexpr double minimumSeconds = 0.01;
    if (split <= old.time.getStart() + tracktion::core::TimeDuration::fromSeconds(minimumSeconds)
        || split >= old.time.getEnd() - tracktion::core::TimeDuration::fromSeconds(minimumSeconds))
        return juce::Result::fail("Move the playhead inside the selected audio clip before splitting.");

    auto* track = te::getAudioTracks(*edit)[1];
    const auto file = clip->getSourceFileReference().getFile();
    edit->getUndoManager().beginNewTransaction("Split audio clip");
    auto right = track->insertWaveClip(clip->getName() + " split", file,
        {{split, old.time.getEnd()}, old.offset + (split - old.time.getStart())}, false);
    if (right == nullptr)
        return juce::Result::fail("The right-hand split clip could not be created.");

    clip->setPosition({{old.time.getStart(), split}, old.offset});
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::deleteAudioClip(te::EditItemID id)
{
    if (auto* clip = findAudioClip(id))
    {
        edit->getUndoManager().beginNewTransaction("Delete audio clip");
        clip->removeFromParent();
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
