#include "BrowserPanel.h"

namespace theta
{
BrowserPanel::BrowserPanel(Session& s) : session(s)
{
    setOpaque(true);
    title.setText("BROWSER", juce::dontSendNotification);
    title.setColour(juce::Label::textColourId, juce::Colour(0xffd5dde4));
    categoriesTitle.setText("Categories", juce::dontSendNotification);
    categoriesTitle.setColour(juce::Label::textColourId, juce::Colour(0xff8f9aa4));
    soundsTitle.setText("Name", juce::dontSendNotification);
    soundsTitle.setColour(juce::Label::textColourId, juce::Colour(0xffaeb8c0));
    search.setTextToShowWhenEmpty("Search", juce::Colour(0xff6f7b85));
    search.onTextChange = [this] { rebuildRows(); };
    search.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff262c32));
    search.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff46515a));

    const std::array labels {"Sounds", "Drums", "Instruments", "Audio FX", "MIDI FX"};
    for (size_t i = 0; i < categories.size(); ++i)
    {
        categories[i].setButtonText(labels[i]);
        categories[i].setRadioGroupId(18, juce::dontSendNotification);
        categories[i].setClickingTogglesState(true);
        categories[i].onClick = [this, i]
        {
            selectedCategory = categories[i].getButtonText();
            rebuildRows();
        };
        addAndMakeVisible(categories[i]);
    }
    categories[0].setToggleState(true, juce::dontSendNotification);

    items = {
        {"Sounds", "Warm pulse", "Soft one-bar 4OSC chord pulse", Session::PatternPreset::WarmPulse},
        {"Sounds", "Acid steps", "Tight 16-step synth riff", Session::PatternPreset::AcidSteps},
        {"Drums", "House kit", "Four-on-floor kick, backbeat, hats", Session::PatternPreset::HouseKit},
        {"Drums", "Break kit", "Syncopated kick/snare/hats groove", Session::PatternPreset::BreakKit},
        {"Drums", "Minimal kit", "Sparse kick/snare/hats sketch", Session::PatternPreset::MinimalKit},
        {"Instruments", "4OSC synth", "Loaded on Pattern synth", std::nullopt},
        {"Instruments", "Utility gain", "Post-synth gain stage", std::nullopt},
        {"Audio FX", "Utility gain", "Track gain control, undoable", std::nullopt},
        {"Audio FX", "EQ placeholder", "Device rack surface coming next", std::nullopt},
        {"MIDI FX", "Snap 1/16", "Grid quantized note entry", std::nullopt},
        {"MIDI FX", "Pattern presets", "Double-click rows to replace notes", std::nullopt}
    };

    list.setRowHeight(38);
    list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff20262c));
    list.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff323a42));
    list.setMultipleSelectionEnabled(false);
    for (auto* component : std::initializer_list<juce::Component*>{&title, &categoriesTitle, &soundsTitle, &search, &list})
        addAndMakeVisible(component);
    rebuildRows();
}

void BrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b2026));
    g.setColour(juce::Colour(0xff303840));
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    g.setColour(juce::Colour(0xff252b31));
    g.fillRect(0, 96, getWidth(), 1);
}

void BrowserPanel::resized()
{
    title.setBounds(14, 12, getWidth() - 28, 24);
    search.setBounds(12, 44, getWidth() - 24, 30);
    categoriesTitle.setBounds(12, 84, getWidth() - 24, 22);
    int y = 112;
    for (auto& button : categories)
    {
        button.setBounds(12, y, getWidth() - 24, 28);
        y += 32;
    }
    soundsTitle.setBounds(12, y + 8, getWidth() - 24, 22);
    list.setBounds(12, y + 34, getWidth() - 24, getHeight() - y - 46);
}

int BrowserPanel::getNumRows()
{
    return static_cast<int>(rows.size());
}

void BrowserPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (!juce::isPositiveAndBelow(row, rows.size())) return;
    const auto& item = items[static_cast<size_t>(rows[static_cast<size_t>(row)])];
    g.fillAll(selected ? juce::Colour(0xff34424a) : juce::Colour(row % 2 == 0 ? 0xff20262c : 0xff242a31));
    g.setColour(item.preset ? juce::Colour(0xffc6d58c) : juce::Colour(0xff8cc5d2));
    g.fillRoundedRectangle(8.0f, height * 0.5f - 4.0f, 8.0f, 8.0f, 1.5f);
    g.setFont(juce::FontOptions(14.0f));
    g.setColour(juce::Colour(0xffe5ebef));
    g.drawText(item.name, 24, 3, width - 30, 17, juce::Justification::centredLeft, true);
    g.setFont(juce::FontOptions(11.0f));
    g.setColour(juce::Colour(0xff94a0aa));
    g.drawText(item.detail, 24, 20, width - 30, 15, juce::Justification::centredLeft, true);
}

void BrowserPanel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (!juce::isPositiveAndBelow(row, rows.size())) return;
    const auto& item = items[static_cast<size_t>(rows[static_cast<size_t>(row)])];
    if (status) status(item.name + " - " + item.detail);
}

void BrowserPanel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    applyRow(row);
}

void BrowserPanel::rebuildRows()
{
    rows.clear();
    const auto query = search.getText().trim().toLowerCase();
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        if (item.category != selectedCategory) continue;
        if (query.isNotEmpty()
            && !item.name.toLowerCase().contains(query)
            && !item.detail.toLowerCase().contains(query))
            continue;
        rows.push_back(static_cast<int>(i));
    }
    list.updateContent();
    list.repaint();
}

void BrowserPanel::applyRow(int row)
{
    if (!juce::isPositiveAndBelow(row, rows.size())) return;
    const auto& item = items[static_cast<size_t>(rows[static_cast<size_t>(row)])];
    if (item.preset)
    {
        session.applyPatternPreset(*item.preset);
        if (status) status("Loaded " + item.name);
    }
    else if (status)
    {
        status(item.name + " is already part of this starter session.");
    }
}
}
