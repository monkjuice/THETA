#pragma once
#include "Session.h"
#include <algorithm>
#include <vector>

namespace theta
{
class DeviceRack final : public juce::Component,
                         private juce::ChangeListener,
                         private juce::ListBoxModel
{
public:
    explicit DeviceRack(Session&);
    ~DeviceRack() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void selectTrack(int track);
    std::function<void(juce::String)> status;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void sync();

    Session& session;
    int selectedTrack = 0, selectedSlot = 0;
    std::vector<Session::DeviceSlot> slots;
    juce::Label title;
    juce::TextButton pattern {"Pattern"}, audio {"Audio 1"}, bypass {"Bypass"}, remove {"Delete"};
    juce::ListBox list {"Devices", this};
};
}
