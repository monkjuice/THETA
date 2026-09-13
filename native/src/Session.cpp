#include "SessionInternal.h"
#include <algorithm>
#include <set>

namespace theta
{
namespace
{
bool commandLineTestMode = false;
}

void Session::setCommandLineTestMode(bool enabled)
{
    commandLineTestMode = enabled;
}

Session::Session() : engine(commandLineTestMode ? "Theta Native Tests" : "Theda Native")
{
    engine.getPluginManager().createBuiltInType<UtilityDevice>();
    engine.getPluginManager().createBuiltInType<DrumDevice>();
    engine.getPluginManager().createBuiltInType<ThetaSpaceDevice>();
    engine.getPluginManager().createBuiltInType<ThetaBloomDevice>();
    engine.getPluginManager().createBuiltInType<ThetaArpDevice>();
    engine.getPluginManager().createBuiltInType<ThetaWaveDevice>();
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
    auto wavePlugin = edit->getPluginCache().createNewPlugin(ThetaWaveDevice::xmlTypeName, {});
    thetaWave = dynamic_cast<ThetaWaveDevice*>(wavePlugin.get());
    if (thetaWave != nullptr)
    {
        thetaWave->setEnabled(false);
        track->pluginList.insertPlugin(wavePlugin, 2, nullptr);
    }
    auto device = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    utility = dynamic_cast<UtilityDevice*>(device.get());
    track->pluginList.insertPlugin(device, track->pluginList.size(), nullptr);
    utility->gain().setParameter(-12.0f, juce::dontSendNotification);
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
    patternClip = track->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
    if (patternClip != nullptr)
    {
        patternClip->setColour(presetColour(PatternPreset::WarmPulse));
        patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
        patternClip->state.setProperty(editorStepsID, defaultSteps, nullptr);
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

void Session::panicReset(bool restartAudioDevice)
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
    if (restartAudioDevice)
    {
        engine.getDeviceManager().deviceManager.restartLastAudioDevice();
        transport.ensureContextAllocated(true);
    }
    sendSynchronousChangeMessage();
}

bool Session::hasNote(int step, int pitch) const
{
    const auto gridSteps = editorStepCount();
    if (step < 0 || step >= gridSteps || pitch < 0 || pitch > 127)
        return false;
    const auto beat = step * stepDurationBeats(editorStepResolution());
    for (auto* note : pattern().getSequence().getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
            return true;
    return false;
}

std::vector<Session::EditorNote> Session::editorNotes() const
{
    std::vector<EditorNote> result;
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    result.reserve(static_cast<size_t>(pattern().getSequence().getNumNotes()));
    for (auto* note : pattern().getSequence().getNotes())
        result.push_back({note->state,
                          note->getStartBeat().inBeats() / stepBeats,
                          note->getLengthBeats().inBeats() / stepBeats,
                          note->getNoteNumber(), note->getVelocity(), note->getColour()});
    return result;
}

juce::Result Session::addNote(double startSteps, int pitch, double lengthSteps,
                              juce::ValueTree* addedState, int velocity)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto gridSteps = editorStepCount();
    if (startSteps < 0.0 || startSteps >= gridSteps || pitch < 0 || pitch > 127)
        return juce::Result::fail("Add notes inside the visible grid.");
    lengthSteps = std::clamp(lengthSteps, 0.001, static_cast<double>(gridSteps) - startSteps);
    constexpr double tolerance = 0.0001;
    for (const auto& existing : editorNotes())
        if (existing.pitch == pitch
            && startSteps < existing.startSteps + existing.lengthSteps - tolerance
            && existing.startSteps < startSteps + lengthSteps - tolerance)
            return juce::Result::fail("Notes on the same lane cannot overlap.");

    auto& sequence = pattern().getSequence();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    auto* added = sequence.addNote(pitch, tracktion::core::BeatPosition::fromBeats(startSteps * stepBeats),
                                   tracktion::core::BeatDuration::fromBeats(lengthSteps * stepBeats),
                                   juce::jlimit(0, 127, velocity), 0, &edit->getUndoManager());
    if (added == nullptr)
        return juce::Result::fail("The note could not be added.");
    if (addedState != nullptr)
        *addedState = added->state;
    pattern().state.removeProperty(starterPlaceholderID, &edit->getUndoManager());
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

bool Session::removeNotes(const std::vector<juce::ValueTree>& states)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto& sequence = pattern().getSequence();
    auto changed = false;
    for (const auto& state : states)
        if (auto* note = sequence.getNoteFor(state))
        {
            sequence.removeNote(*note, &edit->getUndoManager());
            changed = true;
        }
    if (changed)
    {
        markModified();
        if (edit->getTransport().isPlaying())
        {
            panicMidiOnTrack(pattern().getClipTrack());
            edit->restartPlayback();
        }
        sendSynchronousChangeMessage();
    }
    return changed;
}

bool Session::adjustNoteVelocities(const std::vector<juce::ValueTree>& states, int percentageDelta)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (states.empty() || percentageDelta == 0)
        return false;
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    auto changed = false;
    for (const auto& state : states)
        if (auto* note = sequence.getNoteFor(state))
        {
            const auto oldPercent = juce::roundToInt(note->getVelocity() * 100.0 / 127.0);
            const auto newPercent = juce::jlimit(1, 100, oldPercent + percentageDelta);
            const auto newVelocity = juce::jlimit(1, 127, juce::roundToInt(newPercent * 127.0 / 100.0));
            if (newVelocity != note->getVelocity())
            {
                note->setVelocity(newVelocity, undoManager);
                changed = true;
            }
        }
    if (changed)
    {
        markModified();
        sendSynchronousChangeMessage();
    }
    return changed;
}

void Session::beginNoteGesture(juce::String actionName) { edit->getUndoManager().beginNewTransaction(actionName); }
void Session::endNoteGesture() { edit->getUndoManager().beginNewTransaction(); }

int Session::editorStepCount() const
{
    const auto resolution = editorStepResolution();
    const auto bars = std::max(1.0, std::ceil(patternLengthBeats() / 4.0));
    return juce::jlimit(defaultSteps, steps, static_cast<int>(std::ceil(resolution * bars)));
}

int Session::editorStepResolution() const
{
    if (patternClip == nullptr)
        return defaultSteps;
    return juce::jlimit(defaultSteps, 64,
                        static_cast<int>(patternClip->state.getProperty(editorStepsID, defaultSteps)));
}

double Session::patternLengthBeats() const
{
    if (patternClip == nullptr)
        return 4.0;
    const auto position = patternClip->getPosition();
    const auto startBeat = edit->tempoSequence.toBeats(position.time.getStart()).inBeats();
    const auto endBeat = edit->tempoSequence.toBeats(position.time.getEnd()).inBeats();
    return std::max(0.0001, endBeat - startBeat);
}

void Session::setEditorStepCount(int newSteps)
{
    const auto clamped = juce::jlimit(defaultSteps, 64, newSteps);
    if (patternClip == nullptr || editorStepResolution() == clamped)
        return;
    patternClip->state.setProperty(editorStepsID, clamped, &edit->getUndoManager());
    markModified();
    sendSynchronousChangeMessage();
}

void Session::setNote(int step, int pitch, bool enabled)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto gridSteps = editorStepCount();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    if (step < 0 || step >= gridSteps || pitch < 0 || pitch > 127)
        return;
    const auto beat = step * stepBeats;
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    const auto restartLivePlayback = [this]
    {
        if (!edit->getTransport().isPlaying())
            return;
        panicMidiOnTrack(pattern().getClipTrack());
        edit->restartPlayback();
    };
    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
        {
            if (!enabled)
            {
                sequence.removeNote(*note, undoManager);
                markModified();
                restartLivePlayback();
            }
            sendSynchronousChangeMessage();
            return;
        }
    if (enabled)
    {
        for (auto* note : sequence.getNotes())
        {
            if (note->getNoteNumber() != pitch)
                continue;

            const auto noteStartStep = juce::roundToInt(note->getStartBeat().inBeats() / stepBeats);
            const auto noteLengthSteps = std::max(1, static_cast<int>(std::ceil(note->getLengthBeats().inBeats()
                                                                                / stepBeats)));
            if (noteStartStep < step && step < noteStartStep + noteLengthSteps)
            {
                const auto start = tracktion::core::BeatPosition::fromBeats(noteStartStep * stepBeats);
                const auto length = tracktion::core::BeatDuration::fromBeats((step - noteStartStep) * stepBeats);
                note->setStartAndLength(start, length, undoManager);
                markModified();
                break;
            }
        }

        pattern().state.removeProperty(starterPlaceholderID, undoManager);
        const auto duration = std::max(0.02, stepBeats);
        sequence.addNote(pitch, tracktion::core::BeatPosition::fromBeats(beat),
                         tracktion::core::BeatDuration::fromBeats(duration), 100, 0, undoManager);
        markModified();
        restartLivePlayback();
    }
    sendSynchronousChangeMessage();
}

