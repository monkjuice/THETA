#include "Arrangement.h"
#include "Playhead.h"
#include <set>

namespace theta
{
struct Arrangement::Waveform final : juce::ChangeListener
{
    Waveform(Arrangement& a, const juce::File& file)
        : owner(a), thumbnail(512, a.formats, a.thumbnailCache)
    {
        thumbnail.addChangeListener(this);
        readable = thumbnail.setSource(new juce::FileInputSource(file));
    }
    ~Waveform() override { thumbnail.removeChangeListener(this); }
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        for (const auto& clip : owner.clips)
            if (clip.waveform == this)
                owner.repaint(owner.bounds(clip).getIntersection(owner.lane(clip.track)).getSmallestIntegerContainer());
    }
    Arrangement& owner;
    juce::AudioThumbnail thumbnail;
    bool readable = false;
};

Arrangement::Arrangement(Session& s) : session(s), vblank(this, [this] { updatePlayhead(); })
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setTitle("Arrangement");
    formats.registerBasicFormats();
    session.addChangeListener(this);
    session.listeners.add(this);
    scroll.addListener(this);
    snap.setClickingTogglesState(true);
    snap.setToggleState(true, juce::dontSendNotification);
    fitButton.onClick = [this] { fit(); };
    zoomIn.onClick = [this] { zoom(0.5, viewStart + viewSpan * 0.5); };
    zoomOut.onClick = [this] { zoom(2.0, viewStart + viewSpan * 0.5); };
    splitButton.onClick = [this] { splitSelectedAtPlayhead(); };
    duplicateButton.onClick = [this] { duplicateSelected(); };
    snapSize.addItem("1/16", 1);
    snapSize.addItem("1/8", 2);
    snapSize.addItem("1/4", 3);
    snapSize.addItem("1 Bar", 4);
    snapSize.setSelectedId(1, juce::dontSendNotification);
    snapSize.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff262c32));
    snapSize.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff46515a));
    for (auto* control : std::initializer_list<juce::Component*>{&fitButton, &zoomIn, &zoomOut, &splitButton, &duplicateButton, &snap, &scroll})
        addAndMakeVisible(control);
    addAndMakeVisible(snapSize);
    for (int i = 0; i < 2; ++i)
    {
        mute[i].setButtonText("M");
        solo[i].setButtonText("S");
        mute[i].setTooltip("Mute track");
        solo[i].setTooltip("Solo track");
        mute[i].onClick = [this, i] { session.toggleTrackMute(i); };
        solo[i].onClick = [this, i] { session.toggleTrackSolo(i); };
        mute[i].setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff97634c));
        solo[i].setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff657440));
        addAndMakeVisible(mute[i]);
        addAndMakeVisible(solo[i]);
    }
    sync();
}

Arrangement::~Arrangement()
{
    session.removeChangeListener(this);
    session.listeners.remove(this);
    scroll.removeListener(this);
}

juce::Rectangle<float> Arrangement::lane(int track) const
{
    const auto height = (getHeight() - lanesTop - 18.0f) * 0.5f;
    return {headerWidth, lanesTop + track * height, std::max(1.0f, getWidth() - headerWidth), height};
}

float Arrangement::xFor(double seconds) const
{
    return headerWidth + static_cast<float>((seconds - viewStart) / viewSpan * lane(0).getWidth());
}

double Arrangement::timeAt(float x) const
{
    return viewStart + (x - headerWidth) / lane(0).getWidth() * viewSpan;
}

juce::Rectangle<float> Arrangement::bounds(const ClipView& clip) const
{
    const auto p = dragging && clip.id == selected ? preview : clip.position;
    return {xFor(p.start), lane(clip.track).getY() + 5.0f,
            std::max(1.0f, xFor(p.end) - xFor(p.start)), lane(clip.track).getHeight() - 10.0f};
}

