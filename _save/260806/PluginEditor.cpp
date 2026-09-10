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
    getConstrainer()->setFixedAspectRatio (1040.0 / 1280.0);
    setResizeLimits (520, 640, 2080, 2560);
    setSize (1040, 1280);
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
    // 1. Detect edit snapshot changes FIRST, before the param push loop.
    int editSnap = audioProcessor.editSnapshot;

    if (editSnap != webView.localActiveSnapshot)
    {
        webView.localActiveSnapshot = editSnap;
        webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('editSnapshot', " + juce::String (editSnap + 1) + ");");

        // Invalidate ALL scalar param caches so the push loop re-sends every
        // parameter value for the newly active snapshot on this same tick.
        for (int i = 0; i < 7; ++i)
            webView.localParams[i] = -1.0f;

        // Invalidate steps cache
        for (int i = 0; i < 15; ++i)
        {
            webView.localSteps[i].pitchOffset = -999;
            webView.localSteps[i].velocity = -1;
            webView.localSteps[i].modwheel = -1;
            webView.localSteps[i].probability = -1;
            webView.localSteps[i].muted = !audioProcessor.snapshots[editSnap].steps[i].muted;
        }
    }

    // 2. Push per-snapshot scalar parameters.
    //    Read directly from snapshots[editSnap] — the authoritative per-snapshot store.
    auto& snap = audioProcessor.snapshots[editSnap];

    // enabled
    {
        float val = snap.enabled ? 1.0f : 0.0f;
        if (std::abs (val - webView.localParams[0]) > 0.001f)
        {
            webView.localParams[0] = val;
            AetherWebView::logToFile ("timer pushing: enabled = " + juce::String (val));
            webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('enabled', " + juce::String (snap.enabled ? "true" : "false") + ");");
        }
    }
    // delayTimeMs
    {
        float val = snap.delayTimeMs;
        if (std::abs (val - webView.localParams[1]) > 0.001f)
        {
            webView.localParams[1] = val;
            AetherWebView::logToFile ("timer pushing: delayTimeMs = " + juce::String (val));
            webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('delayTimeMs', " + juce::String (val) + ");");
        }
    }
    // syncDivision
    {
        float val = (float)snap.syncDivision;
        if (std::abs (val - webView.localParams[2]) > 0.001f)
        {
            webView.localParams[2] = val;
            AetherWebView::logToFile ("timer pushing: syncDivision = " + juce::String (snap.syncDivision));
            webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('syncDivision', " + juce::String (snap.syncDivision) + ");");
        }
    }
    // stepCount
    {
        float val = (float)snap.stepCount;
        if (std::abs (val - webView.localParams[3]) > 0.001f)
        {
            webView.localParams[3] = val;
            AetherWebView::logToFile ("timer pushing: stepCount = " + juce::String (snap.stepCount));
            webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('stepCount', " + juce::String (snap.stepCount) + ");");
        }
    }
    // killOnStop (Global param — read from APVTS)
    {
        auto* kosP = dynamic_cast<juce::AudioParameterBool*>(audioProcessor.apvts.getParameter("killOnStop"));
        bool kosBool = kosP ? kosP->get() : true;
        float val = kosBool ? 1.0f : 0.0f;
        if (std::abs (val - webView.localParams[4]) > 0.001f)
        {
            webView.localParams[4] = val;
            AetherWebView::logToFile ("timer pushing: killOnStop = " + juce::String (val));
            webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('killOnStop', " + juce::String (kosBool ? "true" : "false") + ");");
        }
    }
    // activeSnapshot — single global param, correctly read from APVTS
    {
        auto* actP = dynamic_cast<juce::AudioParameterInt*>(audioProcessor.apvts.getParameter("activeSnapshot"));
        int actVal = actP ? actP->get() : 1;
        float val = (float)actVal;
        if (std::abs (val - webView.localParams[5]) > 0.001f)
        {
            webView.localParams[5] = val;
            AetherWebView::logToFile ("timer pushing: activeSnapshot = " + juce::String (actVal));
            webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('activeSnapshot', " + juce::String (actVal) + ");");
        }
    }
    // killOnSwitch (Global param — read from APVTS)
    {
        auto* kswP = dynamic_cast<juce::AudioParameterBool*>(audioProcessor.apvts.getParameter("killOnSwitch"));
        bool kswBool = kswP ? kswP->get() : false;
        float val = kswBool ? 1.0f : 0.0f;
        if (std::abs (val - webView.localParams[6]) > 0.001f)
        {
            webView.localParams[6] = val;
            AetherWebView::logToFile ("timer pushing: killOnSwitch = " + juce::String (val));
            webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('killOnSwitch', " + juce::String (kswBool ? "true" : "false") + ");");
        }
    }

    // 3. Sync sequence steps from C++ snapshots to JS
    bool stepsChanged = false;
    for (int i = 0; i < 15; ++i)
    {
        if (snap.steps[i].pitchOffset != webView.localSteps[i].pitchOffset ||
            snap.steps[i].velocity    != webView.localSteps[i].velocity    ||
            snap.steps[i].modwheel    != webView.localSteps[i].modwheel    ||
            snap.steps[i].probability != webView.localSteps[i].probability ||
            snap.steps[i].muted       != webView.localSteps[i].muted)
        {
            webView.localSteps[i] = snap.steps[i];
            stepsChanged = true;
        }
    }

    if (stepsChanged)
    {
        juce::String jsonSteps = getStepsJson();
        webView.evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateStepsFromCpp('" + jsonSteps + "');");
    }

    // 4. Send Activity Hits to Visualizer
    int hits = audioProcessor.activityHits.exchange(0);
    if (hits > 0)
    {
        webView.evaluateJavascript("if (window.aetherUI && window.aetherUI.triggerActivity) window.aetherUI.triggerActivity(" + juce::String(hits) + ");");
    }
}

// ==========================================================================
// Helper to serialize steps to JSON string
// ==========================================================================
juce::String AetherAudioProcessorEditor::getStepsJson()
{
    int editSnap = audioProcessor.editSnapshot;
    
    juce::var stepsArray;
    
    for (int i = 0; i < 15; ++i)
    {
        juce::DynamicObject::Ptr stepObj = new juce::DynamicObject();
        stepObj->setProperty ("pitch", audioProcessor.snapshots[editSnap].steps[i].pitchOffset);
        stepObj->setProperty ("velocity", audioProcessor.snapshots[editSnap].steps[i].velocity);
        stepObj->setProperty ("modwheel", audioProcessor.snapshots[editSnap].steps[i].modwheel);
        stepObj->setProperty ("probability", audioProcessor.snapshots[editSnap].steps[i].probability);
        stepObj->setProperty ("muted", audioProcessor.snapshots[editSnap].steps[i].muted);
        stepsArray.append (juce::var (stepObj.get()));
    }
    
    return juce::JSON::toString (stepsArray, true).replace ("'", "\\'");
}
