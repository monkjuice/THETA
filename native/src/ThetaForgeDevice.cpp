#include "ThetaForgeDevice.h"

namespace theta
{
namespace
{
constexpr std::array<const char*, 23> ids {
    "oscAPosition", "oscBPosition", "oscBLevel", "oscBTune", "subLevel", "noiseLevel", "unison", "detune",
    "cutoff", "resonance", "attack", "decay", "sustain", "release", "filterEnvAmount", "filterAttack",
    "filterDecay", "filterSustain", "filterRelease", "lfoRate", "lfoCutoff", "drive", "output"};
constexpr std::array<const char*, 23> names {
    "A Position", "B Position", "B Level", "B Tune", "Sub", "Noise", "Unison", "Detune", "Cutoff", "Resonance",
    "Attack", "Decay", "Sustain", "Release", "Filter Env", "Filter Attack", "Filter Decay", "Filter Sustain",
    "Filter Release", "LFO Rate", "LFO Cutoff", "Drive", "Output"};
constexpr std::array<float, 23> defaults {
    .55f, .18f, .25f, 7.0f, .12f, 0.0f, 2.0f, .18f, 7800.0f, .12f, .01f, .24f, .75f, .35f,
    .25f, .005f, .3f, .35f, .3f, .5f, 0.0f, .08f, .75f};

juce::NormalisableRange<float> rangeFor(int index)
{
    if (index == 3) return {-24.0f, 24.0f, 1.0f};
    if (index == 6) return {1.0f, 8.0f, 1.0f};
    if (index == 8) return {30.0f, 18000.0f, 0.0f, .25f};
    if (index == 10 || index == 11 || index == 15 || index == 16) return {.001f, 4.0f, 0.0f, .35f};
    if (index == 13 || index == 18) return {.001f, 8.0f, 0.0f, .35f};
    if (index == 14 || index == 20) return {-1.0f, 1.0f};
    if (index == 19) return {.05f, 20.0f, 0.0f, .35f};
    if (index == 22) return {0.0f, 1.25f};
    return {0.0f, 1.0f};
}
}

ThetaForgeDevice::ThetaForgeDevice(te::PluginCreationInfo info) : Plugin(info)
{
    auto* undo = getUndoManager();
    juce::CachedValue<float>* values[] {
        &oscAPosition, &oscBPosition, &oscBLevel, &oscBTune, &subLevel, &noiseLevel, &unison, &detune,
        &cutoff, &resonance, &attack, &decay, &sustain, &release, &filterEnvAmount, &filterAttack,
        &filterDecay, &filterSustain, &filterRelease, &lfoRate, &lfoCutoff, &drive, &output};
    for (int i = 0; i < static_cast<int>(parameters.size()); ++i)
    {
        values[i]->referTo(state, ids[static_cast<size_t>(i)], undo, defaults[static_cast<size_t>(i)]);
        parameters[static_cast<size_t>(i)] = addParam(ids[static_cast<size_t>(i)], names[static_cast<size_t>(i)], rangeFor(i));
        parameters[static_cast<size_t>(i)]->attachToCurrentValue(*values[i]);
    }
}

ThetaForgeDevice::~ThetaForgeDevice()
{
    notifyListenersOfDeletion();
    for (auto* parameter : getAutomatableParameters()) parameter->detachFromCurrentValue();
}

void ThetaForgeDevice::initialise(const te::PluginInitialisationInfo& info) { core.initialise(info.sampleRate); }
void ThetaForgeDevice::reset() { core.reset(); }

forge::Patch ThetaForgeDevice::patch()
{
    return {oscAPosition, oscBPosition, oscBLevel, oscBTune, subLevel, noiseLevel, unison, detune,
            cutoff, resonance, attack, decay, sustain, release, filterEnvAmount, filterAttack,
            filterDecay, filterSustain, filterRelease, lfoRate, lfoCutoff, drive, output};
}

void ThetaForgeDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    if (!context.destBuffer) return;
    SCOPED_REALTIME_CHECK
    auto& buffer = *context.destBuffer;
    if (context.bufferForMidiMessages)
        for (const auto& event : *context.bufferForMidiMessages)
        {
            if (event.isNoteOn()) core.noteOn(event.getNoteNumber(), event.getFloatVelocity());
            else if (event.isNoteOff()) core.noteOff(event.getNoteNumber());
            else if (event.isAllNotesOff()) core.allNotesOff();
        }
    const auto values = patch();
    for (int i = context.bufferStartSample; i < context.bufferStartSample + context.bufferNumSamples; ++i)
    {
        float left, right;
        core.renderSample(values, left, right);
        if (buffer.getNumChannels()) buffer.setSample(0, i, buffer.getSample(0, i) + left);
        if (buffer.getNumChannels() > 1) buffer.setSample(1, i, buffer.getSample(1, i) + right);
    }
}

void ThetaForgeDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, oscAPosition, oscBPosition, oscBLevel, oscBTune, subLevel, noiseLevel,
                                     unison, detune, cutoff, resonance, attack, decay, sustain, release,
                                     filterEnvAmount, filterAttack, filterDecay, filterSustain, filterRelease,
                                     lfoRate, lfoCutoff, drive, output);
    for (auto* parameter : getAutomatableParameters()) parameter->updateFromAttachedValue();
}
}
