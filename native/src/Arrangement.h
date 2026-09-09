#pragma once
#include "Session.h"
#include <map>

namespace theta
{
class Arrangement final : public juce::Component,
                          private juce::ChangeListener,
                          private juce::ScrollBar::Listener,
                          private Session::Listener
{
public:
    explicit Arrangement(Session&);
    ~Arrangement() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void fit();
    std::function<void(juce::String)> status;
private:
    friend int runArrangementTest();
    struct Waveform;
    struct ClipView
    {
        te::EditItemID id;
        juce::String name;
        ClipGeometry position;
        Waveform* waveform = nullptr;
        double speed = 1.0;
        int track = 0;
    };
    void sync();
    void updateScroll();
    void zoom(double factor, double anchor);
    void cancelDrag();
    void updatePlayhead();
    float xFor(double seconds) const;
    double timeAt(float x) const;
    double snapped(double seconds, bool bypass) const;
    juce::Rectangle<float> lane(int track) const;
    juce::Rectangle<float> bounds(const ClipView&) const;
    int hit(juce::Point<float>) const;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void scrollBarMoved(juce::ScrollBar*, double) override;
    void editWillChange() override;
    void editDidChange() override;
    Session& session;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache {32};
    std::map<juce::String, std::unique_ptr<Waveform>> waveforms;
    std::vector<ClipView> clips;
    juce::TextButton fitButton {"Fit"}, zoomIn {"+"}, zoomOut {"-"}, snap {"Snap 1/16"};
    std::array<juce::TextButton, 2> mute, solo;
    juce::ScrollBar scroll {false};
    juce::VBlankAttachment vblank;
    double viewStart = 0.0, viewSpan = 8.0, songEnd = 2.0;
    te::EditItemID selected;
    bool dragging = false;
    ClipGesture gesture = ClipGesture::move;
    ClipGeometry original, preview;
    double dragTime = 0.0, sourceDuration = 0.0;
    float playhead = -1.0f;
    static constexpr float headerWidth = 148.0f, rulerTop = 32.0f, lanesTop = 56.0f;
};
}
