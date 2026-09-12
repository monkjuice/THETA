#include "DeviceRackTest.h"
#include "../../Session.h"
#include <stdexcept>

namespace theta
{
void runPatternDeviceRackTest()
{
    const auto require = [](bool valid, const char* message)
    {
        if (!valid) throw std::runtime_error(message);
    };
    Session session;
    require(session.utility != nullptr && session.audioUtility != nullptr && session.synth != nullptr
            && session.thetaWave != nullptr && session.drums != nullptr,
            "Session creates synth, wavetable, drum, and Utility devices");
    auto* effectTrack = te::getAudioTracks(*session.edit)[1];
    const auto initialAudioPluginCount = effectTrack->pluginList.size();
    require(session.addAudioEffect(Session::AudioEffect::Equaliser).wasOk(), "Audio FX browser action inserts EQ");
    require(effectTrack->pluginList.size() == initialAudioPluginCount + 1, "Audio FX insert grows the audio track chain");
    auto audioDevices = session.deviceSlots(1);
    require(static_cast<int>(audioDevices.size()) == effectTrack->pluginList.size(), "Audio device rack mirrors the plugin chain");
    require(audioDevices.back().removable, "Inserted audio effect is removable");
    require(session.toggleDeviceEnabled(1, static_cast<int>(audioDevices.size()) - 1).wasOk(), "Device rack bypasses selected effect");
    require(!effectTrack->pluginList[effectTrack->pluginList.size() - 1]->isEnabled(), "Bypass disables the effect plugin");
    session.undo();
    require(effectTrack->pluginList[effectTrack->pluginList.size() - 1]->isEnabled(), "Undo restores effect enabled state");
    require(session.deleteDevice(1, static_cast<int>(audioDevices.size()) - 1).wasOk(), "Device rack deletes inserted effect");
    require(effectTrack->pluginList.size() == initialAudioPluginCount, "Delete removes inserted audio effect");
    session.undo();
    require(effectTrack->pluginList.size() == initialAudioPluginCount + 1, "Undo restores deleted audio effect");
    session.undo();
    require(effectTrack->pluginList.size() == initialAudioPluginCount, "Undo removes inserted audio effect");
}
}
