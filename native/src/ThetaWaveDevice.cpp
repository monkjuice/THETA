#include "ThetaWaveDevice.h"
#include <algorithm>
#include <cmath>

namespace theta
{
ThetaWaveDevice::ThetaWaveDevice(te::PluginCreationInfo info) : Plugin(info)
{
    auto* undo = getUndoManager();
    position.referTo(state, "position", undo, 0.35f);
    shape.referTo(state, "shape", undo, 0.55f);
    sub.referTo(state, "sub", undo, 0.18f);
    cutoff.referTo(state, "cutoff", undo, 6400.0f);
    resonance.referTo(state, "resonance", undo, 0.15f);
    attack.referTo(state, "attack", undo, 0.018f);
    decay.referTo(state, "decay", undo, 0.18f);
    sustain.referTo(state, "sustain", undo, 0.72f);
    releaseTime.referTo(state, "release", undo, 0.28f);
    unison.referTo(state, "unison", undo, 1.0f);
    detune.referTo(state, "detune", undo, 0.08f);
    width.referTo(state, "width", undo, 0.42f);
    outputDb.referTo(state, "outputDb", undo, -8.0f);

    positionParam = addParam("position", "Position", {0.0f, 1.0f});
    shapeParam = addParam("shape", "Shape", {0.0f, 1.0f});
    subParam = addParam("sub", "Sub", {0.0f, 1.0f});
    cutoffParam = addParam("cutoff", "Cutoff", {80.0f, 18000.0f, 0.0f, 0.45f});
    resonanceParam = addParam("resonance", "Resonance", {0.0f, 1.0f});
    attackParam = addParam("attack", "Attack", {0.001f, 5.0f, 0.0f, 0.35f});
    decayParam = addParam("decay", "Decay", {0.001f, 8.0f, 0.0f, 0.35f});
    sustainParam = addParam("sustain", "Sustain", {0.0f, 1.0f});
    releaseParam = addParam("release", "Release", {0.001f, 10.0f, 0.0f, 0.35f});
    unisonParam = addParam("unison", "Unison", {1.0f, 4.0f, 1.0f});
    detuneParam = addParam("detune", "Detune", {0.0f, 0.35f});
    widthParam = addParam("width", "Width", {0.0f, 1.0f});
    outputParam = addParam("outputDb", "Output", {-36.0f, 6.0f});

    for (auto pair : std::initializer_list<std::pair<te::AutomatableParameter::Ptr*, juce::CachedValue<float>*>>{
             {&positionParam, &position}, {&shapeParam, &shape}, {&subParam, &sub}, {&cutoffParam, &cutoff},
             {&resonanceParam, &resonance}, {&attackParam, &attack}, {&decayParam, &decay},
             {&sustainParam, &sustain}, {&releaseParam, &releaseTime}, {&unisonParam, &unison},
             {&detuneParam, &detune}, {&widthParam, &width}, {&outputParam, &outputDb}})
        (*pair.first)->attachToCurrentValue(*pair.second);

    auto percentText = [] (float value) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; };
    auto msText = [] (float value) { return juce::String(juce::roundToInt(value * 1000.0f)) + "ms"; };
    positionParam->valueToStringFunction = percentText;
    shapeParam->valueToStringFunction = percentText;
    subParam->valueToStringFunction = percentText;
    cutoffParam->valueToStringFunction = [] (float value) { return juce::String(juce::roundToInt(value)) + "Hz"; };
    resonanceParam->valueToStringFunction = percentText;
    attackParam->valueToStringFunction = msText;
    decayParam->valueToStringFunction = msText;
    sustainParam->valueToStringFunction = percentText;
    releaseParam->valueToStringFunction = msText;
    unisonParam->valueToStringFunction = [] (float value) { return juce::String(juce::roundToInt(value)); };
    detuneParam->valueToStringFunction = percentText;
    widthParam->valueToStringFunction = percentText;
    outputParam->valueToStringFunction = [] (float value) { return juce::String(value, 1) + " dB"; };
}

ThetaWaveDevice::~ThetaWaveDevice()
{
    notifyListenersOfDeletion();
    for (auto* parameter : getAutomatableParameters())
        parameter->detachFromCurrentValue();
}

void ThetaWaveDevice::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    reset();
}

void ThetaWaveDevice::reset()
{
    for (auto& voice : voices)
        voice.active = false;
    filterL = filterR = 0.0f;
}

void ThetaWaveDevice::midiPanic()
{
    reset();
}

void ThetaWaveDevice::trigger(int note, float velocity)
{
    auto& voice = voices[nextVoice++ % voices.size()];
    voice = {};
    voice.active = true;
    voice.note = note;
    voice.velocity = std::clamp(velocity, 0.0f, 1.0f);
}

void ThetaWaveDevice::release(int note)
{
    for (auto& voice : voices)
        if (voice.active && !voice.released && voice.note == note)
        {
            voice.released = true;
            voice.releaseStart = voice.envelope;
        }
}

