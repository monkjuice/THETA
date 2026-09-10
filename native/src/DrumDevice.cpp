#include "DrumDevice.h"
#include <cmath>

namespace theta
{
DrumDevice::DrumDevice(te::PluginCreationInfo info) : Plugin(info) {}

DrumDevice::~DrumDevice()
{
    notifyListenersOfDeletion();
}

void DrumDevice::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    reset();
}

void DrumDevice::reset()
{
    for (auto& voice : voices)
        voice.active = false;
}

void DrumDevice::midiPanic()
{
    reset();
}

bool DrumDevice::hasNameForMidiNoteNumber(int note, int midiChannel, juce::String& name)
{
    juce::ignoreUnused(midiChannel);
    if (note == 48) name = "Kick";
    else if (note == 53) name = "Snare";
    else if (note == 56) name = "Clap";
    else if (note == 58) name = "Hat";
    else return false;
    return true;
}

void DrumDevice::trigger(int note, float velocity)
{
    auto& voice = voices[nextVoice++ % voices.size()];
    voice.type = note < 52 ? VoiceType::kick : note == 56 ? VoiceType::clap : note < 57 ? VoiceType::snare : VoiceType::hat;
    voice.active = true;
    voice.age = 0.0f;
    voice.velocity = std::clamp(velocity, 0.0f, 1.0f);
    voice.phase = 0.0f;
    voice.noise = 0.0f;
    voice.seed = voice.seed * 1664525u + 1013904223u + static_cast<uint32_t>(note * 97);
}

float DrumDevice::nextNoise(Voice& voice) noexcept
{
    voice.seed = voice.seed * 1664525u + 1013904223u;
    return static_cast<float>((static_cast<int>((voice.seed >> 9) & 0xffff) - 32768) / 32768.0);
}

float DrumDevice::render(Voice& voice)
{
    const auto t = voice.age;
    const auto dt = static_cast<float>(1.0 / sampleRate);
    voice.age += dt;

    if (voice.type == VoiceType::kick)
    {
        if (t > 0.55f) { voice.active = false; return 0.0f; }
        const auto env = std::exp(-t * 9.0f);
        const auto freq = 42.0f + 95.0f * std::exp(-t * 24.0f);
        voice.phase += juce::MathConstants<float>::twoPi * freq * dt;
        return std::sin(voice.phase) * env * voice.velocity * 0.9f;
    }

    if (voice.type == VoiceType::snare)
    {
        if (t > 0.32f) { voice.active = false; return 0.0f; }
        const auto noiseEnv = std::exp(-t * 15.0f);
        const auto bodyEnv = std::exp(-t * 18.0f);
        voice.phase += juce::MathConstants<float>::twoPi * 185.0f * dt;
        return (nextNoise(voice) * noiseEnv * 0.62f + std::sin(voice.phase) * bodyEnv * 0.28f) * voice.velocity;
    }

    if (voice.type == VoiceType::clap)
    {
        if (t > 0.42f) { voice.active = false; return 0.0f; }
        const auto burst = std::exp(-std::fmod(t, 0.031f) * 58.0f);
        const auto tail = std::exp(-t * 10.0f);
        const auto noise = nextNoise(voice);
        voice.noise = noise - voice.noise * 0.54f;
        return voice.noise * (0.45f * burst + 0.24f * tail) * voice.velocity;
    }

    if (t > 0.12f) { voice.active = false; return 0.0f; }
    const auto noise = nextNoise(voice);
    voice.noise = noise - voice.noise * 0.72f;
    return voice.noise * std::exp(-t * 45.0f) * voice.velocity * 0.32f;
}

void DrumDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.bufferNumSamples == 0)
        return;

    SCOPED_REALTIME_CHECK
    for (int channel = 0; channel < context.destBuffer->getNumChannels(); ++channel)
        context.destBuffer->clear(channel, context.bufferStartSample, context.bufferNumSamples);

    if (context.bufferForMidiMessages != nullptr)
        for (const auto& midi : *context.bufferForMidiMessages)
            if (midi.isNoteOn())
                trigger(midi.getNoteNumber(), midi.getFloatVelocity());

    for (int frame = context.bufferStartSample; frame < context.bufferStartSample + context.bufferNumSamples; ++frame)
    {
        float sample = 0.0f;
        for (auto& voice : voices)
            if (voice.active)
                sample += render(voice);
        sample = std::clamp(sample, -0.95f, 0.95f);
        for (int channel = 0; channel < context.destBuffer->getNumChannels(); ++channel)
            context.destBuffer->setSample(channel, frame, sample);
    }
}
}