void Arrangement::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1d2228));
    g.setFont(juce::FontOptions(12.0f));
    g.setColour(juce::Colour(0xffbbc4cc));
    g.drawText("ARRANGEMENT", 10, 0, 138, 30, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff8a969f));
    g.drawText("Drag audio to move / edges to trim / split / duplicate", 570, 0, getWidth() - 580, 30, juce::Justification::centredLeft);
    for (int track = 0; track < 2; ++track)
    {
        const auto row = lane(track);
        g.setColour(juce::Colour(track == 0 ? 0xff242b31 : 0xff20272e));
        g.fillRect(row);
        if (track == selectedTrack)
        {
            g.setColour(juce::Colour(0xff343f47));
            g.fillRect(row.withWidth(headerWidth));
            g.setColour(juce::Colour(0xffc6d58c));
            g.fillRect(row.withWidth(3.0f));
        }
        g.setColour(juce::Colour(0xffc4cbd1));
        g.drawText(track == 0 ? "01  Pattern synth" : "02  Audio 1", 10, static_cast<int>(row.getY()) + 8, 130, 22, juce::Justification::centredLeft);
    }

    const auto beatSeconds = 60.0 / session.tempo();
    auto rulerStep = beatSeconds;
    while (rulerStep / viewSpan * lane(0).getWidth() < 64.0) rulerStep *= 2.0;
    for (auto time = std::ceil(viewStart / rulerStep) * rulerStep; time <= viewStart + viewSpan; time += rulerStep)
    {
        const auto x = xFor(time);
        g.setColour(juce::Colour(0xff35404a));
        g.drawVerticalLine(static_cast<int>(x), rulerTop, getHeight() - 18.0f);
        const int beat = juce::roundToInt(time / beatSeconds);
        g.setColour(juce::Colour(0xff8c99a4));
        g.drawText(juce::String(beat / 4 + 1) + "." + juce::String(beat % 4 + 1),
                   static_cast<int>(x) + 4, static_cast<int>(rulerTop), 64, 24, juce::Justification::centredLeft);
    }
    const auto dirty = g.getClipBounds().toFloat();
    bool hasAudio = false;
    for (const auto& clip : clips)
    {
        hasAudio |= clip.track == 1;
        const auto box = bounds(clip);
        const auto visible = box.getIntersection(lane(clip.track));
        if (visible.isEmpty() || !visible.intersects(dirty)) continue;
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(lane(clip.track).getSmallestIntegerContainer());
        g.setColour(juce::Colour(clip.track == 0 ? 0xff414c34 : 0xff284b59));
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(juce::Colour(clip.id == selected ? 0xffdce9b1 : 0xff617985));
        g.drawRoundedRectangle(box.reduced(0.5f), 3.0f, clip.id == selected ? 2.0f : 1.0f);
        g.setColour(juce::Colour(0xffe0e7ec));
        g.drawText(clip.name, visible.reduced(6.0f, 0).withHeight(23.0f), juce::Justification::centredLeft, true);
        if (clip.waveform)
        {
            const auto position = dragging && clip.id == selected ? preview : clip.position;
            auto waveArea = visible.withTop(box.getY() + 26.0f).reduced(0, 5).getSmallestIntegerContainer();
            if (clip.waveform->thumbnail.getTotalLength() > 0.0)
            {
                const auto start = (position.offset + std::max(0.0, timeAt(visible.getX()) - position.start)) * clip.speed;
                const auto end = start + visible.getWidth() / lane(0).getWidth() * viewSpan * clip.speed;
                g.setColour(juce::Colour(0xff8cc5d2));
                clip.waveform->thumbnail.drawChannels(g, waveArea, start, end, 0.85f);
            }
            else
            {
                g.setColour(juce::Colour(0xffa1b1b9));
                g.drawText(clip.waveform->readable ? "Reading waveform..." : "Missing or unreadable audio",
                           waveArea.reduced(6, 0), juce::Justification::centredLeft, true);
            }
        }
        else
        {
            const auto noteArea = box.withTop(box.getY() + 28.0f).reduced(6.0f, 5.0f);
            g.setColour(juce::Colour(0x553f4837));
            for (int step = 1; step < Session::steps; ++step)
            {
                const auto x = xFor(clip.position.start + step * (clip.position.end - clip.position.start) / Session::steps);
                if (x > noteArea.getX() && x < noteArea.getRight())
                    g.drawVerticalLine(static_cast<int>(x), noteArea.getY(), noteArea.getBottom());
            }
            for (const auto& note : clip.midiNotes)
            {
                const auto x1 = xFor(note.start);
                const auto x2 = xFor(note.end);
                const auto w = std::max(3.0f, x2 - x1);
                const auto pitchScale = static_cast<float>(note.pitch - Session::lowestNote)
                    / static_cast<float>(std::max(1, Session::pitches - 1));
                const auto h = std::max(4.0f, noteArea.getHeight() / Session::pitches - 1.0f);
                const auto y = noteArea.getBottom() - h - pitchScale * (noteArea.getHeight() - h);
                const juce::Rectangle<float> noteBox {x1, y, w, h};
                if (!noteBox.intersects(visible)) continue;
                g.setColour(juce::Colour(0xffc6d58c));
                g.fillRoundedRectangle(noteBox, 2.0f);
                g.setColour(juce::Colour(0xffe8f1bd));
                g.drawRoundedRectangle(noteBox.reduced(0.5f), 2.0f, 1.0f);
            }
            if (clip.midiNotes.empty())
            {
                g.setColour(juce::Colour(0xff9daa7e));
                g.drawText("Edit notes below", noteArea, juce::Justification::centredLeft, true);
            }
        }
    }
    if (!hasAudio)
    {
        g.setColour(juce::Colour(0xff75828e));
        g.drawText("Add audio to see its waveform here", lane(1).reduced(16, 0), juce::Justification::centredLeft);
    }
    if (playhead >= headerWidth)
    {
        g.setColour(playheadColour);
        g.fillRect(playhead, rulerTop, 2.0f, getHeight() - rulerTop - 18.0f);
    }
}

