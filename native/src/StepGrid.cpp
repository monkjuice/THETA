#include "StepGrid.h"
#include "Playhead.h"
#include <algorithm>
#include <cmath>
#include <limits>

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
    finishSubdivision();
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

juce::Rectangle<float> StepGrid::boundsFor(const VisibleNote& note) const
{
    auto bounds = cell(static_cast<int>(std::floor(note.start)), note.row);
    bounds.translate(static_cast<float>(note.start - std::floor(note.start)) * cellWidth(), 0.0f);
    bounds.setWidth(std::max(3.0f, static_cast<float>(note.length) * cellWidth()));
    bounds.setRight(std::min(bounds.getRight(), gridRight()));
    return bounds;
}

const StepGrid::VisibleNote* StepGrid::noteForState(const juce::ValueTree& state) const
{
    const auto found = std::find_if(visibleNotes.begin(), visibleNotes.end(), [&state](const auto& note)
    {
        return note.state == state;
    });
    return found == visibleNotes.end() ? nullptr : &*found;
}

bool StepGrid::isSelected(const juce::ValueTree& state) const
{
    return std::find(selectedNoteStates.begin(), selectedNoteStates.end(), state) != selectedNoteStates.end();
}

std::vector<juce::ValueTree> StepGrid::selectedStates() const
{
    std::vector<juce::ValueTree> result;
    std::bitset<Session::steps * Session::pitches> representedCells;
    for (const auto& state : selectedNoteStates)
        if (const auto* note = noteForState(state))
        {
            const auto index = note->row * Session::steps + static_cast<int>(std::floor(note->start));
            if (index >= 0 && selectedNotes.test(static_cast<size_t>(index)))
            {
                result.push_back(state);
                representedCells.set(static_cast<size_t>(index));
            }
        }

    // Keep cell-addressed selection commands meaningful. If a selected cell
    // has no explicit instance selection, it represents every retrigger that
    // begins in that cell.
    for (const auto& note : visibleNotes)
    {
        const auto index = note.row * Session::steps + static_cast<int>(std::floor(note.start));
        if (index >= 0 && selectedNotes.test(static_cast<size_t>(index))
            && !representedCells.test(static_cast<size_t>(index)))
            result.push_back(note.state);
    }
    return result;
}

void StepGrid::setSelectedStates(std::vector<juce::ValueTree> states)
{
    selectedNoteStates = std::move(states);
    selectedNotes.reset();
    for (const auto& state : selectedNoteStates)
        if (const auto* note = noteForState(state))
        {
            const auto index = note->row * Session::steps + static_cast<int>(std::floor(note->start));
            if (index >= 0) selectedNotes.set(static_cast<size_t>(index));
        }
}

void StepGrid::zoomIn()
{
    zoomAt(1.5, labelWidth + gridWidth() * 0.5f);
}

void StepGrid::zoomOut()
{
    zoomAt(1.0 / 1.5, labelWidth + gridWidth() * 0.5f);
}

