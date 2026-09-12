#include "StepGrid.h"
#include "Playhead.h"
#include <algorithm>
#include <cmath>

namespace theta
{
namespace
{
juce::String drumLaneName(int pitch)
{
    if (pitch == 48) return "Kick";
    if (pitch == 50) return "Low Tom";
    if (pitch == 52) return "Mid Tom";
    if (pitch == 53) return "Snare";
    if (pitch == 54) return "High Tom";
    if (pitch == 56) return "Clap";
    if (pitch == 58) return "Closed Hat";
    if (pitch == 59) return "Open Hat";
    return juce::MidiMessage::getMidiNoteName(pitch, true, true, 4);
}

bool isShortcutDown(const juce::ModifierKeys& mods)
{
    return mods.isCommandDown() || mods.isCtrlDown();
}
}

StepGrid::StepGrid(Session& s) : session(s), vblank(this, [this] { updatePlayhead(); })
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setTitle("Pattern notes");
    setDescription("One bar step editor. Drag to draw or erase notes.");
    horizontalScroll.addListener(this);
    addAndMakeVisible(horizontalScroll);
    session.addChangeListener(this);
    changeListenerCallback(nullptr);
}

StepGrid::~StepGrid()
{
    if (gesture != Gesture::none) session.endNoteGesture();
    horizontalScroll.removeListener(this);
    session.removeChangeListener(this);
}

juce::Rectangle<float> StepGrid::cell(int step, int row) const
{
    const auto width = cellWidth();
    const auto height = rowAreaHeight() / Session::pitches;
    return {labelWidth + static_cast<float>(step - stepScroll) * width, headerHeight + row * height, width, height};
}

void StepGrid::zoomIn()
{
    stepZoom = std::min(8.0, stepZoom * 1.5);
    syncHorizontalScroll();
    updatePlayhead();
    repaint();
}

void StepGrid::zoomOut()
{
    stepZoom = std::max(1.0, stepZoom / 1.5);
    syncHorizontalScroll();
    updatePlayhead();
    repaint();
}

void StepGrid::setScaleHighlight(int selection)
{
    if (scaleHighlight != selection)
    {
        scaleHighlight = selection;
        repaint();
    }
}

float StepGrid::rowAreaHeight() const
{
    return std::max(1.0f, getHeight() - headerHeight - (horizontalScroll.isVisible() ? scrollHeight : 0.0f));
}

float StepGrid::cellWidth() const
{
    return gridWidth() / static_cast<float>(visibleStepSpan());
}

float StepGrid::gridRight() const
{
    return static_cast<float>(getWidth());
}

float StepGrid::gridWidth() const
{
    return std::max(1.0f, gridRight() - labelWidth);
}

double StepGrid::visibleStepSpan() const
{
    return std::clamp(static_cast<double>(session.editorStepCount()) / stepZoom,
                      1.0, static_cast<double>(session.editorStepCount()));
}

