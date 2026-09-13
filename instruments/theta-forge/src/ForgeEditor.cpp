#include "ForgeEditor.h"
#include "../ui/ForgeVisuals.h"

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
        control.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
        control.slider.setColour(juce::Slider::rotarySliderFillColourId, ui::accentForParameter(static_cast<int>(i)));
        control.slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff30414b));
        control.slider.setColour(juce::Slider::thumbColourId, ui::accentForParameter(static_cast<int>(i)).brighter(0.3f));
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
    ui::paint(g, getLocalBounds(), [this](int index)
    {
        return processor.state.getRawParameterValue(ids[static_cast<size_t>(index)])->load();
    });
}

void Editor::resized()
{
    for (int i = 0; i < static_cast<int>(controls.size()); ++i)
    {
        auto cell = ui::controlCell(getLocalBounds(), i).reduced(7);
        controls[static_cast<size_t>(i)].label.setBounds(cell.removeFromTop(20));
        controls[static_cast<size_t>(i)].slider.setBounds(cell);
    }
}

void Editor::timerCallback()
{
    repaint();
}
}
