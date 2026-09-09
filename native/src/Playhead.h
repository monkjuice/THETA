#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace theta
{
// Submit one damage region for a complete old-to-new frame. The margin covers
// physical-pixel rounding at fractional desktop scales (125%, 150%, etc.).
inline juce::Rectangle<int> playheadDamage(int previous, int next, juce::Rectangle<int> area)
{
    const auto strip = [area](int x)
    {
        return x < 0 ? juce::Rectangle<int>{}
                     : juce::Rectangle<int>{x - 2, area.getY(), 6, area.getHeight()}.getIntersection(area);
    };
    return strip(previous).getUnion(strip(next));
}

inline void movePlayhead(juce::Component& owner, int& current, int next, juce::Rectangle<int> area)
{
    if (current == next) return;
    const auto damage = playheadDamage(current, next, area);
    current = next;
    if (!damage.isEmpty()) owner.repaint(damage);
}
}
