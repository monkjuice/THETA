#pragma once
#include "Session.h"
#include <bitset>
#include <vector>

namespace theta
{
class StepGrid final : public juce::Component, private juce::ChangeListener, private juce::ScrollBar::Listener
{
public:
    explicit StepGrid(Session&);
    ~StepGrid() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void resized() override;
private:
    friend int runArrangementTest();
    enum class Gesture { none, draw, move };
    struct CopiedNote { int step = 0, pitch = 0; };
    juce::Rectangle<float> cell(int step, int row) const;
    float rowAreaHeight() const;
    float cellWidth() const;
    float gridRight() const;
    float gridWidth() const;
    double visibleStepSpan() const;
    void syncHorizontalScroll();
    int hit(juce::Point<float>) const;
    void apply(int index);
    void toggleSelection(int index);
    void clearSelection();
    bool copySelection();
    bool pasteSelection();
    bool deleteSelection();
    juce::Result moveCurrentNoteTo(int index);
    int pitchForIndex(int index) const;
    int indexForCell(int step, int pitch) const;
    int automaticLowestPitch() const;
    void rebuildVisibleNotes();
    float playheadXForTime(double seconds) const;
    void syncResolutionBox();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void scrollBarMoved(juce::ScrollBar*, double) override;
    void updatePlayhead();
    Session& session;
    std::bitset<Session::steps * Session::pitches> notes, visited, selectedNotes;
    std::vector<CopiedNote> noteClipboard;
    Gesture gesture = Gesture::none;
    bool adding = true, showingDrumLabels = false, noteMoved = false, manualPitchScroll = false, updatingResolutionBox = false;
    int lastHit = -1, movingNoteIndex = -1, pasteAnchorIndex = -1;
    int visibleStepCount = Session::defaultSteps;
    int lowestVisiblePitch = Session::lowestNote;
    double stepScroll = 0.0, stepZoom = 1.0;
    float playhead = -1.0f;
    juce::ComboBox resolutionBox;
    juce::ScrollBar horizontalScroll {false};
    juce::VBlankAttachment vblank;
    static constexpr float labelWidth = 54.0f, headerHeight = 26.0f, scrollHeight = 14.0f;
};
}
