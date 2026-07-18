# Implementation Plan - Fix Plugin State Loading on Project Load

This plan details how we will fix the issue where the Aether plugin state (knobs, buttons, and sequencer step grid) fails to restore when loading a saved project in a DAW/Host.

## Root Cause Analysis

1. **Asynchronous WebView2 Bridge Injection**: When the plugin editor UI is opened, JUCE's WebView2 browser component loads `index.html` asynchronously. The native JS bridge object (`window.__JUCE__` and `window.__JUCE__.backend`) is injected by the host process dynamically.
2. **Dropped Initial Query**: In `app.js`, `sendParamToCpp("queryall", 0)` is invoked immediately during top-level load. At this early stage, `window.__JUCE__` is not yet ready. The call falls back to `console.log` and is dropped, meaning C++ never receives the `"queryall"` request.
3. **C++ Timer Cache Sync Failure**: 
   - The C++ side runs a 30Hz timer (`timerCallback`) that syncs parameters and sequence steps from the processor to JS if they differ from the cache (`webView.localParams` and `webView.localSteps`).
   - In the very first timer iterations (before the WebView is loaded), it detects a mismatch between its cached copies (which are default-initialized) and the loaded values. It updates its caches to the loaded values and tries to evaluate JS to update the frontend.
   - However, since the Web page is not yet loaded and `window.aetherUI` does not exist, the evaluation is silently ignored.
   - Because the C++ cache was updated to the loaded values, subsequent timer ticks detect no changes and send nothing.
   - Without the `"queryall"` request arriving to reset the C++ cache, the Web UI remains stuck displaying its default values instead of the project's loaded state.

---

## Proposed Changes

We will implement a robust message queuing system in the frontend JavaScript (`app.js`). Instead of dropping calls when the bridge is not ready, any early calls to `sendParamToCpp` will be queued. A background checker will poll for the availability of `window.__JUCE__.backend` every 30ms, flushing the queue as soon as it becomes available.

### Frontend UI

#### [MODIFY] [app.js](file:///c:/Dropbox/DSP/JUCE_projects/Aether/app.js)
- Introduce a message queue (`juceMessageQueue`) and a bridge readiness flag (`juceBridgeReady`).
- Update `sendParamToCpp` to:
  - If the bridge is ready, call it immediately.
  - If the bridge is not ready, check if `window.__JUCE__` exists. If it does, mark as ready and send. Otherwise, push the message to `juceMessageQueue`.
- Add `checkJuceBridge()` which runs periodically (every 30ms) until the bridge is ready, then flushes the queue.

---

## Verification Plan

### Automated Build
- No C++ changes are required, so rebuilding is optional unless files are bundled via Jucer.

### Manual Verification
1. Load the Aether plugin in a DAW/Host.
2. Modify several steps in the sequencer and change some parameter values (e.g., turn loop on, select Random mode, adjust delay time).
3. Save the DAW project and close the DAW.
4. Reopen the DAW project.
5. Open the Aether plugin UI and verify that all knobs, toggles, and sequencer step values are successfully restored to the saved state.
