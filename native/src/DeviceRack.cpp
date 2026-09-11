#include "DeviceRack.h"
#include <cmath>
#include <optional>

namespace theta
{
namespace
{
std::optional<Session::AudioEffect> effectFromBrowserDrop(const juce::String& description)
{
    if (!description.startsWith("theta-browser:effect:")) return std::nullopt;
    const auto id = description.fromLastOccurrenceOf(":", false, false);
    if (id == "Equaliser")  return Session::AudioEffect::Equaliser;
    if (id == "Reverb")     return Session::AudioEffect::Reverb;
    if (id == "Delay")      return Session::AudioEffect::Delay;
    if (id == "Compressor") return Session::AudioEffect::Compressor;
    if (id == "ThetaSpace") return Session::AudioEffect::ThetaSpace;
    if (id == "ThetaBloom") return Session::AudioEffect::ThetaBloom;
    return std::nullopt;
}

std::optional<Session::Instrument> instrumentFromBrowserDrop(const juce::String& description)
{
    if (!description.startsWith("theta-browser:instrument:")) return std::nullopt;
    const auto id = description.fromLastOccurrenceOf(":", false, false);
    if (id == "FourOsc") return Session::Instrument::FourOsc;
    if (id == "Drums")   return Session::Instrument::Drums;
    if (id == "Utility") return Session::Instrument::Utility;
    return std::nullopt;
}

std::optional<Session::MidiEffect> midiEffectFromBrowserDrop(const juce::String& description)
{
    if (!description.startsWith("theta-browser:midi-effect:")) return std::nullopt;
    const auto id = description.fromLastOccurrenceOf(":", false, false);
    if (id == "ThetaArp") return Session::MidiEffect::ThetaArp;
    return std::nullopt;
}
}

class DeviceRack::FloatingDeviceWindow final : public juce::DocumentWindow
{
public:
    class Editor final : public juce::Component,
                         private juce::Timer
    {
    public:
        Editor(Session& s, int t, int sl) : session(s), track(t), slot(sl)
        {
            setOpaque(true);
            setSize(680, 430);
            refresh();
            startTimerHz(30);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff111316));
            const auto bounds = getLocalBounds().toFloat();
            g.setColour(juce::Colour(0xff1a1d21));
            g.fillRect(bounds.reduced(18.0f, 18.0f));
            g.setColour(juce::Colour(0xff313841));
            g.drawRect(bounds.reduced(18.0f, 18.0f), 1.0f);
            g.setColour(juce::Colour(0xffeef2f4));
            g.setFont(juce::FontOptions(24.0f));
            g.drawText(deviceName, 34, 28, getWidth() - 68, 34, juce::Justification::centredLeft, true);
            g.setColour(juce::Colour(0xff8cc5d2));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText("THETA FX", 36, 62, 120, 18, juce::Justification::centredLeft, true);

            const juce::Rectangle<float> scope(210.0f, 88.0f, 260.0f, 126.0f);
            g.setColour(juce::Colour(0xff171b20));
            g.fillRect(scope);
            g.setColour(juce::Colour(0xff333b45));
            g.drawRect(scope, 1.0f);
            g.setColour(juce::Colour(0x668cc5d2));
            for (int i = 0; i < 56; ++i)
            {
                const auto x = scope.getX() + 10.0f + i * (scope.getWidth() - 20.0f) / 55.0f;
                const auto h = 8.0f
                    + std::sin(animationPhase + i * 0.67f) * 13.0f
                    + std::sin(animationPhase * 0.41f + i * 0.21f) * 20.0f;
                g.drawVerticalLine(static_cast<int>(x), scope.getCentreY() - h, scope.getCentreY() + h);
            }
        }

