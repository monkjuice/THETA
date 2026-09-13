#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <algorithm>
#include <cmath>

// The reusable sound engine. It deliberately owns no AudioProcessor, UI,
// Tracktion, state tree, filesystem, or allocation in renderSample().
namespace theta::forge
{
struct Patch
{
    float oscAPosition = 0.55f, oscBPosition = 0.18f, oscBLevel = 0.25f, oscBTune = 7.0f;
    float subLevel = 0.12f, noiseLevel = 0.0f, unison = 2.0f, detune = 0.18f;
    float cutoff = 7800.0f, resonance = 0.12f, attack = 0.01f, decay = 0.24f, sustain = 0.75f, release = 0.35f;
};

class Core final
{
public:
    void initialise(double newSampleRate) { sampleRate = std::max(1.0, newSampleRate); reset(); }
    void reset() { voices = {}; nextVoice = 0; }
    void noteOn(int note, float velocity)
    {
        auto& voice = voices[nextVoice++ % voices.size()];
        voice = {}; voice.active = true; voice.note = note; voice.velocity = juce::jlimit(0.0f, 1.0f, velocity);
    }
    void noteOff(int note)
    {
        for (auto& voice : voices)
            if (voice.active && voice.note == note && !voice.released) { voice.released = true; voice.releaseStart = voice.envelope; }
    }
    void allNotesOff() { reset(); }
    void renderSample(const Patch& patch, float& left, float& right)
    {
        left = right = 0.0f;
        for (auto& voice : voices)
        {
            const auto dry = renderVoice(voice, patch);
            if (!voice.active && dry == 0.0f) continue;
            const auto g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(30.0f, static_cast<float>(sampleRate * 0.3), patch.cutoff) / static_cast<float>(sampleRate));
            const auto damping = 1.0f / (1.0f + juce::jlimit(0.0f, 1.0f, patch.resonance) * 15.0f);
            const auto h = 1.0f / (1.0f + 2.0f * damping * g + g * g);
            const auto high = (dry - 2.0f * damping * voice.band - voice.low) * h;
            voice.band += g * high; voice.low += g * voice.band;
            left += voice.low - voice.band * patch.detune * 0.08f;
            right += voice.low + voice.band * patch.detune * 0.08f;
        }
        left = std::tanh(left) * 0.22f; right = std::tanh(right) * 0.22f;
    }

private:
    struct Voice { bool active = false, released = false; int note = 0; float velocity = 0, phaseA = 0, phaseB = 0, phaseSub = 0, envelope = 0, releaseStart = 0, low = 0, band = 0; };
    static float morph(float phase, float position)
    {
        phase -= std::floor(phase); const auto sine = std::sin(phase * juce::MathConstants<float>::twoPi);
        const auto triangle = 1.0f - 4.0f * std::abs(phase - 0.5f), saw = phase * 2.0f - 1.0f, square = phase < 0.5f ? 1.0f : -1.0f;
        const float frames[] {sine, triangle, saw, square}; const auto scaled = juce::jlimit(0.0f, 1.0f, position) * 3.0f;
        const auto index = std::min(2, static_cast<int>(scaled)); const auto mix = scaled - static_cast<float>(index);
        return frames[index] + (frames[index + 1] - frames[index]) * mix;
    }
    float renderVoice(Voice& voice, const Patch& patch)
    {
        if (!voice.active) return 0.0f; const auto dt = static_cast<float>(1.0 / sampleRate);
        if (voice.released) { voice.envelope -= voice.releaseStart * dt / std::max(0.001f, patch.release); if (voice.envelope <= 0.0001f) { voice.active = false; return 0.0f; } }
        else if (voice.envelope < 1.0f) voice.envelope = std::min(1.0f, voice.envelope + dt / std::max(0.001f, patch.attack));
        else if (voice.envelope > patch.sustain) voice.envelope = std::max(patch.sustain, voice.envelope - (1.0f - patch.sustain) * dt / std::max(0.001f, patch.decay));
        const auto hz = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(voice.note)); const auto hzB = hz * std::pow(2.0f, patch.oscBTune / 12.0f);
        auto a = 0.0f; const auto count = juce::jlimit(1, 8, juce::roundToInt(patch.unison));
        for (int i = 0; i < count; ++i) { const auto spread = count == 1 ? 0.0f : static_cast<float>(i) / (count - 1) - 0.5f; a += morph(voice.phaseA * std::pow(2.0f, spread * patch.detune * 0.08f), patch.oscAPosition); }
        a /= count; const auto b = morph(voice.phaseB, patch.oscBPosition);
        const auto sample = (a * (1.0f - patch.oscBLevel) + b * patch.oscBLevel + std::sin(voice.phaseSub * juce::MathConstants<float>::twoPi) * patch.subLevel + std::sin((voice.phaseA + voice.phaseB * 1.618f) * 5387.0f) * patch.noiseLevel) * voice.envelope * voice.velocity;
        voice.phaseA = std::fmod(voice.phaseA + hz * dt, 1.0f); voice.phaseB = std::fmod(voice.phaseB + hzB * dt, 1.0f); voice.phaseSub = std::fmod(voice.phaseSub + hz * 0.5f * dt, 1.0f); return sample;
    }
    std::array<Voice, 16> voices {}; double sampleRate = 48000.0; size_t nextVoice = 0;
};
}
