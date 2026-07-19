// PluginEditor.cpp
#include "PluginEditor.h"
#include "PluginProcessor.h"

// ==========================================================================
// Constructor
// ==========================================================================
AetherAudioProcessorEditor::AetherAudioProcessorEditor (AetherAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), webView (p)
{
    startTimerHz (30);
    
    // Add WebView UI
    addAndMakeVisible (webView);

    // Point the web view to the virtual origin managed by the C++ ResourceProvider
    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    // Configure Editor sizing (fixed aspect ratio and resizable)
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (1040.0 / 1200.0);
    setResizeLimits (520, 600, 2080, 2400);
    setSize (1040, 1200);
}

// ==========================================================================
// Destructor
// ==========================================================================
AetherAudioProcessorEditor::~AetherAudioProcessorEditor()
{
}

// ==========================================================================
// Painting & Layout
// ==========================================================================
void AetherAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Draw solid very dark background matching the CSS theme in case of slow loading
    g.fillAll (juce::Colour::fromString ("#050505"));
}

void AetherAudioProcessorEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

// ==========================================================================
// Timer Callback - Bidirectional Parameter & Sequence Step Sync
// ==========================================================================
void AetherAudioProcessorEditor::timerCallback()
{
    // 1. Detect active snapshot changes FIRST, before the param push loop.
    //    This ensures localParams is invalidated before we compare values,
    //    so the push loop will always re-push all scalar params on a snapshot change.
    int activeSnap = (int)std::round(audioProcessor.apvts.getRawParameterValue ("activeSnapshot")->load()) - 1;
    activeSnap = juce::jlimit (0, 8, activeSnap);

    if (activeSnap != webView.localActiveSnapshot)
    {
        webView.localActiveSnapshot = activeSnap;

        // Invalidate ALL scalar param caches so the push loop below re-sends every
        // parameter value for the newly active snapshot on this same tick.
        for (int i = 0; i < 5; ++i)   // indices 0-4: enabled, delayTimeMs, syncDivision, stepCount, killOnStop
            webView.localParams[i] = -1.0f;

        // Invalidate steps cache so the loop below will push the new steps
        for (int i = 0; i < 15; ++i)
        {
            webView.localSteps[i].pitchOffset = -999;
            webView.localSteps[i].velocity = -1;
            webView.localSteps[i].modwheel = -1;
            webView.localSteps[i].probability = -1;
            webView.localSteps[i].muted = !audioProcessor.snapshots[activeSnap].steps[i].muted;
        }
    }

    // 2. Sync DAW-automated/saved parameters from C++ APVTS back to JS UI
    juce::String paramIDs[6] = { "enabled", "delayTimeMs", "syncDivision", "stepCount", "killOnStop", "activeSnapshot" };
    
    for (int p = 0; p < 6; ++p)
    {
        if (auto* rawVal = audioProcessor.apvts.getRawParameterValue (paramIDs[p]))
        {
            float val = rawVal->load();
            if (std::abs (val - webView.localParams[p]) > 0.001f)
            {
                webView.localParams[p] = val;
                AetherWebView::logToFile ("timer pushing: " + paramIDs[p] + " = " + juce::String (val));
                
                if (paramIDs[p] == "syncDivision")
                {
                    webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('" + paramIDs[p] + "', " + juce::String ((int)std::round(val)) + ");");
                }
                else if (paramIDs[p] == "stepCount")
                {
                    webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('" + paramIDs[p] + "', " + juce::String ((int)std::round(val)) + ");");
                }
                else if (paramIDs[p] == "enabled" || paramIDs[p] == "killOnStop")
                {
                    bool boolVal = (val > 0.5f);
                    webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('" + paramIDs[p] + "', " + (boolVal ? "true" : "false") + ");");
                }
                else
                {
                    webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('" + paramIDs[p] + "', " + juce::String (val) + ");");
                }
            }
        }
    }

    // 3. Sync sequence steps changes from C++ back to JS
    bool stepsChanged = false;
    for (int i = 0; i < 15; ++i)
    {
        if (audioProcessor.snapshots[activeSnap].steps[i].pitchOffset != webView.localSteps[i].pitchOffset ||
            audioProcessor.snapshots[activeSnap].steps[i].velocity != webView.localSteps[i].velocity ||
            audioProcessor.snapshots[activeSnap].steps[i].modwheel != webView.localSteps[i].modwheel ||
            audioProcessor.snapshots[activeSnap].steps[i].probability != webView.localSteps[i].probability ||
            audioProcessor.snapshots[activeSnap].steps[i].muted != webView.localSteps[i].muted)
        {
            webView.localSteps[i] = audioProcessor.snapshots[activeSnap].steps[i];
            stepsChanged = true;
        }
    }

    if (stepsChanged)
    {
        juce::String jsonSteps = getStepsJson();
        webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateStepsFromCpp('" + jsonSteps + "');");
    }
}

// ==========================================================================
// Helper to serialize steps to JSON string
// ==========================================================================
juce::String AetherAudioProcessorEditor::getStepsJson()
{
    int activeSnap = (int)audioProcessor.apvts.getRawParameterValue ("activeSnapshot")->load() - 1;
    activeSnap = juce::jlimit (0, 8, activeSnap);

    juce::var stepsArray;
    
    for (int i = 0; i < 15; ++i)
    {
        juce::DynamicObject::Ptr stepObj = new juce::DynamicObject();
        stepObj->setProperty ("pitch", audioProcessor.snapshots[activeSnap].steps[i].pitchOffset);
        stepObj->setProperty ("velocity", audioProcessor.snapshots[activeSnap].steps[i].velocity);
        stepObj->setProperty ("modwheel", audioProcessor.snapshots[activeSnap].steps[i].modwheel);
        stepObj->setProperty ("probability", audioProcessor.snapshots[activeSnap].steps[i].probability);
        stepObj->setProperty ("muted", audioProcessor.snapshots[activeSnap].steps[i].muted);
        stepsArray.append (juce::var (stepObj.get()));
    }
    
    return juce::JSON::toString (stepsArray, true).replace ("'", "\\'");
}