float ThetaWaveDevice::wave(float phase) const
{
    phase -= std::floor(phase);
    const auto sine = std::sin(phase * juce::MathConstants<float>::twoPi);
    const auto tri = 1.0f - 4.0f * std::abs(phase - 0.5f);
    const auto saw = phase * 2.0f - 1.0f;
    const auto square = phase < 0.5f ? 1.0f : -1.0f;
    const auto folded = std::sin((phase + positionParam->getCurrentValue() * 0.35f) * juce::MathConstants<float>::twoPi)
        * 0.58f + std::sin(phase * juce::MathConstants<float>::twoPi * (2.0f + shapeParam->getCurrentValue() * 6.0f)) * 0.42f;
    const auto morph = std::clamp(positionParam->getCurrentValue(), 0.0f, 1.0f) * 4.0f;
    const auto index = std::min(3, static_cast<int>(morph));
    const auto mix = morph - static_cast<float>(index);
    const float shapes[] {sine, tri, saw, square, folded};
    return shapes[index] + (shapes[index + 1] - shapes[index]) * mix;
}

float ThetaWaveDevice::renderVoice(Voice& voice)
{
    if (!voice.active)
        return 0.0f;

    const auto dt = static_cast<float>(1.0 / sampleRate);
    const auto attackSeconds = std::max(0.001f, attackParam->getCurrentValue());
    const auto decaySeconds = std::max(0.001f, decayParam->getCurrentValue());
    const auto sustainLevel = std::clamp(sustainParam->getCurrentValue(), 0.0f, 1.0f);
    const auto releaseSeconds = std::max(0.001f, releaseParam->getCurrentValue());

    if (voice.released)
    {
        voice.envelope -= voice.releaseStart * dt / releaseSeconds;
        if (voice.envelope <= 0.0001f)
        {
            voice.active = false;
            return 0.0f;
        }
    }
    else if (voice.envelope < 1.0f)
    {
        voice.envelope = std::min(1.0f, voice.envelope + dt / attackSeconds);
    }
    else if (voice.envelope > sustainLevel)
    {
        voice.envelope = std::max(sustainLevel, voice.envelope - (1.0f - sustainLevel) * dt / decaySeconds);
    }

    const auto frequency = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(voice.note));
    const auto unisonCount = juce::jlimit(1, 4, juce::roundToInt(unisonParam->getCurrentValue()));
    auto sample = 0.0f;
    const auto basePhase = voice.phase;
    for (int i = 0; i < unisonCount; ++i)
    {
        const auto spread = unisonCount == 1 ? 0.0f : (static_cast<float>(i) / static_cast<float>(unisonCount - 1) - 0.5f);
        const auto cents = spread * detuneParam->getCurrentValue() * 48.0f;
        const auto detunedPhase = basePhase * std::pow(2.0f, cents / 1200.0f);
        sample += wave(detunedPhase + spread * widthParam->getCurrentValue() * 0.12f);
    }
    voice.phase += frequency * dt;
    if (voice.phase >= 1.0f)
        voice.phase -= std::floor(voice.phase);
    sample /= static_cast<float>(unisonCount);

    voice.subPhase += frequency * 0.5f * dt;
    if (voice.subPhase >= 1.0f)
        voice.subPhase -= std::floor(voice.subPhase);
    const auto subOsc = std::sin(voice.subPhase * juce::MathConstants<float>::twoPi);
    sample = sample * (1.0f - subParam->getCurrentValue() * 0.45f) + subOsc * subParam->getCurrentValue() * 0.45f;
    return sample * voice.envelope * voice.velocity;
}

void ThetaWaveDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.bufferNumSamples == 0)
        return;

    SCOPED_REALTIME_CHECK
    auto& buffer = *context.destBuffer;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, context.bufferStartSample, context.bufferNumSamples);

    if (context.bufferForMidiMessages != nullptr)
        for (const auto& midi : *context.bufferForMidiMessages)
        {
            if (midi.isNoteOn())
                trigger(midi.getNoteNumber(), midi.getFloatVelocity());
            else if (midi.isNoteOff())
                release(midi.getNoteNumber());
            else if (midi.isAllNotesOff())
                midiPanic();
        }

    const auto output = juce::Decibels::decibelsToGain(outputParam->getCurrentValue());
    const auto cutoffHz = std::clamp(cutoffParam->getCurrentValue(), 80.0f, static_cast<float>(sampleRate * 0.45));
    const auto filterAmount = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * cutoffHz / static_cast<float>(sampleRate));
    const auto resonancePush = 1.0f + resonanceParam->getCurrentValue() * 1.8f;

    for (int frame = context.bufferStartSample; frame < context.bufferStartSample + context.bufferNumSamples; ++frame)
    {
        auto mono = 0.0f;
        for (auto& voice : voices)
            mono += renderVoice(voice);
        mono = std::tanh(mono * resonancePush) * output;
        filterL += (mono - filterL) * filterAmount;
        filterR += (mono - filterR) * filterAmount;
        const auto side = mono - filterL;
        const auto left = std::clamp(filterL - side * widthParam->getCurrentValue() * 0.18f, -0.98f, 0.98f);
        const auto right = std::clamp(filterR + side * widthParam->getCurrentValue() * 0.18f, -0.98f, 0.98f);
        if (buffer.getNumChannels() > 0)
            buffer.setSample(0, frame, left);
        if (buffer.getNumChannels() > 1)
            buffer.setSample(1, frame, right);
    }
}

void ThetaWaveDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, position, shape, sub, cutoff, resonance, attack, decay, sustain, releaseTime,
                                     unison, detune, width, outputDb);
    for (auto* parameter : getAutomatableParameters())
        parameter->updateFromAttachedValue();
}
}