void Arrangement::resized()
{
    fitButton.setBounds(152, 3, 44, 26);
    zoomOut.setBounds(204, 3, 32, 26);
    zoomIn.setBounds(240, 3, 32, 26);
    splitButton.setBounds(282, 3, 58, 26);
    duplicateButton.setBounds(348, 3, 52, 26);
    snap.setBounds(410, 3, 58, 26);
    snapSize.setBounds(474, 3, 74, 26);
    for (int i = 0; i < 2; ++i)
    {
        mute[i].setBounds(12, static_cast<int>(lane(i).getY()) + 38, 42, 26);
        solo[i].setBounds(62, static_cast<int>(lane(i).getY()) + 38, 42, 26);
    }
    scroll.setBounds(static_cast<int>(headerWidth), getHeight() - 14, getWidth() - static_cast<int>(headerWidth), 14);
    updateScroll();
    updatePlayhead();
}

void Arrangement::sync()
{
    clips.clear();
    std::set<juce::String> usedFiles;
    const auto tracks = te::getAudioTracks(*session.edit);
    songEnd = 0.0;
    for (int track = 0; track < 2; ++track)
    {
        mute[track].setToggleState(tracks[track]->isMuted(false), juce::dontSendNotification);
        solo[track].setToggleState(tracks[track]->isSolo(false), juce::dontSendNotification);
        for (auto* clip : tracks[track]->getClips())
        {
            const auto p = clip->getPosition();
            ClipView view {clip->itemID, clip->getName(), {p.time.getStart().inSeconds(), p.time.getEnd().inSeconds(), p.offset.inSeconds()}, nullptr, {}, clip->getSpeedRatio(), track};
            if (dynamic_cast<te::WaveAudioClip*>(clip))
            {
                const auto file = clip->getSourceFileReference().getFile();
                const auto key = file.getFullPathName();
                usedFiles.insert(key);
                auto& waveform = waveforms[key];
                if (!waveform) waveform = std::make_unique<Waveform>(*this, file);
                view.waveform = waveform.get();
            }
            else if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
            {
                for (auto* note : midi->getSequence().getNotes())
                {
                    const auto noteStart = session.edit->tempoSequence.toTime(note->getStartBeat()).inSeconds();
                    const auto noteEnd = session.edit->tempoSequence.toTime(note->getStartBeat() + note->getLengthBeats()).inSeconds();
                    view.midiNotes.push_back({view.position.start + noteStart, view.position.start + noteEnd, note->getNoteNumber()});
                }
            }
            songEnd = std::max(songEnd, view.position.end);
            clips.push_back(view);
        }
    }
    std::erase_if(waveforms, [&usedFiles](const auto& item) { return !usedFiles.contains(item.first); });
    updateScroll();
    repaint();
}

