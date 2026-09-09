#pragma once
#include "UtilityDevice.h"
#include "ClipGeometry.h"

namespace theta
{
// Message-thread facade. The engine owns scheduling, streaming and playback.
// Member order keeps the engine alive until its edit and devices are released.
class Session : public juce::ChangeBroadcaster
{
public:
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
    void togglePlayback();
    void stop();
    static constexpr int steps = 16, pitches = 12, lowestNote = 48;
    te::MidiClip& pattern() const { return *patternClip; }
    bool hasNote(int step, int pitch) const;
    void setNote(int step, int pitch, bool enabled);
    void beginNoteGesture();
    void endNoteGesture();
    void clearPattern();
    double tempo() const;
    void setTempo(double bpm);
    void undo();
    void redo();
    void refreshLoop();
    te::WaveAudioClip* findAudioClip(te::EditItemID) const;
    juce::Result editAudioClip(te::EditItemID, ClipGeometry, ClipGesture);
    void deleteAudioClip(te::EditItemID);
    void toggleTrackMute(int track);
    void toggleTrackSolo(int track);
    // Keep the established settings location so existing audio-device choices survive.
    te::Engine engine {"Theda Native"};
    std::unique_ptr<te::Edit> edit;
    UtilityDevice* utility = nullptr; // owned by edit's plugin list
private:
    void refreshAfterUndoRedo(bool changed);
    te::MidiClip* patternClip = nullptr; // owned by edit
    // Engine initialization also changes its edit flag asynchronously. Track
    // user commands separately so startup cannot dirty an untouched document.
    juce::int64 changeRevision = 0, savedRevision = 0;
};
int runSelfTest();
int runPatternTest();
int runArrangementTest();
}