        void resized() override
        {
            const auto count = static_cast<int>(sliders.size());
            const int top = 244;
            for (int i = 0; i < count; ++i)
            {
                const int col = i % 3;
                const int row = i / 3;
                const int x = 36 + col * 210;
                const int y = top + row * 74;
                sliders[i]->setBounds(x, y, 82, 58);
                labels[i]->setBounds(x + 90, y + 4, 94, 22);
                values[i]->setBounds(x + 90, y + 28, 76, 22);
            }
        }

        void timerCallback() override
        {
            animationPhase += 0.12f;
            const juce::Rectangle<int> scope(210, 88, 260, 126);
            repaint(scope.expanded(2));
        }

        void refresh()
        {
            const auto deviceSlots = session.deviceSlots(track);
            deviceName = juce::isPositiveAndBelow(slot, deviceSlots.size()) ? deviceSlots[static_cast<size_t>(slot)].name : "Device";
            parameters = session.deviceParameters(track, slot);
            while (labels.size() < static_cast<int>(parameters.size()))
            {
                const auto index = labels.size();
                auto* label = labels.add(new juce::Label());
                auto* value = values.add(new juce::Label());
                auto* slider = sliders.add(new juce::Slider());
                label->setColour(juce::Label::textColourId, juce::Colour(0xffdce5ea));
                label->setFont(juce::FontOptions(12.0f));
                value->setColour(juce::Label::textColourId, juce::Colour(0xff94a9b4));
                value->setFont(juce::FontOptions(12.0f));
                value->setJustificationType(juce::Justification::centredRight);
                slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff8cc5d2));
                slider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff242b31));
                slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xffc6d58c));
                slider->onDragStart = [this, index] { session.beginDeviceParameterGesture(track, slot, index); };
                slider->onValueChange = [this, index, slider]
                {
                    if (!syncing)
                    {
                        session.setDeviceParameter(track, slot, index, static_cast<float>(slider->getValue()));
                        const auto next = session.deviceParameters(track, slot);
                        if (juce::isPositiveAndBelow(index, next.size()))
                            values[index]->setText(next[static_cast<size_t>(index)].valueText, juce::dontSendNotification);
                    }
                };
                slider->onDragEnd = [this, index]
                {
                    session.endDeviceParameterGesture(track, slot, index);
                    refresh();
                };
                addAndMakeVisible(label);
                addAndMakeVisible(value);
                addAndMakeVisible(slider);
            }

            syncing = true;
            for (int i = 0; i < labels.size(); ++i)
            {
                const auto visible = i < static_cast<int>(parameters.size()) && i < 6;
                labels[i]->setVisible(visible);
                values[i]->setVisible(visible);
                sliders[i]->setVisible(visible);
                if (!visible) continue;
                const auto& parameter = parameters[static_cast<size_t>(i)];
                labels[i]->setText(parameter.name, juce::dontSendNotification);
                values[i]->setText(parameter.valueText, juce::dontSendNotification);
                sliders[i]->setRange(parameter.minimum, parameter.maximum, parameter.discrete ? 1.0 : 0.0);
                sliders[i]->setValue(parameter.value, juce::dontSendNotification);
            }
            syncing = false;
            resized();
            repaint();
        }

    private:
        Session& session;
        int track = 0, slot = 0;
        bool syncing = false;
        float animationPhase = 0.0f;
        juce::String deviceName;
        std::vector<Session::DeviceParameter> parameters;
        juce::OwnedArray<juce::Label> labels, values;
        juce::OwnedArray<juce::Slider> sliders;
    };

    FloatingDeviceWindow(Session& session, int track, int slot)
        : DocumentWindow("Theta Device", juce::Colour(0xff0f1114), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(false, false);
        setContentOwned(new Editor(session, track, slot), true);
        centreWithSize(680, 430);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }
};

