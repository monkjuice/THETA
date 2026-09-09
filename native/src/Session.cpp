#include "Session.h"

namespace theda
{
Session::Session()
{
    engine.getPluginManager().createBuiltInType<UtilityDevice>();
    edit = te::createEmptyEdit(engine, {});
    edit->state.setProperty("thedaFormatVersion", 1, nullptr);
    edit->tempoSequence.getTempo(0)->setBpm(120.0);
    edit->ensureNumberOfAudioTracks(2);
    auto* track = te::getAudioTracks(*edit)[0];
    track->setName("Pattern synth");
    auto synth = edit->getPluginCache().createNewPlugin(te::FourOscPlugin::xmlTypeName, {});
    track->pluginList.insertPlugin(synth, 0, nullptr);
    auto device = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    utility = dynamic_cast<UtilityDevice*>(device.get());
    track->pluginList.insertPlugin(device, 1, nullptr);
    utility->gain().setParameter(-12.0f, juce::dontSendNotification);
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
    patternClip = track->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
    te::getAudioTracks(*edit)[1]->setName("Audio 1");
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
    if (edit->getUndoManager().undo()) markModified();
    edit->tempoSequence.updateTempoData();
    refreshLoop();
    sendSynchronousChangeMessage();
}

void Session::redo()
{
    if (edit->getUndoManager().redo()) markModified();
    edit->tempoSequence.updateTempoData();
    refreshLoop();
    sendSynchronousChangeMessage();
}

juce::ValueTree Session::projectSnapshot()
{
    edit->flushState();
    auto snapshot = edit->state.createCopy();
    snapshot.setProperty("thedaSnapshotRevision", changeRevision, nullptr);
    return snapshot;
}

void Session::markModified()
{
    ++changeRevision;
    edit->markAsChanged();
}

juce::Result Session::restoreProject(const juce::ValueTree& state, const juce::File& file)
{
    if (!state.hasType(te::IDs::EDIT) || static_cast<int>(state.getProperty("thedaFormatVersion")) != 1)
        return juce::Result::fail("This is not a supported Theda native project.");
    auto candidate = te::loadEditFromState(engine, state.createCopy());
    if (!candidate) return juce::Result::fail("The project could not be loaded.");
    candidate->editFileRetriever = [file] { return file; };
    const auto tracks = te::getAudioTracks(*candidate);
    if (tracks.size() != 2)
        return juce::Result::fail("This editor requires a pattern track and an audio track.");
    te::MidiClip* nextPattern = nullptr;
    UtilityDevice* nextUtility = nullptr;
    bool hasSynth = false;
    for (auto* clip : tracks[0]->getClips())
        if (auto* midi = dynamic_cast<te::MidiClip*>(clip)) nextPattern = midi;
    for (auto plugin : tracks[0]->pluginList)
    {
        if (auto* device = dynamic_cast<UtilityDevice*>(plugin)) nextUtility = device;
        if (dynamic_cast<te::FourOscPlugin*>(plugin) != nullptr) hasSynth = true;
    }
    if (!nextPattern || !nextUtility || !hasSynth)
        return juce::Result::fail("The project is missing its pattern or synth devices.");
    listeners.call(&Listener::editWillChange);
    stop();
    edit = std::move(candidate);
    patternClip = nextPattern;
    utility = nextUtility;
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
    if (static_cast<juce::int64>(snapshot.getProperty("thedaSnapshotRevision")) == changeRevision)
    {
        savedRevision = changeRevision;
        edit->resetChangedStatus();
    }
    sendSynchronousChangeMessage();
}
}
