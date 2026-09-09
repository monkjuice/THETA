#include "Session.h"
#include "StepGrid.h"
#include "ProjectFiles.h"
#include "Theme.h"
#include "Arrangement.h"
#include "StartupScreen.h"
#include <stdexcept>

namespace theta
{
class ControlWindow final : public juce::Component,
                            private Session::Listener,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit ControlWindow(Session& s) : session(s), grid(s), arrangement(s), files(s)
    {
        setOpaque(true);
        files.status = [this](const juce::String& message) { status.setText(message, juce::dontSendNotification); };
        files.loadingChanged = [this](bool loading) { setEnabled(!loading); };
        arrangement.status = files.status;
        open.onClick = [this] { files.open(); };
        save.onClick = [this] { files.save(); };
        title.setText("THETA", juce::dontSendNotification);
        title.setFont(juce::FontOptions(26.0f));
        status.setText("PATTERN 1  /  4OSC     Draw notes, then press Play", juce::dontSendNotification);
        gainLabel.setText("SYNTH GAIN", juce::dontSendNotification);
        hint.setText("1 BAR  /  1/16     Drag to draw or erase. Right-drag erases. Space plays when the grid is focused.", juce::dontSendNotification);
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
        play.onClick = [this] { session.togglePlayback(); };
        stop.onClick = [this] { session.stop(); };
        import.onClick = [this] { chooseAudio(); };
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
                 &title, &status, &position, &gainLabel, &gain, &play, &stop, &import, &settings,
                 &grid, &arrangement, &tempo, &undo, &redo, &clear, &hint, &open, &save, &documentName, &patternLabel})
            addAndMakeVisible(component);
        session.edit->getTransport().addChangeListener(this);
        session.addChangeListener(this);
        session.listeners.add(this);
        session.edit->getUndoManager().addChangeListener(this);
        patternLabel.setText("PATTERN 1  /  NOTE EDITOR", juce::dontSendNotification);
        patternLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb8c4aa));
        setSize(1120, 840);
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
        g.setColour(juce::Colour(0xff24282d));
        g.fillRoundedRectangle(24.0f, static_cast<float>(getHeight() - 78), static_cast<float>(getWidth() - 48), 54.0f, 8.0f);
    }

    void resized() override
    {
        title.setBounds(24, 20, 200, 38);
        documentName.setBounds(190, 26, getWidth() - 530, 30);
        settings.setBounds(getWidth() - 152, 26, 128, 30);
        open.setBounds(getWidth() - 320, 26, 72, 30);
        save.setBounds(getWidth() - 240, 26, 72, 30);
        status.setBounds(24, 68, getWidth() - 48, 28);
        play.setBounds(24, 116, 90, 36);
        stop.setBounds(124, 116, 90, 36);
        import.setBounds(232, 116, 128, 36);
        tempo.setBounds(380, 119, 135, 30);
        undo.setBounds(536, 119, 60, 30);
        redo.setBounds(604, 119, 60, 30);
        clear.setBounds(672, 119, 88, 30);
        position.setBounds(getWidth() - 165, 116, 140, 36);
        arrangement.setBounds(24, 174, getWidth() - 48, 246);
        patternLabel.setBounds(24, 430, getWidth() - 48, 24);
        grid.setBounds(24, 464, getWidth() - 48, getHeight() - 590);
        hint.setBounds(24, getHeight() - 117, getWidth() - 48, 28);
        gainLabel.setBounds(40, getHeight() - 66, 140, 28);
        gain.setBounds(180, getHeight() - 66, getWidth() - 220, 30);
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
                safe->status.setText(result.wasOk() ? selected.getResult().getFileName()
                                                   : result.getErrorMessage(), juce::dontSendNotification);
            });
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        play.setButtonText(session.edit->getTransport().isPlaying() ? "Pause" : "Play");
        tempo.setValue(session.tempo(), juce::dontSendNotification);
        if (!gain.isMouseButtonDown())
            gain.setValue(session.utility->gain().getCurrentValue(), juce::dontSendNotification);
        undo.setEnabled(session.edit->getUndoManager().canUndo());
        redo.setEnabled(session.edit->getUndoManager().canRedo());
        const auto name = session.projectFile == juce::File{} ? juce::String("Untitled") : session.projectFile.getFileNameWithoutExtension();
        documentName.setText(name + (session.hasUnsavedChanges() ? " *" : ""), juce::dontSendNotification);
    }

    void timerCallback() override
    {
        const auto text = juce::String(session.edit->getTransport().getPosition().inSeconds(), 1) + " s";
        if (position.getText() != text)
            position.setText(text, juce::dontSendNotification);
    }

    Session& session;
    juce::Label title, status, position, gainLabel, hint, documentName, patternLabel;
    juce::Slider gain;
    StepGrid grid;
    Arrangement arrangement;
    juce::Slider tempo;
    juce::TextButton undo {"Undo"}, redo {"Redo"}, clear {"Clear"};
    juce::TextButton play {"Play"}, stop {"Stop"}, import {"Add audio"}, settings {"Audio settings"};
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::DialogWindow> audioSettings;
    juce::TextButton open {"Open"}, save {"Save"};
    ProjectFiles files;
};

class Application final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "Theta"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String& args) override
    {
        if (args == "--self-test" || args == "--pattern-test" || args == "--arrangement-test")
        {
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
                session->engine.getDeviceManager().initialise(0, 2);
            else
            {
                window->setContentOwned(new ControlWindow(*session), true);
                window->setResizable(true, false);
                window->setResizeLimits(960, 780, 2400, 1600);
                window->centreWithSize(1120, 840);
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
            setUsingNativeTitleBar(true);
            setResizable(false, false);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    Theme theme;
    std::unique_ptr<Session> session;
    std::unique_ptr<Window> window;
    juce::Component::SafePointer<StartupScreen> loading;
    int startupStage = 0;
    bool startupTest = false;
};
}
START_JUCE_APPLICATION(theta::Application)