void StepGrid::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1d2228));
    if (hasKeyboardFocus(true))
    {
        g.setColour(juce::Colour(0xff55c7eb).withAlpha(0.12f));
        g.fillRect(getLocalBounds().removeFromTop(static_cast<int>(headerHeight)));
    }
    g.setFont(juce::FontOptions(12.0f));
    const auto dirty = g.getClipBounds().toFloat();
    const auto steps = session.editorStepCount();
    const auto firstVisibleStep = std::max(0, static_cast<int>(std::floor(stepScroll)));
    const auto lastVisibleStep = std::min(steps - 1, static_cast<int>(std::ceil(stepScroll + visibleStepSpan())));
    for (int step = 0; step < steps; ++step)
    {
        const auto headerCell = cell(step, 0).withY(0).withHeight(headerHeight);
        if (headerCell.getRight() < labelWidth || headerCell.getX() > gridRight())
            continue;
        g.setColour(juce::Colour(step % 4 == 0 ? 0xffd4dacd : 0xff78818a));
        g.drawText(juce::String(step + 1), headerCell, juce::Justification::centred);
    }
    for (int row = 0; row < Session::pitches; ++row)
    {
        const auto pitch = lowestVisiblePitch + Session::pitches - 1 - row;
        const bool black = juce::MidiMessage::isMidiNoteBlack(pitch);
        const auto scaleIndex = scaleHighlight - 2;
        const bool scaleEnabled = !session.isPatternDrums() && scaleIndex >= 0;
        const auto root = scaleEnabled ? scaleIndex % 12 : 0;
        const bool minor = scaleEnabled && scaleIndex >= 12;
        static constexpr std::array<int, 7> major {0, 2, 4, 5, 7, 9, 11};
        static constexpr std::array<int, 7> naturalMinor {0, 2, 3, 5, 7, 8, 10};
        const auto& scale = minor ? naturalMinor : major;
        const auto pitchClass = (pitch % 12 + 12) % 12;
        const bool inScale = !scaleEnabled || std::find(scale.begin(), scale.end(), (pitchClass - root + 12) % 12) != scale.end();
        const bool namedDrum = session.isPatternDrums()
                            && (pitch == 48 || pitch == 50 || pitch == 52 || pitch == 53 || pitch == 54
                                || pitch == 56 || pitch == 58 || pitch == 59);
        auto key = cell(0, row).withX(0).withWidth(labelWidth - 4);
        g.setColour(juce::Colour(namedDrum ? 0xff3a3325 : black ? 0xff15191e : 0xff30373e));
        g.fillRect(key.reduced(0, 1));
        g.setColour(juce::Colour(namedDrum ? 0xffffc16a : 0xffbac2ca));
        g.drawText(session.isPatternDrums() ? drumLaneName(pitch) : juce::MidiMessage::getMidiNoteName(pitch, true, true, 4),
                   key, juce::Justification::centred);
        for (int step = firstVisibleStep; step <= lastVisibleStep; ++step)
        {
            const auto bounds = cell(step, row);
            if (!dirty.intersects(bounds)) continue;
            const auto barColour = step / 4 % 2 == 0 ? juce::Colour(0xff46515a) : juce::Colour(0xff3b4650);
            g.setColour(barColour);
            g.fillRect(bounds);
            if (scaleEnabled && inScale)
            {
                g.setColour(juce::Colour(0x123b8d9e));
                g.fillRect(bounds);
            }
        }
        // Keep every pitch-row boundary identical; bar shading must not make
        // any row look merged with its neighbour.
        g.setColour(juce::Colour(0xff27323b));
        g.fillRect(juce::Rectangle<float>(labelWidth, std::floor(cell(0, row).getBottom()), gridWidth(), 1.0f));
        for (int step = firstVisibleStep; step <= lastVisibleStep + 1; ++step)
        {
            const auto x = cell(step, row).getX();
            g.setColour(juce::Colour(step % 4 == 0 ? 0xff252d35 : 0xff303941));
            g.drawVerticalLine(juce::roundToInt(x), cell(0, row).getY(), cell(0, row).getBottom());
        }

        for (int step = 0; step <= lastVisibleStep; ++step)
        {
            const auto index = row * Session::steps + step;
            if (!notes.test(static_cast<size_t>(index)))
                continue;

            const auto length = std::max(0.0625f, noteLengths[static_cast<size_t>(index)]);
            auto bounds = cell(step, row);
            bounds.translate(noteStartOffsets[static_cast<size_t>(index)] * cellWidth(), 0.0f);
            bounds.setWidth(std::max(3.0f, cellWidth() * length));
            bounds.setRight(std::min(bounds.getRight(), gridRight()));
            if (bounds.getRight() < labelWidth || !dirty.intersects(bounds))
                continue;

            const auto selected = selectedNotes.test(static_cast<size_t>(index));
            g.setColour(selected ? juce::Colour(0xffe9a84a) : juce::Colour(0xffc6d58c));
            g.fillRect(bounds);
            g.setColour(selected ? juce::Colour(0x77482d15) : juce::Colour(0x55363f46));
            g.fillRect(bounds.withWidth(1.0f));
            g.setColour(selected ? juce::Colour(0xffffe3a3) : juce::Colour(0xffe8f2aa));
            g.fillRect(bounds.withX(bounds.getRight() - 2.0f).withWidth(2.0f));
            if (selected)
            {
                g.setColour(juce::Colour(0xfffff0c2));
                g.drawRect(bounds.reduced(1.0f), 2.0f);
            }
        }
    }
    if (!session.isPatternDrums())
    {
        const auto maxLowest = 127 - Session::pitches + 1;
        const auto thumbHeight = std::max(18.0f, rowAreaHeight() * (static_cast<float>(Session::pitches) / 128.0f));
        const auto thumbTravel = std::max(1.0f, rowAreaHeight() - thumbHeight);
        const auto thumbY = headerHeight + (maxLowest - lowestVisiblePitch) / static_cast<float>(maxLowest) * thumbTravel;
        const auto right = gridRight();
        const juce::Rectangle<float> thumb(right - 5.0f, thumbY, 3.0f, thumbHeight);
        g.setColour(juce::Colour(0x55313b44));
        g.fillRect(juce::Rectangle<float>(right - 6.0f, headerHeight + 2.0f, 4.0f, rowAreaHeight() - 4.0f));
        g.setColour(juce::Colour(0xaa8cc5d2));
        g.fillRoundedRectangle(thumb, 1.5f);
    }
    if (playhead >= 0)
    {
        g.setColour(playheadColour);
        g.fillRect(playhead, headerHeight, 2.0f, rowAreaHeight());
    }
    if (hasKeyboardFocus(true))
    {
        g.setColour(juce::Colour(0xff55c7eb));
        g.drawRect(getLocalBounds().toFloat().reduced(1.0f), 2.0f);
    }
}