void StepGrid::zoomAt(double factor, float pointerX)
{
    const auto anchor = std::clamp((pointerX - labelWidth) / gridWidth(), 0.0f, 1.0f);
    const auto anchorStep = stepScroll + static_cast<double>(anchor) * visibleStepSpan();
    stepZoom = std::clamp(stepZoom * factor, 1.0, 8.0);
    stepScroll = anchorStep - static_cast<double>(anchor) * visibleStepSpan();
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
        for (const auto& note : visibleNotes)
        {
            if (note.row != row)
                continue;
            auto bounds = boundsFor(note);
            if (bounds.getRight() < labelWidth || !dirty.intersects(bounds))
                continue;

            const auto selected = isSelected(note.state);
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
    // Draw the grid after all cells. This avoids the next row's fractional
    // fill covering the preceding row separator on high-DPI displays.
    g.setColour(juce::Colour(0xff202930));
    for (int row = 0; row <= Session::pitches; ++row)
    {
        const auto y = headerHeight + row * rowAreaHeight() / Session::pitches;
        g.fillRect(juce::Rectangle<float>(labelWidth, std::floor(y), gridWidth(), 1.0f));
    }
    for (int row = 0; row < Session::pitches; ++row)
    {
        std::array<int, Session::steps + 2> sustainedBoundaryDeltas {};
        for (const auto& note : visibleNotes)
        {
            if (note.row != row)
                continue;
            const auto noteStart = note.start;
            const auto noteEnd = noteStart + std::max(0.0625, note.length);
            constexpr float boundaryTolerance = 0.0001f;
            const auto firstCovered = std::clamp(static_cast<int>(std::floor(noteStart + boundaryTolerance)) + 1,
                                                 0, steps + 1);
            const auto afterLastCovered = std::clamp(static_cast<int>(std::ceil(noteEnd - boundaryTolerance)),
                                                     0, steps + 1);
            if (firstCovered < afterLastCovered)
            {
                ++sustainedBoundaryDeltas[static_cast<size_t>(firstCovered)];
                --sustainedBoundaryDeltas[static_cast<size_t>(afterLastCovered)];
            }
        }

        int sustainedNotes = 0;
        const auto rowTop = headerHeight + row * rowAreaHeight() / Session::pitches;
        const auto rowBottom = headerHeight + (row + 1) * rowAreaHeight() / Session::pitches;
        for (int step = 0; step <= lastVisibleStep + 1; ++step)
        {
            sustainedNotes += sustainedBoundaryDeltas[static_cast<size_t>(step)];
            if (step >= firstVisibleStep && sustainedNotes == 0)
            {
                const auto x = cell(step, 0).getX();
                g.setColour(juce::Colour(step % 4 == 0 ? 0xff252d35 : 0xff303941));
                g.drawVerticalLine(juce::roundToInt(x), rowTop, rowBottom);
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
    if (gesture == Gesture::select && !selectionBox.isEmpty())
    {
        g.setColour(juce::Colour(0x3355c7eb));
        g.fillRect(selectionBox);
        g.setColour(juce::Colour(0xff55c7eb));
        static constexpr float dash[] {4.0f, 3.0f};
        g.drawDashedLine(juce::Line<float>(selectionBox.getTopLeft(), selectionBox.getTopRight()), dash, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(selectionBox.getTopRight(), selectionBox.getBottomRight()), dash, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(selectionBox.getBottomRight(), selectionBox.getBottomLeft()), dash, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(selectionBox.getBottomLeft(), selectionBox.getTopLeft()), dash, 2, 1.0f);
    }
    if (subdivisionActive && subdivisionCount >= 2)
    {
        const auto centre = subdivisionSourceBounds.getCentre();
        const auto badge = juce::Rectangle<float>(centre.x - 17.0f, subdivisionSourceBounds.getY() - 28.0f, 34.0f, 22.0f);
        g.setColour(juce::Colour(0xffe5e8df));
        g.fillRoundedRectangle(badge, 3.0f);
        g.setColour(juce::Colour(0xff252a30));
        g.drawRoundedRectangle(badge, 3.0f, 1.0f);
        g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
        g.drawText(juce::String(subdivisionCount), badge, juce::Justification::centred);
    }
}

int StepGrid::cellHit(juce::Point<float> point) const
{
    if (point.x < labelWidth || point.y < headerHeight || point.x >= gridRight() || point.y >= headerHeight + rowAreaHeight())
        return -1;
    const auto steps = session.editorStepCount();
    const auto step = static_cast<int>(stepScroll + (point.x - labelWidth) / cellWidth());
    if (step < 0 || step >= steps)
        return -1;
    const auto row = static_cast<int>((point.y - headerHeight) / rowAreaHeight() * Session::pitches);
    return row * Session::steps + step;
}

int StepGrid::hit(juce::Point<float> point) const
{
    if (cellHit(point) < 0)
        return -1;
    // Prefer the shortest containing note. This makes tightly packed
    // retriggers easy to pick even next to a long sustained note.
    auto best = -1;
    auto bestWidth = std::numeric_limits<float>::max();
    for (int candidate = 0; candidate < static_cast<int>(visibleNotes.size()); ++candidate)
        if (const auto bounds = boundsFor(visibleNotes[static_cast<size_t>(candidate)]); bounds.contains(point)
            && bounds.getWidth() < bestWidth)
        {
            best = candidate;
            bestWidth = bounds.getWidth();
        }
    return best;
}

int StepGrid::resizeHit(juce::Point<float> point) const
{
    if (point.y < headerHeight || point.y >= headerHeight + rowAreaHeight())
        return -1;
    for (int index = 0; index < static_cast<int>(visibleNotes.size()); ++index)
    {
        auto bounds = boundsFor(visibleNotes[static_cast<size_t>(index)]);
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
    if (isShortcutDown(modifiers) && note >= 0)
        setMouseCursor(juce::MouseCursor::NormalCursor);
    else if (!modifiers.isRightButtonDown() && resizeHit(position) >= 0)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (cellHit(position) >= 0)
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
    finishSubdivision();
    if (!event.mods.isRightButtonDown() && juce::KeyPress::isKeyCurrentlyDown('S')
        && event.position.x >= labelWidth && event.position.y >= headerHeight)
    {
        grabKeyboardFocus();
        gesture = Gesture::select;
        selectionAnchor = event.position;
        selectionBox = {};
        selectedNotes.reset();
        selectedNoteStates.clear();
        repaint();
        return;
    }
    if (!event.mods.isRightButtonDown()
        && event.position.y >= 0.0f && event.position.y < headerHeight
        && event.position.x >= labelWidth && event.position.x < gridRight())
    {
        const auto localStep = std::clamp(stepScroll + (event.position.x - labelWidth) / cellWidth(),
                                          0.0, static_cast<double>(session.editorStepCount()));
        const auto localBeat = localStep * 4.0 / static_cast<double>(session.editorStepResolution());
        const auto& position = session.pattern().getPosition();
        const auto clipStartBeat = session.edit->tempoSequence.toBeats(position.time.getStart()).inBeats();
        const auto offsetBeat = position.offset.inSeconds() * session.tempo() / 60.0;
        const auto requestedTime = session.edit->tempoSequence.toTime(
            tracktion::core::BeatPosition::fromBeats(clipStartBeat - offsetBeat + localBeat)).inSeconds();
        const auto clippedTime = std::clamp(requestedTime, position.time.getStart().inSeconds(), position.time.getEnd().inSeconds());
        session.edit->getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(clippedTime));
        updatePlayhead();
        return;
    }
    if (!event.mods.isRightButtonDown() && !isShortcutDown(event.mods))
    {
        const auto resizeIndex = resizeHit(event.position);
        if (resizeIndex >= 0)
        {
            const auto& note = visibleNotes[static_cast<size_t>(resizeIndex)];
            grabKeyboardFocus();
            gesture = Gesture::resize;
            resizingNoteState = note.state;
            const auto bounds = boundsFor(note);
            resizingFromLeft = event.position.x < bounds.getCentreX();
            resizingStartStep = note.start;
            noteMoved = false;
            session.beginNoteGesture("Resize note");
            return;
        }
    }
    const auto noteIndex = hit(event.position);
    const auto cellIndex = cellHit(event.position);
    if (cellIndex < 0) return;
    grabKeyboardFocus();
    lastHit = cellIndex;
    pasteAnchorIndex = cellIndex;
    if (isShortcutDown(event.mods) && noteIndex >= 0)
    {
        toggleSelection(noteIndex);
        return;
    }
    if (!event.mods.isRightButtonDown() && noteIndex >= 0)
    {
        const auto& clicked = visibleNotes[static_cast<size_t>(noteIndex)];
        gesture = Gesture::move;
        movingNoteState = clicked.state;
        movingNotes.clear();
        movingGroup = isSelected(clicked.state);
        if (movingGroup)
            for (const auto& state : selectedStates())
                if (const auto* note = noteForState(state))
                    movingNotes.push_back({state, note->start, note->pitch});
        if (movingNotes.empty())
            movingNotes.push_back({clicked.state, clicked.start, clicked.pitch});
        const auto grabbedCell = cellHit(event.position);
        lastMoveStep = grabbedCell % Session::steps;
        lastMovePitch = pitchForIndex(grabbedCell);
        dragPosition = event.position;
        verticalAutoScroll = 0.0f;
        noteMoved = false;
        session.beginNoteGesture("Move note");
        startTimerHz(60);
        return;
    }

    gesture = Gesture::draw;
    adding = !event.mods.isRightButtonDown() && noteIndex < 0;
    visited.reset();
    session.beginNoteGesture(adding ? "Draw notes" : "Erase notes");
    if (!adding && noteIndex >= 0)
        session.removeNotes({visibleNotes[static_cast<size_t>(noteIndex)].state});
    else
        apply(cellIndex);
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
    if (index < 0 || index >= static_cast<int>(visibleNotes.size()))
    {
        repaint();
        return;
    }
    const auto& note = visibleNotes[static_cast<size_t>(index)];
    auto current = selectedStates();
    if (std::find(current.begin(), current.end(), note.state) != current.end())
        std::erase(current, note.state);
    else
        current.push_back(note.state);
    setSelectedStates(std::move(current));
    const auto bounds = boundsFor(note);
    repaint(bounds.getSmallestIntegerContainer().expanded(3));
}

bool StepGrid::selectAllNotes()
{
    std::vector<juce::ValueTree> all;
    all.reserve(visibleNotes.size());
    for (const auto& note : visibleNotes) all.push_back(note.state);
    setSelectedStates(std::move(all));
    repaint();
    return !selectedNoteStates.empty();
}

void StepGrid::clearSelection()
{
    if (selectedNotes.none() && selectedNoteStates.empty())
        return;
    selectedNotes.reset();
    selectedNoteStates.clear();
    repaint();
}

bool StepGrid::copySelection()
{
    noteClipboard.clear();
    const auto states = selectedStates();
    if (states.empty())
        return false;

    auto minStep = static_cast<double>(Session::steps);
    auto minPitch = 128;
    for (const auto& state : states)
        if (const auto* note = noteForState(state))
        {
            minStep = std::min(minStep, note->start);
            minPitch = std::min(minPitch, note->pitch);
        }
    if (minStep >= session.editorStepCount() || minPitch > 127)
        return false;
    clipboardBasePitch = minPitch;

    for (const auto& state : states)
        if (const auto* note = noteForState(state))
            noteClipboard.push_back({note->start - minStep, note->pitch - minPitch, std::max(0.001, note->length)});
    return !noteClipboard.empty();
}

bool StepGrid::canPasteAt(int step) const
{
    for (const auto& copied : noteClipboard)
    {
        const auto targetStep = step + copied.step;
        const auto targetPitch = clipboardBasePitch + copied.pitch;
        for (const auto& existing : visibleNotes)
            if (existing.pitch == targetPitch
                && targetStep < existing.start + existing.length - 0.0001
                && existing.start < targetStep + copied.length - 0.0001)
                return false;
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
    std::vector<juce::ValueTree> pasted;
    for (const auto& note : noteClipboard)
    {
        const auto step = anchorStep + note.step;
        const auto pitch = anchorPitch + note.pitch;
        juce::ValueTree state;
        if (session.addNote(step, pitch, note.length, &state).wasOk())
            pasted.push_back(state);
    }
    session.endNoteGesture();
    rebuildVisibleNotes();
    setSelectedStates(std::move(pasted));
    pasteAnchorIndex = indexForCell(std::min(session.editorStepCount() - 1, anchorStep + requiredSteps), anchorPitch);
    repaint();
    return true;
}

bool StepGrid::deleteSelection()
{
    const auto states = selectedStates();
    if (states.empty())
        return false;

    session.beginNoteGesture("Delete notes");
    session.removeNotes(states);
    session.endNoteGesture();
    clearSelection();
    return true;
}

bool StepGrid::beginSubdivision()
{
    const auto states = selectedStates();
    if (states.empty()) return false;
    auto start = std::numeric_limits<double>::max();
    auto end = 0.0;
    auto pitch = -1;
    for (const auto& state : states)
        if (const auto* note = noteForState(state))
        {
            if (pitch >= 0 && pitch != note->pitch) return false;
            pitch = note->pitch;
            start = std::min(start, note->start);
            end = std::max(end, note->start + note->length);
        }
    if (pitch < 0 || end <= start + 0.0001) return false;
    subdivisionCount = juce::jlimit(2, 32, juce::roundToInt(end - start));
    const auto row = lowestVisiblePitch + Session::pitches - 1 - pitch;
    subdivisionSourceBounds = cell(static_cast<int>(std::floor(start)), row);
    subdivisionSourceBounds.translate(static_cast<float>(start - std::floor(start)) * cellWidth(), 0.0f);
    subdivisionSourceBounds.setWidth(static_cast<float>(end - start) * cellWidth());
    session.beginNoteGesture("Divide note");
    subdivisionActive = true;
    std::vector<juce::ValueTree> replacements;
    if (session.redistributeNotes(states, subdivisionCount, replacements).failed())
    {
        finishSubdivision();
        return false;
    }
    rebuildVisibleNotes();
    setSelectedStates(std::move(replacements));
    startTimerHz(30);
    repaint();
    return true;
}

bool StepGrid::adjustSubdivision(int delta)
{
    if (!subdivisionActive || delta == 0) return false;
    const auto next = juce::jlimit(2, 32, subdivisionCount + delta);
    if (next == subdivisionCount) return true;
    std::vector<juce::ValueTree> replacements;
    if (session.redistributeNotes(selectedNoteStates, next, replacements).failed()) return false;
    subdivisionCount = next;
    rebuildVisibleNotes();
    setSelectedStates(std::move(replacements));
    repaint();
    return true;
}

void StepGrid::finishSubdivision()
{
    if (!subdivisionActive) return;
    subdivisionActive = false;
    session.endNoteGesture();
    if (gesture != Gesture::move) stopTimer();
    repaint();
}

bool StepGrid::fillSelectionToClipEnd()
{
    const auto states = selectedStates();
    if (states.empty())
        return false;

    session.beginNoteGesture("Fill note to clip end");
    auto changed = false;
    for (const auto& state : states)
        if (session.fillNoteToClipEnd(state).wasOk()) changed = true;
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
    std::vector<juce::ValueTree> sources;
    sources.reserve(movingNotes.size());
    for (const auto& note : movingNotes)
        sources.push_back(note.state);
    const auto result = session.moveNotes(sources, stepDelta, pitchDelta);
    if (result.wasOk())
    {
        std::vector<juce::ValueTree> moved;
        for (auto& note : movingNotes)
        {
            note.step += stepDelta;
            note.pitch += pitchDelta;
            moved.push_back(note.state);
        }
        if (movingGroup) setSelectedStates(std::move(moved));
        else clearSelection();
        noteMoved = true;
    }
    return result;
}

void StepGrid::moveDraggedNotesAt(juce::Point<float> position)
{
    const auto index = cellHit(position);
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
    if (!resizingNoteState.isValid())
        return juce::Result::ok();
    if (index < 0)
        return juce::Result::ok();
    const auto targetStep = index % Session::steps;
    const auto length = std::max(0.0625, targetStep - resizingStartStep + 1.0);
    const auto result = session.resizeNote(resizingNoteState, length);
    if (result.wasOk())
        noteMoved = true;
    return result;
}

juce::Result StepGrid::resizeCurrentNoteTo(juce::Point<float> position, bool freeLength)
{
    if (!resizingNoteState.isValid())
        return juce::Result::ok();
    if (resizingFromLeft)
    {
        auto newStart = static_cast<double>(stepScroll + (position.x - labelWidth) / cellWidth());
        if (!freeLength && newStart < std::floor(resizingStartStep))
            newStart = std::round(newStart);
        else
            newStart = std::round(newStart * 16.0) / 16.0;
        const auto result = session.resizeNoteFromLeft(resizingNoteState, newStart);
        if (result.wasOk())
        {
            resizingStartStep = newStart;
            noteMoved = true;
        }
        return result;
    }
    auto length = stepScroll + (position.x - labelWidth) / cellWidth() - resizingStartStep;
    length = std::max(0.0625, length);
    // The first grid space may be freely adjusted. Once past it, resize snaps
    // to grid boundaries unless Alt/Option is held.
    if (!freeLength && length > 1.0)
        length = std::round(length);
    else
        length = std::round(length * 16.0) / 16.0;
    const auto result = session.resizeNote(resizingNoteState, length);
    if (result.wasOk()) noteMoved = true;
    return result;
}

void StepGrid::mouseDrag(const juce::MouseEvent& event)
{
    if (gesture == Gesture::none) return;
    dragPosition = event.position;
    if (gesture == Gesture::select)
    {
        selectionBox = juce::Rectangle<float>(selectionAnchor, event.position).getIntersection(
            {labelWidth, headerHeight, gridWidth(), rowAreaHeight()});
        updateMarqueeSelection();
        repaint();
        return;
    }
    const auto index = cellHit(event.position);
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

    if (!adding)
    {
        if (const auto noteIndex = hit(event.position); noteIndex >= 0)
            session.removeNotes({visibleNotes[static_cast<size_t>(noteIndex)].state});
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
    if (gesture == Gesture::move && !movingGroup && !noteMoved && movingNoteState.isValid())
        session.removeNotes({movingNoteState});
    if (gesture != Gesture::none) session.endNoteGesture();
    gesture = Gesture::none;
    selectionAnchor = {-1.0f, -1.0f};
    selectionBox = {};
    lastHit = -1;
    movingNoteState = {};
    movingNotes.clear();
    movingGroup = false;
    lastMoveStep = -1;
    lastMovePitch = -1;
    dragPosition = {-1.0f, -1.0f};
    stopTimer();
    resizingNoteState = {};
    resizingFromLeft = false;
    noteMoved = false;
}

void StepGrid::updateMarqueeSelection()
{
    selectedNotes.reset();
    selectedNoteStates.clear();
    if (selectionBox.isEmpty()) return;
    for (const auto& note : visibleNotes)
        if (boundsFor(note).intersects(selectionBox)) selectedNoteStates.push_back(note.state);
    setSelectedStates(selectedNoteStates);
}

void StepGrid::timerCallback()
{
    if (subdivisionActive)
    {
        if (!isShortcutDown(juce::ModifierKeys::getCurrentModifiersRealtime())) finishSubdivision();
        return;
    }
    scrollDraggedNotes();
    moveDraggedNotesAt(dragPosition);
}

bool StepGrid::keyPressed(const juce::KeyPress& key)
{
    const auto command = isShortcutDown(key.getModifiers());
    if (subdivisionActive && command
        && (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::rightKey
            || key.getKeyCode() == juce::KeyPress::downKey || key.getKeyCode() == juce::KeyPress::leftKey))
        return adjustSubdivision(key.getKeyCode() == juce::KeyPress::upKey
                              || key.getKeyCode() == juce::KeyPress::rightKey ? 1 : -1);
    if (subdivisionActive && !(command && key.getKeyCode() == 'E'))
        finishSubdivision();
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
    if (command && key.getKeyCode() == 'E')
        return subdivisionActive || beginSubdivision();
    if (command && key.getKeyCode() == 'V')
        return pasteSelection();
    if (key.getModifiers().isShiftDown()
        && (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey))
    {
        const auto states = selectedStates();
        if (states.empty()) return false;
        const auto delta = key.getKeyCode() == juce::KeyPress::rightKey ? 1.0 : -1.0;
        const auto amount = key.getModifiers().isAltDown() ? delta / 16.0 : delta;
        session.beginNoteGesture("Resize notes");
        for (const auto& state : states)
            if (const auto* note = noteForState(state))
                session.resizeNote(state, std::max(0.0625, note->length + amount));
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
    finishSubdivision();
    repaint();
}

void StepGrid::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (gesture != Gesture::none)
        return;
    const auto wheelDelta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
    if (std::abs(wheelDelta) < 0.0001f)
        return;
    if (isShortcutDown(event.mods))
    {
        if (!subdivisionActive)
        {
            beginSubdivision();
            return;
        }
        adjustSubdivision(wheelDelta > 0.0f ? 1 : -1);
        return;
    }
    finishSubdivision();
    if (event.mods.isShiftDown())
    {
        // Keep the step below the pointer stable while the visible range changes.
        zoomAt(std::exp(wheelDelta * 2.0f), event.position.x);
        return;
    }
    if (session.isPatternDrums())
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
    std::vector<VisibleNote> nextVisible;
    for (const auto& note : session.editorNotes())
    {
        const auto row = lowestVisiblePitch + Session::pitches - 1 - note.pitch;
        const auto step = static_cast<int>(std::floor(note.startSteps));
        if (row >= 0 && row < Session::pitches && step >= 0 && step < steps)
        {
            nextVisible.push_back({note.state, note.startSteps, note.lengthSteps, note.pitch, row});
            next.set(static_cast<size_t>(row * Session::steps + step));
            nextLengths[static_cast<size_t>(row * Session::steps + step)] =
                static_cast<float>(std::max(0.0625, note.lengthSteps));
            nextStartOffsets[static_cast<size_t>(row * Session::steps + step)] =
                static_cast<float>(note.startSteps - step);
        }
    }
    const auto changed = next ^ notes;
    const auto lengthsChanged = nextLengths != noteLengths;
    const auto offsetsChanged = nextStartOffsets != noteStartOffsets;
    const auto stepCountChanged = steps != visibleStepCount;
    if (stepCountChanged)
        stepZoom = std::max(1.0, static_cast<double>(steps) / static_cast<double>(Session::defaultSteps));
    visibleStepCount = steps;
    syncHorizontalScroll();
    notes = next;
    noteLengths = nextLengths;
    noteStartOffsets = nextStartOffsets;
    visibleNotes = std::move(nextVisible);
    std::erase_if(selectedNoteStates, [this](const auto& state) { return noteForState(state) == nullptr; });
    setSelectedStates(selectedNoteStates);
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
