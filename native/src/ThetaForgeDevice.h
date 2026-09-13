#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include "ForgeCore.h"

namespace theta { namespace te = tracktion::engine;
class ThetaForgeDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "theta.forge.v1";
    explicit ThetaForgeDevice(te::PluginCreationInfo);
    ~ThetaForgeDevice() override;
    juce::String getName() const override { return "Theta Forge"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Theta"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    bool isSynth() override { return true; } bool takesMidiInput() override { return true; } bool producesAudioWhenNoAudioInput() override { return true; }
    void initialise(const te::PluginInitialisationInfo&) override; void deinitialise() override {}
    void reset() override; void midiPanic() override { reset(); } void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;
private:
    forge::Patch patch();
    forge::Core core;
    juce::CachedValue<float> oscAPosition, oscBPosition, oscBLevel, oscBTune, subLevel, noiseLevel, unison, detune, cutoff, resonance, attack, decay, sustain, release;
    std::array<te::AutomatableParameter::Ptr, 14> parameters;
}; } 
