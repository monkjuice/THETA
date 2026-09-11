#include "Session.h"
#include "StepGrid.h"
#include "ProjectFiles.h"
#include "Theme.h"
#include "Arrangement.h"
#include "BrowserPanel.h"
#include "DeviceRack.h"
#include "StartupScreen.h"
#include <cmath>
#include <stdexcept>

namespace theta
{
juce::File thetaLogFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Theta").getChildFile("theta.log");
}

void avoidLegacyDirectSound(te::Engine& engine)
{
   #if JUCE_WINDOWS
    auto& manager = engine.getDeviceManager().deviceManager;
    const auto currentType = manager.getCurrentAudioDeviceType();
    if (currentType.isNotEmpty() && currentType != "DirectSound")
        return;

    for (auto* type : manager.getAvailableDeviceTypes())
        if (type != nullptr && type->getTypeName() == "Windows Audio")
        {
            juce::Logger::writeToLog("Theta: using Windows Audio instead of legacy DirectSound");
            manager.setCurrentAudioDeviceType("Windows Audio", true);
            return;
        }
   #else
    juce::ignoreUnused(engine);
   #endif
}

void prepareCommandLineAudio()
{
   #if JUCE_WINDOWS
    te::Engine testEngine {"Theta Native Tests"};
    avoidLegacyDirectSound(testEngine);
    testEngine.getDeviceManager().deviceManager.closeAudioDevice();
   #endif
}

