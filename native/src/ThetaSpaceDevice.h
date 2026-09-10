#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace theta
{
namespace te = tracktion::engine;

class ThetaSpaceDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "theta.space.v1";
    static const char* getPluginName() { return "Theta Space"; }
    explicit ThetaSpaceDevice(te::PluginCreationInfo);
    ~ThetaSpaceDevice() override;
    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Theta"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

private:
    juce::CachedValue<float> mix, size, smear, drive, width, outputDb;
    te::AutomatableParameter::Ptr mixParam, sizeParam, smearParam, driveParam, widthParam, outputParam;
    juce::Reverb reverb;
    std::vector<float> delayL, delayR, dryL, dryR;
    double sampleRate = 48000.0;
    int writeIndex = 0;
};
}
