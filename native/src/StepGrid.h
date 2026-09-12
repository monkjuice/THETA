#pragma once
#include "Session.h"
#include <array>
#include <bitset>
#include <vector>

namespace theta
{
class StepGrid final : public juce::Component, private juce::ChangeListener, private juce::ScrollBar::Listener, private juce::Timer
{
public:
    explicit StepGrid(Session&);
    ~StepGrid() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void zoomIn();
    void zoomOut();
    void setScaleHighlight(int selection);
    void focusGained(juce::Component::FocusChangeType) override;
    void focusLost(juce::Component::FocusChangeType) override;
    void resized() override;
private:
    friend int runArrangementTest();
    enum class Gesture { none, draw, move, resize };
    struct CopiedNote { int step = 0, pitch = 0; double length = 1.0; };
    struct MovingNote { int step = 0, pitch = 0; };
    juce::Rectangle<float> cell(int step, int row) const;
    float rowAreaHeight() const;
    float cellWidth() const;
    float gridRight() const;
    float gridWidth() const;
    double visibleStepSpan() const;
    void syncHorizontalScroll();
    void scrollDraggedNotes();
    void moveDraggedNotesAt(juce::Point<float>);
    int hit(juce::Point<float>) const;
    int resizeHit(juce::Point<float>) const;
    void apply(int index);
    void toggleSelection(int index);
    bool selectAllNotes();
    bool canPasteAt(int step) const;
    void clearSelection();
    bool copySelection();
    bool pasteSelection();
    bool deleteSelection();
    bool fillSelectionToClipEnd();
    juce::Result moveCurrentNotesBy(int stepDelta, int pitchDelta);
    juce::Result resizeCurrentNoteTo(int index);
    juce::Result resizeCurrentNoteTo(juce::Point<float>, bool freeLength);
    void updatePointer(juce::Point<float>, const juce::ModifierKeys&);
    int pitchForIndex(int index) const;
    int indexForCell(int step, int pitch) const;
    int automaticLowestPitch() const;
    void rebuildVisibleNotes();
    float playheadXForTime(double seconds) const;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void scrollBarMoved(juce::ScrollBar*, double) override;
    void timerCallback() override;
    void updatePlayhead();
    Session& session;
    std::bitset<Session::steps * Session::pitches> notes, visited, selectedNotes;
    std::array<float, Session::steps * Session::pitches> noteLengths {}, noteStartOffsets {};
    std::vector<CopiedNote> noteClipboard;
    std::vector<MovingNote> movingNotes;
    Gesture gesture = Gesture::none;
    bool adding = true, showingDrumLabels = false, noteMoved = false, manualPitchScroll = false, movingGroup = false, resizingFromLeft = false;
    int lastHit = -1, movingNoteIndex = -1, resizingNoteIndex = -1, pasteAnchorIndex = -1, clipboardBasePitch = 0;
    int lastMoveStep = -1, lastMovePitch = -1;
    int visibleStepCount = Session::defaultSteps;
    int lowestVisiblePitch = Session::lowestNote;
    double stepScroll = 0.0, stepZoom = 1.0;
    double resizingStartStep = 0.0, resizingEndStep = 0.0;
    int scaleHighlight = 1;
    float verticalAutoScroll = 0.0f;
    juce::Point<float> dragPosition {-1.0f, -1.0f};
    float playhead = -1.0f;
    juce::ScrollBar horizontalScroll {false};
    juce::VBlankAttachment vblank;
    static constexpr float labelWidth = 54.0f, headerHeight = 26.0f, scrollHeight = 14.0f;
};
}
