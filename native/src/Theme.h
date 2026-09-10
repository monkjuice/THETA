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
