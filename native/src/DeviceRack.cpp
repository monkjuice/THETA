#include "DeviceRack.h"

namespace theta
{
DeviceRack::DeviceRack(Session& s) : session(s)
{
    setOpaque(true);
    title.setText("DEVICE RACK", juce::dontSendNotification);
    title.setColour(juce::Label::textColourId, juce::Colour(0xffcbd6de));
    title.setFont(juce::FontOptions(13.0f));
    pattern.setClickingTogglesState(true);
    audio.setClickingTogglesState(true);
    pattern.setRadioGroupId(29, juce::dontSendNotification);
    audio.setRadioGroupId(29, juce::dontSendNotification);
    pattern.onClick = [this] { selectTrack(0); };
    audio.onClick = [this] { selectTrack(1); };
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
    for (auto* component : std::initializer_list<juce::Component*>{&title, &pattern, &audio, &bypass, &remove, &list})
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
    title.setBounds(12, 4, 104, 24);
    pattern.setBounds(120, 5, 76, 24);
    audio.setBounds(202, 5, 76, 24);
    bypass.setBounds(getWidth() - 154, 5, 68, 24);
    remove.setBounds(getWidth() - 80, 5, 68, 24);
    list.setBounds(12, 34, getWidth() - 24, getHeight() - 42);
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

void DeviceRack::changeListenerCallback(juce::ChangeBroadcaster*)
{
    sync();
}

void DeviceRack::selectTrack(int track)
{
    selectedTrack = track;
    pattern.setToggleState(track == 0, juce::dontSendNotification);
    audio.setToggleState(track == 1, juce::dontSendNotification);
    selectedSlot = 0;
    sync();
}

void DeviceRack::sync()
{
    slots = session.deviceSlots(selectedTrack);
    selectedSlot = juce::jlimit(0, std::max(0, static_cast<int>(slots.size()) - 1), selectedSlot);
    list.updateContent();
    if (!slots.empty())
        list.selectRow(selectedSlot, juce::dontSendNotification);
    bypass.setEnabled(!slots.empty());
    remove.setEnabled(!slots.empty() && slots[static_cast<size_t>(selectedSlot)].removable);
    bypass.setButtonText(!slots.empty() && !slots[static_cast<size_t>(selectedSlot)].enabled ? "Enable" : "Bypass");
}
}