int StepGrid::hit(juce::Point<float> point) const
{
    if (point.x < labelWidth || point.y < headerHeight || point.x >= gridRight() || point.y >= headerHeight + rowAreaHeight())
        return -1;
    const auto steps = session.editorStepCount();
    const auto step = static_cast<int>(stepScroll + (point.x - labelWidth) / cellWidth());
    if (step < 0 || step >= steps)
        return -1;
    const auto row = static_cast<int>((point.y - headerHeight) / rowAreaHeight() * Session::pitches);
    // A sustained note is selected from anywhere in its drawn body, not only
    // its start cell. This is especially important for Ctrl-click selection.
    for (int candidate = 0; candidate < steps; ++candidate)
    {
        const auto candidateIndex = row * Session::steps + candidate;
        if (!notes.test(static_cast<size_t>(candidateIndex))) continue;
        auto bounds = cell(candidate, row);
        bounds.translate(noteStartOffsets[static_cast<size_t>(candidateIndex)] * cellWidth(), 0.0f);
        bounds.setWidth(std::max(3.0f, cellWidth() * noteLengths[static_cast<size_t>(candidateIndex)]));
        if (bounds.contains(point)) return candidateIndex;
    }
    return row * Session::steps + step;
}

int StepGrid::resizeHit(juce::Point<float> point) const
{
    if (point.y < headerHeight || point.y >= headerHeight + rowAreaHeight())
        return -1;
    const auto row = static_cast<int>((point.y - headerHeight) / rowAreaHeight() * Session::pitches);
    if (row < 0 || row >= Session::pitches)
        return -1;
    const auto steps = session.editorStepCount();
    const auto lastVisibleStep = std::min(steps - 1, static_cast<int>(std::ceil(stepScroll + visibleStepSpan())));
    for (int step = 0; step <= lastVisibleStep; ++step)
    {
        const auto index = row * Session::steps + step;
        if (!notes.test(static_cast<size_t>(index)))
            continue;
        const auto length = std::max(0.0625f, noteLengths[static_cast<size_t>(index)]);
        auto bounds = cell(step, row);
        bounds.translate(noteStartOffsets[static_cast<size_t>(index)] * cellWidth(), 0.0f);
        bounds.setWidth(std::max(3.0f, cellWidth() * length));
        bounds.setRight(std::min(bounds.getRight(), gridRight()));
        if (bounds.getRight() < labelWidth || bounds.getX() > gridRight())
            continue;
        const auto handleWidth = std::min(5.0f, std::max(3.0f, bounds.getWidth() * 0.25f));
        const auto rightHandle = bounds.withX(bounds.getRight() - handleWidth).withWidth(handleWidth);
        const auto leftHandle = bounds.withWidth(handleWidth);
        if (rightHandle.contains(point) || leftHandle.contains(point))
            return index;
    }
    return -1;
}

void StepGrid::updatePointer(juce::Point<float> position, const juce::ModifierKeys& modifiers)
{
    const auto note = hit(position);
    if (isShortcutDown(modifiers) && note >= 0 && notes.test(static_cast<size_t>(note)))
        setMouseCursor(juce::MouseCursor::NormalCursor);
    else if (!modifiers.isRightButtonDown() && resizeHit(position) >= 0)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (note >= 0)
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void StepGrid::mouseMove(const juce::MouseEvent& event)
{
    updatePointer(event.position, event.mods);
}

void StepGrid::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isRightButtonDown() && !isShortcutDown(event.mods))
    {
        const auto resizeIndex = resizeHit(event.position);
        if (resizeIndex >= 0)
        {
            grabKeyboardFocus();
            gesture = Gesture::resize;
            resizingNoteIndex = resizeIndex;
            const auto row = resizeIndex / Session::steps;
            const auto step = resizeIndex % Session::steps;
            const auto offset = noteStartOffsets[static_cast<size_t>(resizeIndex)];
            const auto length = noteLengths[static_cast<size_t>(resizeIndex)];
            const auto left = cell(step, row).getX() + offset * cellWidth();
            resizingFromLeft = event.position.x < left + cellWidth() * length * 0.5f;
            resizingStartStep = step + offset;
            resizingEndStep = resizingStartStep + length;
            noteMoved = false;
            session.beginNoteGesture("Resize note");
            return;
        }
    }
    const auto index = hit(event.position);
    if (index < 0) return;
    grabKeyboardFocus();
    lastHit = index;
    pasteAnchorIndex = index;
    if (isShortcutDown(event.mods))
    {
        toggleSelection(index);
        return;
    }
    if (!event.mods.isRightButtonDown() && notes.test(static_cast<size_t>(index)))
    {
        gesture = Gesture::move;
        movingNoteIndex = index;
        movingNotes.clear();
        movingGroup = selectedNotes.test(static_cast<size_t>(index));
        if (movingGroup)
            for (int row = 0; row < Session::pitches; ++row)
                for (int step = 0; step < session.editorStepCount(); ++step)
                {
                    const auto selected = row * Session::steps + step;
                    if (selectedNotes.test(static_cast<size_t>(selected)) && notes.test(static_cast<size_t>(selected)))
                        movingNotes.push_back({step, pitchForIndex(selected)});
                }
        if (movingNotes.empty())
            movingNotes.push_back({index % Session::steps, pitchForIndex(index)});
        lastMoveStep = index % Session::steps;
        lastMovePitch = pitchForIndex(index);
        dragPosition = event.position;
        verticalAutoScroll = 0.0f;
        noteMoved = false;
        session.beginNoteGesture("Move note");
        startTimerHz(60);
        return;
    }

    gesture = Gesture::draw;
    adding = !event.mods.isRightButtonDown() && !notes.test(static_cast<size_t>(index));
    visited.reset();
    session.beginNoteGesture(adding ? "Draw notes" : "Erase notes");
    apply(index);
}

