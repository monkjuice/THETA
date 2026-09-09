# Native engine evaluation

A C++20/Tracktion/JUCE application with a playable one-bar pattern editor, a two-track arrangement view with audio waveforms and clip editing, Tracktion's 4OSC synth, our internal Utility gain device, transport, audio hardware settings, and native project save/open. The original Electron prototype remains in place.

Windows is the active development and validation target. CMake, JUCE controls, file handling, and engine integration remain portable to macOS, which has not yet been built or tested. The arrangement currently supports the existing synth and audio tracks; adding/reordering tracks, arranging multiple MIDI patterns, recording, and export UI remain unfinished.

Windows validation includes the Release build and four CTest cases. They cover device DSP, note editing/undo, tempo changes, a real MIDI-to-synth WAV render, audio import, project state round trips, arrangement pointer gestures and waveform drawing, track control undo, and rendering the correct source region after trimming. The startup check exercises the loading-screen-to-editor lifecycle. This does not establish hardware audio latency, frame pacing, or macOS behavior.

## Use

- Startup shows engine, device, and workspace phases before opening the editor. Phases correspond to actual initialization steps, not an estimated percentage. Engine construction still requires the message thread, so the loading screen may remain static during an individual phase.
- Draw notes in the 16-step grid; drag an existing note or right-drag to erase. Each stroke is one undo action. The initial octave is MIDI 48–59; pitch-name labels use C4 for middle C.
- Press Play to hear the pattern through 4OSC. Change BPM to keep the pattern one bar long. Synth Gain controls the Utility after the instrument.
- Use Undo/Redo or Ctrl+Z / Ctrl+Shift+Z (Command on macOS). Space toggles playback when the grid is focused.
- Save/Open use `.thetaedit` projects, separate from the web prototype's `.theta` format. Ctrl+S saves, Ctrl+Shift+S chooses another file, and Ctrl+O opens. An asterisk marks unsaved changes; opening/closing offers Save, Discard, or Cancel.
- Older `.thedaedit` projects still open. Existing files retain their name when saved; new projects use `.thetaedit`. The legacy `theda.utility.v1` device ID, persisted state keys, and audio-settings storage location are intentionally retained for compatibility.
- Add Audio places files sequentially on a separate track, starting at zero. The loop covers the pattern and imported audio; the synth pattern plays only its first bar when the audio extends beyond it. Imported media stays at its original path, so keep those files in place. Projects do not yet bundle or relink media.
- Audio waveforms appear in the arrangement above the note editor. Drag a clip's body to move it and its edges to trim it. Changes commit on release as one undo step; Escape cancels a drag. Delete/Backspace removes the selected audio clip. Trimmed material can be revealed again by extending the edges to the source boundaries.
- M/S toggle track mute/solo. Click the ruler to seek. Use Fit, +/-, the scrollbar, or the wheel to navigate; Ctrl+wheel zooms around the pointer. Snap 1/16 toggles beat snapping, and Alt temporarily bypasses it during a drag. The synth clip stays at bar one for now.

## Build

Requires Python 3.12+, CMake 3.24+, and a C++20 compiler (Visual Studio 2022 with Desktop C++ tools on Windows; Xcode command-line tools on macOS).

```sh
python native/scripts/fetch-dependencies.py
cmake -S native -B native/build
cmake --build native/build --config Release --parallel 2
ctest --test-dir native/build -C Release --output-on-failure
```

Windows executable: `native/build/ThetaNative_artefacts/Release/Theta.exe`. macOS produces an app bundle in the target artefacts directory. For the workspace-local Windows CMake installation, replace `cmake` with `native/.tools/cmake-3.31.6-windows-x86_64/bin/cmake.exe` and `ctest` with the adjacent `ctest.exe`.

## Boundaries

- Tracktion owns edits, playback graphs, streaming, parameters and device state; the UI does not schedule audio.
- Utility's processing path uses an atomic parameter read and a preallocated smoother. It handles sub-buffer offsets and shares the same gain ramp across channels. Its stable type ID is `theda.utility.v1`, with a `gainDb` parameter.
- Slider feedback happens directly in the control event path; gain gestures use the engine's automation hooks. A 10 Hz timer updates only transport text. The playhead uses JUCE's display-vblank callback and one padded damage region covering its old and new positions; note data stays cached between edits. The note ruler is outside this region, and arrangement navigation updates the line immediately. Neither is an audio scheduler. Rendering regression checks compare incremental frames with full renders at 100%, 125%, 150%, and 200% scale; these do not measure live display frame pacing.
- Arrangement clip geometry is cached between edit notifications. Waveforms share a thumbnail per source file, scan samples on JUCE's thumbnail worker, and repaint their own clip regions as data arrives. Pointer movement updates only a local preview; the engine edit and undo transaction change once on release. UI selection stores clip IDs rather than pointers that would become stale during undo or project replacement.
- Save serializes a detached engine snapshot on a background worker, writes a temporary file beside the destination, and replaces the target on success. Edits during saving remain dirty. Open parses off-thread, validates before replacing the active edit, and detaches UI listeners before releasing the old edit. Engine reconstruction still happens on the message thread; controls are disabled during open to prevent losing concurrent edits.
- No synthetic large sessions, capacity claims, or FPS benchmark harness are included. Frame pacing and pointer latency remain design requirements to measure later. Import metadata work still runs on the message thread and needs background preparation before the production browser grows.
- The small tests check Utility DSP and state, grouped note undo/redo, tempo/loop duration, a finite non-silent 48 kHz render, audio import starting at zero, saved MIDI/tempo/media references, invalid-file rejection, and edits made after a save snapshot. They do not verify physical audio drivers.

Dependencies are fixed to the source-study Tracktion revision and its exact JUCE submodule revision. See the fetch script for hashes. Their upstream licence files are retained inside `.deps`; this evaluation does not change the licensing findings in the research.
