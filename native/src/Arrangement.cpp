#include "Arrangement.h"
#include "Playhead.h"
#include <optional>
#include <set>

namespace theta
{
namespace
{
bool isSupportedAudioFile(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".aiff" || extension == ".aif"
        || extension == ".flac" || extension == ".ogg" || extension == ".mp3";
}

std::optional<Session::PatternPreset> patternPresetFromId(const juce::String& id)
{
    if (id == "WarmPulse")  return Session::PatternPreset::WarmPulse;
    if (id == "AcidSteps")  return Session::PatternPreset::AcidSteps;
    if (id == "HouseKit")   return Session::PatternPreset::HouseKit;
    if (id == "BreakKit")   return Session::PatternPreset::BreakKit;
    if (id == "MinimalKit") return Session::PatternPreset::MinimalKit;
    return std::nullopt;
}

std::optional<Session::AudioEffect> audioEffectFromId(const juce::String& id)
{
    if (id == "Equaliser")  return Session::AudioEffect::Equaliser;
    if (id == "Reverb")     return Session::AudioEffect::Reverb;
    if (id == "Delay")      return Session::AudioEffect::Delay;
    if (id == "Compressor") return Session::AudioEffect::Compressor;
    return std::nullopt;
}

juce::String browserDropKind(const juce::String& description)
{
    if (!description.startsWith("theta-browser:")) return {};
    return description.fromFirstOccurrenceOf("theta-browser:", false, false)
        .upToFirstOccurrenceOf(":", false, false);
}

juce::String browserDropId(const juce::String& description)
{
    return description.fromLastOccurrenceOf(":", false, false);
}
}

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
    trackScrollBar.addListener(this);
    fitButton.setButtonText(L"\u26f6");
    zoomOut.setButtonText(L"\u2212");
    zoomIn.setButtonText(L"+");
    splitButton.setButtonText(L"\u2702");
    duplicateButton.setButtonText(L"\u2398");
    addTrack.setButtonText(L"+");
    removeTrack.setButtonText(L"\u2212");
    snap.setButtonText(L"\u25c7");
    fitButton.setTooltip("Fit arrangement");
    zoomOut.setTooltip("Zoom out");
    zoomIn.setTooltip("Zoom in");
    splitButton.setTooltip("Split selected clip");
    duplicateButton.setTooltip("Duplicate selected clip");
    addTrack.setTooltip("Add track");
    removeTrack.setTooltip("Remove selected track");
    snap.setTooltip("Toggle clip snap");
    snap.setClickingTogglesState(true);
    snap.setToggleState(true, juce::dontSendNotification);
    fitButton.onClick = [this] { fit(); };
    zoomIn.onClick = [this] { zoom(0.5, viewStart + viewSpan * 0.5); };
    zoomOut.onClick = [this] { zoom(2.0, viewStart + viewSpan * 0.5); };
    splitButton.onClick = [this] { splitSelectedAtPlayhead(); };
    duplicateButton.onClick = [this] { duplicateSelected(); };
    addTrack.onClick = [this]
    {
        const auto result = session.addAudioTrack();
        if (result.failed() && status) status(result.getErrorMessage());
    };
    removeTrack.onClick = [this]
    {
        const auto result = session.removeAudioTrack(selectedTrack);
        if (result.failed() && status) status(result.getErrorMessage());
    };
    snapSize.addItem("1/16", 1);
    snapSize.addItem("1/8", 2);
    snapSize.addItem("1/4", 3);
    snapSize.addItem("1 Bar", 4);
    snapSize.setSelectedId(1, juce::dontSendNotification);
    snapSize.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff262c32));
    snapSize.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff46515a));
    for (auto* control : std::initializer_list<juce::Component*>{&fitButton, &zoomIn, &zoomOut, &splitButton, &duplicateButton, &addTrack, &removeTrack, &snap, &scroll, &trackScrollBar})
        addAndMakeVisible(control);
    addAndMakeVisible(snapSize);
    sync();
}

