#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace theta::forge
{
struct Parameters
{
    std::atomic<float>* oscAPosition = nullptr;
    std::atomic<float>* oscBPosition = nullptr;
    std::atomic<float>* oscBLevel = nullptr;
    std::atomic<float>* oscBTune = nullptr;
    std::atomic<float>* subLevel = nullptr;
    std::atomic<float>* noiseLevel = nullptr;
    std::atomic<float>* unison = nullptr;
    std::atomic<float>* detune = nullptr;
    std::atomic<float>* cutoff = nullptr;
    std::atomic<float>* resonance = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* decay = nullptr;
    std::atomic<float>* sustain = nullptr;
    std::atomic<float>* release = nullptr;
};

class Sound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

class Voice final : public juce::SynthesiserVoice
{
public:
    explicit Voice(const Parameters&);
    bool canPlaySound(juce::SynthesiserSound*) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>&, int startSample, int numSamples) override;

private:
    static float morph(float phase, float position);
    const Parameters& parameters;
    juce::ADSR envelope;
    double sampleRate = 44100.0;
    float phaseA = 0.0f, phaseB = 0.0f, phaseSub = 0.0f, velocity = 0.0f;
    float low = 0.0f, band = 0.0f;
};
}