int Session::noteLengthSteps(int step, int pitch) const
{
    const auto gridSteps = editorStepCount();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    if (step < 0 || step >= gridSteps || pitch < 0 || pitch > 127)
        return 0;
    const auto beat = step * stepBeats;
    for (auto* note : pattern().getSequence().getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
            return std::max(1, static_cast<int>(std::ceil(note->getLengthBeats().inBeats() / stepBeats)));
    return 0;
}

juce::Result Session::resizeNote(int step, int pitch, int lengthSteps)
{
    return resizeNote(step, pitch, static_cast<double>(lengthSteps));
}

juce::Result Session::resizeNote(int step, int pitch, double lengthSteps)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto gridSteps = editorStepCount();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    if (step < 0 || step >= gridSteps || pitch < 0 || pitch > 127)
        return juce::Result::fail("Resize notes inside the visible grid.");
    lengthSteps = std::clamp(lengthSteps, 0.0625, static_cast<double>(gridSteps - step));
    const auto beat = step * stepBeats;
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    auto nextNoteStep = static_cast<double>(gridSteps);
    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch && note->getStartBeat().inBeats() > beat + 0.0001)
            nextNoteStep = std::min(nextNoteStep, note->getStartBeat().inBeats() / stepBeats);
    lengthSteps = std::min(lengthSteps, std::max(0.0625, nextNoteStep - step));

    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
        {
            const auto start = tracktion::core::BeatPosition::fromBeats(beat);
            const auto length = tracktion::core::BeatDuration::fromBeats(lengthSteps * stepBeats);
            note->setStartAndLength(start, length, undoManager);
            markModified();
            sendSynchronousChangeMessage();
            return juce::Result::ok();
        }
    return juce::Result::fail("Select a note to resize.");
}

