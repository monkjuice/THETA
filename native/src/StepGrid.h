#pragma once
#include "Session.h"
#include <bitset>

namespace theta
{
class StepGrid final : public juce::Component, private juce::ChangeListener
{
public:
    explicit StepGrid(Session&);
    ~StepGrid() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void resized() override;
private:
    friend int runArrangementTest();
    juce::Rectangle<float> cell(int step, int row) const;
    int hit(juce::Point<float>) const;
    void apply(int index);
    int visibleLowestPitch() const;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void updatePlayhead();
    Session& session;
    std::bitset<Session::steps * Session::pitches> notes, visited;
    bool drawing = false, adding = true, showingDrumLabels = false;
    int lastHit = -1;
    int lowestVisiblePitch = Session::lowestNote;
    float playhead = -1.0f;
    juce::VBlankAttachment vblank;
    static constexpr float labelWidth = 54.0f, headerHeight = 26.0f;
};
}
