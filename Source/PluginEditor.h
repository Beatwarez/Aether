// PluginEditor.h
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ==========================================================================
// Custom WebBrowserComponent Subclass for JS-to-C++ Parameter Bridging
// ==========================================================================
class AetherWebView : public juce::WebBrowserComponent
{
public:
    // Caches to avoid redundant C++ -> JS messages
    float localParams[6] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    DelayStep localSteps[15];
    int localStepCount = -1;
    int localActiveSnapshot = -1;

    static void logToFile (const juce::String& message)
    {
        auto logFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("AetherSynthDebug.txt");
        logFile.appendText (message + "\n");
    }

    static juce::String getFullStateJson (AetherAudioProcessor& p)
    {
        int activeSnap = (int)p.apvts.getRawParameterValue ("activeSnapshot")->load() - 1;
        activeSnap = juce::jlimit (0, 8, activeSnap);

        juce::DynamicObject::Ptr stateObj = new juce::DynamicObject();
        
        stateObj->setProperty ("isEnabled", (bool)(p.apvts.getRawParameterValue ("enabled")->load() > 0.5f));
        stateObj->setProperty ("delayMode", (int)p.apvts.getRawParameterValue ("syncDivision")->load() == 0 ? "ms" : "sync");
        stateObj->setProperty ("delayTimeMs", (double)p.apvts.getRawParameterValue ("delayTimeMs")->load());
        
        int syncIdx = (int)p.apvts.getRawParameterValue ("syncDivision")->load();
        juce::StringArray SYNC_DIVISIONS = {
          "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
          "1/1d", "1/2d", "1/4d", "1/8d", "1/16d", "1/32d",
          "1/1t", "1/2t", "1/4t", "1/8t", "1/16t", "1/32t"
        };
        juce::String syncDivisionStr = (syncIdx == 0) ? "ms" : SYNC_DIVISIONS[syncIdx - 1];
        stateObj->setProperty ("syncDivision", syncDivisionStr);
        
        stateObj->setProperty ("stepCount", p.snapshots[activeSnap].stepCount);
        stateObj->setProperty ("killOnStop", (bool)(p.apvts.getRawParameterValue ("killOnStop")->load() > 0.5f));
        stateObj->setProperty ("activeSnapshot", activeSnap);
        
        juce::var stepsArray;
        for (int i = 0; i < 15; ++i)
        {
            juce::DynamicObject::Ptr stepObj = new juce::DynamicObject();
            stepObj->setProperty ("pitch", p.snapshots[activeSnap].steps[i].pitchOffset);
            stepObj->setProperty ("velocity", p.snapshots[activeSnap].steps[i].velocity);
            stepObj->setProperty ("modwheel", p.snapshots[activeSnap].steps[i].modwheel);
            stepObj->setProperty ("probability", p.snapshots[activeSnap].steps[i].probability);
            stepObj->setProperty ("muted", p.snapshots[activeSnap].steps[i].muted);
            stepsArray.append (juce::var (stepObj.get()));
        }
        stateObj->setProperty ("steps", stepsArray);
        
        return juce::JSON::toString (juce::var (stateObj.get()), true).replace ("'", "\\'");
    }