juce::Result Session::resizeNote(const juce::ValueTree& state, double lengthSteps)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto& sequence = pattern().getSequence();
    auto* note = sequence.getNoteFor(state);
    if (note == nullptr)
        return juce::Result::fail("Select a note to resize.");
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    const auto startStep = note->getStartBeat().inBeats() / stepBeats;
    lengthSteps = std::clamp(lengthSteps, 0.001, static_cast<double>(editorStepCount()) - startStep);
    auto nextStart = static_cast<double>(editorStepCount());
    for (auto* other : sequence.getNotes())
        if (other != note && other->getNoteNumber() == note->getNoteNumber()
            && other->getStartBeat() > note->getStartBeat())
            nextStart = std::min(nextStart, other->getStartBeat().inBeats() / stepBeats);
    lengthSteps = std::min(lengthSteps, std::max(0.001, nextStart - startStep));
    note->setStartAndLength(note->getStartBeat(),
                            tracktion::core::BeatDuration::fromBeats(lengthSteps * stepBeats),
                            &edit->getUndoManager());
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::resizeNoteFromLeft(double startStep, int pitch, double newStartStep)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    const auto startBeat = startStep * stepBeats;
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch && std::abs(note->getStartBeat().inBeats() - startBeat) < 0.0001)
        {
            const auto endStep = startStep + note->getLengthBeats().inBeats() / stepBeats;
            auto previousEnd = 0.0;
            for (auto* other : sequence.getNotes())
                if (other->getNoteNumber() == pitch && other != note && other->getStartBeat().inBeats() < startBeat)
                    previousEnd = std::max(previousEnd, (other->getStartBeat().inBeats() + other->getLengthBeats().inBeats()) / stepBeats);
            newStartStep = std::clamp(newStartStep, previousEnd, endStep - 0.0625);
            note->setStartAndLength(tracktion::core::BeatPosition::fromBeats(newStartStep * stepBeats),
                                    tracktion::core::BeatDuration::fromBeats((endStep - newStartStep) * stepBeats), undoManager);
            markModified();
            sendSynchronousChangeMessage();
            return juce::Result::ok();
        }
    return juce::Result::fail("Select a note to resize.");
}

juce::Result Session::resizeNoteFromLeft(const juce::ValueTree& state, double newStartStep)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto& sequence = pattern().getSequence();
    auto* note = sequence.getNoteFor(state);
    if (note == nullptr)
        return juce::Result::fail("Select a note to resize.");
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    const auto endStep = note->getEndBeat().inBeats() / stepBeats;
    auto previousEnd = 0.0;
    for (auto* other : sequence.getNotes())
        if (other != note && other->getNoteNumber() == note->getNoteNumber()
            && other->getStartBeat() < note->getStartBeat())
            previousEnd = std::max(previousEnd, other->getEndBeat().inBeats() / stepBeats);
    newStartStep = std::clamp(newStartStep, previousEnd, endStep - 0.001);
    note->setStartAndLength(tracktion::core::BeatPosition::fromBeats(newStartStep * stepBeats),
                            tracktion::core::BeatDuration::fromBeats((endStep - newStartStep) * stepBeats),
                            &edit->getUndoManager());
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

bool Session::ensurePatternLengthSteps(int requiredSteps)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (requiredSteps <= editorStepCount())
        return true;
    if (requiredSteps > steps || patternClip == nullptr)
        return false;

    const auto position = pattern().getPosition();
    const auto startBeat = edit->tempoSequence.toBeats(position.time.getStart());
    const auto end = edit->tempoSequence.toTime(startBeat + tracktion::core::BeatDuration::fromBeats(
        requiredSteps * stepDurationBeats(editorStepResolution())));
    pattern().setLength(end - position.time.getStart(), true);
    markModified();
    refreshLoop();
    sendSynchronousChangeMessage();
    return true;
}

juce::Result Session::fillNoteToClipEnd(int step, int pitch)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto gridSteps = editorStepCount();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    if (step < 0 || step >= gridSteps || pitch < 0 || pitch > 127)
        return juce::Result::fail("Select a note inside the visible grid.");

    const auto beat = step * stepBeats;
    const auto endBeat = patternLengthBeats();
    if (endBeat <= beat + 0.0001)
        return juce::Result::fail("The selected note already starts at the clip end.");

    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    auto nextNoteBeat = endBeat;
    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch && note->getStartBeat().inBeats() > beat + 0.0001)
            nextNoteBeat = std::min(nextNoteBeat, note->getStartBeat().inBeats());

    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
        {
            const auto start = tracktion::core::BeatPosition::fromBeats(beat);
            const auto length = tracktion::core::BeatDuration::fromBeats(std::max(0.0001, nextNoteBeat - beat));
            note->setStartAndLength(start, length, undoManager);
            markModified();
            sendSynchronousChangeMessage();
            return juce::Result::ok();
        }
    return juce::Result::fail("Select a note to fill.");
}

juce::Result Session::fillNoteToClipEnd(const juce::ValueTree& state)
{
    auto& sequence = pattern().getSequence();
    auto* note = sequence.getNoteFor(state);
    if (note == nullptr)
        return juce::Result::fail("Select a note to fill.");
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    const auto startStep = note->getStartBeat().inBeats() / stepBeats;
    return resizeNote(state, patternLengthBeats() / stepBeats - startStep);
}

