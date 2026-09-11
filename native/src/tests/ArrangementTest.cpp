#include "../Arrangement.h"
#include "../Theme.h"
#include "../StepGrid.h"
#include "../Playhead.h"
#include <stdexcept>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace theta
{
int runArrangementTest()
{
    try
    {
        const auto require = [](bool valid, const char* message)
        {
            if (!valid) throw std::runtime_error(message);
        };
        const auto close = [](double a, double b) { return std::abs(a - b) < 0.0001; };
        const auto isMidiNotePixel = [](juce::Colour colour)
        {
            return colour.getRed() > 150 && colour.getGreen() > 165 && colour.getBlue() > 95;
        };
        juce::TemporaryFile source(".wav");
        juce::WavAudioFormat wav;
        {
            std::unique_ptr<juce::OutputStream> stream = source.getFile().createOutputStream();
            auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                .withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
            require(writer != nullptr, "Create one-second audio fixture");
            juce::AudioBuffer<float> audio(2, 48000);
            for (int i = 0; i < 48000; ++i)
            {
                const auto sample = static_cast<float>(std::sin(i * 2.0 * juce::MathConstants<double>::pi * 440.0 / 48000.0))
                    * (i < 24000 ? 0.1f : 0.7f);
                audio.setSample(0, i, sample);
                audio.setSample(1, i, sample);
            }
            require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Write fixture");
        }
        Session session;
        Theme theme;
        Arrangement view(session);
        view.setLookAndFeel(&theme);
        view.setSize(1000, 246);
        view.fit();
        view.sync();
        require(view.clips.empty(), "Fresh startup hides the internal empty Pattern 1 placeholder");
        session.setNote(0, 48, true);
        view.sync();
        require(view.clips.size() == 1, "Drawing the first note makes the startup pattern clip visible");
        session.undo();
        view.sync();
        require(view.clips.empty(), "Undoing the first note hides the empty startup pattern again");
        int notifiedTrack = -1;
        view.trackSelected = [&notifiedTrack](int track) { notifiedTrack = track; };
        require(view.isInterestedInDragSource({"theta-browser:preset:HouseKit", nullptr, {160, 82}}),
                "Arrangement accepts browser drag payloads");
        require(view.applyBrowserDrop("theta-browser:preset:HouseKit", 0).wasOk(), "Browser drum drop loads a kit");
        require(session.isPatternDrums(), "Dropped drum kit enables the drum editor");
        const auto patternDevices = session.deviceSlots(0).size();
        require(view.applyBrowserDrop("theta-browser:effect:Reverb", 0).wasOk(), "Audio FX can be dropped on the pattern clip track");
        require(session.deviceSlots(0).size() == patternDevices + 1, "Dropped Reverb appears on the pattern track rack");
        const auto midiEffectSlots = session.deviceSlots(0).size();
        require(view.applyBrowserDrop("theta-browser:midi-effect:ThetaArp", 0).wasOk(), "MIDI FX can be dropped on the pattern track");
        require(session.deviceSlots(0).size() == midiEffectSlots + 1, "Dropped MIDI FX appears in the pattern track rack");
        const auto midiDropDevices = session.deviceSlots(0).size();
        view.itemDropped({"theta-browser:effect:Delay", nullptr, {static_cast<int>(view.xFor(0.25)), 82}});
        require(session.deviceSlots(0).size() == midiDropDevices + 1,
                "Dropping audio FX on a MIDI clip falls back to the clip's track rack");
        const auto patternClipsBeforeDrop = te::getAudioTracks(*session.edit)[0]->getClips().size();
        view.itemDropped({"theta-browser:preset:BreakKit", nullptr, {static_cast<int>(view.xFor(1.0)), 82}});
        require(te::getAudioTracks(*session.edit)[0]->getClips().size() == patternClipsBeforeDrop + 1,
                "Dragging a browser preset to the arrangement adds a pattern clip instead of replacing the first one");
        require(session.edit->getTransport().getLoopRange().getEnd().inSeconds() >= 3.0,
                "Loop range follows added clips on the first track");
        session.undo();
        const auto effectSlots = static_cast<int>(session.deviceSlots(1).size());
        require(view.applyBrowserDrop("theta-browser:effect:Delay", 1).wasOk(), "Browser effect drop inserts on the target audio track");
        require(static_cast<int>(session.deviceSlots(1).size()) == effectSlots + 1, "Dropped effect appears in the target audio rack");

        // Compare incremental frames to complete renders, including fractional
        // Windows display scales, wraparound, seeks, and hiding the playhead.
        StepGrid grid(session);
        grid.setSize(1000, 250);
        const auto checkPlayhead = [&require](juce::Component& panel, float& position, juce::Rectangle<int> area)
        {
            for (const auto scale : {1.0f, 1.25f, 2.0f})
            {
                const auto render = [&panel, scale](juce::Image& target, juce::Rectangle<int> dirty)
                {
                    juce::Graphics g(target);
                    // Native damage is an outward-rounded physical-pixel region,
                    // not an antialiased fractional clip applied over old pixels.
                    g.reduceClipRegion((dirty.toFloat() * scale).getSmallestIntegerContainer());
                    g.addTransform(juce::AffineTransform::scale(scale));
                    panel.paint(g);
                };
                juce::Image frame(juce::Image::RGB, juce::roundToInt(panel.getWidth() * scale),
                                  juce::roundToInt(panel.getHeight() * scale), true);
                position = -1;
                render(frame, panel.getLocalBounds());
                for (const auto next : {150.0f, 150.25f, 150.5f, 155.75f, 420.0f, 998.25f, 150.0f, -1.0f})
                {
                    const auto damage = playheadDamage(position, next, area);
                    movePlayhead(panel, position, next, area);
                    render(frame, damage);
                    juce::Image expected(juce::Image::RGB, frame.getWidth(), frame.getHeight(), true);
                    render(expected, panel.getLocalBounds());
                    for (int y = 0; y < frame.getHeight(); ++y)
                        for (int x = 0; x < frame.getWidth(); ++x)
                        {
                            if (frame.getPixelAt(x, y) != expected.getPixelAt(x, y))
                                std::fprintf(stderr, "%s scale %.2f next %.2f pixel %d,%d actual %s expected %s\n",
                                             panel.getTitle().toRawUTF8(), scale, next, x, y,
                                             frame.getPixelAt(x, y).toString().toRawUTF8(), expected.getPixelAt(x, y).toString().toRawUTF8());
                            require(frame.getPixelAt(x, y) == expected.getPixelAt(x, y),
                                    "Incremental playhead frame must match full render without trails or missing pixels");
                        }
                }
            }
        };
        checkPlayhead(view, view.playhead, view.getLocalBounds().withTrimmedTop(32).withTrimmedBottom(18));
        checkPlayhead(grid, grid.playhead, grid.getLocalBounds().withTrimmedTop(26));

        session.applyPatternPreset(Session::PatternPreset::AcidSteps);
        view.sync();
        auto midiPicture = view.createComponentSnapshot(view.getLocalBounds());
        int midiPixels = 0;
        for (int y = 95; y < 122; ++y)
            for (int x = 155; x < 620; ++x)
                if (isMidiNotePixel(midiPicture.getPixelAt(x, y))) ++midiPixels;
        require(midiPixels >= 25, "Pattern lane must draw visible MIDI notes");
        session.applyPatternPreset(Session::PatternPreset::SubBass);
        view.sync();
        auto bassMidiPicture = view.createComponentSnapshot(view.getLocalBounds());
        int bassMidiPixels = 0;
        for (int y = 95; y < 122; ++y)
            for (int x = 155; x < 620; ++x)
                if (isMidiNotePixel(bassMidiPicture.getPixelAt(x, y))) ++bassMidiPixels;
        require(bassMidiPixels >= 25, "Low bass MIDI clips must draw visible arrangement note bars");
        session.undo();
        view.sync();
        const auto patternID = session.pattern().itemID;
        view.selected = patternID;
        const auto patternPosition = session.pattern().getPosition();
        view.original = {patternPosition.time.getStart().inSeconds(), patternPosition.time.getEnd().inSeconds(),
                         patternPosition.offset.inSeconds()};
        view.preview = {1.0, 1.0 + patternPosition.time.getLength().inSeconds(), patternPosition.offset.inSeconds()};
        view.originalTrack = view.previewTrack = 0;
        view.dragging = true;
        auto dragPreviewPicture = view.createComponentSnapshot(view.getLocalBounds());
        int dragPreviewOldPixels = 0, dragPreviewNewPixels = 0;
        for (int y = 95; y < 122; ++y)
            for (int x = 155; x < 620; ++x)
            {
                if (!isMidiNotePixel(dragPreviewPicture.getPixelAt(x, y))) continue;
                if (x < view.xFor(0.8)) ++dragPreviewOldPixels;
                if (x >= view.xFor(1.0)) ++dragPreviewNewPixels;
            }
        require(dragPreviewOldPixels == 0 && dragPreviewNewPixels >= 25,
                "Dragged MIDI clip contents must follow the live preview before mouse-up");
        view.dragging = false;
        require(session.editClip(patternID, {0.0, 0.25, 0.0}, ClipGesture::trimRight).wasOk(),
                "Can create a very short MIDI clip for paint clipping");
        view.sync();
        auto shortMidiPicture = view.createComponentSnapshot(view.getLocalBounds());
        int escapedMidiPixels = 0;
        for (int y = 95; y < 122; ++y)
            for (int x = static_cast<int>(view.xFor(0.25)) + 4; x < 620; ++x)
                if (isMidiNotePixel(shortMidiPicture.getPixelAt(x, y))) ++escapedMidiPixels;
        require(escapedMidiPixels == 0, "Short MIDI clips must clip note bars to the visible clip bounds");
        session.undo();
        view.sync();
        view.selected = patternID;
        view.duplicateSelected();
        require(te::getAudioTracks(*session.edit)[0]->getClips().size() == 2, "Duplicate creates a second MIDI pattern clip");
        session.undo();
        require(te::getAudioTracks(*session.edit)[0]->getClips().size() == 1, "Undo duplicate restores one MIDI pattern clip");

        const auto runNativeRenderPeerTest = juce::SystemStats::getEnvironmentVariable("THETA_NATIVE_RENDER_TEST", {}) == "1";
        if (runNativeRenderPeerTest)
        {
            // isShowing() requires a visible desktop peer. Keep this tiny navigation
            // check offscreen and remove the peer before exercising clip gestures.
            view.setTopLeftPosition(-10000, -10000);
            view.addToDesktop(0);
            view.setVisible(true);
            juce::Thread::sleep(1);
       #if JUCE_WINDOWS
            // Exercise the native damage handoff, not just software image painting.
            // Simulate a new position arriving in vblank while earlier damage is
            // already queued. Both strips must reach D2D before vblank drawing.
            auto* peer = view.getPeer();
            const auto direct2D = peer->getAvailableRenderingEngines().indexOf("Direct2D");
            require(direct2D >= 0, "Windows rendering regression requires Direct2D");
            peer->setCurrentRenderingEngine(direct2D);
            const auto hwnd = static_cast<HWND>(peer->getNativeHandle());
            UpdateWindow(hwnd);
            view.repaint(150, 32, 6, 196);
            require(GetUpdateRect(hwnd, nullptr, FALSE) != 0, "Repaint queues native window damage");
            movePlayhead(view, view.playhead, 300, view.getLocalBounds().withTrimmedTop(32).withTrimmedBottom(18));
            require(GetUpdateRect(hwnd, nullptr, FALSE) == 0, "Playhead damage must reach Direct2D before the vblank callback returns");
       #endif
        }
        session.edit->getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(0.5));
        view.zoom(0.5, 0.5);
        require(view.playhead == view.xFor(0.5), "Zoom must preserve the fractional playhead position before the next vblank");
        require(close(playheadTime(session.edit->getTransport()), 0.5), "Stopped playhead follows the seek position");
        view.fit();
        require(view.playhead == view.xFor(0.5), "Fit must update the playhead immediately");
        session.stop();
        if (runNativeRenderPeerTest)
        {
            view.setVisible(false);
            view.removeFromDesktop();
            juce::Thread::sleep(1);
        }

        // Start asynchronous waveform scanning after the deterministic frame check.
        require(session.importAudio(source.getFile()).wasOk(), "Import audio");
        auto* clip = te::getAudioTracks(*session.edit)[1]->getClips()[0];
        const auto id = clip->itemID;
        view.sync();
        view.fit();
        view.selected = id;
        session.edit->getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(0.5));
        view.splitSelectedAtPlayhead();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 2, "Split creates a second audio clip");
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1, "Undo split restores one audio clip");
        clip = session.findAudioClip(id);
        require(clip != nullptr, "Original clip remains after undo split");
        view.sync();
        view.fit();
        view.selected = id;
        view.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.125), "Right arrow nudges selected audio by one sixteenth");
        session.undo();
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.0), "Undo restores nudge");
        view.snapSize.setSelectedId(2, juce::dontSendNotification);
        view.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.25), "Right arrow follows the selected one-eighth snap grid");
        session.undo();
        view.snapSize.setSelectedId(1, juce::dontSendNotification);
        view.duplicateSelected();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 2, "Duplicate creates a second audio clip");
        auto* duplicate = te::getAudioTracks(*session.edit)[1]->getClips().getLast();
        const auto originalEnd = clip->getPosition().time.getEnd().inSeconds();
        require(close(duplicate->getPosition().time.getStart().inSeconds(), originalEnd),
                "Duplicate places audio clips exactly after the source clip when space is available");
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1, "Undo duplicate restores one audio clip");
        clip = session.findAudioClip(id);
        require(clip != nullptr, "Original clip remains after undo duplicate");
        const auto originalColour = clip->getColour();
        view.keyPressed(juce::KeyPress('C'));
        require(clip->getColour() != originalColour, "C cycles the selected clip colour label");
        session.undo();
        const auto audioTrackSlots = static_cast<int>(session.deviceSlots(1).size());
        const auto clipSlots = session.clipPluginCount(id);
        view.sync();
        view.fit();
        view.itemDropped({"theta-browser:effect:ThetaSpace", nullptr,
                          {static_cast<int>(view.xFor(0.25)), static_cast<int>(view.lane(1).getCentreY())}});
        require(static_cast<int>(session.deviceSlots(1).size()) == audioTrackSlots,
                "Dropping an audio effect on an audio clip leaves the track rack unchanged");
        require(session.clipPluginCount(id) == clipSlots + 1,
                "Dropping an audio effect on an audio clip inserts clip-local FX");
        session.undo();

        const auto event = [&view](juce::Point<float> down, juce::Point<float> point, bool dragged,
                                   juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier))
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                mods, 1.0f, 0, 0, 0, 0,
                &view, &view, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
        };
        const auto drag = [&view, &event](juce::Point<float> down, juce::Point<float> to)
        {
            view.mouseDown(event(down, down, false));
            view.mouseDrag(event(down, to, true));
            view.mouseUp(event(down, to, true));
        };
        const auto automationParameters = session.deviceParameters(0, 0);
        require(!automationParameters.empty(), "Starter device exposes an automatable parameter");
        require(session.setClipAutomationRamp(id, {0, 0, 0}, 0.0, 0.75,
                                              automationParameters.front().minimum, automationParameters.front().maximum).wasOk(),
                "Can add automation before exercising clip gestures");
        view.sync();
        view.fit();
        drag({view.xFor(0.5), view.lane(1).getCentreY()}, {view.xFor(0.75), view.lane(1).getCentreY()});
        require(close(session.findAudioClip(id)->getPosition().time.getStart().inSeconds(), 0.25),
                "Automation overlays do not block a clip move gesture");
        session.undo();
        session.undo();
        view.sync();
        view.fit();
        view.selected = id;
        view.duplicateSelected();
        auto* snapDuplicate = te::getAudioTracks(*session.edit)[1]->getClips().getLast();
        const auto snapDuplicateID = snapDuplicate->itemID;
        require(session.editClip(snapDuplicateID, {1.35, 2.35, snapDuplicate->getPosition().offset.inSeconds()}, ClipGesture::move).wasOk(),
                "Can move duplicate away before testing clip-edge snap");
        view.sync();
        view.selected = snapDuplicateID;
        drag({view.xFor(1.85), view.lane(1).getCentreY()}, {view.xFor(1.46), view.lane(1).getCentreY()});
        require(close(session.findAudioClip(snapDuplicateID)->getPosition().time.getStart().inSeconds(), originalEnd),
                "Dragging a clip near a neighbor edge snaps it flush instead of overlapping");
        session.undo();
        session.undo();
        session.undo();
        require(session.editClip(id, {0.0, 0.5, 0.0}, ClipGesture::trimRight).wasOk(),
                "Can trim the source audio clip before testing trimmed edge snap");
        view.sync();
        view.selected = id;
        view.duplicateSelected();
        auto* trimmedDuplicate = te::getAudioTracks(*session.edit)[1]->getClips().getLast();
        const auto trimmedDuplicateID = trimmedDuplicate->itemID;
        require(close(trimmedDuplicate->getPosition().time.getStart().inSeconds(), 0.5),
                "Duplicate places trimmed audio clips directly after the visible clip end");
        require(session.editClip(trimmedDuplicateID, {1.35, 1.85, trimmedDuplicate->getPosition().offset.inSeconds()}, ClipGesture::move).wasOk(),
                "Can move trimmed duplicate away before testing edge snap");
        view.sync();
        view.selected = trimmedDuplicateID;
        drag({view.xFor(1.6), view.lane(1).getCentreY()}, {view.xFor(0.77), view.lane(1).getCentreY()});
        require(close(session.findAudioClip(trimmedDuplicateID)->getPosition().time.getStart().inSeconds(), 0.5),
                "Dragging a trimmed clip near a neighbor edge snaps it flush instead of overlapping");
        session.undo();
        session.undo();
        session.undo();
        session.undo();
        view.selected = id;
        const auto rulerY = view.rulerTop + 8.0f;
        drag({view.xFor(0.5), rulerY}, {view.xFor(1.5), rulerY});
        auto loopRange = session.edit->getTransport().getLoopRange();
        require(close(loopRange.getStart().inSeconds(), 0.5) && close(loopRange.getEnd().inSeconds(), 1.5),
                "Dragging the arrangement ruler selects the transport loop range");
        require(session.edit->getTransport().looping, "Ruler loop selection enables transport looping");
        session.refreshLoop();
        loopRange = session.edit->getTransport().getLoopRange();
        require(close(loopRange.getStart().inSeconds(), 0.5) && close(loopRange.getEnd().inSeconds(), 1.5),
                "Manual ruler loop selection is not overwritten by auto loop refresh");
        drag({view.xFor(1.25), rulerY}, {view.xFor(1.75), rulerY});
        loopRange = session.edit->getTransport().getLoopRange();
        require(close(loopRange.getStart().inSeconds(), 1.0) && close(loopRange.getEnd().inSeconds(), 2.0),
                "Dragging inside the ruler loop moves the range without changing its length");
        drag({view.xFor(1.0) + 1.0f, rulerY}, {view.xFor(0.75), rulerY});
        loopRange = session.edit->getTransport().getLoopRange();
        require(close(loopRange.getStart().inSeconds(), 0.75) && close(loopRange.getEnd().inSeconds(), 2.0),
                "Dragging the loop start edge expands the loop earlier");
        drag({view.xFor(2.0) - 1.0f, rulerY}, {view.xFor(1.75), rulerY});
        loopRange = session.edit->getTransport().getLoopRange();
        require(close(loopRange.getStart().inSeconds(), 0.75) && close(loopRange.getEnd().inSeconds(), 1.75),
                "Dragging the loop end edge trims the loop later edge");
        const auto rightClick = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
        view.mouseDown(event({view.xFor(1.0), rulerY}, {view.xFor(1.0), rulerY}, false, rightClick));
        require(!session.hasManualLoopRange(), "Right-clicking the ruler loop clears the manual loop range");
        view.mouseDown(event({view.xFor(0.25), rulerY}, {view.xFor(0.25), rulerY}, false));
        view.mouseUp(event({view.xFor(0.25), rulerY}, {view.xFor(0.25), rulerY}, false));
        require(close(playheadTime(session.edit->getTransport()), 0.25), "Clicking the ruler still seeks the playhead");
        view.mouseDown(event({40, 82}, {40, 82}, false));
        require(view.selectedTrack == 0, "Clicking the pattern lane selects the pattern track");
        view.mouseDown(event({40, 180}, {40, 180}, false));
        require(view.selectedTrack == 1 && notifiedTrack == 1, "Clicking the audio lane selects and broadcasts the audio track");
        view.selected = patternID;
        drag({240, 82}, {301, 82});
        require(close(session.pattern().getPosition().time.getStart().inSeconds(), 0.25), "Pointer drag moves MIDI pattern clips");
        session.undo();
        require(close(session.pattern().getPosition().time.getStart().inSeconds(), 0.0), "Undo restores MIDI pattern move");
        view.sync();
        view.selected = patternID;
        const auto patternEnd = session.pattern().getPosition().time.getEnd().inSeconds();
        drag({static_cast<float>(view.xFor(patternEnd) - 2.0), view.lane(0).getCentreY()},
             {static_cast<float>(view.xFor(0.75)), view.lane(0).getCentreY()});
        require(close(session.pattern().getPosition().time.getEnd().inSeconds(), 0.75), "Right trim shrinks MIDI clips");
        view.sync();
        drag({static_cast<float>(view.xFor(0.75) - 2.0), view.lane(0).getCentreY()},
             {static_cast<float>(view.xFor(patternEnd)), view.lane(0).getCentreY()});
        require(close(session.pattern().getPosition().time.getEnd().inSeconds(), patternEnd), "Right trim can expand a previously shrunken MIDI clip");
        session.undo();
        session.undo();
        view.itemDropped({"theta-browser:preset:MinimalKit", nullptr, {static_cast<int>(view.xFor(2.0)), 82}});
        auto* patternTrack = te::getAudioTracks(*session.edit)[0];
        const auto blockedClipCount = patternTrack->getClips().size();
        view.selected = patternID;
        view.duplicateSelected();
        require(patternTrack->getClips().size() == blockedClipCount + 1,
                "Duplicate creates a MIDI copy when the adjacent space is occupied");
        auto* gapCopy = patternTrack->getClips().getLast();
        require(close(gapCopy->getPosition().time.getStart().inSeconds(), 4.0),
                "Duplicate lands after the blocking clip when it cannot fit beside the source");
        view.sync();
        const auto clickX = static_cast<float>(view.xFor(gapCopy->getPosition().time.getStart().inSeconds() + 0.1));
        const auto clickY = view.lane(0).getCentreY();
        view.mouseDown(event({clickX, clickY}, {clickX, clickY}, false));
        view.mouseUp(event({clickX, clickY}, {clickX, clickY}, false));
        require(close(gapCopy->getPosition().time.getStart().inSeconds(), 4.0),
                "Clicking a gap-placed duplicate does not move it to the track end");
        session.undo();
        session.undo();
        view.selected = id;
        juce::StringArray dropped;
        dropped.add(source.getFile().getFullPathName());
        view.filesDropped(dropped, static_cast<int>(view.xFor(1.0)), 180);
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 2, "Dropping an audio file on the audio lane imports a clip");
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1, "Undo restores dropped audio import");
        require(session.trackCount() == 2, "Starter session has pattern and one audio track");
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
        session.togglePlayback();
        require(session.edit->getTransport().isPlaying(), "Transport starts before cross-track clip drag");
        drag({view.xFor(0.2), view.lane(1).getCentreY()}, {view.xFor(0.5), view.lane(2).getCentreY()});
        require(session.edit->getTransport().isPlaying(), "Cross-track clip drag keeps playback running");
        session.stop();
        session.undo();
        view.sync();
        drag({view.xFor(0.2), view.lane(1).getCentreY()}, {view.xFor(0.5), view.lane(session.trackCount() - 1).getBottom() + 12.0f});
        require(session.trackCount() == 4, "Dragging an audio clip below the last lane creates a new track");
        require(te::getAudioTracks(*session.edit)[3]->findClipForID(id) != nullptr, "Drop-created track receives the dragged audio clip");
        require(renderClipRms(*session.findAudioClip(id)) > 0.01f, "Drop-created track moved clip still renders audible audio");
        session.undo();
        require(session.trackCount() == 3, "Undo removes the track created by a clip drag");
        require(te::getAudioTracks(*session.edit)[1]->findClipForID(id) != nullptr, "Undo restores the clip after a drag-created track");
        view.sync();
        require(view.applyBrowserDrop("theta-browser:preset:SirenLead", 2).wasOk(), "Siren lead browser row creates a synth clip");
        require(te::getAudioTracks(*session.edit)[2]->getClips().getLast()->getName() == "Siren lead",
                "Dropped siren preset names the new clip");
        session.undo();
        require(view.applyBrowserDrop("theta-browser:preset:ChordPad", 2).wasOk(), "Chord pad browser row creates a synth clip");
        require(te::getAudioTracks(*session.edit)[2]->getClips().getLast()->getName() == "Chord pad",
                "Dropped chord pad preset names the new clip");
        session.undo();
        require(view.applyBrowserDrop("theta-browser:preset:ReeseBass", 2).wasOk(), "Reese bass browser row creates a synth clip");
        require(te::getAudioTracks(*session.edit)[2]->getClips().getLast()->getName() == "Reese bass",
                "Dropped Reese bass preset names the new clip");
        session.undo();
        require(view.applyBrowserDrop("theta-browser:preset:ClapKit", 2).wasOk(), "Clap kit browser row creates a drum clip");
        require(session.selectPatternClip(te::getAudioTracks(*session.edit)[2]->getClips().getLast()->itemID).wasOk(),
                "Dropped clap preset can be selected for note editing");
        require(session.hasNote(4, 56), "Dropped clap preset selects an editable clip with clap notes");
        session.undo();
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
        StepGrid selectionGrid(session);
        selectionGrid.setSize(1000, 250);
        selectionGrid.changeListenerCallback(nullptr);
        const auto defaultCellWidth = selectionGrid.cell(1, 0).getWidth();
        session.setEditorStepCount(32);
        selectionGrid.changeListenerCallback(nullptr);
        require(selectionGrid.horizontalScroll.isVisible()
                && close(selectionGrid.visibleStepSpan(), static_cast<double>(Session::defaultSteps))
                && std::abs(selectionGrid.cell(1, 0).getWidth() - defaultCellWidth) < 0.1f,
                "Fine editor grids zoom horizontally instead of shrinking cells");
        session.setEditorStepCount(Session::defaultSteps);
        selectionGrid.changeListenerCallback(nullptr);
        require(close(selectionGrid.playheadXForTime(0.5), 54.0),
                "Note editor playhead starts at the selected clip even after bar one");
        require(close(selectionGrid.playheadXForTime(1.5), 527.0),
                "Note editor playhead follows selected clip-local beats");
        const auto selectedGridNotes = selectionGrid.notes;
        require(session.pattern().getSequence().getNumNotes() == 4, "Selected browser synth clip owns its preset notes");
        require(selectedGridNotes.count() == 4, "Selected browser synth clip populates the note editor");
        require(!session.isPatternDrums(), "Selected browser synth clip uses the note editor");
        require(session.selectPatternClip(patternID).wasOk(), "Can switch the editor back to the first pattern clip");
        require(session.selectPatternClip(view.selected).wasOk(), "Can switch the editor back to the non-first MIDI clip");
        selectionGrid.changeListenerCallback(nullptr);
        require(selectionGrid.notes == selectedGridNotes, "Switching clips preserves the selected clip's note editor pattern");
        require(!session.isPatternDrums(), "Switching back restores the selected clip's instrument mode");
        const auto selectedClipNotes = session.pattern().getSequence().getNumNotes();
        session.setNote(1, 48, true);
        require(session.pattern().getSequence().getNumNotes() == selectedClipNotes + 1,
                "Note editor writes into the selected non-first MIDI clip");
        selectionGrid.changeListenerCallback(nullptr);
        const auto gridEvent = [&selectionGrid](juce::Point<float> down, juce::Point<float> point, bool dragged,
                                                juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier))
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                mods, 1.0f, 0, 0, 0, 0,
                &selectionGrid, &selectionGrid, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
        };
        const auto rowForPitch = [&selectionGrid](int pitch)
        {
            return selectionGrid.lowestVisiblePitch + Session::pitches - 1 - pitch;
        };
        const auto resizeCell = selectionGrid.cell(1, rowForPitch(48));
        const juce::Point<float> resizeHandle(resizeCell.getRight() - 3.0f, resizeCell.getCentreY());
        const auto resizeTarget = selectionGrid.cell(4, rowForPitch(48)).getCentre();
        selectionGrid.mouseDown(gridEvent(resizeHandle, resizeHandle, false));
        selectionGrid.mouseDrag(gridEvent(resizeHandle, resizeTarget, true));
        selectionGrid.mouseUp(gridEvent(resizeHandle, resizeTarget, true));
        selectionGrid.changeListenerCallback(nullptr);
        require(session.noteLengthSteps(1, 48) == 4
                && selectionGrid.noteLengths[static_cast<size_t>(selectionGrid.indexForCell(1, 48))] == 4,
                "Dragging a note edge resizes its musical length in the editor");
        const auto splitPoint = selectionGrid.cell(3, rowForPitch(48)).getCentre();
        selectionGrid.mouseDown(gridEvent(splitPoint, splitPoint, false));
        selectionGrid.mouseUp(gridEvent(splitPoint, splitPoint, false));
        selectionGrid.changeListenerCallback(nullptr);
        require(session.noteLengthSteps(1, 48) == 2 && session.hasNote(3, 48),
                "Clicking inside a sustained note creates an independent note and shortens the sustain");
        session.undo();
        session.undo();
        selectionGrid.changeListenerCallback(nullptr);
        const auto moveFrom = selectionGrid.cell(1, rowForPitch(48)).getCentre();
        const auto moveTo = selectionGrid.cell(2, rowForPitch(48)).getCentre();
        selectionGrid.mouseDown(gridEvent(moveFrom, moveFrom, false));
        selectionGrid.mouseDrag(gridEvent(moveFrom, moveTo, true));
        selectionGrid.mouseUp(gridEvent(moveFrom, moveTo, true));
        selectionGrid.changeListenerCallback(nullptr);
        require(!session.hasNote(1, 48) && session.hasNote(2, 48),
                "Dragging a note body moves it without requiring a modifier");
        session.undo();
        selectionGrid.changeListenerCallback(nullptr);
        const auto commandLeft = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);
        selectionGrid.mouseDown(gridEvent(selectionGrid.cell(4, rowForPitch(55)).getCentre(),
                                          selectionGrid.cell(4, rowForPitch(55)).getCentre(), false, commandLeft));
        selectionGrid.mouseDown(gridEvent(selectionGrid.cell(8, rowForPitch(60)).getCentre(),
                                          selectionGrid.cell(8, rowForPitch(60)).getCentre(), false, commandLeft));
        require(selectionGrid.selectedNotes.count() == 2, "Command-click selects multiple note cells");
        require(selectionGrid.keyPressed(juce::KeyPress('C', juce::ModifierKeys::commandModifier, 'c')),
                "Command-C copies selected notes");
        require(selectionGrid.keyPressed(juce::KeyPress('V', juce::ModifierKeys::commandModifier, 'v')),
                "Command-V pastes selected notes");
        require(session.hasNote(5, 55) && session.hasNote(9, 60), "Pasted notes keep the selected shape one step later");
        require(selectionGrid.selectedNotes.count() == 2, "Pasted notes become the active selection");
        require(selectionGrid.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)),
                "Delete removes selected notes");
        require(!session.hasNote(5, 55) && !session.hasNote(9, 60), "Deleted pasted notes are removed from the clip");
        session.undo();
        require(session.hasNote(5, 55) && session.hasNote(9, 60), "Undo restores notes removed by multi-selection delete");
        selectionGrid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        require(selectionGrid.selectedNotes.none(), "Escape clears note selection");
        const auto ctrlLeft = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier);
        selectionGrid.mouseDown(gridEvent(selectionGrid.cell(4, rowForPitch(55)).getCentre(),
                                          selectionGrid.cell(4, rowForPitch(55)).getCentre(), false, ctrlLeft));
        selectionGrid.mouseDown(gridEvent(selectionGrid.cell(8, rowForPitch(60)).getCentre(),
                                          selectionGrid.cell(8, rowForPitch(60)).getCentre(), false, ctrlLeft));
        require(selectionGrid.selectedNotes.count() == 2, "Ctrl-click selects multiple note cells on Windows");
        require(selectionGrid.keyPressed(juce::KeyPress('C', juce::ModifierKeys::ctrlModifier, 'c')),
                "Ctrl-C copies selected notes");
        require(selectionGrid.keyPressed(juce::KeyPress('V', juce::ModifierKeys::ctrlModifier, 'v')),
                "Ctrl-V pastes selected notes");
        selectionGrid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        const auto sourceCell = selectionGrid.cell(1, rowForPitch(48)).getCentre();
        const auto targetCell = selectionGrid.cell(1, rowForPitch(50)).getCentre();
        const auto shiftLeft = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
        selectionGrid.mouseDown(gridEvent(sourceCell, sourceCell, false, shiftLeft));
        selectionGrid.mouseDrag(gridEvent(sourceCell, targetCell, true, shiftLeft));
        selectionGrid.mouseUp(gridEvent(sourceCell, targetCell, true, shiftLeft));
        require(!session.hasNote(1, 48) && session.hasNote(1, 50),
                "Shift-dragging an existing note vertically changes its pitch");
        session.undo();
        require(session.hasNote(1, 48) && !session.hasNote(1, 50),
                "Undo restores note pitch after a grid drag");
        selectionGrid.changeListenerCallback(nullptr);
        const auto eraseCellA = selectionGrid.cell(1, rowForPitch(48)).getCentre();
        const auto eraseCellB = selectionGrid.cell(3, rowForPitch(48)).getCentre();
        selectionGrid.mouseDown(gridEvent(eraseCellA, eraseCellA, false));
        selectionGrid.mouseDrag(gridEvent(eraseCellA, eraseCellB, true));
        selectionGrid.mouseUp(gridEvent(eraseCellA, eraseCellB, true));
        require(!session.hasNote(1, 48) && !session.hasNote(2, 48) && !session.hasNote(3, 48),
                "Dragging from an existing note still erases notes under the cursor");
        session.undo();
        require(session.hasNote(1, 48), "Undo restores notes erased by a grid stroke");
        session.undo();
        const auto audio2DevicesAfterSound = session.deviceSlots(2).size();
        require(view.applyBrowserDrop("theta-browser:effect:ThetaBloom", 2).wasOk(), "Theta Bloom browser row creates an audio effect");
        require(session.deviceSlots(2).size() == audio2DevicesAfterSound + 1, "Dropped Theta Bloom appears on the target track");
        session.undo();
        require(view.applyBrowserDrop("theta-browser:instrument:Drums", 2).wasOk(), "Instrument browser rows can be dropped onto non-first tracks");
        require(session.deviceSlots(2).size() == audio2DevicesAfterSound + 1, "Dropped instrument appears on the target track");
        session.undo();
        const auto instrumentClipCount = te::getAudioTracks(*session.edit)[2]->getClips().size();
        view.itemDropped({"theta-browser:instrument:FourOsc", nullptr,
                          {static_cast<int>(view.xFor(1.0)), static_cast<int>(view.lane(2).getCentreY())}});
        require(te::getAudioTracks(*session.edit)[2]->getClips().size() == instrumentClipCount + 1,
                "Dragging an instrument onto the arrangement creates an editable MIDI clip");
        require(session.pattern().itemID == te::getAudioTracks(*session.edit)[2]->getClips().getLast()->itemID,
                "Dropped instrument clip becomes the note editor target");
        session.undo();
        require(session.removeAudioTrack(2).wasOk(), "Can remove the extra audio track");
        require(session.trackCount() == 2, "Removing extra audio track restores starter track count");
        view.filesDropped(dropped, static_cast<int>(view.xFor(1.0)), view.getHeight() - 3);
        require(session.trackCount() == 3, "Dropping audio below the lanes creates a new audio track");
        require(te::getAudioTracks(*session.edit)[2]->getClips().size() == 1, "Drop-created track receives the audio clip");
        session.undo();
        session.undo();
        require(session.trackCount() == 2, "Undo removes the drop-created audio track");

        // Fit is four seconds wide here. Exercise actual component hit testing,
        // gesture preview, and commit rather than calling only the model facade.
        view.mouseDown(event({240, 180}, {240, 180}, false));
        view.mouseDrag(event({240, 180}, {346.5f, 180}, true));
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.0), "Drag must not rebuild live clip before release");
        view.mouseUp(event({240, 180}, {346.5f, 180}, true));
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.5), "Pointer drag moves audio");
        require(close(clip->getPosition().offset.inSeconds(), 0.0), "Move preserves source offset");
        session.undo();
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.0), "One undo restores move");
        session.redo();

        drag({view.xFor(0.5) + 2.0f, 180}, {view.xFor(0.75), 180});
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.75), "Left handle trims start");
        require(close(clip->getPosition().offset.inSeconds(), 0.25), "Left trim advances source offset");
        drag({view.xFor(1.5) - 2.0f, 180}, {view.xFor(1.25), 180});
        require(close(clip->getPosition().time.getEnd().inSeconds(), 1.25), "Right handle trims end");
        require(close(clip->getPosition().offset.inSeconds(), 0.25), "Right trim preserves source offset");
        drag({view.xFor(1.25) - 2.0f, 180}, {view.xFor(1.5), 180});
        require(close(clip->getPosition().time.getEnd().inSeconds(), 1.5), "Right handle expands audio to available source end");
        drag({view.xFor(0.75) + 2.0f, 180}, {view.xFor(0.5), 180});
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.5), "Left handle expands audio to available source start");
        require(close(clip->getPosition().offset.inSeconds(), 0.0), "Left expansion rewinds source offset");
        session.undo();
        session.undo();
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.75), "Undo restores left expansion");
        require(close(clip->getPosition().time.getEnd().inSeconds(), 1.25), "Undo restores right expansion");
        const auto originalPosition = clip->getPosition();
        view.mouseDown(event({360, 180}, {360, 180}, false));
        view.mouseDrag(event({360, 180}, {500, 180}, true));
        view.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        view.mouseUp(event({360, 180}, {500, 180}, true));
        require(close(clip->getPosition().time.getStart().inSeconds(), originalPosition.time.getStart().inSeconds()), "Escape cancels preview");

        const ClipGeometry geometry {0.75, 1.25, 0.25};
        require(close(previewClipEdit(geometry, ClipGesture::trimLeft, -5, 1).start, 0.5), "Left extension stops at source zero");
        require(close(previewClipEdit(geometry, ClipGesture::trimRight, 99, 1).end, 1.5), "Right extension stops at source end");
        require(close(previewClipEdit(geometry, ClipGesture::move, -5, 1).start, 0), "Move stops at timeline zero");

        // Check the visible waveform, using the real thumbnail scanner. This is
        // a tiny rendering correctness check, not a frame-rate benchmark.
        juce::Image picture;
        int waveformPixels = 0;
        for (int attempt = 0; attempt < 100 && waveformPixels < 100; ++attempt)
        {
            picture = view.createComponentSnapshot(view.getLocalBounds());
            waveformPixels = 0;
            for (int y = 175; y < 215; ++y)
                for (int x = 312; x < 410; ++x)
                    if (picture.getPixelAt(x, y) == juce::Colour(0xff8cc5d2)) ++waveformPixels;
            if (waveformPixels < 100) juce::Thread::sleep(10);
        }
        require(waveformPixels >= 100, "Imported audio must draw a visible waveform");
        const auto screenshot = juce::SystemStats::getEnvironmentVariable("THETA_UI_SNAPSHOT", {});
        if (screenshot.isNotEmpty())
        {
            auto stream = juce::File(screenshot).createOutputStream();
            require(stream != nullptr && juce::PNGImageFormat().writeImageToStream(picture, *stream), "Write arrangement snapshot");
        }

        session.toggleTrackMute(1);
        require(te::getAudioTracks(*session.edit)[1]->isMuted(false), "Audio mute");
        session.undo();
        require(!te::getAudioTracks(*session.edit)[1]->isMuted(false), "Undo mute");
        session.toggleTrackSolo(1);
        require(te::getAudioTracks(*session.edit)[1]->isSolo(false), "Audio solo");
        session.undo();
        view.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
        require(session.findAudioClip(id) == nullptr, "Delete selected clip");
        session.undo();
        require(session.findAudioClip(id) != nullptr, "Undo restores deleted clip");
        const auto saved = session.projectSnapshot();
        juce::TemporaryFile project(".thetaedit");
        require(session.restoreProject(saved, project.getFile()).wasOk(), "Reopen arranged project");
        clip = session.findAudioClip(id);
        require(clip != nullptr, "Clip identity survives reopen");
        require(close(clip->getPosition().offset.inSeconds(), 0.25), "Source offset survives reopen");

        juce::TemporaryFile rendered(".wav");
        te::Renderer::Parameters parameters(*session.edit);
        parameters.destFile = rendered.getFile();
        parameters.audioFormat = &wav;
        parameters.sampleRateForAudio = 48000;
        parameters.bitDepth = 24;
        parameters.time = clip->getPosition().time;
        {
            te::Renderer::RenderTask task("Trimmed audio test", parameters, nullptr, nullptr);
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
            while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                require(juce::Time::getMillisecondCounterHiRes() < deadline, "Render timeout");
            require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
        }
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(rendered.getFile()));
        require(reader != nullptr, "Read trimmed render");
        require(std::abs(static_cast<double>(reader->lengthInSamples) - 24000.0) <= 512.0, "Render matches trimmed duration");
        juce::AudioBuffer<float> output(2, static_cast<int>(reader->lengthInSamples));
        require(reader->read(&output, 0, output.getNumSamples(), 0, true, true), "Read trimmed samples");
        const auto early = output.getRMSLevel(0, 2400, 4800);
        const auto late = output.getRMSLevel(0, 14400, 4800);
        require(early > 0.01f && late > early * 4.0f, "Rendered source window must match the visible trim");
        view.setLookAndFeel(nullptr);
        session.releaseAudioDevice();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
