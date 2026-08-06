# AETHER — Comprehensive Session Summary & Architecture Guide

## 1. Overview of the Device
**AETHER** is a high-performance, polyphonic MIDI Delay audio plugin (VST3 / AU) built with JUCE 8 and HTML5/JS Web View technology.

### Core Features
- **15-Step Polyphonic MIDI Sequencer**: Each snapshot contains 15 programmable steps modifying incoming MIDI note events.
- **Per-Step Parameters**:
  - **Pitch Shift**: Bipolar (-24 to +24 semitones).
  - **Velocity**: (1 to 127).
  - **Modwheel (CC1)**: (0 to 127).
  - **Probability**: Trigger chance (0% to 100%).
  - **Mute**: Per-step toggle.
- **9 Independent Snapshots**: Instantaneous recall and switching between 9 complete preset configurations.
- **Sync & Delay Modes**:
  - **Sync Division**: 18 tempo-synced divisions (`1/1d` to `1/32t`), aligned in a grid layout (Dotted -> Straight -> Triplet).
  - **Millisecond Mode**: Free-running delay time (1 ms to 2000 ms).
- **Global Transport & Safety Features**:
  - **Kill On Stop**: Immediately silences all ringing delay taps and clears MIDI queues when DAW transport stops.
  - **Kill On Switch**: Clears active note queues when switching snapshots to prevent stuck notes.

---

## 2. How the UI Works
The user interface combines modern CSS styling with JUCE's native `WebBrowserComponent`.

### UI Architecture
- **HTML/CSS View Layer (`index.html`, `styles.css`)**: CRT retro aesthetic with dynamic glow effects, single-row grid layouts, and custom range controls.
- **JavaScript Engine (`app.js`)**:
  - Manages frontend local state (`state`).
  - Implements responsive UI scaling (`scaleUI()`).
  - Listens for DOM events and forwards parameter changes to C++ via `sendParamToCpp(paramName, value)`.
  - Exposes `window.aetherUI` to receive live updates from C++ (`updateParamFromCpp`, `initializeState`, `updateStepsFromCpp`).
- **Responsive Scaling & Centering**:
  - Fixed design canvas of `1040px × 1280px`.
  - Layout positioning is natively handled by CSS (`top: 50%; left: 50%; transform: translate(-50%, -50%) scale(...)`).
  - JS calculates `scale = Math.min(vw / 1040, vh / 1280)` without modifying positional coordinates, preventing layout drift or corner snapping on window resizes.

---

## 3. C++ Host Bridge & Audio Processor
- **Processor (`PluginProcessor.cpp` / `.h`)**:
  - Manages APVTS parameters (`AudioParameterInt`, `AudioParameterBool`, `AudioParameterFloat`).
  - Executes MIDI delay queues and timing calculations (`getSyncTimeInMs()`).
  - Handles project state serialization (`getStateInformation()`, `setStateInformation()`).
- **Editor (`PluginEditor.cpp` / `.h`)**:
  - Embeds `juce::WebBrowserComponent` (`AetherWebView`).
  - Binds JavaScript native functions (`sendParamToCpp`, `queryall`, `copyActiveSnapshot`, `pasteActiveSnapshot`).
  - Runs a 30 Hz timer loop (`timerCallback()`) to push updated C++ parameters and step values to the web view.

---

## 4. How to Build in GitHub Actions
The project includes a multi-platform CI/CD workflow defined in `.github/workflows/build.yml`.

### Workflow Triggers
- Pushing to `main` or `master` branches.
- Pull requests.
- Manual trigger via `workflow_dispatch`.

### Build Pipeline Steps
1. **Repository Checkout & JUCE Setup**:
   - Clones JUCE `8.0.13` shallow depth.
   - Adjusts module paths inside `Aether.jucer`.
2. **Projucer CLI Resave**:
   - Downloads Projucer CLI binary.
   - Executes `Projucer --resave Aether.jucer` to generate native Xcode (macOS) or Visual Studio 2022 (Windows) solution files and package web assets (`index.html`, `app.js`, `styles.css`) into `BinaryData.cpp`.
3. **Compilation**:
   - **macOS (macos-14)**: Builds VST3 and AU targets using `xcodebuild`.
   - **Windows (windows-2022)**: Restores NuGet packages and builds `Aether.sln` using `MSBuild` (x64 Release).
4. **Installer Packaging**:
   - **macOS**: Creates `.pkg` installer via `pkgbuild` & `productbuild`.
   - **Windows**: Compiles `installer_win.iss` using Inno Setup (`iscc`).
5. **Artifact Publishing**: Uploads raw plugin binaries and signed/packaged installers to GitHub release artifacts.

---

## 5. Implementation Challenges & Technical Fixes

| Challenge / Bug | Symptom | Technical Root Cause | Resolution / Fix |
| :--- | :--- | :--- | :--- |
| **1. UI Resizing Drift & Snapping** | Resizing plugin window caused UI to snap to top-left corner or drift off screen. | `app.js` modified `top`, `left`, and `transform` coordinates dynamically, conflicting with JUCE's parent window bounds. | Delegated positional centering to CSS (`top: 50%; left: 50%; transform: translate(-50%, -50%)`). JS `scaleUI()` now only applies the `scale(...)` factor. |
| **2. DAW Parameter Lockout** | Modifying parameters in DAW or reloading project locked parameter updates indefinitely. | `isInitializing` flag was set to `true` on load but never set back to `false`. | Added `isInitializing = false;` at the end of `setStateInformation()`. |
| **3. Sync Division Mismatch** | DAW `0` selected `1/1` straight instead of `1/1D` dotted. | Grid HTML displayed Dotted -> Straight -> Triplet, but C++ array ordered Straight -> Dotted -> Triplet. | Reordered `SYNC_DIVISIONS` arrays in JS & C++, and refactored `getSyncTimeInMs()` DSP logic: `step = (syncIdx - 1) / 3`, `type = (syncIdx - 1) % 3`. |
| **4. Float Slider Controls in DAW** | DAW displayed toggles and snapshots as float sliders (`1.000` to `9.000`). | Parameters were defined as `AudioParameterFloat`. | Changed discrete parameters to `AudioParameterInt` (`activeSnapshot`, `syncDivision`, `stepCount`) and `AudioParameterBool` (`enabled`, `killOnStop`, `killOnSwitch`). |
| **5. Stale Parameter Values on Load** | `killOnStop` loaded as `0`, `killOnSwitch` as `1`, `activeSnapshot` as `5` regardless of saved state. | JUCE's `apvts.copyState()` saved `apvts.state` `ValueTree`, which does not auto-sync when C++ code modifies parameters (`*p = val`). `copyState()` wrote stale initial defaults to XML. | Replaced `apvts.copyState()` and `replaceState()` with direct XML attribute parameter serialization in `getStateInformation()` and `setStateInformation()` (`paramsXml->setAttribute(...)` and `*p = val`). |
| **6. Active Snapshot UI View Mismatch** | `activeSnapshot` parameter restored in DSP, but UI rendered Snapshot 1. | `activeSnapshot` (DSP playback) and `editSnapshot` (UI view) were unlinked in C++. Project load updated `activeSnapshot`, but left `editSnapshot = 0`. | Synchronized `editSnapshot = activeSnap` across `setStateInformation()`, `handleAsyncUpdate()`, `parameterChanged()`, and `getFullStateJson()`. |

---

## 6. Current Repository State
- **Branch**: `main`
- **Latest Commit**: `fdfd54f` (Direct XML attribute parameter serialization & CRT green version label styling).
- **Status**: Fully verified, builds cleanly on CI/CD, and restores project states reliably across DAWs.
