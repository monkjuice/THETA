#include "Session.h"

namespace theta
{
namespace
{
const juce::Identifier starterPlaceholderID {"thetaStarterPlaceholder"};
const juce::Identifier editorStepsID {"thetaEditorSteps"};

struct PresetNote { int step, pitch, length; };

enum class SynthPatch { Default, ChordPad, SubBass, ReeseBass };

struct PresetPattern
{
    const PresetNote* notes = nullptr;
    int count = 0;
    juce::String name;
    bool useDrums = false;
    bool useThetaWave = false;
    SynthPatch synthPatch = SynthPatch::Default;
};

PresetPattern presetPattern(Session::PatternPreset preset)
{
    static constexpr PresetNote warmPulse[] {{0, 48, 2}, {4, 55, 2}, {8, 60, 2}, {12, 55, 2}};
    static constexpr PresetNote acidSteps[] {{0, 48, 1}, {3, 51, 1}, {6, 55, 1}, {7, 58, 1}, {10, 55, 1}, {13, 63, 1}, {15, 58, 1}};
    static constexpr PresetNote arpRun[] {{0, 48, 16}, {0, 52, 16}, {0, 55, 16}, {0, 60, 16}};
    static constexpr PresetNote chordPad[] {{0, 48, 7}, {0, 55, 7}, {0, 60, 7}, {0, 64, 7},
                                            {8, 50, 7}, {8, 57, 7}, {8, 62, 7}, {8, 65, 7}};
    static constexpr PresetNote subBass[] {{0, 36, 4}, {4, 36, 2}, {6, 43, 2}, {8, 39, 4}, {12, 34, 4}};
    static constexpr PresetNote reeseBass[] {{0, 36, 8}, {8, 39, 4}, {12, 41, 4}};
    static constexpr PresetNote sirenLead[] {{0, 48, 1}, {1, 55, 1}, {2, 60, 1}, {3, 67, 1}, {4, 72, 2}, {7, 67, 1},
                                             {8, 60, 1}, {9, 55, 1}, {10, 48, 1}, {12, 60, 1}, {14, 67, 1}, {15, 72, 1}};
    static constexpr PresetNote wavePad[] {{0, 48, 8}, {0, 55, 8}, {0, 60, 8}, {8, 50, 8}, {8, 57, 8}, {8, 62, 8}};
    static constexpr PresetNote waveBass[] {{0, 36, 3}, {3, 36, 1}, {4, 43, 2}, {8, 34, 4}, {12, 39, 3}, {15, 41, 1}};
    static constexpr PresetNote wavePluck[] {{0, 60, 1}, {2, 67, 1}, {4, 72, 2}, {7, 67, 1}, {8, 62, 1}, {10, 69, 1}, {12, 74, 2}, {15, 69, 1}};
    static constexpr PresetNote houseKit[] {{0, 48, 1}, {4, 48, 1}, {8, 48, 1}, {12, 48, 1}, {4, 53, 1}, {12, 53, 1},
                                            {2, 58, 1}, {6, 58, 1}, {10, 58, 1}, {14, 58, 1}};
    static constexpr PresetNote breakKit[] {{0, 48, 1}, {3, 48, 1}, {8, 48, 1}, {11, 48, 1}, {4, 53, 1}, {10, 53, 1},
                                            {1, 58, 1}, {3, 58, 1}, {6, 58, 1}, {9, 58, 1}, {12, 58, 1}, {15, 58, 1}};
    static constexpr PresetNote minimalKit[] {{0, 48, 1}, {7, 48, 1}, {12, 48, 1}, {4, 53, 1}, {12, 53, 1}, {2, 58, 1}, {10, 58, 1}, {14, 58, 1}};
    static constexpr PresetNote clapKit[] {{0, 48, 1}, {4, 56, 1}, {8, 48, 1}, {12, 56, 1}, {2, 58, 1}, {6, 58, 1}, {10, 58, 1}, {14, 58, 1}};

    switch (preset)
    {
        case Session::PatternPreset::WarmPulse:  return {warmPulse,  static_cast<int>(std::size(warmPulse)),  "Warm pulse", false, false, SynthPatch::Default};
        case Session::PatternPreset::AcidSteps:  return {acidSteps,  static_cast<int>(std::size(acidSteps)),  "Acid steps", false, false, SynthPatch::Default};
        case Session::PatternPreset::ArpRun:     return {arpRun,     static_cast<int>(std::size(arpRun)),     "Arp run", false, false, SynthPatch::Default};
        case Session::PatternPreset::ChordPad:   return {chordPad,   static_cast<int>(std::size(chordPad)),   "Chord pad", false, false, SynthPatch::ChordPad};
        case Session::PatternPreset::SubBass:    return {subBass,    static_cast<int>(std::size(subBass)),    "Sub bass", false, false, SynthPatch::SubBass};
        case Session::PatternPreset::ReeseBass:  return {reeseBass,  static_cast<int>(std::size(reeseBass)),  "Reese bass", false, false, SynthPatch::ReeseBass};
        case Session::PatternPreset::SirenLead:  return {sirenLead,  static_cast<int>(std::size(sirenLead)),  "Siren lead", false, false, SynthPatch::Default};
        case Session::PatternPreset::WavePad:    return {wavePad,    static_cast<int>(std::size(wavePad)),    "Wave pad", false, true,  SynthPatch::Default};
        case Session::PatternPreset::WaveBass:   return {waveBass,   static_cast<int>(std::size(waveBass)),   "Wave bass", false, true,  SynthPatch::Default};
        case Session::PatternPreset::WavePluck:  return {wavePluck,  static_cast<int>(std::size(wavePluck)),  "Wave pluck", false, true,  SynthPatch::Default};
        case Session::PatternPreset::HouseKit:   return {houseKit,   static_cast<int>(std::size(houseKit)),   "House kit", true,  false, SynthPatch::Default};
        case Session::PatternPreset::BreakKit:   return {breakKit,   static_cast<int>(std::size(breakKit)),   "Break kit", true,  false, SynthPatch::Default};
        case Session::PatternPreset::MinimalKit: return {minimalKit, static_cast<int>(std::size(minimalKit)), "Minimal kit", true,  false, SynthPatch::Default};
        case Session::PatternPreset::ClapKit:    return {clapKit,    static_cast<int>(std::size(clapKit)),    "Clap kit", true,  false, SynthPatch::Default};
    }
    return {};
}

juce::Colour presetColour(Session::PatternPreset preset)
{
    switch (preset)
    {
        case Session::PatternPreset::WarmPulse:  return juce::Colour(0xff4f7d8f);
        case Session::PatternPreset::AcidSteps:  return juce::Colour(0xff2f6e78);
        case Session::PatternPreset::ArpRun:     return juce::Colour(0xff77659a);
        case Session::PatternPreset::ChordPad:   return juce::Colour(0xff5f718f);
        case Session::PatternPreset::SubBass:    return juce::Colour(0xff34535f);
        case Session::PatternPreset::ReeseBass:  return juce::Colour(0xff4a5f38);
        case Session::PatternPreset::SirenLead:  return juce::Colour(0xff8f4f67);
        case Session::PatternPreset::WavePad:    return juce::Colour(0xff5e55b8);
        case Session::PatternPreset::WaveBass:   return juce::Colour(0xff355a86);
        case Session::PatternPreset::WavePluck:  return juce::Colour(0xff4c7a95);
        case Session::PatternPreset::HouseKit:   return juce::Colour(0xff657844);
        case Session::PatternPreset::BreakKit:   return juce::Colour(0xff6f7f43);
        case Session::PatternPreset::MinimalKit: return juce::Colour(0xff506d45);
        case Session::PatternPreset::ClapKit:    return juce::Colour(0xff8a7a42);
    }
    return juce::Colour(0xff4b6671);
}

juce::Colour instrumentColour(Session::Instrument instrument)
{
    switch (instrument)
    {
        case Session::Instrument::FourOsc:    return juce::Colour(0xff3d6f8b);
        case Session::Instrument::ThetaWave:  return juce::Colour(0xff574ec8);
        case Session::Instrument::Drums:      return juce::Colour(0xff738044);
        case Session::Instrument::Utility:    return juce::Colour(0xff56636c);
    }
    return juce::Colour(0xff4b6671);
}

juce::Colour nextClipColour(juce::Colour current)
{
    static constexpr juce::uint32 palette[] {
        0xff3d6f8b, 0xff738044, 0xff8d5f42, 0xff7a5b8f,
        0xff9b4f67, 0xff4b7f68, 0xff8a7a42, 0xff56636c
    };
    int closest = -1;
    for (int i = 0; i < static_cast<int>(std::size(palette)); ++i)
        if (current == juce::Colour(palette[i]))
        {
            closest = i;
            break;
        }
    return juce::Colour(palette[static_cast<size_t>((closest + 1) % static_cast<int>(std::size(palette)))]);
}

bool effectTypeAndName(Session::AudioEffect effect, const char*& type, juce::String& name)
{
    switch (effect)
    {
        case Session::AudioEffect::Equaliser:  type = te::EqualiserPlugin::xmlTypeName;  name = "EQ"; break;
        case Session::AudioEffect::Reverb:     type = te::ReverbPlugin::xmlTypeName;     name = "Reverb"; break;
        case Session::AudioEffect::Delay:      type = te::DelayPlugin::xmlTypeName;      name = "Delay"; break;
        case Session::AudioEffect::Compressor: type = te::CompressorPlugin::xmlTypeName; name = "Compressor"; break;
        case Session::AudioEffect::ThetaSpace: type = ThetaSpaceDevice::xmlTypeName;     name = "Theta Space"; break;
        case Session::AudioEffect::ThetaBloom: type = ThetaBloomDevice::xmlTypeName;     name = "Theta Bloom"; break;
    }
    return type != nullptr;
}

void resetPluginList(te::PluginList* list)
{
    if (list == nullptr)
        return;
    for (auto* plugin : *list)
        if (plugin != nullptr)
        {
            plugin->midiPanic();
            plugin->reset();
        }
}

void fillMidiClip(te::MidiClip& clip, const PresetPattern& preset, juce::UndoManager& undoManager)
{
    clip.state.removeProperty(starterPlaceholderID, &undoManager);
    auto& sequence = clip.getSequence();
    sequence.removeAllNotes(&undoManager);
    for (int i = 0; i < preset.count; ++i)
        sequence.addNote(preset.notes[i].pitch, tracktion::core::BeatPosition::fromBeats(preset.notes[i].step * 0.25),
                         tracktion::core::BeatDuration::fromBeats(std::max(1, preset.notes[i].length) * 0.225), 100, 0,
                         &undoManager);
    clip.setName(preset.name);
}

double stepDurationBeats(int steps)
{
    return 4.0 / static_cast<double>(juce::jlimit(Session::defaultSteps, Session::steps, steps));
}

te::Plugin* findPlugin(te::AudioTrack& track, const juce::String& type)
{
    for (auto* plugin : track.pluginList)
        if (plugin != nullptr && plugin->getPluginType() == type)
            return plugin;
    return nullptr;
}

te::FourOscPlugin* findFourOsc(te::AudioTrack& track)
{
    return dynamic_cast<te::FourOscPlugin*>(findPlugin(track, te::FourOscPlugin::xmlTypeName));
}

ThetaWaveDevice* findThetaWave(te::AudioTrack& track)
{
    return dynamic_cast<ThetaWaveDevice*>(findPlugin(track, ThetaWaveDevice::xmlTypeName));
}

void setPluginParameter(te::AutomatableParameter::Ptr parameter, float value)
{
    if (parameter == nullptr)
        return;
    const auto range = parameter->getValueRange();
    parameter->setParameter(juce::jlimit(range.getStart(), range.getEnd(), value), juce::sendNotification);
}

void applyChordPadPatch(te::FourOscPlugin& synth, juce::UndoManager& undoManager)
{
    setPluginParameter(synth.ampAttack, 0.18f);
    setPluginParameter(synth.ampDecay, 0.55f);
    setPluginParameter(synth.ampSustain, 78.0f);
    setPluginParameter(synth.ampRelease, 0.85f);
    setPluginParameter(synth.ampVelocity, 55.0f);
    setPluginParameter(synth.filterAttack, 0.12f);
    setPluginParameter(synth.filterDecay, 0.45f);
    setPluginParameter(synth.filterSustain, 62.0f);
    setPluginParameter(synth.filterRelease, 0.75f);
    setPluginParameter(synth.filterFreq, 78.0f);
    setPluginParameter(synth.filterResonance, 12.0f);
    setPluginParameter(synth.filterAmount, 0.08f);
    setPluginParameter(synth.chorusSpeed, 0.65f);
    setPluginParameter(synth.chorusDepth, 7.5f);
    setPluginParameter(synth.chorusWidth, 0.9f);
    setPluginParameter(synth.chorusMix, 0.24f);
    setPluginParameter(synth.reverbSize, 0.48f);
    setPluginParameter(synth.reverbDamping, 0.62f);
    setPluginParameter(synth.reverbWidth, 0.95f);
    setPluginParameter(synth.reverbMix, 0.11f);
    setPluginParameter(synth.masterLevel, -9.0f);

    synth.state.setProperty("voiceMode", 2, &undoManager);
    synth.state.setProperty("voices", 32, &undoManager);
    synth.state.setProperty("filterType", 1, &undoManager);
    synth.state.setProperty("filterSlope", 12, &undoManager);
    synth.state.setProperty("chorusOn", true, &undoManager);
    synth.state.setProperty("reverbOn", true, &undoManager);
    synth.state.setProperty("distortionOn", false, &undoManager);

    if (synth.oscParams.size() >= 4)
    {
        setPluginParameter(synth.oscParams[0]->level, -9.0f);
        setPluginParameter(synth.oscParams[0]->detune, 0.02f);
        setPluginParameter(synth.oscParams[0]->spread, 45.0f);
        setPluginParameter(synth.oscParams[1]->level, -12.0f);
        setPluginParameter(synth.oscParams[1]->fineTune, -8.0f);
        setPluginParameter(synth.oscParams[1]->detune, 0.04f);
        setPluginParameter(synth.oscParams[1]->spread, -55.0f);
        setPluginParameter(synth.oscParams[2]->level, -16.0f);
        setPluginParameter(synth.oscParams[2]->tune, 12.0f);
        setPluginParameter(synth.oscParams[2]->fineTune, 5.0f);
        setPluginParameter(synth.oscParams[2]->spread, 70.0f);
        setPluginParameter(synth.oscParams[3]->level, -100.0f);
    }
}

void applySubBassPatch(te::FourOscPlugin& synth, juce::UndoManager& undoManager)
{
    setPluginParameter(synth.ampAttack, 0.006f);
    setPluginParameter(synth.ampDecay, 0.18f);
    setPluginParameter(synth.ampSustain, 88.0f);
    setPluginParameter(synth.ampRelease, 0.22f);
    setPluginParameter(synth.ampVelocity, 42.0f);
    setPluginParameter(synth.filterAttack, 0.0f);
    setPluginParameter(synth.filterDecay, 0.12f);
    setPluginParameter(synth.filterSustain, 55.0f);
    setPluginParameter(synth.filterRelease, 0.18f);
    setPluginParameter(synth.filterFreq, 48.0f);
    setPluginParameter(synth.filterResonance, 3.0f);
    setPluginParameter(synth.filterAmount, -0.03f);
    setPluginParameter(synth.chorusMix, 0.0f);
    setPluginParameter(synth.reverbMix, 0.0f);
    setPluginParameter(synth.legato, 55.0f);
    setPluginParameter(synth.masterLevel, -8.0f);

    synth.state.setProperty("voiceMode", 1, &undoManager);
    synth.state.setProperty("voices", 1, &undoManager);
    synth.state.setProperty("filterType", 1, &undoManager);
    synth.state.setProperty("filterSlope", 24, &undoManager);
    synth.state.setProperty("chorusOn", false, &undoManager);
    synth.state.setProperty("reverbOn", false, &undoManager);
    synth.state.setProperty("distortionOn", false, &undoManager);

    if (synth.oscParams.size() >= 4)
    {
        setPluginParameter(synth.oscParams[0]->level, -5.5f);
        setPluginParameter(synth.oscParams[0]->tune, 0.0f);
        setPluginParameter(synth.oscParams[0]->fineTune, 0.0f);
        setPluginParameter(synth.oscParams[0]->detune, 0.0f);
        setPluginParameter(synth.oscParams[0]->spread, 0.0f);
        setPluginParameter(synth.oscParams[1]->level, -19.0f);
        setPluginParameter(synth.oscParams[1]->tune, 12.0f);
        setPluginParameter(synth.oscParams[1]->fineTune, 0.0f);
        setPluginParameter(synth.oscParams[1]->detune, 0.0f);
        setPluginParameter(synth.oscParams[1]->spread, 0.0f);
        setPluginParameter(synth.oscParams[2]->level, -100.0f);
        setPluginParameter(synth.oscParams[3]->level, -100.0f);
    }
}

void applyReeseBassPatch(te::FourOscPlugin& synth, juce::UndoManager& undoManager)
{
    setPluginParameter(synth.ampAttack, 0.012f);
    setPluginParameter(synth.ampDecay, 0.45f);
    setPluginParameter(synth.ampSustain, 82.0f);
    setPluginParameter(synth.ampRelease, 0.38f);
    setPluginParameter(synth.ampVelocity, 48.0f);
    setPluginParameter(synth.filterAttack, 0.03f);
    setPluginParameter(synth.filterDecay, 0.55f);
    setPluginParameter(synth.filterSustain, 48.0f);
    setPluginParameter(synth.filterRelease, 0.28f);
    setPluginParameter(synth.filterFreq, 63.0f);
    setPluginParameter(synth.filterResonance, 18.0f);
    setPluginParameter(synth.filterAmount, 0.12f);
    setPluginParameter(synth.chorusSpeed, 0.35f);
    setPluginParameter(synth.chorusDepth, 4.5f);
    setPluginParameter(synth.chorusWidth, 0.72f);
    setPluginParameter(synth.chorusMix, 0.16f);
    setPluginParameter(synth.reverbMix, 0.0f);
    setPluginParameter(synth.legato, 85.0f);
    setPluginParameter(synth.masterLevel, -10.0f);

    synth.state.setProperty("voiceMode", 1, &undoManager);
    synth.state.setProperty("voices", 1, &undoManager);
    synth.state.setProperty("filterType", 1, &undoManager);
    synth.state.setProperty("filterSlope", 12, &undoManager);
    synth.state.setProperty("chorusOn", true, &undoManager);
    synth.state.setProperty("reverbOn", false, &undoManager);
    synth.state.setProperty("distortionOn", false, &undoManager);

    if (synth.oscParams.size() >= 4)
    {
        setPluginParameter(synth.oscParams[0]->level, -9.0f);
        setPluginParameter(synth.oscParams[0]->fineTune, -9.0f);
        setPluginParameter(synth.oscParams[0]->detune, 0.08f);
        setPluginParameter(synth.oscParams[0]->spread, -70.0f);
        setPluginParameter(synth.oscParams[1]->level, -9.0f);
        setPluginParameter(synth.oscParams[1]->fineTune, 9.0f);
        setPluginParameter(synth.oscParams[1]->detune, 0.08f);
        setPluginParameter(synth.oscParams[1]->spread, 70.0f);
        setPluginParameter(synth.oscParams[2]->level, -18.0f);
        setPluginParameter(synth.oscParams[2]->tune, -12.0f);
        setPluginParameter(synth.oscParams[2]->spread, 0.0f);
        setPluginParameter(synth.oscParams[3]->level, -100.0f);
    }
}

void applySynthPatch(SynthPatch patch, te::FourOscPlugin& synth, juce::UndoManager& undoManager)
{
    switch (patch)
    {
        case SynthPatch::Default:   break;
        case SynthPatch::ChordPad:  applyChordPadPatch(synth, undoManager); break;
        case SynthPatch::SubBass:   applySubBassPatch(synth, undoManager); break;
        case SynthPatch::ReeseBass: applyReeseBassPatch(synth, undoManager); break;
    }
}

void applyThetaWavePatch(Session::PatternPreset preset, ThetaWaveDevice& wave)
{
    const auto set = [&wave] (int parameterIndex, float value)
    {
        int active = 0;
        for (auto* parameter : wave.getAutomatableParameters())
        {
            if (parameter == nullptr || !parameter->isParameterActive())
                continue;
            if (active++ != parameterIndex)
                continue;
            const auto range = parameter->getValueRange();
            parameter->setParameter(juce::jlimit(range.getStart(), range.getEnd(), value), juce::sendNotification);
            return;
        }
    };

    if (preset == Session::PatternPreset::WavePad)
    {
        set(0, 0.38f);    // Position
        set(1, 0.72f);    // Shape
        set(2, 0.35f);    // Motion
        set(3, 5200.0f);  // Cutoff
        set(4, 0.22f);    // Env
        set(5, 4.0f);     // Drive
        set(6, 0.08f);    // Sub
        set(7, 0.18f);    // Resonance
        set(8, 0.22f);    // Attack
        set(9, 0.85f);    // Decay
        set(10, 0.78f);   // Sustain
        set(11, 1.15f);   // Release
        set(12, 3.0f);    // Unison
        set(13, 0.13f);   // Detune
        set(14, 0.78f);   // Width
        set(15, -12.0f);  // Output
    }
    else if (preset == Session::PatternPreset::WaveBass)
    {
        set(0, 0.58f);
        set(1, 0.34f);
        set(2, 0.12f);
        set(3, 2600.0f);
        set(4, -0.08f);
        set(5, 7.5f);
        set(6, 0.42f);
        set(7, 0.12f);
        set(8, 0.006f);
        set(9, 0.16f);
        set(10, 0.7f);
        set(11, 0.18f);
        set(12, 1.0f);
        set(13, 0.02f);
        set(14, 0.18f);
        set(15, -10.0f);
    }
    else if (preset == Session::PatternPreset::WavePluck)
    {
        set(0, 0.64f);
        set(1, 0.82f);
        set(2, 0.55f);
        set(3, 4200.0f);
        set(4, 0.65f);
        set(5, 8.0f);
        set(6, 0.1f);
        set(7, 0.28f);
        set(8, 0.004f);
        set(9, 0.24f);
        set(10, 0.18f);
        set(11, 0.22f);
        set(12, 2.0f);
        set(13, 0.06f);
        set(14, 0.52f);
        set(15, -11.0f);
    }
}

bool trackHasPlugin(te::AudioTrack& track, const juce::String& type)
{
    return findPlugin(track, type) != nullptr;
}

DrumDevice* findDrumDevice(te::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (auto* drums = dynamic_cast<DrumDevice*>(plugin))
            return drums;
    return nullptr;
}

Session::Instrument activeTrackInstrument(te::AudioTrack& track)
{
    if (auto* drums = findDrumDevice(track))
        if (drums->isEnabled())
            return Session::Instrument::Drums;
    if (auto* wave = findThetaWave(track))
        if (wave->isEnabled())
            return Session::Instrument::ThetaWave;
    return Session::Instrument::FourOsc;
}

te::AutomatableParameter* activeParameterAt(te::Plugin& plugin, int index)
{
    int active = 0;
    for (auto* parameter : plugin.getAutomatableParameters())
        if (parameter != nullptr && parameter->isParameterActive())
        {
            if (active == index)
                return parameter;
            ++active;
        }
    return nullptr;
}

te::AutomatableParameter* fourOscMacroParameterAt(te::FourOscPlugin& synth, int index)
{
    switch (index)
    {
        case 0: return synth.ampAttack;
        case 1: return synth.ampDecay;
        case 2: return synth.ampSustain;
        case 3: return synth.ampRelease;
        case 4: return synth.filterFreq;
        case 5: return synth.legato;
    }
    return nullptr;
}

juce::String fourOscMacroName(int index)
{
    switch (index)
    {
        case 0: return "Attack";
        case 1: return "Decay";
        case 2: return "Sustain";
        case 3: return "Release";
        case 4: return "Filter";
        case 5: return "Glide";
    }
    return {};
}

juce::String formatFourOscMacroValue(int index, float value, te::AutomatableParameter& parameter)
{
    switch (index)
    {
        case 0:
        case 1:
        case 3:
            return juce::String(juce::roundToInt(value * 1000.0f)) + "ms";
        default:
            return parameter.getCurrentValueAsStringWithLabel();
    }
}

te::AutomatableParameter* exposedParameterAt(te::Plugin& plugin, int index)
{
    if (auto* synthPlugin = dynamic_cast<te::FourOscPlugin*>(&plugin))
        return fourOscMacroParameterAt(*synthPlugin, index);
    return activeParameterAt(plugin, index);
}

tracktion::core::TimeRange firstFreeDuplicateRange(te::Clip& source)
{
    const auto old = source.getPosition().time;
    const auto length = old.getLength();
    auto start = old.getEnd();
    auto* owner = source.getClipTrack();
    if (owner == nullptr)
        return {start, start + length};

    bool moved = false;
    do
    {
        moved = false;
        const tracktion::core::TimeRange candidate {start, start + length};
        for (auto* clip : owner->getClips())
        {
            if (clip == nullptr || clip == &source)
                continue;
            const auto occupied = clip->getPosition().time;
            if (candidate.overlaps(occupied))
            {
                start = occupied.getEnd();
                moved = true;
                break;
            }
        }
    }
    while (moved);

    return {start, start + length};
}

juce::Result ensurePlugin(te::Edit& edit, te::AudioTrack& track, const juce::String& type,
                          int insertIndex, te::Plugin*& plugin, bool& changed)
{
    if ((plugin = findPlugin(track, type)) != nullptr)
        return juce::Result::ok();

    auto created = edit.getPluginCache().createNewPlugin(type, {});
    if (created == nullptr)
        return juce::Result::fail("The target track device could not be created.");

    plugin = created.get();
    track.pluginList.insertPlugin(created, juce::jlimit(0, track.pluginList.size(), insertIndex), nullptr);
    changed = true;
    return juce::Result::ok();
}

juce::Result switchTrackInstrument(te::Edit& edit, te::AudioTrack& track, Session::Instrument instrument, bool& changed)
{
    te::Plugin* selected = nullptr;
    const auto selectedType = instrument == Session::Instrument::Drums ? juce::String(DrumDevice::xmlTypeName)
        : instrument == Session::Instrument::ThetaWave ? juce::String(ThetaWaveDevice::xmlTypeName)
        : juce::String(te::FourOscPlugin::xmlTypeName);
    auto result = ensurePlugin(edit, track, selectedType, 0, selected, changed);
    if (result.failed())
        return result;

    if (auto* fourOsc = findPlugin(track, te::FourOscPlugin::xmlTypeName))
        if (fourOsc->isEnabled() != (instrument == Session::Instrument::FourOsc))
        {
            fourOsc->setEnabled(instrument == Session::Instrument::FourOsc);
            changed = true;
        }

    if (auto* wave = findPlugin(track, ThetaWaveDevice::xmlTypeName))
        if (wave->isEnabled() != (instrument == Session::Instrument::ThetaWave))
        {
            wave->setEnabled(instrument == Session::Instrument::ThetaWave);
            changed = true;
        }

    if (auto* drumDevice = findDrumDevice(track))
        if (drumDevice->isEnabled() != (instrument == Session::Instrument::Drums))
        {
            drumDevice->setEnabled(instrument == Session::Instrument::Drums);
            changed = true;
        }

    if (selected != nullptr && !selected->isEnabled())
    {
        selected->setEnabled(true);
        changed = true;
    }

    return juce::Result::ok();
}
}

Session::Session()
{
    engine.getPluginManager().createBuiltInType<UtilityDevice>();
    engine.getPluginManager().createBuiltInType<DrumDevice>();
    engine.getPluginManager().createBuiltInType<ThetaSpaceDevice>();
    engine.getPluginManager().createBuiltInType<ThetaBloomDevice>();
    engine.getPluginManager().createBuiltInType<ThetaArpDevice>();
    engine.getPluginManager().createBuiltInType<ThetaWaveDevice>();
    edit = te::createEmptyEdit(engine, {});
    edit->state.setProperty("thetaFormatVersion", 1, nullptr);
    edit->state.setProperty(editorStepsID, editorSteps, nullptr);
    edit->tempoSequence.getTempo(0)->setBpm(120.0);
    edit->ensureNumberOfAudioTracks(2);
    auto* track = te::getAudioTracks(*edit)[0];
    track->setName("Pattern synth");
    auto synthPlugin = edit->getPluginCache().createNewPlugin(te::FourOscPlugin::xmlTypeName, {});
    synth = dynamic_cast<te::FourOscPlugin*>(synthPlugin.get());
    track->pluginList.insertPlugin(synthPlugin, 0, nullptr);
    auto drumPlugin = edit->getPluginCache().createNewPlugin(DrumDevice::xmlTypeName, {});
    drums = dynamic_cast<DrumDevice*>(drumPlugin.get());
    drums->setEnabled(false);
    track->pluginList.insertPlugin(drumPlugin, 1, nullptr);
    auto wavePlugin = edit->getPluginCache().createNewPlugin(ThetaWaveDevice::xmlTypeName, {});
    thetaWave = dynamic_cast<ThetaWaveDevice*>(wavePlugin.get());
    if (thetaWave != nullptr)
    {
        thetaWave->setEnabled(false);
        track->pluginList.insertPlugin(wavePlugin, 2, nullptr);
    }
    auto device = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    utility = dynamic_cast<UtilityDevice*>(device.get());
    track->pluginList.insertPlugin(device, track->pluginList.size(), nullptr);
    utility->gain().setParameter(-12.0f, juce::dontSendNotification);
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
    patternClip = track->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
    if (patternClip != nullptr)
    {
        patternClip->setColour(presetColour(PatternPreset::WarmPulse));
        patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
    }
    patternClipID = patternClip->itemID;
    auto* audioTrack = te::getAudioTracks(*edit)[1];
    audioTrack->setName("Audio 1");
    auto audioDevice = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    audioUtility = dynamic_cast<UtilityDevice*>(audioDevice.get());
    audioTrack->pluginList.insertPlugin(audioDevice, 0, nullptr);
    refreshLoop();
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
}

void Session::panicReset()
{
    te::TransportControl::stopAllTransports(engine, false, true);
    auto& transport = edit->getTransport();
    transport.stop(false, true);
    transport.setPosition({});

    for (auto* track : te::getAudioTracks(*edit))
    {
        resetPluginList(&track->pluginList);
        for (auto* clip : track->getClips())
            resetPluginList(clip->getPluginList());
    }

    transport.freePlaybackContext();
    engine.getDeviceManager().deviceManager.closeAudioDevice();
    engine.getDeviceManager().deviceManager.restartLastAudioDevice();
    transport.ensureContextAllocated(true);
    sendSynchronousChangeMessage();
}

bool Session::hasNote(int step, int pitch) const
{
    if (step < 0 || step >= editorSteps || pitch < 0 || pitch > 127)
        return false;
    const auto beat = step * stepDurationBeats(editorSteps);
    for (auto* note : pattern().getSequence().getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
            return true;
    return false;
}

void Session::beginNoteGesture(juce::String actionName) { edit->getUndoManager().beginNewTransaction(actionName); }
void Session::endNoteGesture() { edit->getUndoManager().beginNewTransaction(); }

void Session::setEditorStepCount(int newSteps)
{
    const auto clamped = juce::jlimit(defaultSteps, steps, newSteps);
    if (editorSteps == clamped)
        return;
    editorSteps = clamped;
    edit->state.setProperty(editorStepsID, editorSteps, &edit->getUndoManager());
    markModified();
    sendSynchronousChangeMessage();
}

void Session::setNote(int step, int pitch, bool enabled)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (step < 0 || step >= editorSteps || pitch < 0 || pitch > 127)
        return;
    const auto beat = step * stepDurationBeats(editorSteps);
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
        {
            if (!enabled)
            {
                sequence.removeNote(*note, undoManager);
                markModified();
            }
            sendSynchronousChangeMessage();
            return;
        }
    if (enabled)
    {
        pattern().state.removeProperty(starterPlaceholderID, undoManager);
        const auto duration = std::max(0.02, stepDurationBeats(editorSteps) * 0.9);
        sequence.addNote(pitch, tracktion::core::BeatPosition::fromBeats(beat),
                         tracktion::core::BeatDuration::fromBeats(duration), 100, 0, undoManager);
        markModified();
    }
    sendSynchronousChangeMessage();
}

juce::Result Session::moveNote(int sourceStep, int sourcePitch, int targetStep, int targetPitch)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (sourceStep < 0 || sourceStep >= editorSteps || targetStep < 0 || targetStep >= editorSteps
        || sourcePitch < 0 || sourcePitch > 127
        || targetPitch < 0 || targetPitch > 127)
        return juce::Result::fail("Move notes inside the visible pitch grid.");
    if (sourceStep == targetStep && sourcePitch == targetPitch)
        return juce::Result::ok();

    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    auto* moving = static_cast<te::MidiNote*>(nullptr);
    const auto sourceBeat = sourceStep * stepDurationBeats(editorSteps);
    const auto targetBeat = targetStep * stepDurationBeats(editorSteps);
    for (auto* note : sequence.getNotes())
    {
        const auto noteBeat = note->getStartBeat().inBeats();
        if (std::abs(noteBeat - targetBeat) < 0.0001 && note->getNoteNumber() == targetPitch)
            return juce::Result::fail("That note cell is already occupied.");
        if (std::abs(noteBeat - sourceBeat) < 0.0001 && note->getNoteNumber() == sourcePitch)
            moving = note;
    }
    if (moving == nullptr)
        return juce::Result::fail("Select a note to move.");