Arrangement::~Arrangement()
{
    session.removeChangeListener(this);
    session.listeners.remove(this);
    scroll.removeListener(this);
    trackScrollBar.removeListener(this);
}

float Arrangement::laneContentHeight() const
{
    return std::max(1.0f, getHeight() - lanesTop - 18.0f);
}

float Arrangement::laneHeight() const
{
    return std::max(48.0f, std::min(82.0f, laneContentHeight() / 2.0f));
}

juce::Rectangle<float> Arrangement::lane(int track) const
{
    const auto height = laneHeight();
    return {headerWidth, lanesTop + track * height - static_cast<float>(trackScroll),
            std::max(1.0f, getWidth() - headerWidth - 14.0f), height};
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
    g.drawText("Drop browser items or files / drag clips to move / trim edges",
               740, 0, getWidth() - 750, 30, juce::Justification::centredLeft);
    for (int track = 0; track < session.trackCount(); ++track)
    {
        const auto row = lane(track);
        if (row.getBottom() < lanesTop || row.getY() > getHeight() - 18.0f) continue;
        g.setColour(juce::Colour(track == 0 ? 0xff242b31 : 0xff20272e));
        g.fillRect(row.withX(0.0f).withWidth(static_cast<float>(getWidth()) - 14.0f));
        if (track == selectedTrack)
        {
            g.setColour(juce::Colour(0xff343f47));
            g.fillRect(row.withWidth(headerWidth));
            g.setColour(juce::Colour(0xffc6d58c));
            g.fillRect(row.withWidth(3.0f));
        }
        g.setColour(juce::Colour(0xffc4cbd1));
        g.drawText(juce::String(track + 1).paddedLeft('0', 2) + "  " + session.trackName(track),
                   10, static_cast<int>(row.getY()) + 8, 130, 22, juce::Justification::centredLeft);
    }

    const auto beatSeconds = 60.0 / session.tempo();
    auto rulerStep = beatSeconds;
    while (rulerStep / viewSpan * lane(0).getWidth() < 64.0) rulerStep *= 2.0;
    for (auto time = std::ceil(viewStart / rulerStep) * rulerStep; time <= viewStart + viewSpan; time += rulerStep)
    {
        const auto x = xFor(time);
        g.setColour(juce::Colour(0xff35404a));
        g.drawVerticalLine(static_cast<int>(x), static_cast<int>(lanesTop), getHeight() - 18.0f);
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
        const auto visible = box.getIntersection(lane(clip.track))
            .getIntersection({0.0f, lanesTop, static_cast<float>(getWidth() - 14), laneContentHeight()});
        if (visible.isEmpty() || !visible.intersects(dirty)) continue;
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(juce::Rectangle<int>(0, static_cast<int>(lanesTop), getWidth() - 14,
                                                std::max(1, getHeight() - static_cast<int>(lanesTop) - 18)));
        g.setColour(juce::Colour(clip.track == 0 ? 0xff414c34 : 0xff284b59));
        g.fillRect(box);
        g.setColour(juce::Colour(clip.id == selected ? 0xffdce9b1 : 0xff617985));
        g.drawRect(box.reduced(0.5f), clip.id == selected ? 2.0f : 1.0f);
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
                g.fillRect(noteBox);
                g.setColour(juce::Colour(0xffe8f1bd));
                g.drawRect(noteBox.reduced(0.5f), 1.0f);
            }
            if (clip.midiNotes.empty())
            {
                g.setColour(juce::Colour(0xff9daa7e));
                g.drawText("Edit notes below", noteArea, juce::Justification::centredLeft, true);
            }
        }
    }
    if (!hasAudio && session.trackCount() > 1)
    {
        g.setColour(juce::Colour(0xff75828e));
        g.drawText("Drop audio here, or use Add audio", lane(1).reduced(16, 0), juce::Justification::centredLeft);
    }
    if (playhead >= headerWidth)
    {
        g.setColour(playheadColour);
        g.fillRect(playhead, rulerTop, 2.0f, getHeight() - rulerTop - 18.0f);
    }
}

