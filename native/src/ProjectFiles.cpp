#include "ProjectFiles.h"

namespace theta
{
void ProjectFiles::save(bool saveAs, std::function<void(bool)> completion)
{
    if (busy)
    {
        report("Please wait for the current file operation.");
        if (completion) completion(false);
        return;
    }
    if (!saveAs && session.projectFile != juce::File{})
    {
        write(session.projectFile, std::move(completion));
        return;
    }
    busy = true;
    const auto suggested = session.projectFile == juce::File{}
        ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Untitled.thetaedit")
        : session.projectFile.withFileExtension("thetaedit");
    chooser = std::make_unique<juce::FileChooser>("Save project", suggested, "*.thetaedit");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                        | juce::FileBrowserComponent::warnAboutOverwriting,
        [weak = juce::WeakReference<ProjectFiles>(this), completion](const juce::FileChooser& selected)
        {
            if (!weak) return;
            weak->busy = false;
            const auto file = selected.getResult();
            if (file == juce::File{}) { if (completion) completion(false); return; }
            // Preserve the chosen filename so the native overwrite confirmation
            // refers to the exact file that will be replaced.
            weak->write(file, completion);
        });
}

void ProjectFiles::write(const juce::File& file, std::function<void(bool)> completion)
{
    busy = true;
    report("Saving " + file.getFileName() + "...");
    auto snapshot = session.projectSnapshot();
    workers.addJob([weak = juce::WeakReference<ProjectFiles>(this), snapshot, file, completion]
    {
        juce::TemporaryFile temporary(file);
        auto xml = snapshot.createXml();
        const bool success = xml && xml->writeTo(temporary.getFile()) && temporary.overwriteTargetFileWithTemporary();
        juce::MessageManager::callAsync([weak, snapshot, file, completion, success]
        {
            if (!weak) return;
            weak->busy = false;
            if (success) weak->session.projectSaved(snapshot, file);
            weak->report(success ? "Saved " + file.getFileName() : "Could not save the project. The previous file was kept.");
            if (completion) completion(success);
        });
    });
}

void ProjectFiles::confirmUnsaved(std::function<void()> action)
{
    if (busy) { report("Please wait for the current file operation."); return; }
    if (!session.hasUnsavedChanges()) { action(); return; }
    busy = true;
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
        .withTitle("Save your changes?")
        .withMessage("This project has unsaved changes.")
        .withButton("Save").withButton("Discard").withButton("Cancel"),
        [weak = juce::WeakReference<ProjectFiles>(this), action](int result)
        {
            if (!weak) return;
            weak->busy = false;
            if (result == 1)
                weak->save(false, [weak, action](bool saved)
                {
                    if (weak && saved && !weak->session.hasUnsavedChanges()) action();
                });
            else if (result == 2) action();
        });
}

void ProjectFiles::open()
{
    confirmUnsaved([weak = juce::WeakReference<ProjectFiles>(this)] { if (weak) weak->chooseOpen(); });
}

void ProjectFiles::chooseOpen()
{
    busy = true;
    chooser = std::make_unique<juce::FileChooser>("Open project", session.projectFile, "*.thetaedit;*.thedaedit");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [weak = juce::WeakReference<ProjectFiles>(this)](const juce::FileChooser& selected)
        {
            if (!weak) return;
            const auto file = selected.getResult();
            if (file == juce::File{}) { weak->busy = false; return; }
            if (weak->loadingChanged) weak->loadingChanged(true);
            weak->report("Opening " + file.getFileName() + "...");
            weak->workers.addJob([weak, file]
            {
                auto xml = juce::parseXML(file);
                auto state = xml ? juce::ValueTree::fromXml(*xml) : juce::ValueTree{};
                juce::MessageManager::callAsync([weak, file, state]
                {
                    if (!weak) return;
                    weak->busy = false;
                    const auto result = weak->session.restoreProject(state, file);
                    if (weak->loadingChanged) weak->loadingChanged(false);
                    weak->report(result.wasOk() ? "Opened " + file.getFileName() : result.getErrorMessage());
                });
            });
        });
}
}
