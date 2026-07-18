# Implementation Plan - Aether VST3 WebView2 UI Integration

This plan describes how we will integrate the React-based UI in the `aistudio` directory into the `Aether` VST3 plugin using JUCE 8's `WebBrowserComponent` (WebView2).

---

## User Review Required

We propose using a **single-file bundle** for the React app to simplify the JUCE resource provider and avoid managing dynamic filenames in the `.jucer` file.

> [!IMPORTANT]
> **Single-File Bundling**: We will configure Vite using `vite-plugin-singlefile` to inline all JS, CSS, and SVG assets directly into `dist/index.html`. This means the JUCE resource provider only needs to handle a single resource (`BinaryData::index_html`).

---

## Open Questions

> [!IMPORTANT]
> 1. **Plugin Dimensions**: The original C++ UI has a window size of 1040 x 1000. Should we lock the aspect ratio to this size (`1040/1000`), or adjust it based on the React UI's layout requirements?
> 2. **Build Tooling**: Confirm that Node.js and `npm` are available on your development system so that we can compile the `aistudio` UI locally.

---

## Proposed Changes

### Front-End Changes (aistudio UI)

#### [MODIFY] [App.tsx](file:///c:/Dropbox/DSP/JUCE_projects/Aether/aistudio/App.tsx)
- Establish the C++ bridge functions:
  - `sendParamToCpp(param, value)`: Serializes updates (parameters, sequence steps, stops, etc.) and emits the `__juce__invoke` event.
  - `window.aetherUI.initializeState(jsonState)`: Callback for C++ to pass the entire initial parameter and sequence step values on load.
  - `window.aetherUI.updateParamFromCpp(param, value)`: Callback for C++ to update individual slider/toggle/selection parameters when automated by the DAW.
- Wire parameter state changes (`isEnabled`, `delayTimeMs`, `syncDivision`, `stepCount`, `killOnStop`, `isLooping`, `loopMode`, `loopNoteRestart`, and `steps`) to invoke `sendParamToCpp` to update the C++ processor in real time.
- Request the initial state from C++ on component mount by calling `sendParamToCpp("queryall", 0)`.

#### [MODIFY] [vite.config.ts](file:///c:/Dropbox/DSP/JUCE_projects/Aether/aistudio/vite.config.ts)
- Add `vite-plugin-singlefile` plugin to inline bundle resources.
- Output build targets into a clean `dist/` directory.

---

### Back-End Changes (C++ Plugin)

#### [MODIFY] [PluginEditor.h](file:///c:/Dropbox/DSP/JUCE_projects/Aether/Source/PluginEditor.h)
- Remove `AetherLookAndFeel` and custom C++ button/interaction components.
- Add `AetherWebView` class inheriting from `juce::WebBrowserComponent`:
  - Set options for WebView2 with a local user data cache folder.
  - Register a native callback for `sendParamToCpp`.
  - Set up a resource provider serving `index.html` from `BinaryData`.
- In `AetherAudioProcessorEditor`:
  - Replace GUI child components with an instance of `AetherWebView`.
  - Add parameter tracking caching arrays to detect changes.
  - Keep the 30Hz timer to poll C++ APVTS parameter changes and push updates to JS via `evaluateJavascript`.

#### [MODIFY] [PluginEditor.cpp](file:///c:/Dropbox/DSP/JUCE_projects/Aether/Source/PluginEditor.cpp)
- Remove original drawing code, mouse drag handlers, and text/toggle buttons.
- Implement the `sendParamToCpp` callback logic:
  - If `queryall`: Compile parameter values and serializes the 15 steps from `audioProcessor.steps` into a JSON string, then call `window.aetherUI.initializeState(...)`.
  - If standard parameters: Update the APVTS.
  - If step sequence updates: Set the property in `audioProcessor.steps[index]`.
  - If loop stop: Set `audioProcessor.stopRequested = true`.
- Implement timer updates to notify WebView if the host automates a parameter.

#### [MODIFY] [Aether.jucer](file:///c:/Dropbox/DSP/JUCE_projects/Aether/Aether.jucer)
- Enable WebView2 compilation flags: `JUCE_USE_WIN_WEBVIEW2="1"` and `JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING="1"`.
- Add `index.html` from `aistudio/dist/index.html` to a `Resources` group in the jucer project as a compiler resource.

---

## Verification Plan

### Automated Build & Compile
- Run `npm install` and `npm run build` in `aistudio` to generate the inline bundle.
- Re-save `Aether.jucer` to generate visual studio project files.
- Compile Aether in release/debug mode using MSBuild.

### Manual Verification
- Load Aether VST3 in a DAW or plugin host.
- Verify UI parameters (Delay mode, manual MS time dragging, sync division selection, step count, looping toggles, and loop modes) sync bidirectionally.
- Verify changing sequence steps (pitch, velocity, modwheel, probability, mute) alters the played MIDI delay taps correctly.