void StepGrid::apply(int index)
{
    if (index < 0 || visited.test(static_cast<size_t>(index))) return;
    visited.set(static_cast<size_t>(index));
    session.setNote(index % Session::steps,
                    pitchForIndex(index), adding);
}

void StepGrid::toggleSelection(int index)
{
    if (index < 0 || !notes.test(static_cast<size_t>(index)))
    {
        repaint();
        return;
    }
    selectedNotes.flip(static_cast<size_t>(index));
    repaint(cell(index % Session::steps, index / Session::steps).getSmallestIntegerContainer().expanded(3));
}

bool StepGrid::selectAllNotes()
{
    const auto before = selectedNotes;
    selectedNotes.reset();
    const auto steps = session.editorStepCount();
    for (int row = 0; row < Session::pitches; ++row)
        for (int step = 0; step < steps; ++step)
        {
            const auto index = row * Session::steps + step;
            if (notes.test(static_cast<size_t>(index)))
                selectedNotes.set(static_cast<size_t>(index));
        }
    if (selectedNotes != before)
        repaint();
    return selectedNotes.any();
}

void StepGrid::clearSelection()
{
    if (selectedNotes.none())
        return;
    selectedNotes.reset();
    repaint();
}

bool StepGrid::copySelection()
{
    noteClipboard.clear();
    if (selectedNotes.none())
        return false;

    auto minStep = Session::steps;
    auto minPitch = 128;
    const auto steps = session.editorStepCount();
    for (int row = 0; row < Session::pitches; ++row)
    for (int step = 0; step < steps; ++step)
    {
        const auto index = row * Session::steps + step;
        if (selectedNotes.test(static_cast<size_t>(index)) && notes.test(static_cast<size_t>(index)))
        {
            minStep = std::min(minStep, index % Session::steps);
            minPitch = std::min(minPitch, pitchForIndex(index));
        }
    }
    if (minStep >= steps || minPitch > 127)
        return false;
    clipboardBasePitch = minPitch;

    for (int row = 0; row < Session::pitches; ++row)
    for (int step = 0; step < steps; ++step)
    {
        const auto index = row * Session::steps + step;
        if (selectedNotes.test(static_cast<size_t>(index)) && notes.test(static_cast<size_t>(index)))
            noteClipboard.push_back({index % Session::steps - minStep, pitchForIndex(index) - minPitch,
                                     std::max(0.0625, static_cast<double>(noteLengths[static_cast<size_t>(index)]))});
    }
    return !noteClipboard.empty();
}

bool StepGrid::canPasteAt(int step) const
{
    for (const auto& copied : noteClipboard)
    {
        const auto targetStep = step + copied.step;
        const auto targetPitch = clipboardBasePitch + copied.pitch;
        for (int row = 0; row < Session::pitches; ++row)
            for (int existingStep = 0; existingStep < session.editorStepCount(); ++existingStep)
            {
                const auto index = row * Session::steps + existingStep;
                if (!notes.test(static_cast<size_t>(index)) || pitchForIndex(index) != targetPitch)
                    continue;
                const auto existingEnd = existingStep + std::max(0.0625, static_cast<double>(noteLengths[static_cast<size_t>(index)]));
                if (targetStep < existingEnd && existingStep < targetStep + copied.length)
                    return false;
            }
    }
    return true;
}

