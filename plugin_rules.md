# JUCE + Web View Audio Plugin Development — Golden Rules & Gotchas

This document serves as an authoritative guide and checklist for developing JUCE audio plugins with Web View (HTML/CSS/JS) frontends. Follow these rules to avoid common pitfalls in DAW state management, layout rendering, and parameter synchronization.

---

## 1. State Serialization & APVTS (`getStateInformation` / `setStateInformation`)
- **NEVER rely exclusively on `apvts.copyState()` / `apvts.replaceState()` for programmatic parameter updates.**
  - **Gotcha**: Modifying parameters in C++ via `*param = val` or `param->setValueNotifyingHost()` updates the parameter object in memory, but `apvts.copyState()` reads from the `apvts.state` `ValueTree`. The `ValueTree` properties do NOT automatically sync when C++ code assigns parameter values directly.
  - **Golden Rule**: Explicitly serialize and deserialize parameters as XML attributes in `getStateInformation()` and `setStateInformation()` using typed parameter getters (`p->get()`) and native setters (`*p = val`).
  ```cpp
  // Saving:
  if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("killOnStop")))
      paramsXml->setAttribute("killOnStop", p->get() ? 1 : 0);

  // Loading:
  if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("killOnStop")))
      *p = (paramsXml->getIntAttribute("killOnStop", 1) != 0);
  ```

---

## 2. Typed Parameter Getters vs Atomic Float Pointers
- **NEVER use `apvts.getRawParameterValue("id")->load()` to query boolean or integer parameters during state loading or UI initialization.**
  - **Gotcha**: `getRawParameterValue()` returns a pointer to an internal atomic float cache that does not automatically refresh during `replaceState()` or initial plugin loading.
  - **Golden Rule**: Always query typed parameters directly using `dynamic_cast`:
  ```cpp
  auto* boolP = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("paramId"));
  bool val = boolP ? boolP->get() : defaultValue;

  auto* intP = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("paramId"));
  int val = intP ? intP->get() : defaultValue;
  ```

---

## 3. Parameter Types & DAW Representation
- **Always match parameter types to their semantic data representations.**
  - Use `AudioParameterBool` for toggles (renders as native checkboxes in DAWs).
  - Use `AudioParameterInt` for discrete counts/indices (renders as clean integer values in DAWs).
  - Use `AudioParameterFloat` only for continuous range parameters (e.g., gain, delay time in ms, frequency).
  - Avoid `AudioParameterFloat` for discrete selections; DAWs will render decimal sliders (e.g., `1.000` to `9.000`), causing rounding/mapping bugs.

---

## 4. UI View Context vs DSP State Synchronization
- **Always keep the UI edit context (`editSnapshot`) synchronized with the DSP active state (`activeSnapshot`).**
  - **Gotcha**: If active snapshot changes in DSP via DAW project load or host automation, but `editSnapshot` remains unchanged, the C++ editor will continue pushing the wrong snapshot's parameters and step sequences to the UI.
  - **Golden Rule**: In `setStateInformation()`, `parameterChanged()`, `handleAsyncUpdate()`, and `getFullStateJson()`, always set `editSnapshot = activeSnap`.

---

## 5. Initialization Lock Prevention (`isInitializing` Flag)
- **Always set `isInitializing = false;` at the end of `setStateInformation()`.**
  - **Gotcha**: Setting `isInitializing = true;` prevents recursive parameter callbacks during state parsing. If it is never set back to `false`, `parameterChanged()` callbacks will return early indefinitely, locking out parameter updates from both DAW and UI.

---

## 6. Web View Layout, Scaling & Centering
- **NEVER dynamically calculate or modify `top`, `left`, or positional pixel offsets in JavaScript `scaleUI()` functions.**
  - **Gotcha**: Manipulating positional offset coordinates in JS conflicts with JUCE's parent window bounds and webview container, leading to layout drift or snapping to the top-left corner on resize.
  - **Golden Rule**: Delegate positional centering 100% to CSS:
  ```css
  #app-container {
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%) scale(1);
      transform-origin: center center;
  }
  ```
  - In JavaScript, `scaleUI()` should ONLY compute `scale = Math.min(vw / DESIGN_W, vh / DESIGN_H)` and set:
  ```javascript
  container.style.transform = 'translate(-50%, -50%) scale(' + scale + ')';
  ```

---

