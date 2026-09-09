#include "StepGrid.h"
#include "Playhead.h"

namespace theta
{
StepGrid::StepGrid(Session& s) : session(s), vblank(this, [this] { updatePlayhead(); })
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setTitle("Pattern notes");
    setDescription("One bar, sixteen steps, MIDI notes 48 to 59. Drag to draw or erase notes.");
    session.addChangeListener(this);
    changeListenerCallback(nullptr);
}

StepGrid::~StepGrid()
{
    if (drawing) session.endNoteGesture();
    session.removeChangeListener(this);
}

juce::Rectangle<float> StepGrid::cell(int step, int row) const
{
    const auto width = (getWidth() - labelWidth) / Session::steps;
    const auto height = (getHeight() - headerHeight) / Session::pitches;
    return {labelWidth + step * width, headerHeight + row * height, width, height};
}

void StepGrid::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1d2228));
    g.setFont(juce::FontOptions(12.0f));
    const auto dirty = g.getClipBounds().toFloat();
    for (int step = 0; step < Session::steps; ++step)
    {
        g.setColour(juce::Colour(step % 4 == 0 ? 0xffd4dacd : 0xff78818a));
        g.drawText(juce::String(step + 1), cell(step, 0).withY(0).withHeight(headerHeight), juce::Justification::centred);
    }
    for (int row = 0; row < Session::pitches; ++row)
    {
        const auto pitch = Session::lowestNote + Session::pitches - 1 - row;
        const bool black = juce::MidiMessage::isMidiNoteBlack(pitch);
        auto key = cell(0, row).withX(0).withWidth(labelWidth - 4);
        g.setColour(juce::Colour(black ? 0xff15191e : 0xff30373e));
        g.fillRect(key.reduced(0, 1));
        g.setColour(juce::Colour(0xffbac2ca));
        g.drawText(juce::MidiMessage::getMidiNoteName(pitch, true, true, 4), key, juce::Justification::centred);
        for (int step = 0; step < Session::steps; ++step)
        {
            const auto bounds = cell(step, row).reduced(2.0f, 2.0f);
            if (!dirty.intersects(bounds)) continue;
            const bool active = notes.test(static_cast<size_t>(row * Session::steps + step));
            g.setColour(juce::Colour(active ? 0xffc6d58c : (step / 4 % 2 == 0 ? 0xff2a3139 : 0xff252c33)));
            g.fillRoundedRectangle(bounds, 3.0f);
        }
    }
    if (playhead >= 0)
    {
        g.setColour(juce::Colour(0xfff0f4de));
        g.fillRect(playhead, static_cast<int>(headerHeight), 2, getHeight() - static_cast<int>(headerHeight));
    }
}

int StepGrid::hit(juce::Point<float> point) const
{
    if (point.x < labelWidth || point.y < headerHeight || point.x >= getWidth() || point.y >= getHeight())
        return -1;
    const auto step = static_cast<int>((point.x - labelWidth) / (getWidth() - labelWidth) * Session::steps);
    const auto row = static_cast<int>((point.y - headerHeight) / (getHeight() - headerHeight) * Session::pitches);
    return row * Session::steps + step;
}

void StepGrid::mouseDown(const juce::MouseEvent& event)
{
    const auto index = hit(event.position);
    if (index < 0) return;
    grabKeyboardFocus();
    drawing = true;
    adding = !event.mods.isRightButtonDown() && !notes.test(static_cast<size_t>(index));
    visited.reset();
    lastHit = index;
    session.beginNoteGesture();
    apply(index);
}

void StepGrid::apply(int index)
{
    if (index < 0 || visited.test(static_cast<size_t>(index))) return;
    visited.set(static_cast<size_t>(index));
    session.setNote(index % Session::steps,
                    Session::lowestNote + Session::pitches - 1 - index / Session::steps, adding);
}

void StepGrid::mouseDrag(const juce::MouseEvent& event)
{
    if (!drawing) return;
    const auto index = hit(event.position);
    // Fill skipped cells for fast horizontal strokes, without toggling a cell
    // twice when the pointer retraces its path.
    if (index >= 0 && lastHit >= 0 && index / Session::steps == lastHit / Session::steps)
        for (int i = std::min(index, lastHit); i <= std::max(index, lastHit); ++i) apply(i);
    else apply(index);
    lastHit = index;
}

void StepGrid::mouseUp(const juce::MouseEvent&)
{
    if (drawing) session.endNoteGesture();
    drawing = false;
    lastHit = -1;
}

void StepGrid::changeListenerCallback(juce::ChangeBroadcaster*)
{
    std::bitset<Session::steps * Session::pitches> next;
    for (auto* note : session.pattern().getSequence().getNotes())
    {
        const auto row = Session::lowestNote + Session::pitches - 1 - note->getNoteNumber();
        const auto step = juce::roundToInt(note->getStartBeat().inBeats() * 4.0);
        if (row >= 0 && row < Session::pitches && step >= 0 && step < Session::steps)
            next.set(static_cast<size_t>(row * Session::steps + step));
    }
    const auto changed = next ^ notes;
    notes = next;
    for (int i = 0; i < Session::steps * Session::pitches; ++i)
        if (changed.test(static_cast<size_t>(i))) repaint(cell(i % Session::steps, i / Session::steps).getSmallestIntegerContainer());
}

void StepGrid::updatePlayhead()
{
    int next = -1;
    auto& transport = session.edit->getTransport();
    if (isShowing() && transport.isPlaying())
    {
        const auto beat = session.edit->tempoSequence.toBeats(transport.getPosition()).inBeats();
        if (beat >= 0.0 && beat < 4.0)
            next = static_cast<int>(labelWidth + beat / 4.0 * (getWidth() - labelWidth));
    }
    movePlayhead(*this, playhead, next,
                 getLocalBounds().withTrimmedTop(static_cast<int>(headerHeight)));
}

void StepGrid::resized() { updatePlayhead(); repaint(); }
}