bool StepGrid::pasteSelection()
{
    if (noteClipboard.empty())
        return false;

    auto requiredSteps = 0;
    auto minPitchOffset = 0;
    auto maxPitchOffset = 0;
    for (const auto& note : noteClipboard)
    {
        requiredSteps = std::max(requiredSteps, static_cast<int>(std::ceil(note.step + note.length)));
        minPitchOffset = std::min(minPitchOffset, note.pitch);
        maxPitchOffset = std::max(maxPitchOffset, note.pitch);
    }

    const auto steps = session.editorStepCount();
    if (requiredSteps > Session::steps || minPitchOffset < -127 || maxPitchOffset > 127)
        return false;

    auto anchorStep = steps;
    for (int candidate = 0; candidate + requiredSteps <= steps; ++candidate)
        if (canPasteAt(candidate))
        {
            anchorStep = candidate;
            break;
        }
    const auto anchorPitch = juce::jlimit(-minPitchOffset, 127 - maxPitchOffset, clipboardBasePitch);

    session.beginNoteGesture("Paste notes");
    if (!session.ensurePatternLengthSteps(anchorStep + requiredSteps))
    {
        session.endNoteGesture();
        return false;
    }
    selectedNotes.reset();
    for (const auto& note : noteClipboard)
    {
        const auto step = anchorStep + note.step;
        const auto pitch = anchorPitch + note.pitch;
        session.setNote(step, pitch, true);
        session.resizeNote(step, pitch, note.length);
        if (const auto index = indexForCell(step, pitch); index >= 0)
            selectedNotes.set(static_cast<size_t>(index));
    }
    session.endNoteGesture();
    pasteAnchorIndex = indexForCell(std::min(session.editorStepCount() - 1, anchorStep + requiredSteps), anchorPitch);
    repaint();
    return true;
}

bool StepGrid::deleteSelection()
{
    if (selectedNotes.none())
        return false;

    session.beginNoteGesture("Delete notes");
    const auto steps = session.editorStepCount();
    for (int row = 0; row < Session::pitches; ++row)
    for (int step = 0; step < steps; ++step)
    {
        const auto index = row * Session::steps + step;
        if (selectedNotes.test(static_cast<size_t>(index)))
            session.setNote(index % Session::steps, pitchForIndex(index), false);
    }
    session.endNoteGesture();
    clearSelection();
    return true;
}

bool StepGrid::fillSelectionToClipEnd()
{
    if (selectedNotes.none())
        return false;

    session.beginNoteGesture("Fill note to clip end");
    const auto steps = session.editorStepCount();
    auto changed = false;
    for (int row = 0; row < Session::pitches; ++row)
    for (int step = 0; step < steps; ++step)
    {
        const auto index = row * Session::steps + step;
        if (selectedNotes.test(static_cast<size_t>(index)) && notes.test(static_cast<size_t>(index)))
            if (session.fillNoteToClipEnd(step, pitchForIndex(index)).wasOk())
                changed = true;
    }
    session.endNoteGesture();
    if (changed)
        repaint();
    return changed;
}

int StepGrid::pitchForIndex(int index) const
{
    return lowestVisiblePitch + Session::pitches - 1 - index / Session::steps;
}

int StepGrid::indexForCell(int step, int pitch) const
{
    const auto row = lowestVisiblePitch + Session::pitches - 1 - pitch;
    if (step < 0 || step >= session.editorStepCount() || row < 0 || row >= Session::pitches)
        return -1;
    return row * Session::steps + step;
}

juce::Result StepGrid::moveCurrentNotesBy(int stepDelta, int pitchDelta)
{
    if (movingNotes.empty() || (stepDelta == 0 && pitchDelta == 0))
        return juce::Result::ok();
    std::vector<std::pair<int, int>> sources;
    sources.reserve(movingNotes.size());
    for (const auto& note : movingNotes)
        sources.emplace_back(note.step, note.pitch);
    const auto result = session.moveNotes(sources, stepDelta, pitchDelta);
    if (result.wasOk())
    {
        selectedNotes.reset();
        for (auto& note : movingNotes)
        {
            note.step += stepDelta;
            note.pitch += pitchDelta;
            if (const auto selected = indexForCell(note.step, note.pitch); selected >= 0)
                selectedNotes.set(static_cast<size_t>(selected));
        }
        movingNoteIndex = indexForCell(movingNotes.front().step, movingNotes.front().pitch);
        noteMoved = true;
    }
    return result;
}

void StepGrid::moveDraggedNotesAt(juce::Point<float> position)
{
    const auto index = hit(position);
    if (index < 0 || lastMoveStep < 0 || lastMovePitch < 0)
        return;
    const auto step = index % Session::steps;
    const auto pitch = pitchForIndex(index);
    if (moveCurrentNotesBy(step - lastMoveStep, pitch - lastMovePitch).wasOk())
    {
        lastMoveStep = step;
        lastMovePitch = pitch;
    }
}