juce::Result Session::moveNote(int sourceStep, int sourcePitch, int targetStep, int targetPitch)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto gridSteps = editorStepCount();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    if (sourceStep < 0 || sourceStep >= gridSteps || targetStep < 0 || targetStep >= gridSteps
        || sourcePitch < 0 || sourcePitch > 127
        || targetPitch < 0 || targetPitch > 127)
        return juce::Result::fail("Move notes inside the visible pitch grid.");
    if (sourceStep == targetStep && sourcePitch == targetPitch)
        return juce::Result::ok();

    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    auto* moving = static_cast<te::MidiNote*>(nullptr);
    const auto sourceBeat = sourceStep * stepBeats;
    const auto targetBeat = targetStep * stepBeats;
    for (auto* note : sequence.getNotes())
    {
        const auto noteBeat = note->getStartBeat().inBeats();
        if (std::abs(noteBeat - targetBeat) < 0.0001 && note->getNoteNumber() == targetPitch)
            return juce::Result::fail("That note cell is already occupied.");
        if (std::abs(noteBeat - sourceBeat) < 0.0001 && note->getNoteNumber() == sourcePitch)
            moving = note;
    }
    if (moving == nullptr)
        return juce::Result::fail("Select a note to move.");

    const auto targetStart = tracktion::core::BeatPosition::fromBeats(targetBeat);
    const auto maximumLength = tracktion::core::BeatDuration::fromBeats(patternLengthBeats() - targetStart.inBeats());
    if (maximumLength <= tracktion::core::BeatDuration())
        return juce::Result::fail("Move notes inside the clip.");
    moving->setStartAndLength(targetStart, std::min(moving->getLengthBeats(), maximumLength), undoManager);
    moving->setNoteNumber(targetPitch, undoManager);
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::moveNotes(const std::vector<std::pair<int, int>>& sources, int stepDelta, int pitchDelta)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (sources.empty() || (stepDelta == 0 && pitchDelta == 0))
        return juce::Result::ok();

    const auto gridSteps = editorStepCount();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    struct PlannedMove { te::MidiNote* note; int targetStep, targetPitch; };
    std::vector<PlannedMove> moves;
    std::set<std::pair<int, int>> sourceCells, targetCells;

    for (const auto& [sourceStep, sourcePitch] : sources)
    {
        const auto targetStep = sourceStep + stepDelta;
        const auto targetPitch = sourcePitch + pitchDelta;
        if (targetStep < 0 || targetStep >= gridSteps || targetPitch < 0 || targetPitch > 127)
            return juce::Result::fail("Move notes inside the visible grid.");
        if (!sourceCells.insert({sourceStep, sourcePitch}).second
            || !targetCells.insert({targetStep, targetPitch}).second)
            return juce::Result::fail("That note group does not fit here.");
    }

    for (auto* note : pattern().getSequence().getNotes())
    {
        const auto step = juce::roundToInt(note->getStartBeat().inBeats() / stepBeats);
        const auto cell = std::pair {step, note->getNoteNumber()};
        if (sourceCells.contains(cell))
        {
            const auto lengthSteps = std::max(1, static_cast<int>(std::ceil(note->getLengthBeats().inBeats() / stepBeats)));
            if (step + stepDelta + lengthSteps > gridSteps)
                return juce::Result::fail("That note group does not fit here.");
            moves.push_back({note, step + stepDelta, note->getNoteNumber() + pitchDelta});
        }
        else if (targetCells.contains(cell))
            return juce::Result::fail("That note cell is already occupied.");
    }
    if (moves.size() != sources.size())
        return juce::Result::fail("Select notes to move.");

    const auto wasPlaying = edit->getTransport().isPlaying();
    // A moved sustained note may already be active in the current playback
    // graph. Its old note-off can otherwise disappear when the MIDI sequence
    // changes, leaving the instrument sounding indefinitely.
    if (wasPlaying)
        panicMidiOnTrack(pattern().getClipTrack());
    auto* undoManager = &edit->getUndoManager();
    for (const auto& move : moves)
        move.note->setStartAndLength(tracktion::core::BeatPosition::fromBeats(move.targetStep * stepBeats),
                                     move.note->getLengthBeats(), undoManager);
    for (const auto& move : moves)
        move.note->setNoteNumber(move.targetPitch, undoManager);
    markModified();
    if (wasPlaying)
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::moveNotes(const std::vector<juce::ValueTree>& states, double stepDelta, int pitchDelta)
{
    if (states.empty() || (std::abs(stepDelta) < 0.0001 && pitchDelta == 0))
        return juce::Result::ok();

    auto& sequence = pattern().getSequence();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    struct Move { te::MidiNote* note; double start, length; int pitch; };
    std::vector<Move> moves;
    moves.reserve(states.size());
    for (const auto& state : states)
    {
        auto* note = sequence.getNoteFor(state);
        if (note == nullptr)
            return juce::Result::fail("Select notes to move.");
        moves.push_back({note, note->getStartBeat().inBeats() / stepBeats,
                         note->getLengthBeats().inBeats() / stepBeats,
                         note->getNoteNumber()});
    }

    const auto isMoving = [&states](const te::MidiNote& candidate)
    {
        return std::find(states.begin(), states.end(), candidate.state) != states.end();
    };
    constexpr double tolerance = 0.0001;
    auto resolvedDelta = stepDelta;
    for (int pass = 0; pass < sequence.getNumNotes() + 1; ++pass)
    {
        auto adjusted = false;
        for (const auto& move : moves)
        {
            const auto targetPitch = move.pitch + pitchDelta;
            if (targetPitch < 0 || targetPitch > 127)
                return juce::Result::fail("Move notes inside the visible pitch grid.");
            const auto targetStart = move.start + resolvedDelta;
            const auto targetEnd = targetStart + move.length;
            for (auto* other : sequence.getNotes())
            {
                if (isMoving(*other) || other->getNoteNumber() != targetPitch)
                    continue;
                const auto otherStart = other->getStartBeat().inBeats() / stepBeats;
                const auto otherEnd = other->getEndBeat().inBeats() / stepBeats;
                if (targetStart < otherEnd - tolerance && otherStart < targetEnd - tolerance)
                {
                    resolvedDelta += stepDelta < 0.0 ? otherStart - targetEnd : otherEnd - targetStart;
                    adjusted = true;
                    break;
                }
            }
            if (adjusted) break;
        }
        if (!adjusted) break;
    }

    for (const auto& move : moves)
        if (move.start + resolvedDelta < 0.0
            || move.start + resolvedDelta + move.length > editorStepCount() + tolerance)
            return juce::Result::fail("That note group does not fit here.");

    const auto wasPlaying = edit->getTransport().isPlaying();
    if (wasPlaying) panicMidiOnTrack(pattern().getClipTrack());
    auto* undoManager = &edit->getUndoManager();
    for (const auto& move : moves)
    {
        move.note->setStartAndLength(tracktion::core::BeatPosition::fromBeats((move.start + resolvedDelta) * stepBeats),
                                     move.note->getLengthBeats(), undoManager);
        move.note->setNoteNumber(move.pitch + pitchDelta, undoManager);
    }
    markModified();
    if (wasPlaying) edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::redistributeNotes(const std::vector<juce::ValueTree>& states, int divisions,
                                        std::vector<juce::ValueTree>& replacementStates)
{
    replacementStates.clear();
    divisions = juce::jlimit(2, 32, divisions);
    if (states.empty())
        return juce::Result::fail("Select a note to divide.");

    auto& sequence = pattern().getSequence();
    std::vector<te::MidiNote*> sources;
    sources.reserve(states.size());
    for (const auto& state : states)
        if (auto* note = sequence.getNoteFor(state)) sources.push_back(note);
    if (sources.size() != states.size())
        return juce::Result::fail("The selected notes changed before they could be divided.");

    const auto pitch = sources.front()->getNoteNumber();
    auto start = sources.front()->getStartBeat().inBeats();
    auto end = sources.front()->getEndBeat().inBeats();
    for (auto* note : sources)
    {
        if (note->getNoteNumber() != pitch)
            return juce::Result::fail("Divide notes on one pitch lane at a time.");
        start = std::min(start, note->getStartBeat().inBeats());
        end = std::max(end, note->getEndBeat().inBeats());
    }
    if (end - start < 0.0001)
        return juce::Result::fail("The selected note is too short to divide.");
    for (auto* other : sequence.getNotes())
        if (other->getNoteNumber() == pitch
            && std::find(sources.begin(), sources.end(), other) == sources.end()
            && other->getStartBeat().inBeats() < end - 0.0001
            && start < other->getEndBeat().inBeats() - 0.0001)
            return juce::Result::fail("The selected span contains another note.");

    const auto velocity = sources.front()->getVelocity();
    const auto colour = sources.front()->getColour();
    auto* undoManager = &edit->getUndoManager();
    for (auto* note : sources)
        sequence.removeNote(*note, undoManager);
    const auto length = (end - start) / divisions;
    replacementStates.reserve(static_cast<size_t>(divisions));
    for (int division = 0; division < divisions; ++division)
        if (auto* note = sequence.addNote(pitch,
                tracktion::core::BeatPosition::fromBeats(start + division * length),
                tracktion::core::BeatDuration::fromBeats(length), velocity, colour, undoManager))
            replacementStates.push_back(note->state);

    markModified();
    if (edit->getTransport().isPlaying())
    {
        panicMidiOnTrack(pattern().getClipTrack());
        edit->restartPlayback();
    }
    sendSynchronousChangeMessage();
    return replacementStates.size() == static_cast<size_t>(divisions)
        ? juce::Result::ok() : juce::Result::fail("The note could not be divided.");
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
    if (data.useThetaWave)
    {
        bool instrumentChanged = false;
        juce::ignoreUnused(switchTrackInstrument(*edit, *te::getAudioTracks(*edit)[0], Instrument::ThetaWave, instrumentChanged));
        edit->state.setProperty("thetaPatternInstrument", "wave", &edit->getUndoManager());
    }
    else
    {
        setPatternInstrument(data.useDrums);
    }
    if (data.synthPatch != SynthPatch::Default)
        if (auto* fourOsc = findFourOsc(*te::getAudioTracks(*edit)[0]))
            applySynthPatch(data.synthPatch, *fourOsc, edit->getUndoManager());
    if (data.useThetaWave)
        if (auto* wave = findThetaWave(*te::getAudioTracks(*edit)[0]))
            applyThetaWavePatch(preset, *wave);
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
    {
        if (data.useThetaWave)
        {
            bool instrumentChanged = false;
            const auto result = switchTrackInstrument(*edit, *track, Instrument::ThetaWave, instrumentChanged);
            if (result.failed())
                return result;
            edit->state.setProperty("thetaPatternInstrument", "wave", &edit->getUndoManager());
        }
        else
        {
            setPatternInstrument(data.useDrums);
        }
    }
    else
    {
        bool instrumentChanged = false;
        const auto result = switchTrackInstrument(*edit, *track,
                                                  data.useDrums ? Instrument::Drums
                                                      : data.useThetaWave ? Instrument::ThetaWave
                                                      : Instrument::FourOsc,
                                                  instrumentChanged);
        if (result.failed())
            return result;
    }
    if (data.synthPatch != SynthPatch::Default)
        if (auto* fourOsc = findFourOsc(*track))
            applySynthPatch(data.synthPatch, *fourOsc, edit->getUndoManager());
    if (data.useThetaWave)
        if (auto* wave = findThetaWave(*track))
            applyThetaWavePatch(preset, *wave);
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
    const auto name = useDrums ? juce::String("Theta Drums")
        : instrument == Instrument::ThetaWave ? juce::String("Theta Wave")
        : juce::String("4OSC synth");
    const auto start = tracktion::core::TimePosition::fromSeconds(startSeconds);
    const auto end = start + tracktion::core::TimeDuration::fromSeconds(
        edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0)).inSeconds());
    auto* track = tracks[trackIndex];

    edit->getUndoManager().beginNewTransaction("Add " + name);
    bool instrumentChanged = false;
    const auto result = switchTrackInstrument(*edit, *track, instrument, instrumentChanged);
    if (result.failed())
        return result;
    if (trackIndex == 0)
        edit->state.setProperty("thetaPatternInstrument",
                                useDrums ? "drums" : instrument == Instrument::ThetaWave ? "wave" : "synth",
                                &edit->getUndoManager());

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
    if (thetaWave) thetaWave->setEnabled(false);
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
        case Instrument::ThetaWave: type = ThetaWaveDevice::xmlTypeName; name = "Theta Wave"; break;
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
        const auto result = switchTrackInstrument(*edit, *track, instrument, changed);
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

    if (auto* synthPlugin = dynamic_cast<te::FourOscPlugin*>(plugin))
    {
        for (int i = 0; i < 6; ++i)
            if (auto* parameter = fourOscMacroParameterAt(*synthPlugin, i))
            {
                const auto range = parameter->getValueRange();
                if (!std::isfinite(range.getStart()) || !std::isfinite(range.getEnd()) || range.getLength() <= 0.0f)
                    continue;
                const DeviceTarget target {track, slot, i};
                const auto* runtime = findAutomationRuntime(target);
                parameters.push_back({fourOscMacroName(i),
                                      formatFourOscMacroValue(i, parameter->getCurrentValue(), *parameter),
                                      parameter->getCurrentValue(),
                                      range.getStart(),
                                      exposedParameterMaximum(*plugin, i, range.getEnd()),
                                      parameter->isDiscrete(),
                                      hasClipAutomationTarget(*edit, target),
                                      runtime != nullptr && runtime->overridden});
            }
        return parameters;
    }

    if (auto* wavePlugin = dynamic_cast<ThetaWaveDevice*>(plugin))
    {
        for (int i = 0; i < 18; ++i)
            if (auto* parameter = thetaWaveMacroParameterAt(*wavePlugin, i))
            {
                const auto range = parameter->getValueRange();
                if (!std::isfinite(range.getStart()) || !std::isfinite(range.getEnd()) || range.getLength() <= 0.0f)
                    continue;
                const DeviceTarget target {track, slot, i};
                const auto* runtime = findAutomationRuntime(target);
                parameters.push_back({thetaWaveMacroName(i),
                                      formatThetaWaveMacroValue(i, parameter->getCurrentValue(), *parameter),
                                      parameter->getCurrentValue(),
                                      range.getStart(),
                                      range.getEnd(),
                                      parameter->isDiscrete(),
                                      hasClipAutomationTarget(*edit, target),
                                      runtime != nullptr && runtime->overridden});
            }
        return parameters;
    }

    int parameterIndex = 0;
    for (auto* parameter : plugin->getAutomatableParameters())
    {
        if (parameter == nullptr || !parameter->isParameterActive())
            continue;
        const auto currentIndex = parameterIndex++;
        const auto range = parameter->getValueRange();
        if (!std::isfinite(range.getStart()) || !std::isfinite(range.getEnd()) || range.getLength() <= 0.0f)
            continue;
        const DeviceTarget target {track, slot, currentIndex};
        const auto* runtime = findAutomationRuntime(target);
        parameters.push_back({parameter->getParameterShortName(18),
                              parameter->getCurrentValueAsStringWithLabel(),
                              parameter->getCurrentValue(),
                              range.getStart(),
                              range.getEnd(),
                              parameter->isDiscrete(),
                              hasClipAutomationTarget(*edit, target),
                              runtime != nullptr && runtime->overridden});
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
    auto* parameter = exposedParameterAt(*plugin, parameterIndex);
    if (parameter == nullptr) return juce::Result::fail("Select a parameter first.");
    lastTouchedParameter = {track, slot, parameterIndex};
    if (auto* runtime = findAutomationRuntime(lastTouchedParameter); runtime != nullptr && runtime->active)
        runtime->overridden = true;
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
    auto* parameter = exposedParameterAt(*plugin, parameterIndex);
    if (parameter == nullptr) return juce::Result::fail("Select a parameter first.");
    lastTouchedParameter = {track, slot, parameterIndex};
    const auto range = parameter->getValueRange();
    const auto next = juce::jlimit(range.getStart(), exposedParameterMaximum(*plugin, parameterIndex, range.getEnd()), value);
    auto& runtime = automationRuntimeFor(lastTouchedParameter);
    runtime.baseValue = next;
    runtime.hasBaseValue = true;
    if (runtime.active)
        runtime.overridden = true;
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
    auto* parameter = exposedParameterAt(*plugin, parameterIndex);
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
    ThetaWaveDevice* nextThetaWave = nullptr;
    DrumDevice* nextDrums = nullptr;
    for (auto* clip : tracks[0]->getClips())
        if (auto* midi = dynamic_cast<te::MidiClip*>(clip)) nextPattern = midi;
    for (auto plugin : tracks[0]->pluginList)
    {
        if (auto* device = dynamic_cast<UtilityDevice*>(plugin)) nextUtility = device;
        if (auto* device = dynamic_cast<te::FourOscPlugin*>(plugin)) nextSynth = device;
        if (auto* device = dynamic_cast<ThetaWaveDevice*>(plugin)) nextThetaWave = device;
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
    if (!nextThetaWave)
    {
        auto device = candidate->getPluginCache().createNewPlugin(ThetaWaveDevice::xmlTypeName, {});
        nextThetaWave = dynamic_cast<ThetaWaveDevice*>(device.get());
        if (!nextThetaWave) return juce::Result::fail("The wavetable device could not be created.");
        nextThetaWave->setEnabled(false);
        tracks[0]->pluginList.insertPlugin(device, 2, nullptr);
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
    thetaWave = nextThetaWave;
    drums = nextDrums;
    const auto patternInstrument = edit->state.getProperty("thetaPatternInstrument").toString();
    if (patternInstrument == "wave")
    {
        bool instrumentChanged = false;
        juce::ignoreUnused(switchTrackInstrument(*edit, *tracks[0], Instrument::ThetaWave, instrumentChanged));
    }
    else
    {
        setPatternInstrument(patternInstrument == "drums");
    }
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

Session::ClipAutomation Session::clipAutomation(te::EditItemID id) const
{
    const auto automations = clipAutomations(id);
    return automations.empty() ? ClipAutomation{} : automations.front();
}

std::vector<Session::ClipAutomation> Session::clipAutomations(te::EditItemID id) const
{
    std::vector<ClipAutomation> automations;
    ClipAutomation automation;
    auto* clip = findClip(id);
    if (clip == nullptr)
        return automations;

    for (int i = 0; i < clip->state.getNumChildren(); ++i)
    {
        const auto state = clip->state.getChild(i);
        if (!state.hasType(clipAutomationID))
            continue;

        automation = {};
        automation.target.track = static_cast<int>(state.getProperty(automationTrackID, -1));
        automation.target.slot = static_cast<int>(state.getProperty(automationSlotID, -1));
        automation.target.parameter = static_cast<int>(state.getProperty(automationParameterID, -1));
        automation.startSeconds = static_cast<double>(state.getProperty(automationStartID, 0.0));
        automation.endSeconds = static_cast<double>(state.getProperty(automationEndID, 0.0));
        automation.startValue = static_cast<float>(state.getProperty(automationStartValueID, 0.0));
        automation.endValue = static_cast<float>(state.getProperty(automationEndValueID, 0.0));
        automation.active = automation.target.isValid()
            && std::isfinite(automation.startSeconds)
            && std::isfinite(automation.endSeconds)
            && automation.endSeconds > automation.startSeconds;

        const auto parameters = deviceParameters(automation.target.track, automation.target.slot);
        if (juce::isPositiveAndBelow(automation.target.parameter, parameters.size()))
        {
            const auto& parameter = parameters[static_cast<size_t>(automation.target.parameter)];
            automation.parameterName = parameter.name;
            automation.minimum = parameter.minimum;
            automation.maximum = parameter.maximum;
        }
        if (automation.active)
            automations.push_back(automation);
    }
    return automations;
}

juce::Result Session::setClipAutomationRamp(te::EditItemID id, DeviceTarget target, double startSeconds, double endSeconds,
                                            float startValue, float endValue)
{
    auto* clip = findClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select a clip first.");
    if (!target.isValid())
        return juce::Result::fail("Move a device knob first, then draw automation.");

    auto parameters = deviceParameters(target.track, target.slot);
    if (!juce::isPositiveAndBelow(target.parameter, parameters.size()))
        return juce::Result::fail("The last touched knob is no longer available.");
    const auto& parameter = parameters[static_cast<size_t>(target.parameter)];

    if (endSeconds < startSeconds)
    {
        std::swap(startSeconds, endSeconds);
        std::swap(startValue, endValue);
    }
    const auto clipStart = clip->getPosition().time.getStart().inSeconds();
    const auto clipEnd = clip->getPosition().time.getEnd().inSeconds();
    startSeconds = juce::jlimit(clipStart, clipEnd, startSeconds);
    endSeconds = juce::jlimit(clipStart, clipEnd, endSeconds);
    if (endSeconds - startSeconds < 0.02)
        return juce::Result::fail("Draw a longer automation span.");

    startValue = juce::jlimit(parameter.minimum, parameter.maximum, startValue);
    endValue = juce::jlimit(parameter.minimum, parameter.maximum, endValue);

    edit->getUndoManager().beginNewTransaction("Draw clip automation");
    for (int i = clip->state.getNumChildren(); --i >= 0;)
    {
        const auto existing = clip->state.getChild(i);
        if (!existing.hasType(clipAutomationID))
            continue;
        const DeviceTarget existingTarget {
            static_cast<int>(existing.getProperty(automationTrackID, -1)),
            static_cast<int>(existing.getProperty(automationSlotID, -1)),
            static_cast<int>(existing.getProperty(automationParameterID, -1))
        };
        if (sameDeviceTarget(existingTarget, target))
            clip->state.removeChild(existing, &edit->getUndoManager());
    }
    juce::ValueTree automation(clipAutomationID);
    automation.setProperty(automationTrackID, target.track, &edit->getUndoManager());
    automation.setProperty(automationSlotID, target.slot, &edit->getUndoManager());
    automation.setProperty(automationParameterID, target.parameter, &edit->getUndoManager());
    automation.setProperty(automationStartID, startSeconds - clipStart, &edit->getUndoManager());
    automation.setProperty(automationEndID, endSeconds - clipStart, &edit->getUndoManager());
    automation.setProperty(automationStartValueID, startValue, &edit->getUndoManager());
    automation.setProperty(automationEndValueID, endValue, &edit->getUndoManager());
    clip->state.addChild(automation, -1, &edit->getUndoManager());
    if (auto* runtime = findAutomationRuntime(target))
        runtime->overridden = false;
    markModified();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::deleteClipAutomation(te::EditItemID id, DeviceTarget target)
{
    auto* clip = findClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select a clip first.");
    if (!target.isValid())
        return juce::Result::fail("Select an automation lane first.");

    for (int i = clip->state.getNumChildren(); --i >= 0;)
    {
        const auto existing = clip->state.getChild(i);
        if (!existing.hasType(clipAutomationID))
            continue;
        const DeviceTarget existingTarget {
            static_cast<int>(existing.getProperty(automationTrackID, -1)),
            static_cast<int>(existing.getProperty(automationSlotID, -1)),
            static_cast<int>(existing.getProperty(automationParameterID, -1))
        };
        if (sameDeviceTarget(existingTarget, target))
        {
            edit->getUndoManager().beginNewTransaction("Delete clip automation");
            clip->state.removeChild(existing, &edit->getUndoManager());
            if (auto* runtime = findAutomationRuntime(target))
            {
                runtime->active = false;
                runtime->overridden = false;
            }
            markModified();
            edit->getUndoManager().beginNewTransaction();
            sendSynchronousChangeMessage();
            return juce::Result::ok();
        }
    }
    return juce::Result::fail("Automation lane was not found.");
}

Session::AutomationRuntime& Session::automationRuntimeFor(DeviceTarget target)
{
    if (auto* runtime = findAutomationRuntime(target))
        return *runtime;
    automationRuntime.push_back({target});
    return automationRuntime.back();
}

Session::AutomationRuntime* Session::findAutomationRuntime(DeviceTarget target)
{
    for (auto& runtime : automationRuntime)
        if (sameDeviceTarget(runtime.target, target))
            return &runtime;
    return nullptr;
}

const Session::AutomationRuntime* Session::findAutomationRuntime(DeviceTarget target) const
{
    for (const auto& runtime : automationRuntime)
        if (sameDeviceTarget(runtime.target, target))
            return &runtime;
    return nullptr;
}

juce::Result Session::toggleParameterAutomationOverride(int track, int slot, int parameter)
{
    const DeviceTarget target {track, slot, parameter};
    if (!target.isValid())
        return juce::Result::fail("Select an automated parameter first.");
    if (!hasClipAutomationTarget(*edit, target))
        return juce::Result::fail("This parameter has no clip automation.");

    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr)
        return juce::Result::fail("Select a device first.");
    auto* pluginParameter = exposedParameterAt(*plugin, parameter);
    if (pluginParameter == nullptr)
        return juce::Result::fail("Select a parameter first.");

    auto& runtime = automationRuntimeFor(target);
    runtime.baseValue = pluginParameter->getCurrentValue();
    runtime.hasBaseValue = true;
    runtime.overridden = !runtime.overridden;
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::applyClipAutomationAt(double timelineSeconds)
{
    if (!std::isfinite(timelineSeconds) || timelineSeconds < 0.0)
        return;

    bool changed = false;
    std::vector<DeviceTarget> activeTargets;
    const auto targetTracks = te::getAudioTracks(*edit);
    for (auto* track : te::getAudioTracks(*edit))
        for (auto* clip : track->getClips())
        {
            const auto clipStart = clip->getPosition().time.getStart().inSeconds();
            const auto local = timelineSeconds - clipStart;
            for (const auto& automation : clipAutomations(clip->itemID))
            {
                if (local < automation.startSeconds || local > automation.endSeconds)
                    continue;

                const auto amount = (local - automation.startSeconds) / (automation.endSeconds - automation.startSeconds);
                const auto value = static_cast<float>(automation.startValue + (automation.endValue - automation.startValue) * amount);
                if (!juce::isPositiveAndBelow(automation.target.track, targetTracks.size())
                    || !juce::isPositiveAndBelow(automation.target.slot, targetTracks[automation.target.track]->pluginList.size()))
                    continue;
                auto* plugin = targetTracks[automation.target.track]->pluginList[automation.target.slot];
                if (plugin == nullptr)
                    continue;
                if (auto* parameter = exposedParameterAt(*plugin, automation.target.parameter))
                {
                    const auto range = parameter->getValueRange();
                    auto& runtime = automationRuntimeFor(automation.target);
                    activeTargets.push_back(automation.target);
                    if (!runtime.hasBaseValue)
                    {
                        runtime.baseValue = parameter->getCurrentValue();
                        runtime.hasBaseValue = true;
                    }
                    runtime.active = true;
                    if (runtime.overridden)
                        continue;

                    const auto next = juce::jlimit(range.getStart(),
                                                   exposedParameterMaximum(*plugin, automation.target.parameter, range.getEnd()), value);
                    if (std::abs(parameter->getCurrentValue() - next) > 0.0001f)
                    {
                        parameter->setParameter(next, juce::sendNotification);
                        changed = true;
                    }
                }
            }
        }

    for (auto& runtime : automationRuntime)
    {
        if (!runtime.active)
            continue;
        bool stillActive = false;
        for (const auto target : activeTargets)
            if (sameDeviceTarget(runtime.target, target))
            {
                stillActive = true;
                break;
            }
        if (stillActive)
            continue;

        runtime.active = false;
        if (runtime.overridden || !runtime.hasBaseValue)
            continue;
        if (!juce::isPositiveAndBelow(runtime.target.track, targetTracks.size())
            || !juce::isPositiveAndBelow(runtime.target.slot, targetTracks[runtime.target.track]->pluginList.size()))
            continue;
        auto* plugin = targetTracks[runtime.target.track]->pluginList[runtime.target.slot];
        if (plugin == nullptr)
            continue;
        if (auto* parameter = exposedParameterAt(*plugin, runtime.target.parameter))
        {
            const auto range = parameter->getValueRange();
            const auto next = juce::jlimit(range.getStart(),
                                           exposedParameterMaximum(*plugin, runtime.target.parameter, range.getEnd()), runtime.baseValue);
            if (std::abs(parameter->getCurrentValue() - next) > 0.0001f)
            {
                parameter->setParameter(next, juce::sendNotification);
                changed = true;
            }
        }
    }

    if (changed)
        sendSynchronousChangeMessage();
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
    const auto movingMidi = dynamic_cast<te::MidiClip*>(clip) != nullptr;
    const auto sourceInstrument = movingMidi && oldTrackIndex >= 0 ? activeTrackInstrument(*tracks[oldTrackIndex])
                                                                   : Instrument::Utility;
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
    auto& transport = edit->getTransport();
    const auto wasPlaying = transport.isPlaying();
    const auto hadPlaybackContext = transport.isPlayContextActive();
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
        if (movingMidi)
        {
            bool instrumentChanged = false;
            const auto result = switchTrackInstrument(*edit, *target, sourceInstrument, instrumentChanged);
            if (result.failed())
                return result;
            if (targetTrack == 0)
                edit->state.setProperty("thetaPatternInstrument",
                                        sourceInstrument == Instrument::Drums ? "drums"
                                            : sourceInstrument == Instrument::ThetaWave ? "wave" : "synth",
                                        &edit->getUndoManager());
        }
        if (!clip->moveTo(*target))
            return juce::Result::fail("The clip could not be moved to that track.");
    }
    clip->setPosition({{tracktion::core::TimePosition::fromSeconds(next.start),
                       tracktion::core::TimePosition::fromSeconds(next.end)},
                       tracktion::core::TimeDuration::fromSeconds(std::max(0.0, next.offset))});
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (targetTrack != oldTrackIndex && (wasPlaying || hadPlaybackContext))
    {
        transport.freePlaybackContext();
        transport.ensureContextAllocated(true);
        if (wasPlaying)
            transport.play(true);
    }
    else if (wasPlaying)
    {
        edit->restartPlayback();
    }
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
