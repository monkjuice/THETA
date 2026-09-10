#pragma once
#include "Session.h"
#include <algorithm>
#include <vector>

namespace theta
{
class DeviceRack final : public juce::Component,
                         public juce::DragAndDropTarget,
                         private juce::ChangeListener,
                         private juce::ListBoxModel
{
public:
    explicit DeviceRack(Session&);
    ~DeviceRack() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void selectTrack(int track);
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails&) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails&) override;
    std::function<void(juce::String)> status;

private:
    class FloatingDeviceWindow;
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void rebuildParameterControls();
    void sync();

    Session& session;
    int selectedTrack = 0, selectedSlot = 0;
    bool syncing = false;
    std::vector<Session::DeviceSlot> slots;
    std::vector<Session::DeviceParameter> parameters;
    juce::Label title;
    juce::TextButton pattern {"Pattern"}, audio {"Audio 1"}, open {"Open"}, bypass {"Bypass"}, remove {"Delete"};
    juce::ListBox list {"Devices", this};
    juce::OwnedArray<juce::Label> parameterLabels, parameterValues;
    juce::OwnedArray<juce::Slider> parameterSliders;
    std::unique_ptr<FloatingDeviceWindow> floatingWindow;
};
}