void Arrangement::updateScroll()
{
    const auto total = std::max({8.0, songEnd + 60.0 / session.tempo() * 4.0, viewSpan});
    viewStart = std::clamp(viewStart, 0.0, std::max(0.0, total - viewSpan));
    scroll.setRangeLimits(0.0, total, juce::dontSendNotification);
    scroll.setCurrentRange(viewStart, viewSpan, juce::dontSendNotification);
}

void Arrangement::fit()
{
    cancelDrag();
    viewStart = 0.0;
    viewSpan = std::max(4.0, songEnd * 1.1);
    updateScroll();
    updatePlayhead();
    repaint();
}

void Arrangement::zoom(double factor, double anchor)
{
    if (dragging) return;
    const auto fraction = (anchor - viewStart) / viewSpan;
    viewSpan = std::clamp(viewSpan * factor, 0.25, std::max(60.0, songEnd * 2.0));
    viewStart = std::max(0.0, anchor - viewSpan * fraction);
    updateScroll();
    updatePlayhead();
    repaint();
}

void Arrangement::scrollBarMoved(juce::ScrollBar*, double start)
{
    cancelDrag();
    viewStart = start;
    updatePlayhead();
    repaint();
}

double Arrangement::snapped(double seconds, bool bypass) const
{
    const auto unit = snapUnitSeconds();
    return snap.getToggleState() && !bypass ? std::round(seconds / unit) * unit : seconds;
}

int Arrangement::hit(juce::Point<float> point) const
{
    if (point.x < headerWidth) return -1;
    for (int i = static_cast<int>(clips.size()); --i >= 0;)
        if (bounds(clips[static_cast<size_t>(i)]).contains(point)) return i;
    return -1;
}

void Arrangement::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    if (!event.mods.isLeftButtonDown()) return;
    for (int track = 0; track < 2; ++track)
        if (lane(track).withX(0.0f).contains(event.position))
        {
            selectTrack(track);
            break;
        }
    if (event.y >= rulerTop && event.y < lanesTop && event.x >= headerWidth)
    {
        session.edit->getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(std::max(0.0, timeAt(event.position.x))));
        updatePlayhead();
        return;
    }
    const auto index = hit(event.position);
    if (index < 0) { selected = {}; repaint(); return; }
    const auto& clip = clips[static_cast<size_t>(index)];
    selected = clip.id;
    selectTrack(clip.track);
    repaint();
    if (clip.track == 0) return;
    original = preview = clip.position;
    sourceDuration = clip.waveform && clip.waveform->thumbnail.getTotalLength() > 0.0
        ? clip.waveform->thumbnail.getTotalLength() / clip.speed : original.offset + original.end - original.start;
    const auto box = bounds(clip);
    const auto handleWidth = std::min(7.0f, box.getWidth() * 0.25f);
    gesture = event.position.x - box.getX() < handleWidth ? ClipGesture::trimLeft
        : box.getRight() - event.position.x < handleWidth ? ClipGesture::trimRight : ClipGesture::move;
    dragTime = timeAt(event.position.x);
    dragging = true;
}

void Arrangement::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging) return;
    const auto old = preview;
    const auto anchor = gesture == ClipGesture::trimRight ? original.end : original.start;
    preview = previewClipEdit(original, gesture, snapped(anchor + timeAt(event.position.x) - dragTime, event.mods.isAltDown()), sourceDuration);
    const auto row = lane(1);
    const auto invalidate = [this, row](ClipGeometry p)
    {
        repaint(juce::Rectangle<float>(xFor(p.start) - 2.0f, row.getY(), xFor(p.end) - xFor(p.start) + 4.0f, row.getHeight())
            .getIntersection(row).getSmallestIntegerContainer());
    };
    invalidate(old);
    invalidate(preview);
}