    const auto targetStart = tracktion::core::BeatPosition::fromBeats(targetBeat);
    const auto maximumLength = tracktion::core::BeatDuration::fromBeats(4.0 - targetStart.inBeats());
    if (maximumLength <= tracktion::core::BeatDuration())
        return juce::Result::fail("Move notes inside the clip.");
    moving->setStartAndLength(targetStart, std::min(moving->getLengthBeats(), maximumLength), undoManager);
    moving->setNoteNumber(targetPitch, undoManager);
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::clearPattern()
{
    if (pattern().getSequence().getNumNotes() == 0) return;
    edit->getUndoManager().beginNewTransaction("Clear pattern");
    pattern().getSequence().removeAllNotes(&edit->getUndoManager());
    pattern().state.setProperty(starterPlaceholderID, true, &edit->getUndoManager());
    markModified();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
}

void Session::applyPatternPreset(PatternPreset preset)
{
    const auto data = presetPattern(preset);
    edit->getUndoManager().beginNewTransaction("Load " + data.name);
    if (data.useThetaWave)
    {
        bool instrumentChanged = false;
        juce::ignoreUnused(switchTrackInstrument(*edit, *te::getAudioTracks(*edit)[0], Instrument::ThetaWave, instrumentChanged));
        edit->state.setProperty("thetaPatternInstrument", "wave", &edit->getUndoManager());
    }
    else
    {
        setPatternInstrument(data.useDrums);
    }
    if (data.synthPatch != SynthPatch::Default)
        if (auto* fourOsc = findFourOsc(*te::getAudioTracks(*edit)[0]))
            applySynthPatch(data.synthPatch, *fourOsc, edit->getUndoManager());
    if (data.useThetaWave)
        if (auto* wave = findThetaWave(*te::getAudioTracks(*edit)[0]))
            applyThetaWavePatch(preset, *wave);
    fillMidiClip(pattern(), data, edit->getUndoManager());
    markModified();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
}

juce::Result Session::insertPatternPreset(PatternPreset preset, int trackIndex, double startSeconds)
{
    if (!std::isfinite(startSeconds) || startSeconds < 0.0)
        return juce::Result::fail("Invalid pattern drop position.");
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop clips on a track lane.");
    const auto data = presetPattern(preset);
    const auto start = tracktion::core::TimePosition::fromSeconds(startSeconds);
    const auto end = start + tracktion::core::TimeDuration::fromSeconds(
        edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0)).inSeconds());
    edit->getUndoManager().beginNewTransaction("Add " + data.name);
    auto* track = tracks[trackIndex];
    if (trackIndex == 0)
    {
        if (data.useThetaWave)
        {
            bool instrumentChanged = false;
            const auto result = switchTrackInstrument(*edit, *track, Instrument::ThetaWave, instrumentChanged);
            if (result.failed())
                return result;
            edit->state.setProperty("thetaPatternInstrument", "wave", &edit->getUndoManager());
        }
        else
        {
            setPatternInstrument(data.useDrums);
        }
    }
    else
    {
        bool instrumentChanged = false;
        const auto result = switchTrackInstrument(*edit, *track,
                                                  data.useDrums ? Instrument::Drums
                                                      : data.useThetaWave ? Instrument::ThetaWave
                                                      : Instrument::FourOsc,
                                                  instrumentChanged);
        if (result.failed())
            return result;
    }
    if (data.synthPatch != SynthPatch::Default)
        if (auto* fourOsc = findFourOsc(*track))
            applySynthPatch(data.synthPatch, *fourOsc, edit->getUndoManager());
    if (data.useThetaWave)
        if (auto* wave = findThetaWave(*track))
            applyThetaWavePatch(preset, *wave);
    auto clip = track->insertMIDIClip(data.name, {start, end}, nullptr);
    if (clip == nullptr)
        return juce::Result::fail("The pattern clip could not be added.");
    clip->setColour(presetColour(preset));
    fillMidiClip(*clip, data, edit->getUndoManager());
    refreshLoop();
    markModified();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::insertInstrumentClip(Instrument instrument, int trackIndex, double startSeconds)
{
    if (!std::isfinite(startSeconds) || startSeconds < 0.0)
        return juce::Result::fail("Invalid instrument drop position.");
    if (instrument == Instrument::Utility)
        return addInstrument(instrument, trackIndex);

    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop instruments on a track lane.");

    const auto useDrums = instrument == Instrument::Drums;
    const auto name = useDrums ? juce::String("Theta Drums")
        : instrument == Instrument::ThetaWave ? juce::String("Theta Wave")
        : juce::String("4OSC synth");
    const auto start = tracktion::core::TimePosition::fromSeconds(startSeconds);
    const auto end = start + tracktion::core::TimeDuration::fromSeconds(
        edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0)).inSeconds());
    auto* track = tracks[trackIndex];

    edit->getUndoManager().beginNewTransaction("Add " + name);
    bool instrumentChanged = false;
    const auto result = switchTrackInstrument(*edit, *track, instrument, instrumentChanged);
    if (result.failed())
        return result;
    if (trackIndex == 0)
        edit->state.setProperty("thetaPatternInstrument",
                                useDrums ? "drums" : instrument == Instrument::ThetaWave ? "wave" : "synth",
                                &edit->getUndoManager());

    auto clip = track->insertMIDIClip(name, {start, end}, nullptr);
    if (clip == nullptr)
        return juce::Result::fail("The instrument clip could not be added.");
    clip->setColour(instrumentColour(instrument));
    patternClip = clip.get();
    patternClipID = patternClip->itemID;
    refreshLoop();
    markModified();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::setPatternInstrument(bool useDrums)
{
    if (synth) synth->setEnabled(!useDrums);
    if (thetaWave) thetaWave->setEnabled(false);
    if (drums) drums->setEnabled(useDrums);
    edit->state.setProperty("thetaPatternInstrument", useDrums ? "drums" : "synth", &edit->getUndoManager());
}