    static juce::WebBrowserComponent::Options getOptions (AetherWebView* webViewInstance, AetherAudioProcessor& p)
    {
        // Force clear WebView2 cache folder to bypass local caching
        auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("AetherSynth/WebView2Data");
        folder.deleteRecursively();

        logToFile ("--- WebView Initialized ---");

        return juce::WebBrowserComponent::Options()
            .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2()
                .withUserDataFolder (folder))
            .withNativeIntegrationEnabled (true)
            .withResourceProvider ([] (const juce::String& url) -> std::optional<juce::WebBrowserComponent::Resource>
            {
                auto retrieveResource = [] (const char* data, int size, const juce::String& mime)
                {
                    std::vector<std::byte> vec;
                    vec.resize ((size_t) size);
                    std::memcpy (vec.data(), data, (size_t) size);
                    return juce::WebBrowserComponent::Resource { std::move (vec), mime };
                };

                const auto urlToRetrieve = url == "/" ? juce::String ("index.html")
                                                      : url.fromFirstOccurrenceOf ("/", false, false);

                // Serve resources from BinaryData
                if (urlToRetrieve == "index.html")
                    return retrieveResource (BinaryData::index_html, BinaryData::index_htmlSize, "text/html");
                if (urlToRetrieve.contains ("app.js"))
                    return retrieveResource (BinaryData::app_js, BinaryData::app_jsSize, "application/javascript");
                if (urlToRetrieve.contains ("styles.css"))
                    return retrieveResource (BinaryData::styles_css, BinaryData::styles_cssSize, "text/css");

                return std::nullopt;
            })
            .withNativeFunction ("sendParamToCpp", [webViewInstance, &p](const juce::var& args, std::function<void (juce::var)> completion)
            {
                logToFile ("C++: sendParamToCpp called. args size = " + juce::String (args.size()));
                if (args.size() >= 2)
                {
                    juce::String paramName = args[0].toString();
                    
                    if (paramName == "queryall")
                    {
                        // Reset caches to sentinel values so the timer pushes full state
                        for (int i = 0; i < 6; ++i)
                            webViewInstance->localParams[i] = -1.0f;
                        webViewInstance->localStepCount = -1;
                        webViewInstance->localActiveSnapshot = -1;
                        
                        int activeSnap = (int)p.apvts.getRawParameterValue ("activeSnapshot")->load() - 1;
                        activeSnap = juce::jlimit (0, 8, activeSnap);
                        
                        for (int i = 0; i < 15; ++i) {
                            webViewInstance->localSteps[i].pitchOffset = -999;
                            webViewInstance->localSteps[i].velocity = -1;
                            webViewInstance->localSteps[i].modwheel = -1;
                            webViewInstance->localSteps[i].probability = -1;
                            webViewInstance->localSteps[i].muted = !p.snapshots[activeSnap].steps[i].muted;
                        }
                        logToFile ("queryall received: caches reset, timer will push full state.");
                    }
                    else if (paramName == "stepUpdate")
                    {
                        juce::String jsonStr = args[1].toString();
                        auto jsonVar = juce::JSON::parse (jsonStr);
                        if (jsonVar.isObject())
                        {
                            int idx = (int)jsonVar.getProperty("index", -1);
                            if (idx >= 0 && idx < 15)
                            {
                                int activeSnap = (int)p.apvts.getRawParameterValue ("activeSnapshot")->load() - 1;
                                activeSnap = juce::jlimit (0, 8, activeSnap);
                                
                                juce::String prop = jsonVar.getProperty("property", "").toString();
                                juce::var val = jsonVar.getProperty("value", 0);
                                if (prop == "pitch")
                                {
                                    p.snapshots[activeSnap].steps[idx].pitchOffset = (int)val;
                                    webViewInstance->localSteps[idx].pitchOffset = (int)val;
                                }
                                else if (prop == "velocity")
                                {
                                    p.snapshots[activeSnap].steps[idx].velocity = (int)val;
                                    webViewInstance->localSteps[idx].velocity = (int)val;
                                }
                                else if (prop == "modwheel")
                                {
                                    p.snapshots[activeSnap].steps[idx].modwheel = (int)val;
                                    webViewInstance->localSteps[idx].modwheel = (int)val;
                                }
                                else if (prop == "probability")
                                {
                                    p.snapshots[activeSnap].steps[idx].probability = (int)val;
                                    webViewInstance->localSteps[idx].probability = (int)val;
                                }
                                else if (prop == "muted")
                                {
                                    p.snapshots[activeSnap].steps[idx].muted = (bool)val;
                                    webViewInstance->localSteps[idx].muted = (bool)val;
                                }
                            }
                        }
                    }
                    else if (paramName == "randomizeLane" || paramName == "resetLane")
                    {
                        juce::String prop = args[1].toString();
                        int activeSnap = (int)p.apvts.getRawParameterValue ("activeSnapshot")->load() - 1;
                        activeSnap = juce::jlimit (0, 8, activeSnap);
                        
                        juce::Random r;
                        for (int i = 0; i < 15; ++i)
                        {
                            auto& s = p.snapshots[activeSnap].steps[i];
                            if (paramName == "randomizeLane")
                            {
                                if (prop == "pitch") s.pitchOffset = r.nextInt(juce::Range<int>(-24, 25));
                                else if (prop == "velocity") s.velocity = r.nextInt(juce::Range<int>(1, 128));
                                else if (prop == "modwheel") s.modwheel = r.nextInt(juce::Range<int>(0, 128));
                                else if (prop == "probability") s.probability = r.nextInt(juce::Range<int>(0, 101));
                                else if (prop == "muted") s.muted = r.nextBool();
                            }
                            else // resetLane
                            {
                                if (prop == "pitch") s.pitchOffset = 0;
                                else if (prop == "velocity") s.velocity = (int)(127 - (i * (126.0 / 14.0)));
                                else if (prop == "modwheel") s.modwheel = 0;
                                else if (prop == "probability") s.probability = 100;
                                else if (prop == "muted") s.muted = false;
                            }
                        }
                    }
                    else if (paramName == "copyActiveSnapshot")
                    {
                        int activeSnap = (int)p.apvts.getRawParameterValue ("activeSnapshot")->load() - 1;
                        activeSnap = juce::jlimit (0, 8, activeSnap);
                        p.copiedSnapshot = p.snapshots[activeSnap];
                        p.hasCopiedSnapshot = true;
                        logToFile ("copyActiveSnapshot: snapshot " + juce::String (activeSnap + 1) + " copied.");
                    }
                    else if (paramName == "pasteActiveSnapshot")
                    {
                        if (p.hasCopiedSnapshot)
                        {
                            int activeSnap = (int)p.apvts.getRawParameterValue ("activeSnapshot")->load() - 1;
                            activeSnap = juce::jlimit (0, 8, activeSnap);
                            p.snapshots[activeSnap] = p.copiedSnapshot;
                            
                            // Trigger parameterChanged to load parameter values of the pasted snapshot
                            p.parameterChanged ("activeSnapshot", (float)(activeSnap + 1));
                            
                            // Invalidate local active snapshot cache to push steps to JS
                            webViewInstance->localActiveSnapshot = -1;
                            logToFile ("pasteActiveSnapshot: copied state pasted into snapshot " + juce::String (activeSnap + 1) + ".");
                        }
                        else
                        {
                            logToFile ("pasteActiveSnapshot: WARNING - no copied snapshot in buffer!");
                        }
                    }
                    else
                    {
                        float paramValue = (float)args[1];
                        int activeSnap = (int)p.apvts.getRawParameterValue ("activeSnapshot")->load() - 1;
                        activeSnap = juce::jlimit (0, 8, activeSnap);
                        
                        if (paramName == "stepCount")
                            p.snapshots[activeSnap].stepCount = (int)paramValue;
                        else if (paramName == "enabled")
                            p.snapshots[activeSnap].enabled = (paramValue > 0.5f);
                        else if (paramName == "delayTimeMs")
                            p.snapshots[activeSnap].delayTimeMs = paramValue;
                        else if (paramName == "syncDivision")
                            p.snapshots[activeSnap].syncDivision = (int)paramValue;
                        else if (paramName == "killOnStop")
                            p.snapshots[activeSnap].killOnStop = (paramValue > 0.5f);

                        if (auto* rawVal = p.apvts.getRawParameterValue (paramName))
                        {
                            rawVal->store (paramValue);
                        }
                        if (auto* param = p.apvts.getParameter (paramName))
                        {
                            param->beginChangeGesture();
                            if (auto* rangedParam = dynamic_cast<juce::RangedAudioParameter*> (param))
                                rangedParam->setValueNotifyingHost (rangedParam->getNormalisableRange().convertTo0to1 (paramValue));
                            else
                                param->setValueNotifyingHost (paramValue);
                            param->endChangeGesture();
                        }
                    }
                }
                completion (juce::var (true));
            });
    }

    AetherWebView (AetherAudioProcessor& p)
        : juce::WebBrowserComponent (getOptions (this, p)),
          processor (p)
    {
        for (int i = 0; i < 15; ++i)
        {
            localSteps[i].pitchOffset = -999;
            localSteps[i].velocity = -1;
            localSteps[i].modwheel = -1;
            localSteps[i].probability = -1;
            localSteps[i].muted = false;
        }
    }

    // Called by JUCE when the WebView page has fully loaded.
    // At this point window.aetherUI is already defined (set synchronously in app.js).
    // Reset all caches to sentinel values — the timer will detect differences on its
    // next tick and push the complete loaded state to JS safely from the message thread.
    void pageFinishedLoading (const juce::String& /*url*/) override
    {
        for (int i = 0; i < 6; ++i)
            localParams[i] = -1.0f;
        localStepCount = -1;
        localActiveSnapshot = -1;
        
        int activeSnap = (int)processor.apvts.getRawParameterValue ("activeSnapshot")->load() - 1;
        activeSnap = juce::jlimit (0, 8, activeSnap);
        
        for (int i = 0; i < 15; ++i)
        {
            localSteps[i].pitchOffset = -999;
            localSteps[i].velocity    = -1;
            localSteps[i].modwheel    = -1;
            localSteps[i].probability = -1;
            localSteps[i].muted       = !processor.snapshots[activeSnap].steps[i].muted; // guaranteed mismatch
        }
        
        // Evaluate JS to pass full initial state:
        juce::String fullStateJson = getFullStateJson (processor);
        evaluateJavascript ("if (window.aetherUI) window.aetherUI.initializeState('" + fullStateJson + "');");
        logToFile ("pageFinishedLoading: caches reset, timer will push full state on next tick.");
    }

private:
    AetherAudioProcessor& processor;
};

// ==========================================================================
// Plugin Editor Class
// ==========================================================================
class AetherAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   public juce::Timer
{
public:
    AetherAudioProcessorEditor (AetherAudioProcessor&);
    ~AetherAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    AetherAudioProcessor& audioProcessor;
    AetherWebView webView;

    // Helper to serialize all 15 steps to JSON
    juce::String getStepsJson();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AetherAudioProcessorEditor)
};