DeviceRack::DeviceRack(Session& s) : session(s)
{
    setOpaque(true);
    title.setText("RACK", juce::dontSendNotification);
    title.setColour(juce::Label::textColourId, juce::Colour(0xffcbd6de));
    title.setFont(juce::FontOptions(13.0f));
    pattern.setClickingTogglesState(true);
    audio.setClickingTogglesState(true);
    pattern.setButtonText("Pat");
    audio.setButtonText("Trk");
    open.setButtonText("Edit");
    bypass.setButtonText("On");
    remove.setButtonText("Del");
    pattern.setTooltip("Pattern devices");
    audio.setTooltip("Selected audio track devices");
    open.setTooltip("Open selected device editor");
    bypass.setTooltip("Bypass or enable selected device");
    remove.setTooltip("Delete selected device");
    pattern.setRadioGroupId(29, juce::dontSendNotification);
    audio.setRadioGroupId(29, juce::dontSendNotification);
    pattern.onClick = [this] { selectTrack(0); };
    audio.onClick = [this] { selectTrack(1); };
    open.onClick = [this] { openSelectedDevice(); };
    bypass.onClick = [this]
    {
        const auto result = session.toggleDeviceEnabled(selectedTrack, selectedSlot);
        if (result.failed() && status) status(result.getErrorMessage());
    };
    remove.onClick = [this]
    {
        const auto result = session.deleteDevice(selectedTrack, selectedSlot);
        if (result.failed() && status) status(result.getErrorMessage());
    };
    list.setRowHeight(28);
    list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff20262b));
    list.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff343c44));
    for (auto* component : std::initializer_list<juce::Component*>{&title, &pattern, &audio, &open, &bypass, &remove, &list})
        addAndMakeVisible(component);
    session.addChangeListener(this);
    selectTrack(0);
}

DeviceRack::~DeviceRack()
{
    session.removeChangeListener(this);
}

void DeviceRack::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b2025));
    g.setColour(juce::Colour(0xff303840));
    g.drawRect(getLocalBounds());
}

void DeviceRack::resized()
{
    title.setBounds(12, 4, 56, 24);
    pattern.setBounds(74, 5, 34, 24);
    audio.setBounds(112, 5, 34, 24);
    open.setBounds(getWidth() - 158, 5, 44, 24);
    bypass.setBounds(getWidth() - 108, 5, 34, 24);
    remove.setBounds(getWidth() - 68, 5, 34, 24);
    const auto paramCount = std::min(6, static_cast<int>(parameters.size()));
    const auto columns = paramCount > 3 ? 3 : std::max(1, paramCount);
    const auto rows = paramCount > 3 ? 2 : 1;
    const auto parameterHeight = paramCount > 0 ? (rows == 2 ? 206 : 132) : 0;
    const auto availableHeight = std::max(60, getHeight() - 42);
    const auto desiredListHeight = 10 + std::max(3, getNumRows()) * list.getRowHeight();
    const auto maxListHeight = paramCount > 0
        ? std::max(76, availableHeight - parameterHeight - 10)
        : availableHeight;
    const auto listHeight = juce::jlimit(76, maxListHeight, desiredListHeight);
    list.setBounds(12, 34, getWidth() - 24, listHeight);
    const auto parameterArea = juce::Rectangle<int>(12, list.getBottom() + 10, getWidth() - 24, parameterHeight).reduced(2, 0);
    const auto cellWidth = columns > 0 ? parameterArea.getWidth() / columns : parameterArea.getWidth();
    const auto cellHeight = rows > 0 ? parameterArea.getHeight() / rows : parameterArea.getHeight();
    for (int i = 0; i < parameterSliders.size(); ++i)
    {
        auto* name = parameterLabels[i];
        auto* slider = parameterSliders[i];
        auto* value = parameterValues[i];
        if (i >= paramCount)
        {
            name->setVisible(false);
            slider->setVisible(false);
            value->setVisible(false);
            continue;
        }
        name->setVisible(true);
        slider->setVisible(true);
        value->setVisible(true);
        const auto col = i % columns;
        const auto row = i / columns;
        const juce::Rectangle<int> cell(parameterArea.getX() + col * cellWidth,
                                        parameterArea.getY() + row * cellHeight,
                                        cellWidth, cellHeight);
        const auto knobSize = std::min({64, std::max(42, cell.getWidth() - 28), std::max(42, cell.getHeight() - 28)});
        name->setBounds(cell.getX() + 4, cell.getY(), cell.getWidth() - 8, 18);
        slider->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize).translated(0, 4));
        value->setBounds(cell.getX() + 4, cell.getBottom() - 20, cell.getWidth() - 8, 18);
    }
}

