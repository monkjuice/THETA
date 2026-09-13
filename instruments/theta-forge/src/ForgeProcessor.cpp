#include "ForgeProcessor.h"
#include "ForgeEditor.h"

namespace theta::forge
{
namespace
{
std::unique_ptr<juce::RangedAudioParameter> parameter(const char* id, const char* name, juce::NormalisableRange<float> range, float initial)
{
    return std::make_unique<juce::AudioParameterFloat>(id, name, range, initial);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::parameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;
    result.push_back(parameter("oscAPosition", "Osc A Position", {0.0f, 1.0f}, 0.55f));
    result.push_back(parameter("oscBPosition", "Osc B Position", {0.0f, 1.0f}, 0.18f));
    result.push_back(parameter("oscBLevel", "Osc B Level", {0.0f, 1.0f}, 0.25f));
    result.push_back(parameter("oscBTune", "Osc B Tune", {-24.0f, 24.0f, 1.0f}, 7.0f));
    result.push_back(parameter("subLevel", "Sub Level", {0.0f, 1.0f}, 0.12f));
    result.push_back(parameter("noiseLevel", "Noise Level", {0.0f, 1.0f}, 0.0f));
    result.push_back(parameter("unison", "Unison", {1.0f, 8.0f, 1.0f}, 2.0f));
    result.push_back(parameter("detune", "Detune", {0.0f, 1.0f}, 0.18f));
    result.push_back(parameter("cutoff", "Cutoff", {30.0f, 18000.0f, 0.0f, 0.25f}, 7800.0f));
    result.push_back(parameter("resonance", "Resonance", {0.0f, 1.0f}, 0.12f));
    result.push_back(parameter("attack", "Attack", {0.001f, 4.0f, 0.0f, 0.35f}, 0.01f));
    result.push_back(parameter("decay", "Decay", {0.001f, 4.0f, 0.0f, 0.35f}, 0.24f));
    result.push_back(parameter("sustain", "Sustain", {0.0f, 1.0f}, 0.75f));
    result.push_back(parameter("release", "Release", {0.001f, 8.0f, 0.0f, 0.35f}, 0.35f));
    return {result.begin(), result.end()};
}

Processor::Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "ThetaForgeState", parameterLayout())
{
    parameters = {state.getRawParameterValue("oscAPosition"), state.getRawParameterValue("oscBPosition"),
                  state.getRawParameterValue("oscBLevel"), state.getRawParameterValue("oscBTune"),
                  state.getRawParameterValue("subLevel"), state.getRawParameterValue("noiseLevel"),
                  state.getRawParameterValue("unison"), state.getRawParameterValue("detune"),
                  state.getRawParameterValue("cutoff"), state.getRawParameterValue("resonance"),
                  state.getRawParameterValue("attack"), state.getRawParameterValue("decay"),
                  state.getRawParameterValue("sustain"), state.getRawParameterValue("release")};
    synth.addSound(new Sound());
    for (int i = 0; i < 16; ++i)
        synth.addVoice(new Voice(parameters));
}

void Processor::prepareToPlay(double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    synth.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

void Processor::getStateInformation(juce::MemoryBlock& destination)
{
    if (const auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void Processor::setStateInformation(const void* data, int size)
{
    if (const auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new theta::forge::Processor();
}