class ControlWindow final : public juce::Component,
                            public juce::DragAndDropContainer,
                            private Session::Listener,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit ControlWindow(Session& s) : session(s), browser(s), grid(s), arrangement(s), rack(s), files(s)
    {
        setOpaque(true);
        files.status = [this](const juce::String& message) { logStatus(message); };
        files.loadingChanged = [this](bool loading) { setEnabled(!loading); };
        arrangement.status = files.status;
        arrangement.trackSelected = [this](int track) { rack.selectTrack(track); };
        browser.status = files.status;
        rack.status = files.status;
        browserToggle.onClick = [this] { browserOpen = !browserOpen; resized(); repaint(); };
        rackToggle.onClick = [this] { rackOpen = !rackOpen; resized(); repaint(); };
        browserToggle.setTooltip("Hide browser");
        rackToggle.setTooltip("Hide device rack");
        editorResolution.addItem("1/16", 16);
        editorResolution.addItem("1/32", 32);
        editorResolution.addItem("1/64", 64);
        editorResolution.setJustificationType(juce::Justification::centred);
        editorResolution.onChange = [this]
        {
            if (!updatingEditorResolution && editorResolution.getSelectedId() > 0)
                session.setEditorStepCount(editorResolution.getSelectedId());
        };
        for (auto* toggle : {&browserToggle, &rackToggle})
        {
            toggle->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252b31));
            toggle->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff252b31));
            toggle->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaeb8c1));
            toggle->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffdce5ea));
        }
        open.onClick = [this] { files.open(); };
        save.onClick = [this] { files.save(); };
        title.setText("THETA", juce::dontSendNotification);
        title.setFont(juce::FontOptions(22.0f));
        logStatus("PATTERN 1  /  4OSC     Draw notes, then press Play");
        gainLabel.setText("SYNTH GAIN", juce::dontSendNotification);
        audioGainLabel.setText("AUDIO GAIN", juce::dontSendNotification);
        hint.setText({}, juce::dontSendNotification);
        hint.setVisible(false);
        hint.setColour(juce::Label::textColourId, juce::Colour(0xff8d98a3));
        tempo.setSliderStyle(juce::Slider::IncDecButtons);
        tempo.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 90, 30);
        tempo.setRange(40.0, 240.0, 1.0);
        tempo.setValue(session.tempo(), juce::dontSendNotification);
        tempo.setTextValueSuffix(" BPM");
        tempo.onValueChange = [this] { session.setTempo(tempo.getValue()); };
        undo.onClick = [this] { session.undo(); };
        redo.onClick = [this] { session.redo(); };
        clear.onClick = [this] { session.clearPattern(); };
        undo.setButtonText(L"\u21b6");
        redo.setButtonText(L"\u21b7");
        clear.setButtonText(L"\u00d7");
        undo.setTooltip("Undo");
        redo.setTooltip("Redo");
        clear.setTooltip("Clear pattern");
        gain.setSliderStyle(juce::Slider::LinearHorizontal);
        gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 85, 26);
        gain.setRange(-60.0, 6.0, 0.1);
        gain.setValue(session.utility->gain().getCurrentValue(), juce::dontSendNotification);
        gain.setTextValueSuffix(" dB");
        gain.setDoubleClickReturnValue(true, 0.0);
        gain.onDragStart = [this]
        {
            session.edit->getUndoManager().beginNewTransaction("Synth gain");
            session.utility->gain().parameterChangeGestureBegin();
        };
        gain.onDragEnd = [this]
        {
            session.utility->gain().parameterChangeGestureEnd();
            session.edit->getUndoManager().beginNewTransaction();
        };
        gain.onValueChange = [this]
        {
            session.utility->gain().setParameter(static_cast<float>(gain.getValue()), juce::sendNotification);
            session.markModified();
        };
        audioGain.setSliderStyle(juce::Slider::LinearHorizontal);
        audioGain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 85, 26);
        audioGain.setRange(-60.0, 6.0, 0.1);
        audioGain.setValue(session.audioUtility->gain().getCurrentValue(), juce::dontSendNotification);
        audioGain.setTextValueSuffix(" dB");
        audioGain.setDoubleClickReturnValue(true, 0.0);
        audioGain.onDragStart = [this]
        {
            session.edit->getUndoManager().beginNewTransaction("Audio gain");
            session.audioUtility->gain().parameterChangeGestureBegin();
        };
        audioGain.onDragEnd = [this]
        {
            session.audioUtility->gain().parameterChangeGestureEnd();
            session.edit->getUndoManager().beginNewTransaction();
        };
        audioGain.onValueChange = [this]
        {
            session.audioUtility->gain().setParameter(static_cast<float>(audioGain.getValue()), juce::sendNotification);
            session.markModified();
        };
        play.onClick = [this] { session.togglePlayback(); };
        stop.onClick = [this] { session.stop(); };
        panic.onClick = [this]
        {
            session.panicReset();
            logStatus("Panic reset: stopped transport, reset plugins, restarted audio device");
        };
        import.onClick = [this] { chooseAudio(); };
        play.setButtonText(L"\u25b6");
        stop.setButtonText(L"\u25a0");
        panic.setButtonText("!");
        import.setButtonText(L"\uff0b");
        play.setTooltip("Play or pause");
        stop.setTooltip("Stop and return to start");
        panic.setTooltip("Panic reset audio");
        import.setTooltip("Add audio");
        settings.onClick = [this]
        {
            if (audioSettings != nullptr)
            {
                audioSettings->toFront(true);
                return;
            }
            auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
                session.engine.getDeviceManager().deviceManager, 0, 2, 0, 2, true, false, true, false);
            selector->setSize(520, 420);
            juce::DialogWindow::LaunchOptions options;
            options.content.setOwned(selector.release());
            options.dialogTitle = "Audio settings";
            options.dialogBackgroundColour = juce::Colour(0xff202327);
            options.useNativeTitleBar = true;
            audioSettings = options.launchAsync();
        };
        for (auto* component : std::initializer_list<juce::Component*>{
                 &title, &status, &position, &gainLabel, &gain, &audioGainLabel, &audioGain, &play, &stop, &panic, &import, &settings,
                 &browser, &browserToggle, &rackToggle, &grid, &arrangement, &rack, &tempo, &undo, &redo, &clear, &hint, &open, &save,
                 &documentName, &patternLabel, &editorResolution})
            addAndMakeVisible(component);
        session.edit->getTransport().addChangeListener(this);
        session.addChangeListener(this);
        session.listeners.add(this);
        session.edit->getUndoManager().addChangeListener(this);
        patternLabel.setText("PATTERN 1  /  NOTE EDITOR", juce::dontSendNotification);
        patternLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb8c4aa));
        setSize(1120, 760);
        changeListenerCallback(nullptr);
        // This updates a text readout only. Pointer events and control painting
        // are not throttled to this timer; there is no full-window repaint loop.
        startTimerHz(10);
    }

    ~ControlWindow() override
    {
        stopTimer();
        delete audioSettings.getComponent();
        session.edit->getTransport().removeChangeListener(this);
        session.removeChangeListener(this);
        session.listeners.remove(this);
        session.edit->getUndoManager().removeChangeListener(this);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff171a1e));
        const auto bottomX = (browserOpen ? browserWidth : collapsedRailWidth) + 18;
        g.setColour(juce::Colour(0xff24282d));
        g.fillRect(bottomX, getHeight() - 64, getWidth() - bottomX - 24 - (rackOpen ? 0 : collapsedRailWidth), 44);
        if (!browserOpen)
        {
            g.setColour(juce::Colour(0xff11161b));
            g.fillRect(0, browserTop, collapsedRailWidth, getHeight() - browserTop);
        }
        if (browserOpen)
        {
            g.setColour(juce::Colour(0xff3a434b));
            g.fillRect(browserWidth, browserTop, 4, getHeight() - browserTop);
        }
        if (!rackOpen)
        {
            g.setColour(juce::Colour(0xff11161b));
            g.fillRect(getWidth() - collapsedRailWidth, browserTop, collapsedRailWidth, getHeight() - browserTop);
        }
        if (rackOpen)
        {
            g.setColour(juce::Colour(0xff3a434b));
            g.fillRect(rackSplitterBounds());
        }
        g.setColour(juce::Colour(0xff3a434b));
        g.fillRect(arrangementSplitterBounds());
    }

    void resized() override
    {
        constexpr int gap = 18;
        const auto leftWidth = browserOpen ? browserWidth : collapsedRailWidth;
        const auto rightRail = rackOpen ? 0 : collapsedRailWidth;
        const auto editorX = leftWidth + gap;
        const auto editorW = getWidth() - editorX - 24 - rightRail;
        const auto arrangementTop = 132;
        arrangementHeight = juce::jlimit(150, std::max(150, getHeight() - 350), arrangementHeight);
        const auto arrangementBottom = arrangementTop + arrangementHeight;
        const auto lowerTop = arrangementBottom + 34;
        const auto bottomPanelTop = getHeight() - 64;
        const auto lowerH = std::max(112, bottomPanelTop - lowerTop - 14);
        const auto lowerW = rackOpen ? std::max(300, editorW - rackWidth - gap) : editorW;
        title.setBounds(24, 10, 150, 30);
        documentName.setBounds(190, 10, getWidth() - 530, 30);
        settings.setBounds(getWidth() - 152, 12, 128, 28);
        open.setBounds(getWidth() - 320, 12, 72, 28);
        save.setBounds(getWidth() - 240, 12, 72, 28);
        status.setBounds(24, 42, getWidth() - 48, 24);
        play.setBounds(editorX, 82, 38, 34);
        stop.setBounds(editorX + 46, 82, 38, 34);
        panic.setBounds(editorX + 92, 82, 38, 34);
        import.setBounds(editorX + 138, 82, 38, 34);
        tempo.setBounds(editorX + 198, 84, 122, 30);
        undo.setBounds(editorX + 340, 84, 34, 30);
        redo.setBounds(editorX + 380, 84, 34, 30);
        clear.setBounds(editorX + 424, 84, 34, 30);
        position.setBounds(getWidth() - 165, 82, 140, 34);
        browser.setVisible(browserOpen);
        browser.setBounds(0, browserTop, browserWidth, getHeight() - browserTop);
        browserToggle.setButtonText(browserOpen ? "<" : "B");
        browserToggle.setTooltip(browserOpen ? "Hide browser" : "Show browser");
        browserToggle.setBounds(browserOpen ? leftWidth - 28 : 8, browserTop + 14, browserOpen ? 22 : 28, browserOpen ? 22 : 82);
        arrangement.setBounds(editorX, arrangementTop, editorW, arrangementHeight);
        patternLabel.setBounds(editorX, arrangementBottom + 10, std::max(80, lowerW - 92), 24);
        editorResolution.setBounds(editorX + std::max(90, lowerW - 78), arrangementBottom + 12, 70, 20);
        grid.setBounds(editorX, lowerTop, lowerW, lowerH);
        rack.setVisible(rackOpen);
        if (rackOpen)
            rack.setBounds(editorX + lowerW + gap, lowerTop, rackWidth, lowerH);
        else
            rack.setBounds(getWidth(), lowerTop, 0, lowerH);
        rackToggle.setButtonText(rackOpen ? ">" : "R");
        rackToggle.setTooltip(rackOpen ? "Hide device rack" : "Show device rack");
        rackToggle.setBounds(rackOpen ? rack.getRight() - 28 : getWidth() - collapsedRailWidth + 8,
                             rackOpen ? rack.getY() + 5 : lowerTop + 14,
                             rackOpen ? 22 : 28, rackOpen ? 22 : 82);
        browserToggle.toFront(false);
        rackToggle.toFront(false);
        hint.setBounds(0, 0, 0, 0);
        const auto half = (editorW - 28) / 2;
        gainLabel.setBounds(editorX + 16, getHeight() - 54, 100, 26);
        gain.setBounds(editorX + 112, getHeight() - 54, half - 112, 28);
        audioGainLabel.setBounds(editorX + half + 28, getHeight() - 54, 100, 26);
        audioGain.setBounds(editorX + half + 128, getHeight() - 54, editorW - half - 150, 28);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        setMouseCursor(isOverArrangementSplitter(event.position) ? juce::MouseCursor::UpDownResizeCursor
            : isOverSplitter(event.position) ? juce::MouseCursor::LeftRightResizeCursor
            : juce::MouseCursor::NormalCursor);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        resizingBrowser = browserOpen && std::abs(event.x - browserWidth) <= 5 && event.y >= browserTop;
        resizingRack = rackOpen && rackSplitterBounds().expanded(4, 0).contains(event.getPosition());
        resizingArrangement = isOverArrangementSplitter(event.position);
        resizeStartX = event.x;
        resizeStartY = event.y;
        resizeStartBrowserWidth = browserWidth;
        resizeStartRackWidth = rackWidth;
        resizeStartArrangementHeight = arrangementHeight;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (resizingBrowser)
        {
            browserWidth = juce::jlimit(180, 360, resizeStartBrowserWidth + event.x - resizeStartX);
            resized();
            repaint();
        }
        else if (resizingRack)
        {
            rackWidth = juce::jlimit(220, std::max(220, getWidth() - browserWidth - 420), resizeStartRackWidth - (event.x - resizeStartX));
            resized();
            repaint();
        }
        else if (resizingArrangement)
        {
            arrangementHeight = juce::jlimit(150, std::max(150, getHeight() - 350), resizeStartArrangementHeight + event.y - resizeStartY);
            resized();
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        resizingBrowser = false;
        resizingRack = false;
        resizingArrangement = false;
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S')
        {
            files.save(key.getModifiers().isShiftDown());
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O')
        {
            files.open();
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'F')
        {
            browser.focusSearch();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::spaceKey)
        {
            session.togglePlayback();
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
        {
            if (key.getModifiers().isShiftDown()) session.redo();
            else session.undo();
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y')
        {
            session.redo();
            return true;
        }
        return false;
    }

    void requestClose() { files.confirmUnsaved([] { juce::JUCEApplication::getInstance()->quit(); }); }

private:
    juce::TooltipWindow tooltipWindow {this, 700};
    void editWillChange() override
    {
        session.edit->getTransport().removeChangeListener(this);
        session.edit->getUndoManager().removeChangeListener(this);
    }

    void editDidChange() override
    {
        session.edit->getTransport().addChangeListener(this);
        session.edit->getUndoManager().addChangeListener(this);
        changeListenerCallback(nullptr);
    }
    void chooseAudio()
    {
        chooser = std::make_unique<juce::FileChooser>("Add audio", juce::File{}, "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<ControlWindow>(this)](const juce::FileChooser& selected)
            {
                if (safe == nullptr || selected.getResult() == juce::File{}) return;
                const auto result = safe->session.importAudio(selected.getResult());
                if (result.wasOk()) safe->arrangement.fit();
                safe->logStatus(result.wasOk() ? selected.getResult().getFileName()
                                               : result.getErrorMessage());
            });
    }

    void logStatus(const juce::String& message)
    {
        if (message.isEmpty()) return;
        status.setText(message, juce::dontSendNotification);
        juce::Logger::writeToLog("Theta: " + message);
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        const auto playing = session.edit->getTransport().isPlaying();
        play.setButtonText(playing ? juce::String(L"\u275a\u275a") : juce::String(L"\u25b6"));
        play.setTooltip(playing ? "Pause" : "Play");
        tempo.setValue(session.tempo(), juce::dontSendNotification);
        if (!gain.isMouseButtonDown())
            gain.setValue(session.utility->gain().getCurrentValue(), juce::dontSendNotification);
        if (!audioGain.isMouseButtonDown())
            audioGain.setValue(session.audioUtility->gain().getCurrentValue(), juce::dontSendNotification);
        undo.setEnabled(session.edit->getUndoManager().canUndo());
        redo.setEnabled(session.edit->getUndoManager().canRedo());
        patternLabel.setText(session.isPatternDrums() ? "PATTERN 1  /  DRUM EDITOR" : "PATTERN 1  /  NOTE EDITOR",
                             juce::dontSendNotification);
        {
            const juce::ScopedValueSetter<bool> scope(updatingEditorResolution, true);
            editorResolution.setSelectedId(session.editorStepCount(), juce::dontSendNotification);
        }
        const auto name = session.projectFile == juce::File{} ? juce::String("Untitled") : session.projectFile.getFileNameWithoutExtension();
        documentName.setText(name + (session.hasUnsavedChanges() ? " *" : ""), juce::dontSendNotification);
    }

    void timerCallback() override
    {
        const auto text = juce::String(session.edit->getTransport().getPosition().inSeconds(), 1) + " s";
        if (position.getText() != text)
            position.setText(text, juce::dontSendNotification);
    }

    juce::Rectangle<int> rackSplitterBounds() const
    {
        if (!rackOpen) return {};
        return {rack.getX() - 10, rack.getY(), 4, rack.getHeight()};
    }

    juce::Rectangle<int> arrangementSplitterBounds() const
    {
        return {arrangement.getX(), arrangement.getBottom() + 3, arrangement.getWidth(), 4};
    }

    bool isOverSplitter(juce::Point<float> point) const
    {
        return (browserOpen && std::abs(point.x - static_cast<float>(browserWidth)) <= 5.0f && point.y >= static_cast<float>(browserTop))
            || (rackOpen && rackSplitterBounds().expanded(4, 0).toFloat().contains(point));
    }

    bool isOverArrangementSplitter(juce::Point<float> point) const
    {
        return arrangementSplitterBounds().expanded(0, 4).toFloat().contains(point);
    }

    Session& session;
    juce::Label title, status, position, gainLabel, audioGainLabel, hint, documentName, patternLabel;
    juce::Slider gain, audioGain;
    BrowserPanel browser;
    StepGrid grid;
    Arrangement arrangement;
    DeviceRack rack;
    juce::Slider tempo;
    juce::TextButton undo {"Undo"}, redo {"Redo"}, clear {"Clear"};
    juce::TextButton play {"Play"}, stop {"Stop"}, panic {"Panic"}, import {"Add audio"}, settings {"Audio settings"};
    juce::TextButton browserToggle {"<"}, rackToggle {">"};
    juce::ComboBox editorResolution;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::DialogWindow> audioSettings;
    juce::TextButton open {"Open"}, save {"Save"};
    ProjectFiles files;
    int browserWidth = 244, rackWidth = 312, arrangementHeight = 246;
    int resizeStartX = 0, resizeStartY = 0, resizeStartBrowserWidth = 244, resizeStartRackWidth = 312, resizeStartArrangementHeight = 246;
    static constexpr int browserTop = 74, collapsedRailWidth = 44;
    bool browserOpen = true, rackOpen = true, resizingBrowser = false, resizingRack = false, resizingArrangement = false;
    bool updatingEditorResolution = false;
};

class Application final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "Theta"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String& args) override
    {
        const auto logFile = thetaLogFile();
        logFile.getParentDirectory().createDirectory();
        logger = std::make_unique<juce::FileLogger>(logFile, "Theta debug log", 512 * 1024);
        juce::Logger::setCurrentLogger(logger.get());
        juce::Logger::writeToLog("Theta: log started at " + logFile.getFullPathName());
        if (args == "--self-test" || args == "--pattern-test" || args == "--arrangement-test")
        {
            Session::setCommandLineTestMode(true);
            prepareCommandLineAudio();
            setApplicationReturnValue(args == "--self-test" ? runSelfTest()
                : args == "--pattern-test" ? runPatternTest() : runArrangementTest());
            quit();
            return;
        }
        theme.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff343a40));
        theme.setColour(juce::Slider::trackColourId, juce::Colour(0xffc6d58c));
        juce::LookAndFeel::setDefaultLookAndFeel(&theme);
        startupTest = args == "--startup-test";
        window = std::make_unique<Window>();
        loading = new StartupScreen();
        loading->setSize(560, 320);
        window->setContentOwned(loading.getComponent(), true);
        window->centreWithSize(560, 320);
        window->setVisible(!startupTest);
        const auto screenshot = juce::SystemStats::getEnvironmentVariable("THETA_STARTUP_SNAPSHOT", {});
        if (startupTest && screenshot.isNotEmpty())
        {
            if (auto stream = juce::File(screenshot).createOutputStream())
                juce::PNGImageFormat().writeImageToStream(loading->createComponentSnapshot(loading->getLocalBounds()), *stream);
        }
        // Let the window become visible before constructing the engine. Engine
        // initialization requires the message thread; yield between its phases.
        startTimer(40);
    }
    void shutdown() override
    {
        stopTimer();
        window.reset();
        session.reset();
        juce::Logger::setCurrentLogger(nullptr);
        logger.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }
    void systemRequestedQuit() override
    {
        if (window)
            if (auto* controls = dynamic_cast<ControlWindow*>(window->getContentComponent()))
            {
                controls->requestClose();
                return;
            }
        stopTimer();
        quit();
    }

