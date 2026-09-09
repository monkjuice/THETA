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

        // Compare incremental frames to complete renders, including fractional
        // Windows display scales, wraparound, seeks, and hiding the playhead.
        StepGrid grid(session);
        grid.setSize(1000, 250);
        const auto checkPlayhead = [&require](juce::Component& panel, int& position, juce::Rectangle<int> area)
        {
            for (const auto scale : {1.0f, 1.25f, 1.5f, 2.0f})
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
                for (const auto next : {150, 151, 155, 420, 998, 150, -1})
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
                                std::fprintf(stderr, "%s scale %.2f next %d pixel %d,%d actual %s expected %s\n",
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
        require(view.playhead == static_cast<int>(view.xFor(0.5)), "Zoom must keep the playhead visible before the next vblank");
        view.fit();
        require(view.playhead == static_cast<int>(view.xFor(0.5)), "Fit must update the playhead immediately");
        session.stop();
        view.setVisible(false);
        view.removeFromDesktop();

        // Start asynchronous waveform scanning after the deterministic frame check.
        require(session.importAudio(source.getFile()).wasOk(), "Import audio");
        auto* clip = te::getAudioTracks(*session.edit)[1]->getClips()[0];
        const auto id = clip->itemID;
        view.sync();
        view.fit();

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

        drag({255.5f, 180}, {308.75f, 180});
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.75), "Left handle trims start");
        require(close(clip->getPosition().offset.inSeconds(), 0.25), "Left trim advances source offset");
        drag({466.5f, 180}, {413.25f, 180});
        require(close(clip->getPosition().time.getEnd().inSeconds(), 1.25), "Right handle trims end");
        require(close(clip->getPosition().offset.inSeconds(), 0.25), "Right trim preserves source offset");
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