int DeviceRack::getNumRows()
{
    return static_cast<int>(slots.size());
}

void DeviceRack::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (!juce::isPositiveAndBelow(row, slots.size())) return;
    const auto& slot = slots[static_cast<size_t>(row)];
    g.fillAll(selected ? juce::Colour(0xff34424a) : juce::Colour(row % 2 == 0 ? 0xff20262b : 0xff242a30));
    g.setColour(slot.enabled ? juce::Colour(0xffc6d58c) : juce::Colour(0xff6e7780));
    g.fillRect(8, height / 2 - 4, 8, 8);
    g.setFont(juce::FontOptions(13.0f));
    g.setColour(slot.enabled ? juce::Colour(0xffe5ebef) : juce::Colour(0xff9aa4ad));
    g.drawText(slot.name, 24, 0, width / 2 - 24, height, juce::Justification::centredLeft, true);
    g.setColour(juce::Colour(0xff8f9aa4));
    g.drawText(slot.type, width / 2, 0, width / 2 - 12, height, juce::Justification::centredLeft, true);
}

void DeviceRack::selectedRowsChanged(int lastRowSelected)
{
    selectedSlot = juce::jlimit(0, std::max(0, static_cast<int>(slots.size()) - 1), lastRowSelected);
    sync();
}

void DeviceRack::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    selectedSlot = juce::jlimit(0, std::max(0, static_cast<int>(slots.size()) - 1), row);
    openSelectedDevice();
}

void DeviceRack::openSelectedDevice()
{
    if (slots.empty())
        return;
    selectedSlot = juce::jlimit(0, static_cast<int>(slots.size()) - 1, selectedSlot);
    floatingWindow = std::make_unique<FloatingDeviceWindow>(session, selectedTrack, selectedSlot);
    if (status) status("Opened " + slots[static_cast<size_t>(selectedSlot)].name + " device panel");
}

bool DeviceRack::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto description = details.description.toString();
    return effectFromBrowserDrop(description).has_value()
        || instrumentFromBrowserDrop(description).has_value()
        || midiEffectFromBrowserDrop(description).has_value();
}

void DeviceRack::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto description = details.description.toString();
    if (const auto effect = effectFromBrowserDrop(description))
    {
        const auto result = session.addAudioEffect(*effect, selectedTrack);
        if (status) status(result.wasOk() ? "Added effect to " + session.trackName(selectedTrack) : result.getErrorMessage());
    }
    else if (const auto instrument = instrumentFromBrowserDrop(description))
    {
        const auto result = session.addInstrument(*instrument, selectedTrack);
        if (status) status(result.wasOk() ? "Added instrument to " + session.trackName(selectedTrack) : result.getErrorMessage());
    }
    else if (const auto midiEffect = midiEffectFromBrowserDrop(description))
    {
        const auto result = session.addMidiEffect(*midiEffect, selectedTrack);
        if (status) status(result.wasOk() ? "Added MIDI FX to " + session.trackName(selectedTrack) : result.getErrorMessage());
    }
}

