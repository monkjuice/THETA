        require(session.addAudioTrack().wasOk(), "Can create an audio track");
        require(session.trackCount() == 3 && session.trackName(2) == "Audio 2", "New audio track is visible in the arrangement model");
        view.sync();
        view.resized();
        require(view.trackScrollBar.isVisible(), "Adding tracks makes the arrangement lanes vertically scrollable");
        const auto renderClipRms = [&session, &wav, &require](te::Clip& clip)
        {
            juce::TemporaryFile rendered(".wav");
            te::Renderer::Parameters parameters(*session.edit);
            parameters.destFile = rendered.getFile();
            parameters.audioFormat = &wav;
            parameters.sampleRateForAudio = 48000;
            parameters.bitDepth = 24;
            parameters.time = clip.getPosition().time;
            {
                te::Renderer::RenderTask task("Moved audio test", parameters, nullptr, nullptr);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
                while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                    require(juce::Time::getMillisecondCounterHiRes() < deadline, "Moved render timeout");
                require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
            }
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(rendered.getFile()));
            require(reader != nullptr, "Read moved render");
            juce::AudioBuffer<float> output(2, static_cast<int>(reader->lengthInSamples));
            require(reader->read(&output, 0, output.getNumSamples(), 0, true, true), "Read moved render samples");
            return output.getRMSLevel(0, 0, output.getNumSamples());
        };
        scenario("track management: duplicate move");
        view.selected = id;
        view.duplicateSelected();
        auto* movedDuplicate = te::getAudioTracks(*session.edit)[1]->getClips().getLast();
        const auto movedDuplicateID = movedDuplicate->itemID;
        view.sync();
        view.selected = movedDuplicateID;
        drag({view.xFor(movedDuplicate->getPosition().time.getStart().inSeconds() + 0.1), view.lane(1).getCentreY()},
             {view.xFor(0.5), view.lane(2).getCentreY()});
        require(te::getAudioTracks(*session.edit)[2]->findClipForID(movedDuplicateID) != nullptr,
                "Dragging a duplicated audio clip to a newly-created track preserves clip identity");
        require(renderClipRms(*session.findAudioClip(movedDuplicateID)) > 0.01f,
                "Duplicated audio moved to a newly-created track still renders audible audio");
        session.undo();
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->findClipForID(id) != nullptr,
                "Undo restores the original audio clip after duplicate move regression");
        view.sync();
        scenario("track management: cross-track move");
        view.selected = id;
        drag({view.xFor(0.2), view.lane(1).getCentreY()}, {view.xFor(0.5), view.lane(2).getCentreY()});
        require(te::getAudioTracks(*session.edit)[1]->getClips().isEmpty(), "Dragging an audio clip to another lane removes it from the source track");
        require(te::getAudioTracks(*session.edit)[2]->findClipForID(id) != nullptr, "Dragging an audio clip to another lane preserves clip identity on the target track");
        require(close(session.findAudioClip(id)->getPosition().time.getStart().inSeconds(), 0.25), "Cross-track audio drag also updates the clip time");
        require(renderClipRms(*session.findAudioClip(id)) > 0.01f, "Cross-track moved clip still renders audible audio");
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->findClipForID(id) != nullptr, "Undo restores the clip to its original track");
        require(te::getAudioTracks(*session.edit)[2]->findClipForID(id) == nullptr, "Undo removes the clip from the drag target track");
        view.sync();
        scenario("track management: instrument-lane move");
        session.applyPatternPreset(Session::PatternPreset::WavePluck);
        view.sync();
        view.selected = id;
        drag({view.xFor(0.2), view.lane(1).getCentreY()}, {view.xFor(0.5), view.lane(0).getCentreY()});
        require(te::getAudioTracks(*session.edit)[0]->findClipForID(id) != nullptr,
                "Dragging an audio clip onto an instrument lane moves it there");
        require(renderClipRms(*session.findAudioClip(id)) > 0.01f,
                "Audio moved onto a Theta Wave instrument lane still renders audible audio");
        session.undo();
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->findClipForID(id) != nullptr,
                "Undo restores audio after an instrument-lane move");
        view.sync();
        scenario("track management: move during playback");
        session.togglePlayback();
        require(session.edit->getTransport().isPlaying(), "Transport starts before cross-track clip drag");
        drag({view.xFor(0.2), view.lane(1).getCentreY()}, {view.xFor(0.5), view.lane(2).getCentreY()});
        require(session.edit->getTransport().isPlaying(), "Cross-track clip drag keeps playback running");
        session.stop();
        session.undo();
        view.sync();
        scenario("track management: drop-created track");
        drag({view.xFor(0.2), view.lane(1).getCentreY()}, {view.xFor(0.5), view.lane(session.trackCount() - 1).getBottom() + 12.0f});
        require(session.trackCount() == 4, "Dragging an audio clip below the last lane creates a new track");
        require(te::getAudioTracks(*session.edit)[3]->findClipForID(id) != nullptr, "Drop-created track receives the dragged audio clip");
        require(renderClipRms(*session.findAudioClip(id)) > 0.01f, "Drop-created track moved clip still renders audible audio");
        session.undo();
        require(session.trackCount() == 3, "Undo removes the track created by a clip drag");
        require(te::getAudioTracks(*session.edit)[1]->findClipForID(id) != nullptr, "Undo restores the clip after a drag-created track");
        view.sync();
        scenario("track management: preset drops");
        require(view.applyBrowserDrop("theta-browser:preset:SirenLead", 2, 0.0, true).wasOk(), "Siren lead browser row creates a synth clip");
        auto* droppedPreset = te::getAudioTracks(*session.edit)[2]->getClips().getLast();
        require(droppedPreset != nullptr && droppedPreset->getName() == "Siren lead",
                "Dropped siren preset names the new clip");
        session.undo();
        scenario("track management: chord preset");
        require(view.applyBrowserDrop("theta-browser:preset:ChordPad", 2, 0.0, true).wasOk(), "Chord pad browser row creates a synth clip");
        droppedPreset = te::getAudioTracks(*session.edit)[2]->getClips().getLast();
        require(droppedPreset != nullptr && droppedPreset->getName() == "Chord pad",
                "Dropped chord pad preset names the new clip");
        session.undo();
        scenario("track management: reese preset");
        require(view.applyBrowserDrop("theta-browser:preset:ReeseBass", 2, 0.0, true).wasOk(), "Reese bass browser row creates a synth clip");
        droppedPreset = te::getAudioTracks(*session.edit)[2]->getClips().getLast();
        require(droppedPreset != nullptr && droppedPreset->getName() == "Reese bass",
                "Dropped Reese bass preset names the new clip");
        session.undo();
        scenario("track management: clap preset");
        require(view.applyBrowserDrop("theta-browser:preset:ClapKit", 2, 0.0, true).wasOk(), "Clap kit browser row creates a drum clip");
        droppedPreset = te::getAudioTracks(*session.edit)[2]->getClips().getLast();
        require(droppedPreset != nullptr && session.selectPatternClip(droppedPreset->itemID).wasOk(),
                "Dropped clap preset can be selected for note editing");
        require(session.hasNote(4, 56), "Dropped clap preset selects an editable clip with clap notes");
        session.undo();
        scenario("track management: warm pulse drop");
        const auto audio2Clips = te::getAudioTracks(*session.edit)[2]->getClips().size();
        const auto audio2Devices = session.deviceSlots(2).size();
        view.itemDropped({"theta-browser:preset:WarmPulse", nullptr,
                          {static_cast<int>(view.xFor(0.5)), static_cast<int>(view.lane(2).getCentreY())}});
        require(te::getAudioTracks(*session.edit)[2]->getClips().size() == audio2Clips + 1,
                "Dragging a browser sound onto Audio 2 creates the clip on Audio 2");
        require(session.deviceSlots(2).size() > audio2Devices, "Dropped browser sound adds an instrument to the target track");
        view.sync();
        view.selected = te::getAudioTracks(*session.edit)[2]->getClips().getLast()->itemID;
        require(session.selectPatternClip(view.selected).wasOk(), "Selecting a non-first MIDI clip makes it editable in the note editor");
