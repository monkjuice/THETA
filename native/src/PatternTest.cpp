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
        require(session.utility != nullptr && session.audioUtility != nullptr && session.synth != nullptr && session.drums != nullptr,
                "Session creates synth, drum, and Utility devices");
        auto* effectTrack = te::getAudioTracks(*session.edit)[1];
        const auto initialAudioPluginCount = effectTrack->pluginList.size();
        require(session.addAudioEffect(Session::AudioEffect::Equaliser).wasOk(), "Audio FX browser action inserts EQ");
        require(effectTrack->pluginList.size() == initialAudioPluginCount + 1, "Audio FX insert grows the audio track chain");
        auto audioDevices = session.deviceSlots(1);
        require(static_cast<int>(audioDevices.size()) == effectTrack->pluginList.size(), "Audio device rack mirrors the plugin chain");
        require(audioDevices.back().removable, "Inserted audio effect is removable");
        require(session.toggleDeviceEnabled(1, static_cast<int>(audioDevices.size()) - 1).wasOk(), "Device rack bypasses selected effect");
        require(!effectTrack->pluginList[effectTrack->pluginList.size() - 1]->isEnabled(), "Bypass disables the effect plugin");
        session.undo();
        require(effectTrack->pluginList[effectTrack->pluginList.size() - 1]->isEnabled(), "Undo restores effect enabled state");
        require(session.deleteDevice(1, static_cast<int>(audioDevices.size()) - 1).wasOk(), "Device rack deletes inserted effect");
        require(effectTrack->pluginList.size() == initialAudioPluginCount, "Delete removes inserted audio effect");
        session.undo();
        require(effectTrack->pluginList.size() == initialAudioPluginCount + 1, "Undo restores deleted audio effect");
        session.undo();
        require(effectTrack->pluginList.size() == initialAudioPluginCount, "Undo removes inserted audio effect");

        {
            Session parameterSession;
            auto* parameterTrack = te::getAudioTracks(*parameterSession.edit)[1];
            require(parameterSession.addAudioEffect(Session::AudioEffect::Reverb).wasOk(), "Audio FX browser action inserts Reverb");
            const auto reverbSlot = static_cast<int>(parameterTrack->pluginList.size()) - 1;
            auto reverbParameters = parameterSession.deviceParameters(1, reverbSlot);
            require(!reverbParameters.empty(), "Inserted Reverb exposes editable parameters");
            const auto oldReverbValue = reverbParameters.front().value;
            const auto newReverbValue = oldReverbValue == reverbParameters.front().minimum
                ? reverbParameters.front().maximum : reverbParameters.front().minimum;
            require(parameterSession.beginDeviceParameterGesture(1, reverbSlot, 0).wasOk(), "Device rack starts parameter gestures");
            require(parameterSession.setDeviceParameter(1, reverbSlot, 0, newReverbValue).wasOk(), "Device rack edits effect parameters");
            require(parameterSession.endDeviceParameterGesture(1, reverbSlot, 0).wasOk(), "Device rack ends parameter gestures");
            reverbParameters = parameterSession.deviceParameters(1, reverbSlot);
            require(std::abs(reverbParameters.front().value - newReverbValue) < 0.0001f, "Edited effect parameter value is reflected in the rack");
            require(parameterSession.addAudioEffect(Session::AudioEffect::ThetaSpace).wasOk(), "Audio FX browser action inserts Theta Space");
            const auto thetaSlot = static_cast<int>(parameterTrack->pluginList.size()) - 1;
            auto thetaParameters = parameterSession.deviceParameters(1, thetaSlot);
            require(thetaParameters.size() >= 6, "Theta Space exposes its macro controls");
            require(parameterSession.setDeviceParameter(1, thetaSlot, 0, thetaParameters.front().maximum).wasOk(),
                    "Theta Space macro controls are editable");
            require(parameterSession.addInstrument(Session::Instrument::FourOsc, 1).wasOk(), "Audio track can host a synth instrument");
            require(parameterSession.addMidiEffect(Session::MidiEffect::ThetaArp, 1).wasOk(), "MIDI FX browser action inserts Theta Arp");
            int arpSlot = -1, synthSlot = -1;
            for (int i = 0; i < parameterTrack->pluginList.size(); ++i)
            {
                auto* plugin = parameterTrack->pluginList[i];
                if (plugin == nullptr) continue;
                if (plugin->getPluginType() == ThetaArpDevice::xmlTypeName) arpSlot = i;
                if (plugin->getPluginType() == te::FourOscPlugin::xmlTypeName) synthSlot = i;
            }
            require(arpSlot >= 0 && synthSlot >= 0 && arpSlot < synthSlot,
                    "Theta Arp is inserted before the target instrument");
            auto arpParameters = parameterSession.deviceParameters(1, arpSlot);
            require(arpParameters.size() == 3, "Theta Arp exposes rate, octaves, and gate controls");
        }
        auto& sequence = session.pattern().getSequence();
        require(sequence.getNumNotes() == 0, "New pattern must be empty");
        session.beginNoteGesture();
        session.setNote(0, 48, true);
        session.setNote(4, 55, true);
        session.setNote(4, 55, true);
        session.setNote(16, 48, true);
        session.setNote(0, 128, true);
        session.endNoteGesture();
        require(sequence.getNumNotes() == 2, "Idempotent draw and grid bounds");
        session.undo();
        require(sequence.getNumNotes() == 0, "One gesture must undo together");
        session.redo();
        require(sequence.getNumNotes() == 2, "Redo restores the gesture");
        session.applyPatternPreset(Session::PatternPreset::HouseKit);
        require(sequence.getNumNotes() == 10, "Drum kit preset loads notes");
        require(!session.synth->isEnabled() && session.drums->isEnabled(), "Drum kit preset enables drum instrument");
        session.panicReset();
        require(!session.edit->getTransport().isPlaying(), "Panic reset stops playback");
        session.undo();
        require(sequence.getNumNotes() == 2, "Undo restores notes before preset load");
        require(session.synth->isEnabled() && !session.drums->isEnabled(), "Undo restores synth instrument");
        session.redo();
        require(sequence.getNumNotes() == 10, "Redo restores preset load");
        require(!session.synth->isEnabled() && session.drums->isEnabled(), "Redo restores drum instrument");
        session.clearPattern();
        require(sequence.getNumNotes() == 0, "Clear notes");
        session.undo();
        require(sequence.getNumNotes() == 10, "Undo clear");

        session.setTempo(90.0);
        require(std::abs(session.pattern().getPosition().time.getLength().inSeconds() - 8.0 / 3.0) < 0.0001, "One-bar clip follows tempo");
        require(session.hasNote(4, 53), "Tempo change preserves note beats");
        session.applyPatternPreset(Session::PatternPreset::ClapKit);
        require(session.isPatternDrums() && session.hasNote(4, 56) && session.hasNote(12, 56),
                "Clap kit preset loads clap notes into the drum editor");
        session.applyPatternPreset(Session::PatternPreset::ArpRun);
        require(!session.isPatternDrums() && session.hasNote(0, 48) && session.hasNote(0, 60),
                "Arp run preset loads a held synth chord");
        session.applyPatternPreset(Session::PatternPreset::ChordPad);
        require(!session.isPatternDrums() && session.hasNote(0, 48) && session.hasNote(0, 64) && session.hasNote(8, 65),
                "Chord pad preset loads sustained synth chords");
        require(session.synth->ampRelease->getCurrentValue() > 0.5f && session.synth->ampRelease->getCurrentValue() < 1.2f
                && session.synth->chorusMix->getCurrentValue() > 0.2f && session.synth->reverbMix->getCurrentValue() < 0.15f,
                "Chord pad preset shapes the 4OSC patch for controlled sustain");
        session.applyPatternPreset(Session::PatternPreset::SubBass);
        require(!session.isPatternDrums() && session.hasNote(0, 36) && session.hasNote(12, 34),
                "Sub bass preset loads a low mono bass line");
        require(session.synth->ampRelease->getCurrentValue() < 0.4f && session.synth->chorusMix->getCurrentValue() == 0.0f
                && static_cast<int>(session.synth->state.getProperty("voices")) == 1,
                "Sub bass preset shapes the 4OSC patch for clean mono low end");
        session.applyPatternPreset(Session::PatternPreset::ReeseBass);
        require(!session.isPatternDrums() && session.hasNote(0, 36) && session.hasNote(8, 39) && session.hasNote(12, 41),
                "Reese bass preset loads sustained electronic bass notes");
        require(session.synth->chorusMix->getCurrentValue() > 0.1f && session.synth->oscParams[0]->detune->getCurrentValue() > 0.05f
                && static_cast<int>(session.synth->state.getProperty("voices")) == 1,
                "Reese bass preset shapes the 4OSC patch for detuned bass movement");
        session.applyPatternPreset(Session::PatternPreset::SirenLead);
        require(!session.isPatternDrums() && session.hasNote(4, 72) && session.hasNote(10, 48),
                "Siren lead preset loads a rising and falling synth line");
        session.undo();
        session.undo();
        session.undo();
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

        {
            Session arpSession;
            arpSession.applyPatternPreset(Session::PatternPreset::ArpRun);
            require(arpSession.addMidiEffect(Session::MidiEffect::ThetaArp, 0).wasOk(), "Theta Arp can be inserted on the pattern synth");
            juce::TemporaryFile arpOutput(".wav");
            te::Renderer::Parameters arpParameters(*arpSession.edit);
            arpParameters.destFile = arpOutput.getFile();
            arpParameters.audioFormat = &wav;
            arpParameters.sampleRateForAudio = 48000;
            arpParameters.bitDepth = 24;
            arpParameters.time = arpSession.pattern().getPosition().time;
            {
                te::Renderer::RenderTask task("Theta Arp render test", arpParameters, nullptr, nullptr);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
                while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                    require(juce::Time::getMillisecondCounterHiRes() < deadline, "Theta Arp render timed out");
                require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
            }
            std::unique_ptr<juce::AudioFormatReader> arpReader(formats.createReaderFor(arpOutput.getFile()));
            require(arpReader != nullptr, "Read Theta Arp render");
            juce::AudioBuffer<float> arpAudio(static_cast<int>(arpReader->numChannels), static_cast<int>(arpReader->lengthInSamples));
            require(arpReader->read(&arpAudio, 0, arpAudio.getNumSamples(), 0, true, true), "Read Theta Arp samples");
            float arpPeak = 0.0f;
            for (int c = 0; c < arpAudio.getNumChannels(); ++c)
                for (int frame = 0; frame < arpAudio.getNumSamples(); ++frame)
                {
                    const auto sample = arpAudio.getSample(c, frame);
                    require(std::isfinite(sample), "Theta Arp render must stay finite");
                    arpPeak = std::max(arpPeak, std::abs(sample));
                }
            require(arpPeak > 0.0001f && arpPeak < 1.0f, "Theta Arp render must be audible and below clipping");
            arpSession.stop();
            arpSession.panicReset();
        }

        {
            Session fxSession;
            require(fxSession.importAudio(output.getFile()).wasOk(), "Theta Space test imports rendered audio");
            require(fxSession.addAudioEffect(Session::AudioEffect::ThetaSpace).wasOk(), "Theta Space can be inserted for render stability");
            auto* fxTrack = te::getAudioTracks(*fxSession.edit)[1];
            const auto thetaSlot = fxTrack->pluginList.size() - 1;
            auto* thetaSpace = dynamic_cast<ThetaSpaceDevice*>(fxTrack->pluginList[thetaSlot]);
            require(thetaSpace != nullptr, "Inserted effect is Theta Space");
            require(fxSession.setDeviceParameter(1, thetaSlot, 0, 1.0f).wasOk(), "Theta Space test sets wet mix");
            require(fxSession.setDeviceParameter(1, thetaSlot, 2, 0.9f).wasOk(), "Theta Space test sets feedback");
            auto params = thetaSpace->getAutomatableParameters();
            require(params.size() >= 2 && params[1] != nullptr, "Theta Space size parameter is automatable");
            auto& sizeCurve = params[1]->getCurve();
            sizeCurve.addPoint(tracktion::core::TimePosition::fromSeconds(0.0), 0.0f, 0.0f, nullptr);
            sizeCurve.addPoint(tracktion::core::TimePosition::fromSeconds(1.0), 1.0f, 0.0f, nullptr);

            juce::TemporaryFile fxOutput(".wav");
            te::Renderer::Parameters fxParameters(*fxSession.edit);
            fxParameters.destFile = fxOutput.getFile();
            fxParameters.audioFormat = &wav;
            fxParameters.sampleRateForAudio = 48000;
            fxParameters.bitDepth = 24;
            fxParameters.time = fxTrack->getClips()[0]->getPosition().time;
            {
                te::Renderer::RenderTask task("Theta Space size automation test", fxParameters, nullptr, nullptr);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
                while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                    require(juce::Time::getMillisecondCounterHiRes() < deadline, "Theta Space render timed out");
                require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
            }
            std::unique_ptr<juce::AudioFormatReader> fxReader(formats.createReaderFor(fxOutput.getFile()));
            require(fxReader != nullptr, "Read Theta Space render");
            juce::AudioBuffer<float> fxAudio(static_cast<int>(fxReader->numChannels), static_cast<int>(fxReader->lengthInSamples));
            require(fxReader->read(&fxAudio, 0, fxAudio.getNumSamples(), 0, true, true), "Read Theta Space samples");
            float fxPeak = 0.0f;
            for (int c = 0; c < fxAudio.getNumChannels(); ++c)
                for (int frame = 0; frame < fxAudio.getNumSamples(); ++frame)
                {
                    const auto sample = fxAudio.getSample(c, frame);
                    require(std::isfinite(sample), "Theta Space size automation must stay finite");
                    fxPeak = std::max(fxPeak, std::abs(sample));
                }
            require(fxPeak > 0.0001f && fxPeak <= 1.0f, "Theta Space size automation remains bounded and audible");
            fxSession.stop();
            fxSession.panicReset();
        }

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
        require(session.utility != nullptr && session.audioUtility != nullptr && session.synth != nullptr && session.drums != nullptr,
                "Project restore keeps synth, drum, and Utility devices");
        require(!session.synth->isEnabled() && session.drums->isEnabled(), "Project restore keeps drum instrument selection");
        require(session.pattern().getSequence().getNumNotes() == 10, "Notes survive project reopen");
        require(std::abs(session.tempo() - 90.0) < 0.001, "Tempo survives project reopen");
        require(!session.hasUnsavedChanges(), "Opened project is clean");
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1, "Audio clip survives project reopen");
        require(te::getAudioTracks(*session.edit)[1]->getClips()[0]->getSourceFileReference().getFile() == output.getFile(),
                "Imported audio path survives project reopen");
        const auto patternTrack = te::getAudioTracks(*session.edit)[0];
        session.deleteClip(session.pattern().itemID);
        require(patternTrack->getClips().size() >= 1, "Deleting the last edited pattern keeps an editable clip alive");
        require(session.pattern().getSequence().getNumNotes() == 0, "Replacement pattern clip is empty and readable");
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
