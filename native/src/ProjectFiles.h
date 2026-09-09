#pragma once
#include "Session.h"

namespace theda
{
// File workers receive detached project snapshots, never the live engine/edit.
class ProjectFiles
{
public:
    explicit ProjectFiles(Session& s) : session(s) {}
    void save(bool saveAs = false, std::function<void(bool)> completion = {});
    void open();
    void confirmUnsaved(std::function<void()>);
    std::function<void(juce::String)> status;
    std::function<void(bool)> loadingChanged;
private:
    void write(const juce::File&, std::function<void(bool)>);
    void chooseOpen();
    void report(const juce::String& text) { if (status) status(text); }
    Session& session;
    bool busy = false;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool workers {1};
    JUCE_DECLARE_WEAK_REFERENCEABLE(ProjectFiles)
};
}
