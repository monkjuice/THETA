#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>

namespace theta
{
class Theme final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const auto diameter = static_cast<float>(std::min(width, height)) - 4.0f;
        const auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height))
                              .withSizeKeepingCentre(diameter, diameter);
        const auto radius = area.getWidth() * 0.5f;
        const auto centre = area.getCentre();
        const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        g.setColour(slider.findColour(juce::Slider::backgroundColourId));
        g.fillEllipse(area);
        g.setColour(juce::Colour(0xff11161a));
        g.drawEllipse(area.reduced(0.5f), 1.0f);

        const auto arc = area.reduced(3.0f);
        juce::Path backgroundArc;
        backgroundArc.addCentredArc(centre.x, centre.y, arc.getWidth() * 0.5f, arc.getHeight() * 0.5f,
                                    0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(slider.findColour(juce::Slider::backgroundColourId).brighter(0.25f));
        g.strokePath(backgroundArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path valueArc;
        valueArc.addCentredArc(centre.x, centre.y, arc.getWidth() * 0.5f, arc.getHeight() * 0.5f,
                               0.0f, rotaryStartAngle, angle, true);
        g.setColour(slider.findColour(juce::Slider::trackColourId));
        g.strokePath(valueArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(juce::Colour(0xffaab7bf));
        for (const auto tickAngle : {0.0f, juce::MathConstants<float>::halfPi,
                                     juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 1.5f})
        {
            const auto tickCentre = centre + juce::Point<float>(std::sin(tickAngle), -std::cos(tickAngle)) * (radius * 0.43f);
            const auto horizontal = std::abs(std::sin(tickAngle)) > 0.5f;
            const juce::Rectangle<float> tick(horizontal ? tickCentre.x - 5.0f : tickCentre.x - 1.0f,
                                             horizontal ? tickCentre.y - 1.0f : tickCentre.y - 5.0f,
                                             horizontal ? 10.0f : 2.0f,
                                             horizontal ? 2.0f : 10.0f);
            g.fillRect(tick);
        }

        juce::Path pointer;
        pointer.addRectangle(-1.25f, -radius * 0.36f, 2.5f, radius * 0.31f);
        g.setColour(slider.findColour(juce::Slider::thumbColourId));
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
        g.setColour(juce::Colour(0xff0c1013));
        g.drawEllipse(area.reduced(radius * 0.34f), 1.0f);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& background, bool highlighted, bool pressed) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto fill = background.withMultipliedSaturation(button.hasKeyboardFocus(true) ? 1.3f : 0.9f)
                              .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f);
        if (pressed || highlighted) fill = fill.contrasting(pressed ? 0.2f : 0.05f);

        juce::Path shape;
        shape.addRectangle(bounds);
        g.setColour(fill);
        g.fillPath(shape);
        g.setColour(button.findColour(juce::ComboBox::outlineColourId));
        g.strokePath(shape, juce::PathStrokeType(1.0f));
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override
    {
        auto fill = box.findColour(juce::ComboBox::backgroundColourId)
                       .withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.5f);
        if (isButtonDown) fill = fill.contrasting(0.12f);
        g.setColour(fill);
        g.fillRect(0, 0, width, height);
        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRect(0, 0, width, height);

        const auto arrow = juce::Rectangle<float>(static_cast<float>(buttonX), static_cast<float>(buttonY),
                                                 static_cast<float>(buttonW), static_cast<float>(buttonH)).reduced(6.0f, 8.0f);
        juce::Path path;
        path.startNewSubPath(arrow.getX(), arrow.getY());
        path.lineTo(arrow.getCentreX(), arrow.getBottom());
        path.lineTo(arrow.getRight(), arrow.getY());
        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.strokePath(path, juce::PathStrokeType(1.5f));
    }

    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override
    {
        g.setColour(textEditor.findColour(juce::TextEditor::backgroundColourId));
        g.fillRect(0, 0, width, height);
    }

    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override
    {
        const auto colour = textEditor.hasKeyboardFocus(true)
            ? textEditor.findColour(juce::TextEditor::focusedOutlineColourId)
            : textEditor.findColour(juce::TextEditor::outlineColourId);
        g.setColour(colour);
        g.drawRect(0, 0, width, height);
    }
};
}
