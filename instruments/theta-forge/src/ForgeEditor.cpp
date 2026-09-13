#include "ForgeEditor.h"

namespace theta::forge
{
namespace
{
constexpr std::array<const char*, 14> ids {"oscAPosition", "oscBPosition", "oscBLevel", "oscBTune", "subLevel", "noiseLevel", "unison", "detune", "cutoff", "resonance", "attack", "decay", "sustain", "release"};
constexpr std::array<const char*, 14> names {"A POS", "B POS", "B LEVEL", "B TUNE", "SUB", "NOISE", "UNISON", "DETUNE", "CUTOFF", "RES", "ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
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
        control.slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff68d1df));
        control.slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffe3efad));
        control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state, ids[i], control.slider);
        addAndMakeVisible(control.label);
        addAndMakeVisible(control.slider);
    }
    setResizable(true, true);
    setResizeLimits(700, 440, 1400, 900);
    setSize(900, 560);
}

void Editor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0e1419));
    g.setColour(juce::Colour(0xff273640));
    g.drawRect(getLocalBounds().reduced(14));
    g.setColour(juce::Colour(0xffeff8fa));
    g.setFont(juce::FontOptions(31.0f));
    g.drawText("Theta Forge", 30, 24, 350, 40, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff68d1df));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("TWO OSCILLATORS  ·  MODERN WAVETABLE SYNTH", 33, 61, 450, 20, juce::Justification::centredLeft);
    for (int row = 0; row < 2; ++row)
    {
        g.setColour(row == 0 ? juce::Colour(0xff13232c) : juce::Colour(0xff191a2b));
        g.fillRoundedRectangle(25.0f, 105.0f + row * (getHeight() - 130.0f) / 2.0f, getWidth() - 50.0f, (getHeight() - 155.0f) / 2.0f, 7.0f);
    }
}

void Editor::resized()
{
    const auto content = getLocalBounds().reduced(38, 104);
    const auto cellWidth = content.getWidth() / 7;
    const auto cellHeight = content.getHeight() / 2;
    for (size_t i = 0; i < controls.size(); ++i)
    {
        const auto column = static_cast<int>(i) % 7;
        const auto row = static_cast<int>(i) / 7;
        auto cell = juce::Rectangle<int>(content.getX() + column * cellWidth, content.getY() + row * cellHeight, cellWidth, cellHeight).reduced(7);
        controls[i].label.setBounds(cell.removeFromTop(20));
        controls[i].slider.setBounds(cell);
    }
}
}
