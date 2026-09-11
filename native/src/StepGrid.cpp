#include "StepGrid.h"
#include "Playhead.h"
#include <cmath>

namespace theta
{
namespace
{
juce::String drumLaneName(int pitch)
{
    if (pitch == 48) return "Kick";
    if (pitch == 53) return "Snare";
    if (pitch == 56) return "Clap";
    if (pitch == 58) return "Hat";
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
    resolutionBox.addItem("1/16", 16);
    resolutionBox.addItem("1/32", 32);
    resolutionBox.addItem("1/64", 64);
    resolutionBox.setJustificationType(juce::Justification::centred);
    resolutionBox.onChange = [this]
    {
        if (!updatingResolutionBox && resolutionBox.getSelectedId() > 0)
            session.setEditorStepCount(resolutionBox.getSelectedId());
    };
    addAndMakeVisible(resolutionBox);
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
        const bool namedDrum = session.isPatternDrums() && (pitch == 48 || pitch == 53 || pitch == 56 || pitch == 58);
        auto key = cell(0, row).withX(0).withWidth(labelWidth - 4);
        g.setColour(juce::Colour(namedDrum ? 0xff3a3325 : black ? 0xff15191e : 0xff30373e));
        g.fillRect(key.reduced(0, 1));
        g.setColour(juce::Colour(namedDrum ? 0xffffc16a : 0xffbac2ca));
        g.drawText(session.isPatternDrums() ? drumLaneName(pitch) : juce::MidiMessage::getMidiNoteName(pitch, true, true, 4),
                   key, juce::Justification::centred);
        for (int step = firstVisibleStep; step <= lastVisibleStep; ++step)
        {
            auto bounds = cell(step, row).reduced(2.0f, 2.0f);
            if (!dirty.intersects(bounds)) continue;
            g.setColour(juce::Colour(step / 4 % 2 == 0 ? 0xff2a3139 : 0xff252c33));
            g.fillRect(bounds);
        }

        for (int step = 0; step <= lastVisibleStep; ++step)
        {
            const auto index = row * Session::steps + step;
            if (!notes.test(static_cast<size_t>(index)))
                continue;

            const auto length = std::max(1, noteLengths[static_cast<size_t>(index)]);
            auto bounds = cell(step, row).reduced(2.0f, 2.0f);
            bounds.setWidth(std::max(bounds.getWidth(), cellWidth() * length - 4.0f));
            bounds.setRight(std::min(bounds.getRight(), gridRight() - 2.0f));
            if (bounds.getRight() < labelWidth || !dirty.intersects(bounds))
                continue;

            g.setColour(juce::Colour(0xffc6d58c));
            g.fillRect(bounds);
            g.setColour(juce::Colour(0x55363f46));
            g.fillRect(bounds.withWidth(2.0f));
            g.setColour(juce::Colour(0xffe8f2aa));
            g.fillRect(bounds.withX(bounds.getRight() - 3.0f).withWidth(3.0f));
            if (selectedNotes.test(static_cast<size_t>(index)))
            {
                g.setColour(juce::Colour(0xfff4f0b0));
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
        const auto length = std::max(1, noteLengths[static_cast<size_t>(index)]);
        auto bounds = cell(step, row).reduced(2.0f, 2.0f);
        bounds.setWidth(std::max(bounds.getWidth(), cellWidth() * length - 4.0f));
        bounds.setRight(std::min(bounds.getRight(), gridRight() - 2.0f));
        if (bounds.getRight() < labelWidth || bounds.getX() > gridRight())
            continue;
        const auto handleWidth = std::min(5.0f, std::max(3.0f, bounds.getWidth() * 0.25f));
        const auto handle = bounds.withX(bounds.getRight() - handleWidth).withWidth(handleWidth);
        if (handle.contains(point))
            return index;
    }
    return -1;
}

void StepGrid::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isRightButtonDown())
    {
        const auto resizeIndex = resizeHit(event.position);
        if (resizeIndex >= 0)
        {
            grabKeyboardFocus();
            gesture = Gesture::resize;
            resizingNoteIndex = resizeIndex;
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
        noteMoved = false;
        session.beginNoteGesture("Move note");
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

    for (int row = 0; row < Session::pitches; ++row)
    for (int step = 0; step < steps; ++step)
    {
        const auto index = row * Session::steps + step;
        if (selectedNotes.test(static_cast<size_t>(index)) && notes.test(static_cast<size_t>(index)))
            noteClipboard.push_back({index % Session::steps - minStep, pitchForIndex(index) - minPitch});
    }
    pasteAnchorIndex = indexForCell(std::min(steps - 1, minStep + 1), minPitch);
    return !noteClipboard.empty();
}

bool StepGrid::pasteSelection()
{
    if (noteClipboard.empty())
        return false;

    auto maxStepOffset = 0;
    auto minPitchOffset = 0;
    auto maxPitchOffset = 0;
    for (const auto& note : noteClipboard)
    {
        maxStepOffset = std::max(maxStepOffset, note.step);
        minPitchOffset = std::min(minPitchOffset, note.pitch);
        maxPitchOffset = std::max(maxPitchOffset, note.pitch);
    }

    const auto anchor = pasteAnchorIndex >= 0 ? pasteAnchorIndex : 0;
    const auto steps = session.editorStepCount();
    const auto anchorStep = juce::jlimit(0, std::max(0, steps - 1 - maxStepOffset), anchor % Session::steps);
    const auto anchorPitch = juce::jlimit(-minPitchOffset, 127 - maxPitchOffset, pitchForIndex(anchor));

    session.beginNoteGesture("Paste notes");
    selectedNotes.reset();
    for (const auto& note : noteClipboard)
    {
        const auto step = anchorStep + note.step;
        const auto pitch = anchorPitch + note.pitch;
        session.setNote(step, pitch, true);
        if (const auto index = indexForCell(step, pitch); index >= 0)
            selectedNotes.set(static_cast<size_t>(index));
    }
    session.endNoteGesture();
    pasteAnchorIndex = indexForCell(std::min(steps - 1, anchorStep + maxStepOffset + 1), anchorPitch);
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

juce::Result StepGrid::moveCurrentNoteTo(int index)
{
    if (index < 0 || movingNoteIndex < 0 || index == movingNoteIndex)
        return juce::Result::ok();
    const auto result = session.moveNote(movingNoteIndex % Session::steps, pitchForIndex(movingNoteIndex),
                                         index % Session::steps, pitchForIndex(index));
    if (result.wasOk())
    {
        movingNoteIndex = index;
        noteMoved = true;
    }
    return result;
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

void StepGrid::mouseDrag(const juce::MouseEvent& event)
{
    if (gesture == Gesture::none) return;
    const auto index = hit(event.position);
    if (gesture == Gesture::move)
    {
        moveCurrentNoteTo(index);
        return;
    }
    if (gesture == Gesture::resize)
    {
        resizeCurrentNoteTo(index);
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
    if (gesture == Gesture::move && !noteMoved && movingNoteIndex >= 0)
        session.setNote(movingNoteIndex % Session::steps, pitchForIndex(movingNoteIndex), false);
    if (gesture != Gesture::none) session.endNoteGesture();
    gesture = Gesture::none;
    lastHit = -1;
    movingNoteIndex = -1;
    resizingNoteIndex = -1;
    noteMoved = false;
}

bool StepGrid::keyPressed(const juce::KeyPress& key)
{
    const auto command = isShortcutDown(key.getModifiers());
    if (command && key.getKeyCode() == 'C')
        return copySelection();
    if (command && key.getKeyCode() == 'V')
        return pasteSelection();
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
        return deleteSelection();
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        clearSelection();
        return true;
    }
    return false;
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
    syncResolutionBox();
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
    std::array<int, Session::steps * Session::pitches> nextLengths {};
    const auto nextDrumLabels = session.isPatternDrums();
    const auto steps = session.editorStepCount();
    const auto beatsPerStep = 4.0 / static_cast<double>(steps);
    for (auto* note : session.pattern().getSequence().getNotes())
    {
        const auto row = lowestVisiblePitch + Session::pitches - 1 - note->getNoteNumber();
        const auto step = juce::roundToInt(note->getStartBeat().inBeats() / beatsPerStep);
        if (row >= 0 && row < Session::pitches && step >= 0 && step < steps)
        {
            next.set(static_cast<size_t>(row * Session::steps + step));
            nextLengths[static_cast<size_t>(row * Session::steps + step)] =
                std::max(1, static_cast<int>(std::ceil(note->getLengthBeats().inBeats() / beatsPerStep)));
        }
    }
    const auto changed = next ^ notes;
    const auto lengthsChanged = nextLengths != noteLengths;
    const auto stepCountChanged = steps != visibleStepCount;
    visibleStepCount = steps;
    syncHorizontalScroll();
    notes = next;
    noteLengths = nextLengths;
    selectedNotes &= notes;
    if (stepCountChanged || lengthsChanged || showingDrumLabels != nextDrumLabels)
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
    auto localBeat = std::fmod(editBeat - clipStartBeat + offsetBeat, 4.0);
    if (localBeat < 0.0)
        localBeat += 4.0;
    const auto step = localBeat / 4.0 * session.editorStepCount();
    if (step < stepScroll || step > stepScroll + visibleStepSpan())
        return -1.0f;
    return static_cast<float>(labelWidth + (step - stepScroll) * cellWidth());
}

void StepGrid::syncResolutionBox()
{
    const juce::ScopedValueSetter<bool> scope(updatingResolutionBox, true);
    resolutionBox.setSelectedId(session.editorStepCount(), juce::dontSendNotification);
}

void StepGrid::syncHorizontalScroll()
{
    const auto steps = session.editorStepCount();
    stepZoom = std::clamp(static_cast<double>(steps) / static_cast<double>(Session::defaultSteps), 1.0, 4.0);
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
    resolutionBox.setBounds(std::max(0, getWidth() - 74), 3, 66, 20);
    syncHorizontalScroll();
    horizontalScroll.setBounds(static_cast<int>(labelWidth), getHeight() - static_cast<int>(scrollHeight),
                               std::max(1, static_cast<int>(gridRight() - labelWidth)), static_cast<int>(scrollHeight));
    updatePlayhead();
    repaint();
}
}