## 7. Array & Enum Grid Sequence Consistency
- **Keep UI grid sequences, string arrays, and C++ DSP calculations in identical order.**
  - If visual HTML grid displays columns as `Dotted -> Straight -> Triplet`, both JS `SYNC_DIVISIONS` and C++ `SYNC_DIVISIONS` must use the exact same sequence (`1/1d`, `1/1`, `1/1t`...).
  - Math formula for grid columns:
    - `step = (syncIdx - 1) / 3` (e.g., division size level)
    - `type = (syncIdx - 1) % 3` (e.g., 0 = Dotted, 1 = Straight, 2 = Triplet)

---

## 8. Web Asset Embedding & CI/CD Packaging
- **Resave Jucer projects during automated builds.**
  - When running CI/CD on GitHub Actions, execute `Projucer --resave Plugin.jucer` before compiling solutions. This ensures modified `app.js`, `index.html`, and `styles.css` assets are re-packaged into `BinaryData.cpp`.

---

## 9. Web View UI Implementation & Communication Bridge Pattern

### A. C++ Options & Resource Provider (`PluginEditor.h`)
Configure `WebBrowserComponent::Options` with resource provider and native function bindings:
```cpp
return juce::WebBrowserComponent::Options()
    .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
    .withNativeIntegrationEnabled (true)
    .withResourceProvider ([] (const juce::String& url) -> std::optional<juce::WebBrowserComponent::Resource> {
        auto retrieveResource = [] (const char* data, int size, const juce::String& mime) {
            std::vector<std::byte> vec ((size_t) size);
            std::memcpy (vec.data(), data, (size_t) size);
            return juce::WebBrowserComponent::Resource { std::move (vec), mime };
        };

        const auto urlToRetrieve = url == "/" ? "index.html" : url.fromFirstOccurrenceOf ("/", false, false);

        if (urlToRetrieve == "index.html")
            return retrieveResource (BinaryData::index_html, BinaryData::index_htmlSize, "text/html");
        if (urlToRetrieve.contains ("app.js"))
            return retrieveResource (BinaryData::app_js, BinaryData::app_jsSize, "application/javascript");
        if (urlToRetrieve.contains ("styles.css"))
            return retrieveResource (BinaryData::styles_css, BinaryData::styles_cssSize, "text/css");

        return std::nullopt;
    })
    .withNativeFunction ("sendParamToCpp", [webViewInstance, &p](const juce::var& args, std::function<void (juce::var)> completion) {
        if (args.size() >= 2) {
            juce::String paramName = args[0].toString();
            float val = (float)args[1];
            // Update C++ APVTS parameters & snapshots...
        }
    });
```

### B. JS Host Bridge & Message Queue (`app.js`)
Queue bridge messages if JS initializes before the `window.__JUCE__.backend` bridge is ready:
```javascript
let juceBridgeReady = false;
const juceMessageQueue = [];

function sendParamToCpp(param, val) {
    if (juceBridgeReady) {
        const resultId = Math.floor(Math.random() * 1000000);
        window.__JUCE__.backend.emitEvent("__juce__invoke", {
            name: "sendParamToCpp",
            params: [param, val],
            resultId: resultId
        });
    } else if (window.__JUCE__ && window.__JUCE__.backend) {
        juceBridgeReady = true;
        const resultId = Math.floor(Math.random() * 1000000);
        window.__JUCE__.backend.emitEvent("__juce__invoke", {
            name: "sendParamToCpp",
            params: [param, val],
            resultId: resultId
        });
    } else {
        juceMessageQueue.push({ param, val });
    }
}

function checkJuceBridge() {
    if (window.__JUCE__ && window.__JUCE__.backend) {
        juceBridgeReady = true;
        while (juceMessageQueue.length > 0) {
            const msg = juceMessageQueue.shift();
            sendParamToCpp(msg.param, msg.val);
        }
    } else {
        setTimeout(checkJuceBridge, 30);
    }
}
checkJuceBridge();
```

### C. C++ to JS Push Loop with Local Caching (`PluginEditor.cpp`)
Use a 30 Hz `timerCallback()` with local value caching (`webView.localParams[i]`) to push parameter updates to JS without spamming `evaluateJavascript()` calls:
```cpp
void AetherAudioProcessorEditor::timerCallback() {
    auto* kosP = dynamic_cast<juce::AudioParameterBool*>(audioProcessor.apvts.getParameter("killOnStop"));
    bool kosBool = kosP ? kosP->get() : true;
    float val = kosBool ? 1.0f : 0.0f;
    if (std::abs (val - webView.localParams[4]) > 0.001f) {
        webView.localParams[4] = val;
        webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('killOnStop', " + juce::String (kosBool ? "true" : "false") + ");");
    }
}
```
