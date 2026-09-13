#include "ForgeVoice.h"
#include <cmath>

namespace theta::forge
{
Voice::Voice(const Parameters& p) : parameters(p) {}

bool Voice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<Sound*>(sound) != nullptr;
}

void Voice::startNote(int midiNoteNumber, float newVelocity, juce::SynthesiserSound*, int)
{
    juce::ignoreUnused(midiNoteNumber);
    sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    velocity = newVelocity;
    phaseA = phaseB = phaseSub = low = band = 0.0f;
    envelope.setSampleRate(sampleRate);
    envelope.setParameters({parameters.attack->load(), parameters.decay->load(), parameters.sustain->load(), parameters.release->load()});
    envelope.noteOn();
}

void Voice::stopNote(float, bool allowTailOff)
{
    envelope.noteOff();
    if (!allowTailOff)
        clearCurrentNote();
}

float Voice::morph(float phase, float position)
{
    phase -= std::floor(phase);
    const auto sine = std::sin(phase * juce::MathConstants<float>::twoPi);
    const auto triangle = 1.0f - 4.0f * std::abs(phase - 0.5f);
    const auto saw = phase * 2.0f - 1.0f;
    const auto square = phase < 0.5f ? 1.0f : -1.0f;
    const float frames[] {sine, triangle, saw, square};
    const auto scaled = juce::jlimit(0.0f, 1.0f, position) * 3.0f;
    const auto frame = std::min(2, static_cast<int>(scaled));
    const auto fraction = scaled - static_cast<float>(frame);
    return frames[frame] + (frames[frame + 1] - frames[frame]) * fraction;
}

void Voice::renderNextBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    const auto noteHz = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(getCurrentlyPlayingNote()));
    const auto oscBHz = noteHz * std::pow(2.0f, parameters.oscBTune->load() / 12.0f);
    const auto voices = juce::jlimit(1, 8, juce::roundToInt(parameters.unison->load()));
    const auto detune = parameters.detune->load();
    const auto cutoff = parameters.cutoff->load();
    const auto resonance = parameters.resonance->load();
    const auto g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(30.0f, static_cast<float>(sampleRate * 0.3), cutoff) / static_cast<float>(sampleRate));
    const auto damping = 1.0f / (1.0f + resonance * 15.0f);
    const auto h = 1.0f / (1.0f + 2.0f * damping * g + g * g);
    for (int sample = startSample; sample < startSample + numSamples; ++sample)
    {
        auto oscillator = 0.0f;
        for (int voice = 0; voice < voices; ++voice)
        {
            const auto spread = voices == 1 ? 0.0f : static_cast<float>(voice) / static_cast<float>(voices - 1) - 0.5f;
            const auto ratio = std::pow(2.0f, spread * detune * 0.08f);
            oscillator += morph(phaseA * ratio, parameters.oscAPosition->load());
        }
        oscillator /= static_cast<float>(voices);
        oscillator = oscillator * (1.0f - parameters.oscBLevel->load())
            + morph(phaseB, parameters.oscBPosition->load()) * parameters.oscBLevel->load();
        oscillator += std::sin(phaseSub * juce::MathConstants<float>::twoPi) * parameters.subLevel->load();
        const auto noise = std::sin((phaseA + phaseB * 1.618f) * 5387.0f) * parameters.noiseLevel->load();
        const auto input = std::tanh((oscillator + noise) * velocity);
        const auto high = (input - 2.0f * damping * band - low) * h;
        band += g * high;
        low += g * band;
        const auto output = low * envelope.getNextSample();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.addSample(channel, sample, output * 0.22f);
        phaseA += noteHz / static_cast<float>(sampleRate);
        phaseB += oscBHz / static_cast<float>(sampleRate);
        phaseSub += noteHz * 0.5f / static_cast<float>(sampleRate);
        phaseA -= std::floor(phaseA);
        phaseB -= std::floor(phaseB);
        phaseSub -= std::floor(phaseSub);
        if (!envelope.isActive())
        {
            clearCurrentNote();
            break;
        }
    }
}
}
