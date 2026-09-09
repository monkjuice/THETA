#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <array>

namespace theta
{
namespace te = tracktion::engine;

class DrumDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "theta.drums.v1";
    static const char* getPluginName() { return "Theta Drums"; }
    explicit DrumDevice(te::PluginCreationInfo);
    ~DrumDevice() override;
    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Theta"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    bool isSynth() override { return true; }
    bool takesMidiInput() override { return true; }
    bool producesAudioWhenNoAudioInput() override { return true; }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void midiPanic() override;
    void applyToBuffer(const te::PluginRenderContext&) override;
    bool hasNameForMidiNoteNumber(int note, int midiChannel, juce::String& name) override;

private:
    enum class VoiceType { kick, snare, hat };
    struct Voice
    {
        VoiceType type = VoiceType::kick;
        bool active = false;
        float age = 0.0f, velocity = 0.0f, phase = 0.0f, noise = 0.0f;
        uint32_t seed = 1;
    };

    void trigger(int note, float velocity);
    float render(Voice&);
    float nextNoise(Voice&) noexcept;

    std::array<Voice, 32> voices;
    double sampleRate = 48000.0;
    size_t nextVoice = 0;
};
}
