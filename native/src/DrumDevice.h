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
    enum class VoiceType { kick, lowTom, midTom, snare, highTom, clap, closedHat, openHat };
    struct Voice
    {
        VoiceType type = VoiceType::kick;
        bool active = false;
        float age = 0.0f, velocity = 0.0f, phase = 0.0f, noise = 0.0f;
        uint32_t seed = 1;
        int samplePosition = 0;
    };

    void trigger(int note, float velocity);
    float render(Voice&);
    float nextNoise(Voice&) noexcept;
    void loadSamples();
    void loadSample(juce::AudioBuffer<float>& destination, double& sourceRate,
                    const void* data, int dataSize);
    float renderSample(Voice&, const juce::AudioBuffer<float>&, double sourceRate);

    std::array<Voice, 32> voices;
    std::array<juce::AudioBuffer<float>, 7> tr808Samples;
    std::array<double, 7> tr808SampleRates { 44100.0, 44100.0, 44100.0, 44100.0,
                                              44100.0, 44100.0, 44100.0 };
    juce::AudioBuffer<float> clapSample;
    double sampleRate = 48000.0;
    double clapSampleRate = 44100.0;
    size_t nextVoice = 0;
};
}