private:
    void timerCallback() override
    {
        stopTimer();
        if (!window || !loading) return;
        try
        {
            if (window->getContentComponent() != loading.getComponent())
                throw std::runtime_error("Startup screen was replaced before initialization finished.");
            if (auto* peer = window->getPeer()) peer->performAnyPendingRepaintsNow();
            if (startupStage == 0)
                session = std::make_unique<Session>();
            else if (startupStage == 1)
            {
                avoidLegacyDirectSound(session->engine);
                session->engine.getDeviceManager().initialise(0, 2);
                avoidLegacyDirectSound(session->engine);
            }
            else
            {
                window->setUsingNativeTitleBar(true);
                window->setContentOwned(new ControlWindow(*session), true);
                window->setResizable(true, false);
                window->setResizeLimits(960, 680, 2400, 1600);
                window->centreWithSize(1120, 760);
                if (startupTest) quit();
                return;
            }
            loading->setStage(++startupStage);
            startTimer(1);
        }
        catch (const std::exception& error)
        {
            setApplicationReturnValue(1);
            if (loading) loading->showError(error.what());
            std::fprintf(stderr, "Theta startup failed: %s\n", error.what());
            if (startupTest) quit();
        }
    }

    struct Window final : juce::DocumentWindow
    {
        Window() : DocumentWindow("Theta", juce::Colour(0xff171a1e), allButtons)
        {
            setUsingNativeTitleBar(false);
            setResizable(false, false);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    Theme theme;
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<Session> session;
    std::unique_ptr<Window> window;
    juce::Component::SafePointer<StartupScreen> loading;
    int startupStage = 0;
    bool startupTest = false;
};
}
START_JUCE_APPLICATION(theta::Application)