void Arrangement::resized()
{
    fitButton.setBounds(152, 3, 32, 26);
    zoomOut.setBounds(190, 3, 32, 26);
    zoomIn.setBounds(226, 3, 32, 26);
    splitButton.setBounds(268, 3, 34, 26);
    duplicateButton.setBounds(308, 3, 34, 26);
    addTrack.setBounds(352, 3, 34, 26);
    removeTrack.setBounds(392, 3, 34, 26);
    snap.setBounds(436, 3, 34, 26);
    snapSize.setBounds(476, 3, 74, 26);
    syncTrackControls();
    for (int i = 0; i < session.trackCount(); ++i)
    {
        const auto row = lane(i);
        const auto visible = row.getBottom() >= lanesTop && row.getY() <= getHeight() - 18.0f;
        mute[static_cast<size_t>(i)]->setVisible(visible);
        solo[static_cast<size_t>(i)]->setVisible(visible);
        mute[static_cast<size_t>(i)]->setBounds(12, static_cast<int>(row.getY()) + 38, 42, 26);
        solo[static_cast<size_t>(i)]->setBounds(62, static_cast<int>(row.getY()) + 38, 42, 26);
    }
    scroll.setBounds(static_cast<int>(headerWidth), getHeight() - 14, getWidth() - static_cast<int>(headerWidth) - 14, 14);
    trackScrollBar.setBounds(getWidth() - 12, static_cast<int>(lanesTop), 12, getHeight() - static_cast<int>(lanesTop) - 18);
    updateScroll();
    updatePlayhead();
}

void Arrangement::sync()
{
    clips.clear();
    std::set<juce::String> usedFiles;
    const auto tracks = te::getAudioTracks(*session.edit);
    syncTrackControls();
    selectedTrack = juce::jlimit(0, std::max(0, session.trackCount() - 1), selectedTrack);
    songEnd = 0.0;
    for (int track = 0; track < tracks.size(); ++track)
    {
        mute[static_cast<size_t>(track)]->setToggleState(tracks[track]->isMuted(false), juce::dontSendNotification);
        solo[static_cast<size_t>(track)]->setToggleState(tracks[track]->isSolo(false), juce::dontSendNotification);
        for (auto* clip : tracks[track]->getClips())
        {
            const auto p = clip->getPosition();
            ClipView view {clip->itemID, clip->getName(), {p.time.getStart().inSeconds(), p.time.getEnd().inSeconds(), p.offset.inSeconds()}, nullptr, {}, clip->getSpeedRatio(),
                           p.offset.inSeconds() + p.time.getLength().inSeconds(), track};
            if (auto* audio = dynamic_cast<te::WaveAudioClip*>(clip))
            {
                const auto file = clip->getSourceFileReference().getFile();
                const auto key = file.getFullPathName();
                usedFiles.insert(key);
                auto& waveform = waveforms[key];
                if (!waveform) waveform = std::make_unique<Waveform>(*this, file);
                view.waveform = waveform.get();
                view.sourceDuration = audio->getSourceLength().inSeconds() / std::max(0.0001, view.speed);
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

void Arrangement::syncTrackControls()
{
    const auto count = session.trackCount();
    while (static_cast<int>(mute.size()) < count)
    {
        const auto track = static_cast<int>(mute.size());
        auto muteButton = std::make_unique<juce::TextButton>("M");
        auto soloButton = std::make_unique<juce::TextButton>("S");
        muteButton->setTooltip("Mute track");
        soloButton->setTooltip("Solo track");
        muteButton->onClick = [this, track] { session.toggleTrackMute(track); };
        soloButton->onClick = [this, track] { session.toggleTrackSolo(track); };
        muteButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff97634c));
        soloButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff657440));
        addAndMakeVisible(*muteButton);
        addAndMakeVisible(*soloButton);
        mute.push_back(std::move(muteButton));
        solo.push_back(std::move(soloButton));
    }
    while (static_cast<int>(mute.size()) > count)
    {
        mute.pop_back();
        solo.pop_back();
    }
    removeTrack.setEnabled(selectedTrack > 0 && count > 2);
}

