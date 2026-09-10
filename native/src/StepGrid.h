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
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override;
private:
    friend int runArrangementTest();
    enum class Gesture { none, draw, move };
    juce::Rectangle<float> cell(int step, int row) const;
    int hit(juce::Point<float>) const;
    void apply(int index);
    juce::Result moveCurrentNoteTo(int index);
    int pitchForIndex(int index) const;
    int automaticLowestPitch() const;
    void rebuildVisibleNotes();
    float playheadXForTime(double seconds) const;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void updatePlayhead();
    Session& session;
    std::bitset<Session::steps * Session::pitches> notes, visited;
    Gesture gesture = Gesture::none;
    bool adding = true, showingDrumLabels = false, noteMoved = false, manualPitchScroll = false;
    int lastHit = -1, movingNoteIndex = -1;
    int lowestVisiblePitch = Session::lowestNote;
    float playhead = -1.0f;
    juce::VBlankAttachment vblank;
    static constexpr float labelWidth = 54.0f, headerHeight = 26.0f;
};
}
