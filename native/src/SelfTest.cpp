#include "Session.h"
#include <cmath>
#include <stdexcept>

namespace theta
{
int runSelfTest()
{
    try
    {
        auto require = [](bool valid) { if (!valid) throw std::runtime_error("Native device check failed"); };
        Session session;
        require(session.utility != nullptr);
        auto& device = *session.utility;
        device.gain().setParameter(-6.0f, juce::dontSendNotification);
        device.initialise({{}, 48000.0, 512});
        juce::AudioBuffer<float> buffer(2, 520);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 520; ++i) buffer.setSample(c, i, 1.0f);
        te::PluginRenderContext context(&buffer, 4, 512, nullptr, 0.0, {}, false, false, true, false);
        device.applyToBuffer(context);
        const auto expected = std::pow(10.0f, -6.0f / 20.0f);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 520; ++i)
                require(std::abs(buffer.getSample(c, i) - (i >= 4 && i < 516 ? expected : 1.0f)) < 1.0e-5f);

        // Exercise smoothing and equal gain on every channel; frame offsets
        // and stereo consistency catch errors hidden by single-channel tones.
        device.gain().setParameter(0.0f, juce::dontSendNotification);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 520; ++i) buffer.setSample(c, i, 1.0f);
        device.applyToBuffer(context);
        require(buffer.getSample(0, 4) > expected && buffer.getSample(0, 4) < 1.0f);
        require(std::abs(buffer.getSample(0, 515) - 1.0f) < 1.0e-5f);
        for (int i = 4; i < 516; ++i)
            require(buffer.getSample(0, i) == buffer.getSample(1, i));
        te::PluginRenderContext noAudio(nullptr, 0, 0, nullptr, 0.0, {}, false, false, true, false);
        device.applyToBuffer(noAudio);

        auto restored = device.state.createCopy();
        restored.setProperty("gainDb", -12.0f, nullptr);
        device.restorePluginStateFromValueTree(restored);
        require(std::abs(device.gain().getCurrentValue() + 12.0f) < 1.0e-5f);
        auto xml = device.state.createXml();
        auto roundTrip = juce::ValueTree::fromXml(*xml);
        roundTrip.removeProperty(te::IDs::id, nullptr);
        auto reloaded = session.edit->getPluginCache().createNewPlugin(roundTrip);
        require(dynamic_cast<UtilityDevice*>(reloaded.get()) != nullptr);
        require(std::abs(reloaded->getAutomatableParameterByID("gainDb")->getCurrentValue() + 12.0f) < 1.0e-5f);
        device.deinitialise();

        session.drums->initialise({{}, 48000.0, 512});
        juce::AudioBuffer<float> drumBuffer(2, 4096);
        te::MidiMessageArray midi;
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, 56, 1.0f), 0.0, {});
        te::PluginRenderContext drumContext(&drumBuffer, 0, drumBuffer.getNumSamples(), &midi, 0.0, {}, true, false, true, false);
        session.drums->applyToBuffer(drumContext);
        float clapPeak = 0.0f;
        for (int c = 0; c < drumBuffer.getNumChannels(); ++c)
            for (int i = 0; i < drumBuffer.getNumSamples(); ++i)
            {
                const auto sample = drumBuffer.getSample(c, i);
                require(std::isfinite(sample));
                clapPeak = std::max(clapPeak, std::abs(sample));
            }
        require(clapPeak > 0.0001f && clapPeak < 1.0f);
        session.drums->deinitialise();
        return 0;
    }
    catch (const std::exception& error)
    {
        juce::Logger::writeToLog(error.what());
        return 1;
    }
}
}
