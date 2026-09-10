#include "Arrangement.h"
#include "Theme.h"
#include "StepGrid.h"
#include "Playhead.h"
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
        const auto patternID = session.pattern().itemID;
        view.selected = patternID;
        view.duplicateSelected();
        require(te::getAudioTracks(*session.edit)[0]->getClips().size() == 2, "Duplicate creates a second MIDI pattern clip");
        session.undo();
        require(te::getAudioTracks(*session.edit)[0]->getClips().size() == 1, "Undo duplicate restores one MIDI pattern clip");

        // isShowing() requires a visible desktop peer. Keep this tiny navigation
        // check offscreen and remove the peer before exercising clip gestures.
        view.setTopLeftPosition(-10000, -10000);
        view.addToDesktop(0);
        view.setVisible(true);
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
        session.edit->getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(0.5));
        view.zoom(0.5, 0.5);
        require(view.playhead == view.xFor(0.5), "Zoom must preserve the fractional playhead position before the next vblank");
        require(close(playheadTime(session.edit->getTransport()), 0.5), "Stopped playhead follows the seek position");
        view.fit();
        require(view.playhead == view.xFor(0.5), "Fit must update the playhead immediately");
        session.stop();
        view.setVisible(false);
        view.removeFromDesktop();

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

        const auto event = [&view](juce::Point<float> down, juce::Point<float> point, bool dragged)
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                &view, &view, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
        };
        const auto drag = [&view, &event](juce::Point<float> down, juce::Point<float> to)
        {
            view.mouseDown(event(down, down, false));
            view.mouseDrag(event(down, to, true));
            view.mouseUp(event(down, to, true));
        };
        view.mouseDown(event({40, 82}, {40, 82}, false));
        require(view.selectedTrack == 0, "Clicking the pattern lane selects the pattern track");
        view.mouseDown(event({40, 180}, {40, 180}, false));
        require(view.selectedTrack == 1 && notifiedTrack == 1, "Clicking the audio lane selects and broadcasts the audio track");
        view.selected = patternID;
        drag({240, 82}, {301, 82});
        require(close(session.pattern().getPosition().time.getStart().inSeconds(), 0.25), "Pointer drag moves MIDI pattern clips");
        session.undo();
        require(close(session.pattern().getPosition().time.getStart().inSeconds(), 0.0), "Undo restores MIDI pattern move");
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
        view.selected = id;
        drag({view.xFor(0.2), view.lane(1).getCentreY()}, {view.xFor(0.5), view.lane(2).getCentreY()});
        require(te::getAudioTracks(*session.edit)[1]->getClips().isEmpty(), "Dragging an audio clip to another lane removes it from the source track");
        require(te::getAudioTracks(*session.edit)[2]->findClipForID(id) != nullptr, "Dragging an audio clip to another lane preserves clip identity on the target track");
        require(close(session.findAudioClip(id)->getPosition().time.getStart().inSeconds(), 0.25), "Cross-track audio drag also updates the clip time");
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->findClipForID(id) != nullptr, "Undo restores the clip to its original track");
        require(te::getAudioTracks(*session.edit)[2]->findClipForID(id) == nullptr, "Undo removes the clip from the drag target track");
        view.sync();
        drag({view.xFor(0.2), view.lane(1).getCentreY()}, {view.xFor(0.5), view.lane(session.trackCount() - 1).getBottom() + 12.0f});
        require(session.trackCount() == 4, "Dragging an audio clip below the last lane creates a new track");
        require(te::getAudioTracks(*session.edit)[3]->findClipForID(id) != nullptr, "Drop-created track receives the dragged audio clip");
        session.undo();
        require(session.trackCount() == 3, "Undo removes the track created by a clip drag");
        require(te::getAudioTracks(*session.edit)[1]->findClipForID(id) != nullptr, "Undo restores the clip after a drag-created track");
        view.sync();
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
        const auto gridEvent = [&selectionGrid](juce::Point<float> down, juce::Point<float> point, bool dragged)
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                &selectionGrid, &selectionGrid, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
        };
        const auto rowForPitch = [&selectionGrid](int pitch)
        {
            return selectionGrid.lowestVisiblePitch + Session::pitches - 1 - pitch;
        };
        const auto sourceCell = selectionGrid.cell(1, rowForPitch(48)).getCentre();
        const auto targetCell = selectionGrid.cell(1, rowForPitch(50)).getCentre();
        selectionGrid.mouseDown(gridEvent(sourceCell, sourceCell, false));
        selectionGrid.mouseDrag(gridEvent(sourceCell, targetCell, true));
        selectionGrid.mouseUp(gridEvent(sourceCell, targetCell, true));
        require(!session.hasNote(1, 48) && session.hasNote(1, 50),
                "Dragging an existing note vertically changes its pitch");
        session.undo();
        require(session.hasNote(1, 48) && !session.hasNote(1, 50),
                "Undo restores note pitch after a grid drag");
        session.undo();
        const auto audio2DevicesAfterSound = session.deviceSlots(2).size();
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
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