void StepGrid::scrollDraggedNotes()
{
    if (gesture != Gesture::move || dragPosition.x < 0.0f)
        return;

    constexpr auto edge = 28.0f;
    const auto maximumStart = std::max(0.0, static_cast<double>(session.editorStepCount()) - visibleStepSpan());
    const auto horizontalDirection = dragPosition.x < labelWidth + edge ? -1.0
        : dragPosition.x > gridRight() - edge ? 1.0 : 0.0;
    const auto nextStepScroll = std::clamp(stepScroll + horizontalDirection * 0.28, 0.0, maximumStart);
    auto changed = nextStepScroll != stepScroll;
    stepScroll = nextStepScroll;
    if (!session.isPatternDrums())
    {
        const auto verticalDirection = dragPosition.y < headerHeight + edge ? 1.0f
            : dragPosition.y > headerHeight + rowAreaHeight() - edge ? -1.0f : 0.0f;
        verticalAutoScroll += verticalDirection * 0.18f;
        const auto pitchSteps = static_cast<int>(verticalAutoScroll);
        if (pitchSteps != 0)
        {
            const auto nextLowest = juce::jlimit(0, 127 - Session::pitches + 1, lowestVisiblePitch + pitchSteps);
            changed = changed || nextLowest != lowestVisiblePitch;
            lowestVisiblePitch = nextLowest;
            verticalAutoScroll -= static_cast<float>(pitchSteps);
            manualPitchScroll = true;
            rebuildVisibleNotes();
        }
    }
    if (changed)
    {
        horizontalScroll.setCurrentRange(stepScroll, visibleStepSpan(), juce::dontSendNotification);
        updatePlayhead();
        repaint();
    }
}

juce::Result StepGrid::resizeCurrentNoteTo(int index)
{
    if (resizingNoteIndex < 0)
        return juce::Result::ok();
    if (index < 0)
        return juce::Result::ok();
    const auto sourceStep = resizingNoteIndex % Session::steps;
    const auto targetStep = index % Session::steps;
    const auto length = std::max(1, targetStep - sourceStep + 1);
    const auto result = session.resizeNote(sourceStep, pitchForIndex(resizingNoteIndex), length);
    if (result.wasOk())
        noteMoved = true;
    return result;
}

juce::Result StepGrid::resizeCurrentNoteTo(juce::Point<float> position, bool freeLength)
{
    if (resizingNoteIndex < 0)
        return juce::Result::ok();
    const auto sourceStep = resizingNoteIndex % Session::steps;
    if (resizingFromLeft)
    {
        auto newStart = static_cast<double>(stepScroll + (position.x - labelWidth) / cellWidth());
        if (!freeLength && newStart < std::floor(resizingStartStep))
            newStart = std::round(newStart);
        else
            newStart = std::round(newStart * 16.0) / 16.0;
        const auto result = session.resizeNoteFromLeft(resizingStartStep, pitchForIndex(resizingNoteIndex), newStart);
        if (result.wasOk())
        {
            resizingStartStep = newStart;
            noteMoved = true;
        }
        return result;
    }
    auto length = static_cast<double>((position.x - cell(sourceStep, 0).getX()) / cellWidth()) - resizingStartStep + sourceStep;
    length = std::max(0.0625, length);
    // The first grid space may be freely adjusted. Once past it, resize snaps
    // to grid boundaries unless Alt/Option is held.
    if (!freeLength && length > 1.0)
        length = std::round(length);
    else
        length = std::round(length * 16.0) / 16.0;
    const auto result = session.resizeNote(sourceStep, pitchForIndex(resizingNoteIndex), length);
    if (result.wasOk()) noteMoved = true;
    return result;
}

void StepGrid::mouseDrag(const juce::MouseEvent& event)
{
    if (gesture == Gesture::none) return;
    dragPosition = event.position;
    const auto index = hit(event.position);
    if (gesture == Gesture::move)
    {
        scrollDraggedNotes();
        moveDraggedNotesAt(event.position);
        return;
    }
    if (gesture == Gesture::resize)
    {
        resizeCurrentNoteTo(event.position, event.mods.isAltDown());
        return;
    }

    // Fill skipped cells for fast horizontal strokes, without toggling a cell
    // twice when the pointer retraces its path.
    if (index >= 0 && lastHit >= 0 && index / Session::steps == lastHit / Session::steps)
        for (int i = std::min(index, lastHit); i <= std::max(index, lastHit); ++i) apply(i);
    else apply(index);
    lastHit = index;
}