void Arrangement::updateScroll()
{
    const auto total = std::max({8.0, songEnd + 60.0 / session.tempo() * 4.0, viewSpan});
    viewStart = std::clamp(viewStart, 0.0, std::max(0.0, total - viewSpan));
    scroll.setRangeLimits(0.0, total, juce::dontSendNotification);
    scroll.setCurrentRange(viewStart, viewSpan, juce::dontSendNotification);
    const auto trackTotal = static_cast<double>(session.trackCount()) * laneHeight();
    const auto trackVisible = static_cast<double>(laneContentHeight());
    trackScroll = std::clamp(trackScroll, 0.0, std::max(0.0, trackTotal - trackVisible));
    trackScrollBar.setRangeLimits(0.0, std::max(trackVisible, trackTotal), juce::dontSendNotification);
    trackScrollBar.setCurrentRange(trackScroll, trackVisible, juce::dontSendNotification);
    trackScrollBar.setVisible(trackTotal > trackVisible + 1.0);
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

void Arrangement::scrollBarMoved(juce::ScrollBar* bar, double start)
{
    cancelDrag();
    if (bar == &trackScrollBar)
        trackScroll = start;
    else
        viewStart = start;
    updatePlayhead();
    resized();
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
    for (int track = 0; track < session.trackCount(); ++track)
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
    original = preview = clip.position;
    sourceDuration = clip.sourceDuration;
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
    const auto anchor = gesture == ClipGesture::trimRight ? original.end : original.start;
    preview = previewClipEdit(original, gesture, snapped(anchor + timeAt(event.position.x) - dragTime, event.mods.isAltDown()), sourceDuration);
    repaint(lane(selectedTrack).getSmallestIntegerContainer());
}

void Arrangement::mouseUp(const juce::MouseEvent& event)
{
    if (!dragging) return;
    if (event.getDistanceFromDragStart() >= 3)
    {
        mouseDrag(event);
        dragging = false;
        const auto result = session.editClip(selected, preview, gesture);
        if (result.failed() && status) status(result.getErrorMessage());
    }
    cancelDrag();
    repaint();
}

void Arrangement::mouseMove(const juce::MouseEvent& event)
{
    const auto index = hit(event.position);
    auto pointerStyle = juce::MouseCursor::NormalCursor;
    if (index >= 0)
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
    else if (std::abs(wheel.deltaY) > std::abs(wheel.deltaX)
             && static_cast<float>(session.trackCount()) * laneHeight() > laneContentHeight() + 1.0f)
    {
        trackScroll += -wheel.deltaY * laneHeight() * 1.5;
        updateScroll();
        resized();
        repaint();
    }
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
        session.deleteClip(selected);
        return true;
    }
    return false;
}

bool Arrangement::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isSupportedAudioFile(juce::File(path)))
            return true;
    return false;
}

void Arrangement::filesDropped(const juce::StringArray& files, int x, int y)
{
    auto targetTrack = trackAt(static_cast<float>(y));
    if (targetTrack < 0 && session.trackCount() > 0 && static_cast<float>(y) > lane(session.trackCount() - 1).getBottom())
    {
        const auto result = session.addAudioTrack();
        if (result.failed())
        {
            if (status) status(result.getErrorMessage());
            return;
        }
        targetTrack = session.trackCount() - 1;
        selectTrack(targetTrack);
    }
    if (targetTrack < 0)
    {
        if (status) status("Drop audio on the arrangement lanes.");
        return;
    }
    for (const auto& path : files)
    {
        const auto file = juce::File(path);
        if (!isSupportedAudioFile(file)) continue;
        const auto result = session.importAudioAt(file, std::max(1, targetTrack), snapped(std::max(0.0, timeAt(static_cast<float>(x))), false));
        if (result.failed() && status) status(result.getErrorMessage());
    }
    fit();
}

bool Arrangement::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    return details.description.toString().startsWith("theta-browser:");
}

