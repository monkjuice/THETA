#pragma once
#include "UtilityDevice.h"
#include "DrumDevice.h"
#include "ClipGeometry.h"
#include <vector>

namespace theta
{
// Message-thread facade. The engine owns scheduling, streaming and playback.
// Member order keeps the engine alive until its edit and devices are released.
class Session : public juce::ChangeBroadcaster
{
public:
    enum class PatternPreset
    {
        WarmPulse,
        AcidSteps,
        HouseKit,
        BreakKit,
        MinimalKit
    };
    enum class AudioEffect
    {
        Equaliser,
        Reverb,
        Delay,
        Compressor
    };
    enum class Instrument
    {
        FourOsc,
        Drums,
        Utility
    };
    struct DeviceSlot
    {
        juce::String name;
        juce::String type;
        bool enabled = true;
        bool removable = false;
    };
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void editWillChange() = 0;
        virtual void editDidChange() = 0;
    };
    Session();
    juce::ValueTree projectSnapshot();
    juce::Result restoreProject(const juce::ValueTree&, const juce::File&);
    void projectSaved(const juce::ValueTree&, const juce::File&);
    bool hasUnsavedChanges() const { return changeRevision != savedRevision; }
    void markModified();
    juce::File projectFile;
    juce::ListenerList<Listener> listeners;
    juce::Result importAudio(const juce::File&);
    juce::Result importAudioAt(const juce::File&, int track, double startSeconds);
    void togglePlayback();
    void stop();
    static constexpr int steps = 16, pitches = 16, lowestNote = 48;
    te::MidiClip& pattern() const { return *patternClip; }
    bool hasNote(int step, int pitch) const;
    void setNote(int step, int pitch, bool enabled);
    void beginNoteGesture();
    void endNoteGesture();
    void clearPattern();
    void applyPatternPreset(PatternPreset);
    juce::Result insertPatternPreset(PatternPreset, int track, double startSeconds);
    juce::Result selectPatternClip(te::EditItemID);
    bool isPatternDrums() const;
    juce::Result addAudioEffect(AudioEffect, int track = 1);
    juce::Result addInstrument(Instrument, int track);
    int trackCount() const;
    juce::String trackName(int track) const;
    juce::Result addAudioTrack();
    juce::Result removeAudioTrack(int track);
    std::vector<DeviceSlot> deviceSlots(int track) const;
    juce::Result toggleDeviceEnabled(int track, int slot);
    juce::Result deleteDevice(int track, int slot);
    double tempo() const;
    void setTempo(double bpm);
    void undo();
    void redo();
    void refreshLoop();
    te::Clip* findClip(te::EditItemID) const;
    te::WaveAudioClip* findAudioClip(te::EditItemID) const;
    juce::Result editClip(te::EditItemID, ClipGeometry, ClipGesture);
    juce::Result splitClip(te::EditItemID, double splitTimeSeconds);
    juce::Result duplicateClip(te::EditItemID);
    void deleteClip(te::EditItemID);
    void toggleTrackMute(int track);
    void toggleTrackSolo(int track);
    // Keep the established settings location so existing audio-device choices survive.
    te::Engine engine {"Theda Native"};
    std::unique_ptr<te::Edit> edit;
    UtilityDevice* utility = nullptr; // owned by edit's plugin list
    UtilityDevice* audioUtility = nullptr; // owned by edit's plugin list
    te::FourOscPlugin* synth = nullptr; // owned by edit's plugin list
    DrumDevice* drums = nullptr; // owned by edit's plugin list
private:
    void refreshAfterUndoRedo(bool changed);
    void setPatternInstrument(bool useDrums);
    void ensureEditablePatternClip();
    te::MidiClip* patternClip = nullptr; // owned by edit
    te::EditItemID patternClipID;
    // Engine initialization also changes its edit flag asynchronously. Track
    // user commands separately so startup cannot dirty an untouched document.
    juce::int64 changeRevision = 0, savedRevision = 0;
};
int runSelfTest();
int runPatternTest();
int runArrangementTest();
}
