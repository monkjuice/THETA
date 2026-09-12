#include "ClipGeometryTest.h"
#include "../../ClipGeometry.h"
#include "../../Playhead.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace theta
{
int runArrangementGeometryTest()
{
    try
    {
        const auto close = [](double a, double b) { return std::abs(a - b) < 0.0001; };
        const ClipGeometry geometry {0.75, 1.25, 0.25};
        if (!close(previewClipEdit(geometry, ClipGesture::trimLeft, -5, 1).start, 0.5))
            throw std::runtime_error("Left extension stops at source zero");
        if (!close(previewClipEdit(geometry, ClipGesture::trimRight, 99, 1).end, 1.5))
            throw std::runtime_error("Right extension stops at source end");
        if (!close(previewClipEdit(geometry, ClipGesture::move, -5, 1).start, 0))
            throw std::runtime_error("Move stops at timeline zero");

        const juce::Rectangle<int> lane {100, 20, 800, 200};
        if (!playheadDamage(-1.0f, -1.0f, lane).isEmpty())
            throw std::runtime_error("Hidden playheads cause no damage");
        if (playheadDamage(-1.0f, 150.25f, lane) != juce::Rectangle<int>(148, 20, 7, 200))
            throw std::runtime_error("Fractional playhead damage rounds outwards");
        if (playheadDamage(150.25f, 155.75f, lane) != juce::Rectangle<int>(148, 20, 12, 200))
            throw std::runtime_error("Playhead movement covers old and new positions");
        if (playheadDamage(-1.0f, 99.0f, lane) != juce::Rectangle<int>(100, 20, 3, 200))
            throw std::runtime_error("Playhead damage stays inside its lane");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