bool Session::isPatternDrums() const
{
    auto* track = patternClip != nullptr ? patternClip->getClipTrack() : nullptr;
    if (track == nullptr) return drums != nullptr && drums->isEnabled();
    auto* audioTrack = dynamic_cast<te::AudioTrack*>(track);
    if (audioTrack == nullptr) return false;
    if (auto* drumDevice = findDrumDevice(*audioTrack))
        return drumDevice->isEnabled();
    return false;
}

juce::Result Session::selectPatternClip(te::EditItemID id)
{
    auto* midi = dynamic_cast<te::MidiClip*>(findClip(id));
    if (midi == nullptr) return juce::Result::fail("Select a MIDI clip to edit notes.");
    patternClip = midi;
    patternClipID = id;
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::addAudioEffect(AudioEffect effect, int trackIndex)
{
    const char* type = nullptr;
    juce::String name;
    if (!effectTypeAndName(effect, type, name))
        return juce::Result::fail("That audio effect could not be created.");

    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop audio effects on a track.");

    auto* track = tracks[trackIndex];
    edit->getUndoManager().beginNewTransaction("Add " + name);
    auto plugin = edit->getPluginCache().createNewPlugin(type, {});
    if (plugin == nullptr)
        return juce::Result::fail(name + " could not be created.");
    track->pluginList.insertPlugin(plugin, track->pluginList.size(), nullptr);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::addClipAudioEffect(AudioEffect effect, te::EditItemID clipID)
{
    const char* type = nullptr;
    juce::String name;
    if (!effectTypeAndName(effect, type, name))
        return juce::Result::fail("That audio effect could not be created.");

    auto* clip = dynamic_cast<te::AudioClipBase*>(findClip(clipID));
    if (clip == nullptr)
        return juce::Result::fail("Clip effects can be dropped on audio clips.");
    if (!clip->canHaveEffects())
        return juce::Result::fail("This audio clip cannot host effects while warped or reversed.");

    auto plugin = edit->getPluginCache().createNewPlugin(type, {});
    if (plugin == nullptr)
        return juce::Result::fail(name + " could not be created.");
    if (!plugin->canBeAddedToClip())
        return juce::Result::fail(name + " cannot be added to a clip.");

    edit->getUndoManager().beginNewTransaction("Add " + name + " to clip");
    clip->getPluginList()->insertPlugin(plugin, clip->getPluginList()->size(), nullptr);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::addInstrument(Instrument instrument, int trackIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop instruments on a track.");

    auto* track = tracks[trackIndex];
    const char* type = nullptr;
    juce::String name;
    switch (instrument)
    {
        case Instrument::FourOsc: type = te::FourOscPlugin::xmlTypeName; name = "4OSC"; break;
        case Instrument::ThetaWave: type = ThetaWaveDevice::xmlTypeName; name = "Theta Wave"; break;
        case Instrument::Drums:   type = DrumDevice::xmlTypeName;        name = "Theta Drums"; break;
        case Instrument::Utility: type = UtilityDevice::xmlTypeName;     name = "Utility"; break;
    }

    edit->getUndoManager().beginNewTransaction("Add " + name);
    bool changed = false;
    if (instrument == Instrument::Utility)
    {
        te::Plugin* plugin = nullptr;
        const auto result = ensurePlugin(*edit, *track, type, track->pluginList.size(), plugin, changed);
        if (result.failed())
            return juce::Result::fail(name + " could not be created.");
    }
    else
    {
        const auto result = switchTrackInstrument(*edit, *track, instrument, changed);
        if (result.failed())
            return juce::Result::fail(name + " could not be created.");
    }
    edit->getUndoManager().beginNewTransaction();
    if (changed)
        markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::addMidiEffect(MidiEffect effect, int trackIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop MIDI FX on an instrument track.");

    const char* type = nullptr;
    juce::String name;
    switch (effect)
    {
        case MidiEffect::ThetaArp: type = ThetaArpDevice::xmlTypeName; name = "Theta Arp"; break;
    }

    auto* track = tracks[trackIndex];
    edit->getUndoManager().beginNewTransaction("Add " + name);
    auto plugin = edit->getPluginCache().createNewPlugin(type, {});
    if (plugin == nullptr)
        return juce::Result::fail(name + " could not be created.");

    int insertIndex = 0;
    for (int i = 0; i < track->pluginList.size(); ++i)
    {
        auto* existing = track->pluginList[i];
        if (existing != nullptr && (existing->getPluginType() == te::FourOscPlugin::xmlTypeName
                                    || existing->getPluginType() == DrumDevice::xmlTypeName))
        {
            insertIndex = i;
            break;
        }
        insertIndex = i + 1;
    }

    track->pluginList.insertPlugin(plugin, juce::jlimit(0, track->pluginList.size(), insertIndex), nullptr);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

int Session::trackCount() const
{
    return te::getAudioTracks(*edit).size();
}

juce::String Session::trackName(int track) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return {};
    return tracks[track]->getName();
}

juce::Result Session::addAudioTrack()
{
    const auto tracks = te::getAudioTracks(*edit);
    edit->getUndoManager().beginNewTransaction("Add audio track");
    auto newTrack = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr, false);
    if (newTrack == nullptr)
        return juce::Result::fail("Could not create audio track.");
    newTrack->setName("Audio " + juce::String(tracks.size()));
    auto audioDevice = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    newTrack->pluginList.insertPlugin(audioDevice, 0, nullptr);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::removeAudioTrack(int track)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (track <= 0 || !juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("Select an audio track to remove.");
    if (tracks.size() <= 2)
        return juce::Result::fail("Keep at least one audio track.");
    const auto removingEditedPatternTrack = patternClip != nullptr && patternClip->getClipTrack() == tracks[track];
    edit->getUndoManager().beginNewTransaction("Remove audio track");
    edit->deleteTrack(tracks[track]);
    if (removingEditedPatternTrack)
    {
        patternClip = nullptr;
        patternClipID = {};
        ensureEditablePatternClip();
    }
    if (track == 1)
        audioUtility = nullptr;
    const auto refreshed = te::getAudioTracks(*edit);
    if (audioUtility == nullptr && refreshed.size() > 1)
        for (auto plugin : refreshed[1]->pluginList)
            if (auto* device = dynamic_cast<UtilityDevice*>(plugin))
                audioUtility = device;
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

std::vector<Session::DeviceSlot> Session::deviceSlots(int track) const
{
    std::vector<DeviceSlot> slots;
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return slots;
    for (auto* plugin : tracks[track]->pluginList)
    {
        if (plugin == nullptr) continue;
        const auto type = plugin->getPluginType();
        const auto coreStarterDevice = track == 0 && (type == UtilityDevice::xmlTypeName
            || type == te::FourOscPlugin::xmlTypeName || type == DrumDevice::xmlTypeName);
        slots.push_back({plugin->getDisplayName(), type, plugin->isEnabled(),
                         !coreStarterDevice && type != UtilityDevice::xmlTypeName});
    }
    return slots;
}

std::vector<Session::DeviceParameter> Session::deviceParameters(int track, int slot) const
{
    std::vector<DeviceParameter> parameters;
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return parameters;
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return parameters;

    if (auto* synthPlugin = dynamic_cast<te::FourOscPlugin*>(plugin))
    {
        for (int i = 0; i < 6; ++i)
            if (auto* parameter = fourOscMacroParameterAt(*synthPlugin, i))
            {
                const auto range = parameter->getValueRange();
                if (!std::isfinite(range.getStart()) || !std::isfinite(range.getEnd()) || range.getLength() <= 0.0f)
                    continue;
                parameters.push_back({fourOscMacroName(i),
                                      formatFourOscMacroValue(i, parameter->getCurrentValue(), *parameter),
                                      parameter->getCurrentValue(),
                                      range.getStart(),
                                      range.getEnd(),
                                      parameter->isDiscrete()});
            }
        return parameters;
    }

    for (auto* parameter : plugin->getAutomatableParameters())
    {
        if (parameter == nullptr || !parameter->isParameterActive())
            continue;
        const auto range = parameter->getValueRange();
        if (!std::isfinite(range.getStart()) || !std::isfinite(range.getEnd()) || range.getLength() <= 0.0f)
            continue;
        parameters.push_back({parameter->getParameterShortName(18),
                              parameter->getCurrentValueAsStringWithLabel(),
                              parameter->getCurrentValue(),
                              range.getStart(),
                              range.getEnd(),
                              parameter->isDiscrete()});
    }
    return parameters;
}

juce::Result Session::beginDeviceParameterGesture(int track, int slot, int parameterIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return juce::Result::fail("Select a device first.");
    auto* parameter = exposedParameterAt(*plugin, parameterIndex);
    if (parameter == nullptr) return juce::Result::fail("Select a parameter first.");
    parameter->parameterChangeGestureBegin();
    return juce::Result::ok();
}

juce::Result Session::setDeviceParameter(int track, int slot, int parameterIndex, float value)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return juce::Result::fail("Select a device first.");
    auto* parameter = exposedParameterAt(*plugin, parameterIndex);
    if (parameter == nullptr) return juce::Result::fail("Select a parameter first.");
    const auto range = parameter->getValueRange();
    const auto next = juce::jlimit(range.getStart(), range.getEnd(), value);
    parameter->setParameter(next, juce::sendNotification);
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::endDeviceParameterGesture(int track, int slot, int parameterIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return juce::Result::fail("Select a device first.");
    auto* parameter = exposedParameterAt(*plugin, parameterIndex);
    if (parameter == nullptr) return juce::Result::fail("Select a parameter first.");
    parameter->parameterChangeGestureEnd();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::toggleDeviceEnabled(int track, int slot)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return juce::Result::fail("Select a device first.");
    edit->getUndoManager().beginNewTransaction(plugin->isEnabled() ? "Bypass device" : "Enable device");
    plugin->setEnabled(!plugin->isEnabled());
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::deleteDevice(int track, int slot)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a removable device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    const auto type = plugin->getPluginType();
    const auto coreStarterDevice = track == 0 && (type == UtilityDevice::xmlTypeName
        || type == te::FourOscPlugin::xmlTypeName || type == DrumDevice::xmlTypeName);
    if (plugin == nullptr || coreStarterDevice || type == UtilityDevice::xmlTypeName)
        return juce::Result::fail("Core devices stay in the starter track chain.");
    edit->getUndoManager().beginNewTransaction("Delete device");
    plugin->removeFromParent();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::undo()
{
    refreshAfterUndoRedo(edit->getUndoManager().undo());
}

void Session::redo()
{
    refreshAfterUndoRedo(edit->getUndoManager().redo());
}

void Session::refreshAfterUndoRedo(bool changed)
{
    if (changed) markModified();
    editorSteps = juce::jlimit(defaultSteps, steps, static_cast<int>(edit->state.getProperty(editorStepsID, defaultSteps)));
    ensureEditablePatternClip();
    edit->tempoSequence.updateTempoData();
    refreshLoop();
    if (changed && edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
}

void Session::ensureEditablePatternClip()
{
    if (auto* midi = dynamic_cast<te::MidiClip*>(findClip(patternClipID)))
    {
        patternClip = midi;
        return;
    }

    const auto tracks = te::getAudioTracks(*edit);
    if (!tracks.isEmpty())
        for (auto* clip : tracks[0]->getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
            {
                patternClip = midi;
                patternClipID = midi->itemID;
                return;
            }

    if (!tracks.isEmpty())
    {
        const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
        patternClip = tracks[0]->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
        if (patternClip != nullptr)
        {
            patternClip->setColour(presetColour(PatternPreset::WarmPulse));
            patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
            patternClipID = patternClip->itemID;
        }
    }
}

juce::ValueTree Session::projectSnapshot()
{
    edit->flushState();
    auto snapshot = edit->state.createCopy();
    snapshot.setProperty("thetaSnapshotRevision", changeRevision, nullptr);
    return snapshot;
}

void Session::markModified()
{
    ++changeRevision;
    edit->markAsChanged();
}

juce::Result Session::restoreProject(const juce::ValueTree& state, const juce::File& file)
{
    if (!state.hasType(te::IDs::EDIT) || static_cast<int>(state.getProperty("thetaFormatVersion")) != 1)
        return juce::Result::fail("This is not a supported Theta native project.");
    auto candidate = te::loadEditFromState(engine, state.createCopy());
    if (!candidate) return juce::Result::fail("The project could not be loaded.");
    candidate->editFileRetriever = [file] { return file; };
    const auto tracks = te::getAudioTracks(*candidate);
    if (tracks.size() < 2)
        return juce::Result::fail("This editor requires a pattern track and at least one audio track.");
    te::MidiClip* nextPattern = nullptr;
    UtilityDevice* nextUtility = nullptr;
    UtilityDevice* nextAudioUtility = nullptr;
    te::FourOscPlugin* nextSynth = nullptr;
    ThetaWaveDevice* nextThetaWave = nullptr;
    DrumDevice* nextDrums = nullptr;
    for (auto* clip : tracks[0]->getClips())
        if (auto* midi = dynamic_cast<te::MidiClip*>(clip)) nextPattern = midi;
    for (auto plugin : tracks[0]->pluginList)
    {
        if (auto* device = dynamic_cast<UtilityDevice*>(plugin)) nextUtility = device;
        if (auto* device = dynamic_cast<te::FourOscPlugin*>(plugin)) nextSynth = device;
        if (auto* device = dynamic_cast<ThetaWaveDevice*>(plugin)) nextThetaWave = device;
        if (auto* device = dynamic_cast<DrumDevice*>(plugin)) nextDrums = device;
    }
    for (auto plugin : tracks[1]->pluginList)
        if (auto* device = dynamic_cast<UtilityDevice*>(plugin)) nextAudioUtility = device;
    if (!nextPattern || !nextUtility || !nextSynth)
        return juce::Result::fail("The project is missing its pattern or synth devices.");
    if (!nextDrums)
    {
        auto device = candidate->getPluginCache().createNewPlugin(DrumDevice::xmlTypeName, {});
        nextDrums = dynamic_cast<DrumDevice*>(device.get());
        if (!nextDrums) return juce::Result::fail("The drum device could not be created.");
        nextDrums->setEnabled(false);
        tracks[0]->pluginList.insertPlugin(device, 1, nullptr);
    }
    if (!nextThetaWave)
    {
        auto device = candidate->getPluginCache().createNewPlugin(ThetaWaveDevice::xmlTypeName, {});
        nextThetaWave = dynamic_cast<ThetaWaveDevice*>(device.get());
        if (!nextThetaWave) return juce::Result::fail("The wavetable device could not be created.");
        nextThetaWave->setEnabled(false);
        tracks[0]->pluginList.insertPlugin(device, 2, nullptr);
    }
    if (!nextAudioUtility)
    {
        auto device = candidate->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
        nextAudioUtility = dynamic_cast<UtilityDevice*>(device.get());
        tracks[1]->pluginList.insertPlugin(device, 0, nullptr);
    }
    listeners.call(&Listener::editWillChange);
    stop();
    edit = std::move(candidate);
    editorSteps = juce::jlimit(defaultSteps, steps, static_cast<int>(edit->state.getProperty(editorStepsID, defaultSteps)));
    patternClip = nextPattern;
    patternClipID = patternClip->itemID;
    utility = nextUtility;
    audioUtility = nextAudioUtility;
    synth = nextSynth;
    thetaWave = nextThetaWave;
    drums = nextDrums;
    const auto patternInstrument = edit->state.getProperty("thetaPatternInstrument").toString();
    if (patternInstrument == "wave")
    {
        bool instrumentChanged = false;
        juce::ignoreUnused(switchTrackInstrument(*edit, *tracks[0], Instrument::ThetaWave, instrumentChanged));
    }
    else
    {
        setPatternInstrument(patternInstrument == "drums");
    }
    projectFile = file;
    savedRevision = ++changeRevision;
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
    refreshLoop();
    listeners.call(&Listener::editDidChange);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::projectSaved(const juce::ValueTree& snapshot, const juce::File& file)
{
    projectFile = file;
    // Edits made while the worker wrote the snapshot must remain unsaved.
    if (static_cast<juce::int64>(snapshot.getProperty("thetaSnapshotRevision")) == changeRevision)
    {
        savedRevision = changeRevision;
        edit->resetChangedStatus();
    }
    sendSynchronousChangeMessage();
}

te::Clip* Session::findClip(te::EditItemID id) const
{
    for (auto* track : te::getAudioTracks(*edit))
        if (auto* clip = track->findClipForID(id))
            return clip;
    return nullptr;
}

te::WaveAudioClip* Session::findAudioClip(te::EditItemID id) const
{
    return dynamic_cast<te::WaveAudioClip*>(findClip(id));
}

bool Session::shouldShowClipInArrangement(te::Clip& clip) const
{
    auto* midi = dynamic_cast<te::MidiClip*>(&clip);
    if (midi == nullptr)
        return true;
    return !static_cast<bool>(clip.state.getProperty(starterPlaceholderID, false))
        || midi->getSequence().getNumNotes() > 0;
}

juce::Result Session::editClip(te::EditItemID id, ClipGeometry next, ClipGesture gesture, int targetTrack)
{
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip first.");
    if (!std::isfinite(next.start) || !std::isfinite(next.end) || !std::isfinite(next.offset)
        || next.start < 0.0 || next.end <= next.start || next.offset < -1.0e-8
        || next.end > te::Edit::getMaximumEditEnd().inSeconds())
        return juce::Result::fail("Invalid clip position.");
    const auto tracks = te::getAudioTracks(*edit);
    auto* oldTrack = clip->getClipTrack();
    const auto oldTrackIndex = tracks.indexOf(dynamic_cast<te::AudioTrack*>(oldTrack));
    const auto movingMidi = dynamic_cast<te::MidiClip*>(clip) != nullptr;
    const auto sourceInstrument = movingMidi && oldTrackIndex >= 0 ? activeTrackInstrument(*tracks[oldTrackIndex])
                                                                   : Instrument::Utility;
    if (targetTrack < 0 || gesture != ClipGesture::move)
        targetTrack = oldTrackIndex;
    if (targetTrack < 0 || targetTrack > tracks.size())
        return juce::Result::fail("Drop the clip on a track lane.");
    const auto old = clip->getPosition();
    if (std::abs(old.time.getStart().inSeconds() - next.start) < 1.0e-8
        && std::abs(old.time.getEnd().inSeconds() - next.end) < 1.0e-8
        && std::abs(old.offset.inSeconds() - next.offset) < 1.0e-8
        && targetTrack == oldTrackIndex)
        return juce::Result::ok();
    auto& transport = edit->getTransport();
    const auto wasPlaying = transport.isPlaying();
    const auto hadPlaybackContext = transport.isPlayContextActive();
    edit->getUndoManager().beginNewTransaction(gesture == ClipGesture::move ? "Move clip" : "Trim clip");
    if (targetTrack == tracks.size())
    {
        auto newTrack = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr, false);
        if (newTrack == nullptr)
            return juce::Result::fail("Could not create a track for the moved clip.");
        newTrack->setName("Audio " + juce::String(tracks.size()));
        auto audioDevice = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
        newTrack->pluginList.insertPlugin(audioDevice, 0, nullptr);
        targetTrack = tracks.size();
    }
    const auto refreshedTracks = te::getAudioTracks(*edit);
    if (targetTrack != oldTrackIndex)
    {
        auto* target = refreshedTracks[targetTrack];
        if (movingMidi)
        {
            bool instrumentChanged = false;
            const auto result = switchTrackInstrument(*edit, *target, sourceInstrument, instrumentChanged);
            if (result.failed())
                return result;
            if (targetTrack == 0)
                edit->state.setProperty("thetaPatternInstrument",
                                        sourceInstrument == Instrument::Drums ? "drums"
                                            : sourceInstrument == Instrument::ThetaWave ? "wave" : "synth",
                                        &edit->getUndoManager());
        }
        if (!clip->moveTo(*target))
            return juce::Result::fail("The clip could not be moved to that track.");
    }
    clip->setPosition({{tracktion::core::TimePosition::fromSeconds(next.start),
                       tracktion::core::TimePosition::fromSeconds(next.end)},
                       tracktion::core::TimeDuration::fromSeconds(std::max(0.0, next.offset))});
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (targetTrack != oldTrackIndex && (wasPlaying || hadPlaybackContext))
    {
        transport.freePlaybackContext();
        transport.ensureContextAllocated(true);
        if (wasPlaying)
            transport.play(true);
    }
    else if (wasPlaying)
    {
        edit->restartPlayback();
    }
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::splitClip(te::EditItemID id, double splitTimeSeconds)
{
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip to split.");
    if (!std::isfinite(splitTimeSeconds)) return juce::Result::fail("Invalid split position.");

    const auto old = clip->getPosition();
    const auto split = tracktion::core::TimePosition::fromSeconds(splitTimeSeconds);
    constexpr double minimumSeconds = 0.01;
    if (split <= old.time.getStart() + tracktion::core::TimeDuration::fromSeconds(minimumSeconds)
        || split >= old.time.getEnd() - tracktion::core::TimeDuration::fromSeconds(minimumSeconds))
        return juce::Result::fail("Move the playhead inside the selected clip before splitting.");

    edit->getUndoManager().beginNewTransaction("Split audio clip");
    auto* track = clip->getClipTrack();
    auto* right = track != nullptr ? track->splitClip(*clip, split) : nullptr;
    if (right == nullptr)
        return juce::Result::fail("The right-hand split clip could not be created.");
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::duplicateClip(te::EditItemID id)
{
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip to duplicate.");
    const auto old = clip->getPosition();
    auto* track = clip->getClipTrack();
    if (track == nullptr) return juce::Result::fail("The selected clip is not on a track.");
    edit->getUndoManager().beginNewTransaction("Duplicate clip");
    const auto duplicateRange = firstFreeDuplicateRange(*clip);
    te::Clip* copy = nullptr;
    if (auto* audio = dynamic_cast<te::WaveAudioClip*>(clip))
    {
        copy = track->insertWaveClip(audio->getName() + " copy", audio->getSourceFileReference().getFile(),
            {duplicateRange, old.offset}, false).get();
        if (copy != nullptr)
            copy->setColour(audio->getColour());
    }
    else if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
        if (auto midiCopy = track->insertMIDIClip(midi->getName() + " copy",
            duplicateRange, nullptr))
        {
            midiCopy->cloneFrom(midi);
            midiCopy->setPosition({duplicateRange, old.offset});
            copy = midiCopy.get();
        }
    if (copy == nullptr)
        return juce::Result::fail("The duplicate clip could not be created.");
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::deleteClip(te::EditItemID id)
{
    if (auto* clip = findClip(id))
    {
        const auto deletingPattern = clip == patternClip;
        edit->getUndoManager().beginNewTransaction("Delete audio clip");
        clip->removeFromParent();
        if (deletingPattern)
        {
            patternClip = nullptr;
            const auto tracks = te::getAudioTracks(*edit);
            if (!tracks.isEmpty())
            {
                for (auto* existing : tracks[0]->getClips())
                    if (auto* midi = dynamic_cast<te::MidiClip*>(existing))
                    {
                        patternClip = midi;
                        break;
                    }
                if (patternClip == nullptr)
                {
                    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
                    patternClip = tracks[0]->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
                    if (patternClip != nullptr)
                    {
                        patternClip->setColour(presetColour(PatternPreset::WarmPulse));
                        patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
                    }
                }
                if (patternClip != nullptr)
                    patternClipID = patternClip->itemID;
            }
        }
        refreshLoop();
        edit->getUndoManager().beginNewTransaction();
        markModified();
        sendSynchronousChangeMessage();
    }
}

juce::Result Session::cycleClipColour(te::EditItemID id)
{
    auto* clip = findClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select a clip first.");
    edit->getUndoManager().beginNewTransaction("Color clip");
    clip->setColour(nextClipColour(clip->getColour()));
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

int Session::clipPluginCount(te::EditItemID id) const
{
    auto* clip = dynamic_cast<te::AudioClipBase*>(findClip(id));
    if (clip == nullptr || clip->getPluginList() == nullptr)
        return 0;
    return clip->getPluginList()->size();
}

void Session::toggleTrackMute(int track)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return;
    edit->getUndoManager().beginNewTransaction("Track mute");
    tracks[track]->state.setProperty(te::IDs::mute, !tracks[track]->isMuted(false), &edit->getUndoManager());
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
}

void Session::toggleTrackSolo(int track)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return;
    edit->getUndoManager().beginNewTransaction("Track solo");
    tracks[track]->state.setProperty(te::IDs::solo, !tracks[track]->isSolo(false), &edit->getUndoManager());
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
}
}
