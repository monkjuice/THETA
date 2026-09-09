#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace theta
{
// Each footprint includes physical-pixel rounding at fractional desktop scales.
inline juce::Rectangle<int> playheadDamage(int previous, int next, juce::Rectangle<int> area)
{
    const auto strip = [area](int x)
    {
        return x < 0 ? juce::Rectangle<int>{}
                     : juce::Rectangle<int>{x - 2, area.getY(), 6, area.getHeight()}.getIntersection(area);
    };
    return strip(previous).getUnion(strip(next));
}

void movePlayhead(juce::Component& owner, int& current, int next, juce::Rectangle<int> area);
}