void StepGrid::mouseUp(const juce::MouseEvent&)
{
    if (gesture == Gesture::move && !movingGroup && !noteMoved && movingNoteIndex >= 0)
        session.setNote(movingNoteIndex % Session::steps, pitchForIndex(movingNoteIndex), false);
    if (gesture != Gesture::none) session.endNoteGesture();
    gesture = Gesture::none;
    lastHit = -1;
    movingNoteIndex = -1;
    movingNotes.clear();
    movingGroup = false;
    lastMoveStep = -1;
    lastMovePitch = -1;
    dragPosition = {-1.0f, -1.0f};
    stopTimer();
    resizingNoteIndex = -1;
    resizingFromLeft = false;
    noteMoved = false;
}

void StepGrid::timerCallback()
{
    scrollDraggedNotes();
    moveDraggedNotesAt(dragPosition);
}

bool StepGrid::keyPressed(const juce::KeyPress& key)
{
    const auto command = isShortcutDown(key.getModifiers());
    if (command && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown()) session.redo();
        else session.undo();
        return true;
    }
    if (command && key.getKeyCode() == 'Y')
    {
        session.redo();
        return true;
    }
    if (command && key.getKeyCode() == 'A')
        return selectAllNotes();
    if (command && key.getKeyCode() == 'C')
        return copySelection();
    if (command && key.getKeyCode() == 'V')
        return pasteSelection();
    if (key.getModifiers().isShiftDown()
        && (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey))
    {
        if (selectedNotes.none()) return false;
        const auto delta = key.getKeyCode() == juce::KeyPress::rightKey ? 1.0 : -1.0;
        const auto amount = key.getModifiers().isAltDown() ? delta / 16.0 : delta;
        session.beginNoteGesture("Resize notes");
        for (int row = 0; row < Session::pitches; ++row)
            for (int step = 0; step < session.editorStepCount(); ++step)
            {
                const auto index = row * Session::steps + step;
                if (selectedNotes.test(static_cast<size_t>(index)) && notes.test(static_cast<size_t>(index)))
                    session.resizeNote(step, pitchForIndex(index),
                                       std::max(0.0625, static_cast<double>(noteLengths[static_cast<size_t>(index)]) + amount));
            }
        session.endNoteGesture();
        return true;
    }
    if (key.getKeyCode() == 'F')
        return fillSelectionToClipEnd();
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
        return deleteSelection();
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        clearSelection();
        return true;
    }
    return false;
}

void StepGrid::focusGained(juce::Component::FocusChangeType)
{
    repaint();
}

void StepGrid::focusLost(juce::Component::FocusChangeType)
{
    repaint();
}

void StepGrid::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (gesture != Gesture::none || session.isPatternDrums())
        return;
    const auto wheelDelta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
    if (std::abs(wheelDelta) < 0.0001f)
        return;
    const auto semitones = std::max(1, juce::roundToInt(std::abs(wheelDelta) * 8.0f));
    lowestVisiblePitch = juce::jlimit(0, 127 - Session::pitches + 1,
                                      lowestVisiblePitch + (wheelDelta > 0.0f ? semitones : -semitones));
    manualPitchScroll = true;
    rebuildVisibleNotes();
}

int StepGrid::automaticLowestPitch() const
{
    if (session.isPatternDrums())
        return Session::lowestNote;

    int minPitch = 128, maxPitch = 0;
    for (auto* note : session.pattern().getSequence().getNotes())
    {
        minPitch = std::min(minPitch, note->getNoteNumber());
        maxPitch = std::max(maxPitch, note->getNoteNumber());
    }

    if (minPitch > maxPitch)
        return Session::lowestNote;

    auto base = std::min(Session::lowestNote, minPitch);
    if (maxPitch >= base + Session::pitches)
        base = maxPitch - Session::pitches + 1;
    return juce::jlimit(0, 127 - Session::pitches + 1, base);
}

void StepGrid::changeListenerCallback(juce::ChangeBroadcaster*)
{
    syncHorizontalScroll();
    const auto previousLowestPitch = lowestVisiblePitch;
    if (session.isPatternDrums())
        manualPitchScroll = false;
    if (!manualPitchScroll)
        lowestVisiblePitch = automaticLowestPitch();
    rebuildVisibleNotes();
    if (previousLowestPitch != lowestVisiblePitch)
        repaint();
}

