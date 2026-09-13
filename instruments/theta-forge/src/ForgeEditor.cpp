#include "ForgeEditor.h"

#include <cmath>

namespace theta::forge
{
namespace
{
constexpr std::array<const char*, 14> ids {"oscAPosition", "oscBPosition", "oscBLevel", "oscBTune", "subLevel", "noiseLevel", "unison", "detune", "cutoff", "resonance", "attack", "decay", "sustain", "release"};
constexpr std::array<const char*, 14> names {"A POS", "B POS", "B LEVEL", "B TUNE", "SUB", "NOISE", "UNISON", "DETUNE", "CUTOFF", "RES", "ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
constexpr std::array<const char*, 14> tips {
    "Scan oscillator A's harmonic shape", "Scan oscillator B's harmonic shape", "Set oscillator B's level", "Tune oscillator B in semitones",
    "Blend a grounded sub oscillator", "Add a little heat and air", "Stack voices for width", "Spread stacked oscillator voices",
    "Open or close the low-pass filter", "Emphasise the filter edge", "Set how the sound begins", "Set the fall after the attack",
    "Set the held level", "Set how the sound fades"};

float waveform(float phase, float position)
{
    const auto sine = std::sin(phase * juce::MathConstants<float>::twoPi);
    const auto saw = phase * 2.0f - 1.0f;
    const auto square = phase < 0.5f ? 1.0f : -1.0f;
    const auto first = juce::jmap(juce::jlimit(0.0f, 1.0f, position * 2.0f), sine, saw);
    return juce::jmap(juce::jlimit(0.0f, 1.0f, position * 2.0f - 1.0f), first, square);
}

void drawWaveform(juce::Graphics& g, juce::Rectangle<float> area, float position, juce::Colour colour)
{
    juce::Path path;
    constexpr int points = 160;
    for (int i = 0; i <= points; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(points);
        const auto x = area.getX() + phase * area.getWidth();
        const auto y = area.getCentreY() - waveform(phase, position) * area.getHeight() * 0.38f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(colour.withAlpha(0.16f));
    g.strokePath(path, juce::PathStrokeType(7.0f));
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(1.8f));
}
}

Editor::Editor(Processor& p) : AudioProcessorEditor(&p), processor(p)
{
    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto& control = controls[i];
        control.label.setText(names[i], juce::dontSendNotification);
        control.label.setJustificationType(juce::Justification::centred);
        control.label.setColour(juce::Label::textColourId, juce::Colour(0xffb8c8d2));
        control.label.setFont(juce::FontOptions(11.0f));
        control.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        control.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, 17);
        control.slider.setColour(juce::Slider::rotarySliderFillColourId, i < 8 ? juce::Colour(0xffff7a59) : juce::Colour(0xff4de0c1));
        control.slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff30414b));
        control.slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffffd37a));
        control.slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffeff8fa));
        control.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        control.slider.setTooltip(tips[i]);
        control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state, ids[i], control.slider);
        addAndMakeVisible(control.label);
        addAndMakeVisible(control.slider);
    }
    setResizable(true, true);
    setResizeLimits(700, 560, 1400, 900);
    setSize(980, 640);
    startTimerHz(24);
}

void Editor::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient background(juce::Colour(0xff10151e), 0.0f, 0.0f, juce::Colour(0xff202235), bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill(background);
    g.fillRect(bounds);
    const auto frame = bounds.reduced(16.0f);
    g.setColour(juce::Colour(0xff33424d));
    g.drawRoundedRectangle(frame, 9.0f, 1.0f);
    g.setColour(juce::Colour(0xffff7a59));
    g.fillRoundedRectangle(frame.getX(), frame.getY(), 6.0f, frame.getHeight(), 3.0f);

    g.setColour(juce::Colour(0xfff7f4e9));
    g.setFont(juce::FontOptions(36.0f, juce::Font::bold));
    g.drawText("FORGE", 36, 26, 220, 42, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffffc66d));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("SHAPE SOUND. LEAVE A MARK.", 39, 68, 260, 18, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff94a9b6));
    g.drawText("TWO OSCILLATOR INSTRUMENT", getWidth() - 300, 43, 260, 18, juce::Justification::centredRight);

    const auto waveArea = juce::Rectangle<float>(36.0f, 104.0f, getWidth() - 72.0f, 105.0f);
    g.setColour(juce::Colour(0xff151f2a));
    g.fillRoundedRectangle(waveArea, 8.0f);
    g.setColour(juce::Colour(0xff2c3a45));
    for (int line = 1; line < 4; ++line)
        g.drawHorizontalLine(static_cast<int>(waveArea.getY() + waveArea.getHeight() * line / 4.0f), waveArea.getX(), waveArea.getRight());
    drawWaveform(g, waveArea.reduced(12.0f, 16.0f), processor.state.getRawParameterValue("oscAPosition")->load(), juce::Colour(0xffff7a59));
    drawWaveform(g, waveArea.reduced(12.0f, 28.0f), processor.state.getRawParameterValue("oscBPosition")->load(), juce::Colour(0xff4de0c1));

    const auto lowerY = 226.0f;
    g.setColour(juce::Colour(0xff18212c));
    g.fillRoundedRectangle(36.0f, lowerY, getWidth() - 72.0f, getHeight() - lowerY - 32.0f, 8.0f);
    g.setColour(juce::Colour(0xff94a9b6));
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("OSCILLATOR FORGE", 52, 238, 220, 16, juce::Justification::centredLeft);
    g.drawText("TONE", getWidth() / 2 + 10, 238, 100, 16, juce::Justification::centredLeft);
    g.drawText("AMP CONTOUR", getWidth() / 2 + 10, getHeight() / 2 + 14, 150, 16, juce::Justification::centredLeft);
}

void Editor::resized()
{
    const auto left = 44;
    const auto available = getWidth() - 88;
    const auto oscillatorWidth = available * 3 / 5;
    const auto oscillatorCell = oscillatorWidth / 4;
    for (size_t i = 0; i < 8; ++i)
    {
        const auto row = static_cast<int>(i) / 4;
        const auto column = static_cast<int>(i) % 4;
        auto cell = juce::Rectangle<int>(left + column * oscillatorCell, 262 + row * 154, oscillatorCell, 144).reduced(7);
        controls[i].label.setBounds(cell.removeFromTop(20));
        controls[i].slider.setBounds(cell);
    }
    const auto rightX = left + oscillatorWidth + 8;
    const auto rightWidth = available - oscillatorWidth - 8;
    for (size_t i = 8; i < controls.size(); ++i)
    {
        const auto local = static_cast<int>(i) - 8;
        const auto row = local / 2;
        const auto column = local % 2;
        auto cell = juce::Rectangle<int>(rightX + column * (rightWidth / 2), 262 + row * 154, rightWidth / 2, 144).reduced(7);
        controls[i].label.setBounds(cell.removeFromTop(20));
        controls[i].slider.setBounds(cell);
    }
}

void Editor::timerCallback()
{
    repaint();
}
}
