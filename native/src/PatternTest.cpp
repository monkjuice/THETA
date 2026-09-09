#include "Session.h"
#include <stdexcept>

namespace theta
{
int runPatternTest()
{
    try
    {
        const auto require = [](bool valid, const char* message)
        {
            if (!valid) throw std::runtime_error(message);
        };
        Session session;
        require(session.utility != nullptr && session.audioUtility != nullptr, "Session creates synth and audio Utility devices");
        auto& sequence = session.pattern().getSequence();
        require(sequence.getNumNotes() == 0, "New pattern must be empty");
        session.beginNoteGesture();
        session.setNote(0, 48, true);
        session.setNote(4, 55, true);
        session.setNote(4, 55, true);
        session.setNote(16, 48, true);
        session.setNote(0, 72, true);
        session.endNoteGesture();
        require(sequence.getNumNotes() == 2, "Idempotent draw and grid bounds");
        session.undo();
        require(sequence.getNumNotes() == 0, "One gesture must undo together");
        session.redo();
        require(sequence.getNumNotes() == 2, "Redo restores the gesture");
        session.applyPatternPreset(Session::PatternPreset::HouseKit);
        require(sequence.getNumNotes() == 10, "Drum kit preset loads notes");
        session.undo();
        require(sequence.getNumNotes() == 2, "Undo restores notes before preset load");
        session.redo();
        require(sequence.getNumNotes() == 10, "Redo restores preset load");
        session.clearPattern();
        require(sequence.getNumNotes() == 0, "Clear notes");
        session.undo();
        require(sequence.getNumNotes() == 10, "Undo clear");

        session.setTempo(90.0);
        require(std::abs(session.pattern().getPosition().time.getLength().inSeconds() - 8.0 / 3.0) < 0.0001, "One-bar clip follows tempo");
        require(session.hasNote(4, 53), "Tempo change preserves note beats");
        session.undo();
        require(std::abs(session.tempo() - 120.0) < 0.001, "Undo tempo");
        session.redo();
        require(std::abs(session.tempo() - 90.0) < 0.001, "Redo tempo");
        require(std::abs(session.edit->getTransport().getLoopRange().getLength().inSeconds() - 8.0 / 3.0) < 0.0001, "Loop follows tempo after redo");

        // A small real engine render proves MIDI reaches the synth and Utility.
        // It needs no audio hardware, synthetic large session, or progress UI.
        juce::TemporaryFile output(".wav");
        juce::WavAudioFormat wav;
        te::Renderer::Parameters parameters(*session.edit);
        parameters.destFile = output.getFile();
        parameters.audioFormat = &wav;
        parameters.sampleRateForAudio = 48000;
        parameters.bitDepth = 24;
        parameters.time = session.pattern().getPosition().time;
        {
            te::Renderer::RenderTask task("Pattern test", parameters, nullptr, nullptr);
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
            while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                require(juce::Time::getMillisecondCounterHiRes() < deadline, "Render timed out");
            require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
        }
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(output.getFile()));
        require(reader != nullptr, "Render must create a readable WAV");
        require(reader->sampleRate == 48000.0, "Render sample rate");
        require(std::abs(static_cast<double>(reader->lengthInSamples) - 128000.0) <= 512.0, "Render duration follows tempo");
        juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
        require(reader->read(&audio, 0, audio.getNumSamples(), 0, true, true), "Read rendered audio");
        float peak = 0.0f;
        for (int c = 0; c < audio.getNumChannels(); ++c)
            for (int frame = 0; frame < audio.getNumSamples(); ++frame)
            {
                const auto sample = audio.getSample(c, frame);
                require(std::isfinite(sample), "Render must contain finite samples");
                peak = std::max(peak, std::abs(sample));
            }
        require(peak > 0.0001f && peak < 1.0f, "Synth output must be audible and below clipping");
        reader.reset();
        require(session.importAudio(output.getFile()).wasOk(), "Import rendered audio");
        auto* audioTrack = te::getAudioTracks(*session.edit)[1];
        require(audioTrack->getClips().size() == 1, "Import creates an audio clip");
        require(audioTrack->getClips()[0]->getPosition().time.getStart().inSeconds() == 0.0, "First audio import starts at zero");

        juce::TemporaryFile project(".thetaedit");
        const auto snapshot = session.projectSnapshot();
        auto projectXml = snapshot.createXml();
        require(projectXml->writeTo(project.getFile()), "Write project snapshot");
        session.projectSaved(snapshot, project.getFile());
        require(!session.hasUnsavedChanges(), "Saved snapshot is clean");
        session.clearPattern();
        require(session.hasUnsavedChanges(), "Edits become dirty immediately");
        session.projectSaved(snapshot, project.getFile());
        require(session.hasUnsavedChanges(), "Saving an older snapshot must preserve dirty state");
        require(session.restoreProject(juce::ValueTree("invalid"), project.getFile()).failed(), "Reject invalid project");
        require(session.pattern().getSequence().getNumNotes() == 0, "Failed open preserves current edit");
        auto loadedXml = juce::parseXML(project.getFile());
        require(loadedXml != nullptr, "Read saved project");
        require(session.restoreProject(juce::ValueTree::fromXml(*loadedXml), project.getFile()).wasOk(), "Restore project");
        require(session.utility != nullptr && session.audioUtility != nullptr, "Project restore keeps synth and audio Utility devices");
        require(session.pattern().getSequence().getNumNotes() == 10, "Notes survive project reopen");
        require(std::abs(session.tempo() - 90.0) < 0.001, "Tempo survives project reopen");
        require(!session.hasUnsavedChanges(), "Opened project is clean");
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1, "Audio clip survives project reopen");
        require(te::getAudioTracks(*session.edit)[1]->getClips()[0]->getSourceFileReference().getFile() == output.getFile(),
                "Imported audio path survives project reopen");
        return 0;
    }
    catch (const std::exception& error)
    {
        // A GUI executable's stderr is still captured when launched by CTest.
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