void StepGrid::rebuildVisibleNotes()
{
    std::bitset<Session::steps * Session::pitches> next;
    std::array<float, Session::steps * Session::pitches> nextLengths {}, nextStartOffsets {};
    const auto nextDrumLabels = session.isPatternDrums();
    const auto steps = session.editorStepCount();
    const auto beatsPerStep = 4.0 / static_cast<double>(session.editorStepResolution());
    for (auto* note : session.pattern().getSequence().getNotes())
    {
        const auto row = lowestVisiblePitch + Session::pitches - 1 - note->getNoteNumber();
        const auto startInSteps = note->getStartBeat().inBeats() / beatsPerStep;
        const auto step = static_cast<int>(std::floor(startInSteps));
        if (row >= 0 && row < Session::pitches && step >= 0 && step < steps)
        {
            next.set(static_cast<size_t>(row * Session::steps + step));
            nextLengths[static_cast<size_t>(row * Session::steps + step)] =
                static_cast<float>(std::max(0.0625, note->getLengthBeats().inBeats() / beatsPerStep));
            nextStartOffsets[static_cast<size_t>(row * Session::steps + step)] =
                static_cast<float>(startInSteps - step);
        }
    }
    const auto changed = next ^ notes;
    const auto lengthsChanged = nextLengths != noteLengths;
    const auto offsetsChanged = nextStartOffsets != noteStartOffsets;
    const auto stepCountChanged = steps != visibleStepCount;
    visibleStepCount = steps;
    syncHorizontalScroll();
    notes = next;
    noteLengths = nextLengths;
    noteStartOffsets = nextStartOffsets;
    selectedNotes &= notes;
    if (stepCountChanged || lengthsChanged || offsetsChanged || showingDrumLabels != nextDrumLabels)
    {
        showingDrumLabels = nextDrumLabels;
        repaint();
        return;
    }
    if (manualPitchScroll)
    {
        repaint();
        return;
    }
    for (int row = 0; row < Session::pitches; ++row)
    for (int step = 0; step < steps; ++step)
    {
        const auto index = row * Session::steps + step;
        if (changed.test(static_cast<size_t>(index))) repaint(cell(step, row).getSmallestIntegerContainer());
    }
}

void StepGrid::updatePlayhead()
{
    float next = -1.0f;
    auto& transport = session.edit->getTransport();
    if (isShowing() && transport.isPlaying())
        next = playheadXForTime(playheadTime(transport));
    movePlayhead(*this, playhead, next,
                 getLocalBounds().withTrimmedTop(static_cast<int>(headerHeight))
                                 .withTrimmedBottom(horizontalScroll.isVisible() ? static_cast<int>(scrollHeight) : 0));
}

float StepGrid::playheadXForTime(double seconds) const
{
    const auto& position = session.pattern().getPosition();
    const auto clipStart = position.time.getStart().inSeconds();
    const auto clipEnd = position.time.getEnd().inSeconds();
    if (seconds < clipStart || seconds >= clipEnd)
        return -1.0f;

    const auto editBeat = session.edit->tempoSequence.toBeats(tracktion::core::TimePosition::fromSeconds(seconds)).inBeats();
    const auto clipStartBeat = session.edit->tempoSequence.toBeats(position.time.getStart()).inBeats();
    const auto offsetBeat = position.offset.inSeconds() * session.tempo() / 60.0;
    auto localBeat = std::fmod(editBeat - clipStartBeat + offsetBeat, session.patternLengthBeats());
    if (localBeat < 0.0)
        localBeat += session.patternLengthBeats();
    const auto step = localBeat / 4.0 * session.editorStepResolution();
    if (step < stepScroll || step > stepScroll + visibleStepSpan())
        return -1.0f;
    return static_cast<float>(labelWidth + (step - stepScroll) * cellWidth());
}

void StepGrid::syncHorizontalScroll()
{
    const auto steps = session.editorStepCount();
    stepZoom = std::max(1.0, static_cast<double>(steps) / static_cast<double>(Session::defaultSteps));
    const auto visible = visibleStepSpan();
    const auto maximumStart = std::max(0.0, static_cast<double>(steps) - visible);
    stepScroll = std::clamp(stepScroll, 0.0, maximumStart);
    horizontalScroll.setVisible(maximumStart > 0.001);
    horizontalScroll.setRangeLimits(0.0, static_cast<double>(steps), juce::dontSendNotification);
    horizontalScroll.setCurrentRange(stepScroll, visible, juce::dontSendNotification);
}

void StepGrid::scrollBarMoved(juce::ScrollBar* bar, double start)
{
    if (bar != &horizontalScroll)
        return;
    const auto maximumStart = std::max(0.0, static_cast<double>(session.editorStepCount()) - visibleStepSpan());
    stepScroll = std::clamp(start, 0.0, maximumStart);
    updatePlayhead();
    repaint();
}

void StepGrid::resized()
{
    syncHorizontalScroll();
    horizontalScroll.setBounds(static_cast<int>(labelWidth), getHeight() - static_cast<int>(scrollHeight),
                               std::max(1, static_cast<int>(gridRight() - labelWidth)), static_cast<int>(scrollHeight));
    updatePlayhead();
    repaint();
}
}