void DeviceRack::rebuildParameterControls()
{
    while (parameterLabels.size() < static_cast<int>(parameters.size()))
    {
        const auto index = parameterLabels.size();
        auto* name = parameterLabels.add(new juce::Label());
        auto* value = parameterValues.add(new juce::Label());
        auto* slider = parameterSliders.add(new juce::Slider());
        name->setColour(juce::Label::textColourId, juce::Colour(0xffdfe6ea));
        name->setFont(juce::FontOptions(12.0f));
        name->setJustificationType(juce::Justification::centred);
        value->setColour(juce::Label::textColourId, juce::Colour(0xffb7c1ca));
        value->setFont(juce::FontOptions(11.0f));
        value->setJustificationType(juce::Justification::centred);
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider->setColour(juce::Slider::trackColourId, juce::Colour(0xffc6d58c));
        slider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff242b31));
        slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xff4bb0d2));
        slider->onDragStart = [this, index]
        {
            const auto result = session.beginDeviceParameterGesture(selectedTrack, selectedSlot, index);
            if (result.failed() && status) status(result.getErrorMessage());
        };
        slider->onValueChange = [this, index, slider]
        {
            if (syncing) return;
            const auto result = session.setDeviceParameter(selectedTrack, selectedSlot, index, static_cast<float>(slider->getValue()));
            if (result.failed() && status) status(result.getErrorMessage());
        };
        slider->onDragEnd = [this, index]
        {
            const auto result = session.endDeviceParameterGesture(selectedTrack, selectedSlot, index);
            if (result.failed() && status) status(result.getErrorMessage());
        };
        addAndMakeVisible(name);
        addAndMakeVisible(value);
        addAndMakeVisible(slider);
    }

    syncing = true;
    for (int i = 0; i < parameterLabels.size(); ++i)
    {
        const auto visible = i < static_cast<int>(parameters.size());
        parameterLabels[i]->setVisible(visible);
        parameterValues[i]->setVisible(visible);
        parameterSliders[i]->setVisible(visible);
        if (!visible) continue;
        const auto& parameter = parameters[static_cast<size_t>(i)];
        parameterLabels[i]->setText(parameter.name, juce::dontSendNotification);
        parameterValues[i]->setText(parameter.valueText, juce::dontSendNotification);
        parameterSliders[i]->setRange(parameter.minimum, parameter.maximum, parameter.discrete ? 1.0 : 0.0);
        parameterSliders[i]->setValue(parameter.value, juce::dontSendNotification);
        parameterSliders[i]->setTooltip(parameter.name + ": " + parameter.valueText);
    }
    syncing = false;
    resized();
    repaint();
}

void DeviceRack::changeListenerCallback(juce::ChangeBroadcaster*)
{
    sync();
}

void DeviceRack::selectTrack(int track)
{
    selectedTrack = juce::jlimit(0, std::max(0, session.trackCount() - 1), track);
    pattern.setToggleState(selectedTrack == 0, juce::dontSendNotification);
    audio.setButtonText("Trk");
    audio.setToggleState(selectedTrack > 0, juce::dontSendNotification);
    selectedSlot = 0;
    sync();
}

void DeviceRack::sync()
{
    selectedTrack = juce::jlimit(0, std::max(0, session.trackCount() - 1), selectedTrack);
    pattern.setToggleState(selectedTrack == 0, juce::dontSendNotification);
    audio.setButtonText("Trk");
    audio.setToggleState(selectedTrack > 0, juce::dontSendNotification);
    slots = session.deviceSlots(selectedTrack);
    selectedSlot = juce::jlimit(0, std::max(0, static_cast<int>(slots.size()) - 1), selectedSlot);
    parameters = session.deviceParameters(selectedTrack, selectedSlot);
    list.updateContent();
    if (!slots.empty())
        list.selectRow(selectedSlot, juce::dontSendNotification);
    bypass.setEnabled(!slots.empty());
    open.setEnabled(!slots.empty());
    remove.setEnabled(!slots.empty() && slots[static_cast<size_t>(selectedSlot)].removable);
    bypass.setButtonText(!slots.empty() && !slots[static_cast<size_t>(selectedSlot)].enabled ? "Off" : "On");
    rebuildParameterControls();
}
}