void Arrangement::mouseUp(const juce::MouseEvent& event)
{
    if (!dragging) return;
    if (event.getDistanceFromDragStart() >= 3)
    {
        mouseDrag(event);
        dragging = false;
        const auto result = session.editAudioClip(selected, preview, gesture);
        if (result.failed() && status) status(result.getErrorMessage());
    }
    cancelDrag();
    repaint();
}

void Arrangement::mouseMove(const juce::MouseEvent& event)
{
    const auto index = hit(event.position);
    auto pointerStyle = juce::MouseCursor::NormalCursor;
    if (index >= 0 && clips[static_cast<size_t>(index)].track == 1)
    {
        const auto box = bounds(clips[static_cast<size_t>(index)]);
        const auto handle = std::min(7.0f, box.getWidth() * 0.25f);
        pointerStyle = event.position.x - box.getX() < handle || box.getRight() - event.position.x < handle
            ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
    }
    setMouseCursor(pointerStyle);
}

void Arrangement::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (dragging) return;
    if (event.mods.isCommandDown()) zoom(std::exp(-wheel.deltaY * 2.0), timeAt(event.position.x));
    else
    {
        viewStart -= (std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY) * viewSpan * 0.3;
        updateScroll();
        updatePlayhead();
        repaint();
    }
}

bool Arrangement::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
    {
        nudgeSelected(key.getKeyCode() == juce::KeyPress::rightKey ? 1 : -1, key.getModifiers().isShiftDown());
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'E')
    {
        splitSelectedAtPlayhead();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'D')
    {
        duplicateSelected();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::escapeKey && dragging)
    {
        cancelDrag();
        repaint();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        cancelDrag();
        session.deleteAudioClip(selected);
        return true;
    }
    return false;
}

void Arrangement::cancelDrag() { dragging = false; }

void Arrangement::selectTrack(int track)
{
    track = juce::jlimit(0, 1, track);
    if (selectedTrack == track) return;
    selectedTrack = track;
    if (trackSelected) trackSelected(track);
    repaint();
}

void Arrangement::splitSelectedAtPlayhead()
{
    cancelDrag();
    const auto result = session.splitAudioClip(selected, playheadTime(session.edit->getTransport()));
    if (result.failed() && status) status(result.getErrorMessage());
}

void Arrangement::duplicateSelected()
{
    cancelDrag();
    const auto result = session.duplicateAudioClip(selected);
    if (result.failed() && status) status(result.getErrorMessage());
}

void Arrangement::nudgeSelected(int direction, bool byBar)
{
    cancelDrag();
    auto* clip = session.findAudioClip(selected);
    if (!clip) return;
    const auto old = clip->getPosition();
    const auto delta = (byBar ? 60.0 / session.tempo() * 4.0 : snapUnitSeconds()) * (direction < 0 ? -1.0 : 1.0);
    const auto length = old.time.getLength().inSeconds();
    const auto start = std::max(0.0, old.time.getStart().inSeconds() + delta);
    const auto result = session.editAudioClip(selected, {start, start + length, old.offset.inSeconds()}, ClipGesture::move);
    if (result.failed() && status) status(result.getErrorMessage());
}

double Arrangement::snapUnitSeconds() const
{
    const auto beatSeconds = 60.0 / session.tempo();
    switch (snapSize.getSelectedId())
    {
        case 2: return beatSeconds * 0.5;
        case 3: return beatSeconds;
        case 4: return beatSeconds * 4.0;
        default: return beatSeconds * 0.25;
    }
}

void Arrangement::changeListenerCallback(juce::ChangeBroadcaster*) { cancelDrag(); sync(); }
void Arrangement::editWillChange() { cancelDrag(); clips.clear(); waveforms.clear(); selected = {}; }
void Arrangement::editDidChange() { sync(); fit(); }

void Arrangement::updatePlayhead()
{
    float next = -1.0f;
    if (isShowing())
    {
        const auto x = xFor(playheadTime(session.edit->getTransport()));
        if (x >= headerWidth && x < getWidth()) next = x;
    }
    movePlayhead(*this, playhead, next,
                 getLocalBounds().withTrimmedTop(static_cast<int>(rulerTop)).withTrimmedBottom(18));
}
}
