#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace theta
{
class Theme final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& background, bool highlighted, bool pressed) override
    {
        constexpr float cornerRadius = 3.0f;
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto fill = background.withMultipliedSaturation(button.hasKeyboardFocus(true) ? 1.3f : 0.9f)
                              .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f);
        if (pressed || highlighted) fill = fill.contrasting(pressed ? 0.2f : 0.05f);

        // Joined controls keep their shared edges square, including BPM buttons.
        const bool left = button.isConnectedOnLeft(), right = button.isConnectedOnRight();
        const bool top = button.isConnectedOnTop(), bottom = button.isConnectedOnBottom();
        juce::Path shape;
        shape.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                                  cornerRadius, cornerRadius,
                                  !(left || top), !(right || top), !(left || bottom), !(right || bottom));
        g.setColour(fill);
        g.fillPath(shape);
        g.setColour(button.findColour(juce::ComboBox::outlineColourId));
        g.strokePath(shape, juce::PathStrokeType(1.0f));
    }
};
}