void Arrangement::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    auto targetTrack = trackAt(static_cast<float>(details.localPosition.y));
    const auto description = details.description.toString();
    if (browserDropKind(description) == "effect"
        && targetTrack < 0
        && session.trackCount() > 0
        && static_cast<float>(details.localPosition.y) > lane(session.trackCount() - 1).getBottom())
    {
        const auto result = session.addAudioTrack();
        if (result.failed())
        {
            if (status) status(result.getErrorMessage());
            return;
        }
        targetTrack = session.trackCount() - 1;
    }

    const auto result = applyBrowserDrop(description, targetTrack,
                                         snapped(std::max(0.0, timeAt(static_cast<float>(details.localPosition.x))), false),
                                         true);
    if (result.failed() && status) status(result.getErrorMessage());
}

void Arrangement::cancelDrag() { dragging = false; }

void Arrangement::selectTrack(int track)
{
    track = juce::jlimit(0, std::max(0, session.trackCount() - 1), track);
    if (selectedTrack == track) return;
    selectedTrack = track;
    if (trackSelected) trackSelected(track);
    removeTrack.setEnabled(selectedTrack > 0 && session.trackCount() > 2);
    repaint();
}

void Arrangement::splitSelectedAtPlayhead()
{
    cancelDrag();
    const auto result = session.splitClip(selected, playheadTime(session.edit->getTransport()));
    if (result.failed() && status) status(result.getErrorMessage());
}

void Arrangement::duplicateSelected()
{
    cancelDrag();
    const auto result = session.duplicateClip(selected);
    if (result.failed() && status) status(result.getErrorMessage());
}

void Arrangement::nudgeSelected(int direction, bool byBar)
{
    cancelDrag();
    auto* clip = session.findClip(selected);
    if (!clip) return;
    const auto old = clip->getPosition();
    const auto delta = (byBar ? 60.0 / session.tempo() * 4.0 : snapUnitSeconds()) * (direction < 0 ? -1.0 : 1.0);
    const auto length = old.time.getLength().inSeconds();
    const auto start = std::max(0.0, old.time.getStart().inSeconds() + delta);
    const auto result = session.editClip(selected, {start, start + length, old.offset.inSeconds()}, ClipGesture::move);
    if (result.failed() && status) status(result.getErrorMessage());
}

juce::Result Arrangement::applyBrowserDrop(const juce::String& description, int track, double startSeconds, bool insertPreset)
{
    const auto kind = browserDropKind(description);
    const auto id = browserDropId(description);

    if (kind == "preset")
    {
        const auto preset = patternPresetFromId(id);
        if (!preset) return juce::Result::fail("That browser item cannot be loaded here.");
        const auto result = insertPreset ? session.insertPatternPreset(*preset, std::max(0, track), startSeconds)
                                         : juce::Result::ok();
        if (result.failed()) return result;
        if (!insertPreset) session.applyPatternPreset(*preset);
        selectTrack(insertPreset ? std::max(0, track) : 0);
        if (status) status(insertPreset ? "Added pattern clip from browser" : "Loaded browser preset on Pattern 1");
        return juce::Result::ok();
    }

    if (kind == "effect")
    {
        const auto effect = audioEffectFromId(id);
        if (!effect) return juce::Result::fail("That browser item cannot be inserted here.");
        if (track <= 0) return juce::Result::fail("Drop audio effects on an audio track.");
        const auto result = session.addAudioEffect(*effect, track);
        if (result.failed()) return result;
        selectTrack(track);
        if (status) status("Added browser effect to " + session.trackName(track));
        return juce::Result::ok();
    }

    if (kind == "info")
    {
        if (status) status(id + " is already available in this starter session.");
        return juce::Result::ok();
    }

    return juce::Result::fail("Drop sounds, drums, or audio effects on the arrangement.");
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

int Arrangement::trackAt(float y) const
{
    for (int track = 0; track < session.trackCount(); ++track)
        if (lane(track).contains(juce::Point<float>(headerWidth, y)))
            return track;
    return -1;
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
