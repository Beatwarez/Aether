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
    float localParams[8] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    DelayStep localSteps[15];
    int localStepCount = -1;

    static void logToFile (const juce::String& message)
    {
        auto logFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("AetherSynthDebug.txt");
        logFile.appendText (message + "\n");
    }

    static juce::String getFullStateJson (AetherAudioProcessor& p)
    {
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
        
        stateObj->setProperty ("stepCount", (int)p.apvts.getRawParameterValue ("stepCount")->load());
        stateObj->setProperty ("killOnStop", (bool)(p.apvts.getRawParameterValue ("killOnStop")->load() > 0.5f));
        stateObj->setProperty ("isLooping", (bool)(p.apvts.getRawParameterValue ("loopEnabled")->load() > 0.5f));
        
        int loopModeIdx = (int)p.apvts.getRawParameterValue ("loopMode")->load();
        juce::String loopModeStr = (loopModeIdx == 0) ? "forward" : (loopModeIdx == 1 ? "pendulum" : "random");
        stateObj->setProperty ("loopMode", loopModeStr);
        
        stateObj->setProperty ("loopNoteRestart", (bool)(p.apvts.getRawParameterValue ("loopRestart")->load() > 0.5f));
        
        juce::var stepsArray;
        for (int i = 0; i < 15; ++i)
        {
            juce::DynamicObject::Ptr stepObj = new juce::DynamicObject();
            stepObj->setProperty ("pitch", p.steps[i].pitchOffset);
            stepObj->setProperty ("velocity", p.steps[i].velocity);
            stepObj->setProperty ("modwheel", p.steps[i].modwheel);
            stepObj->setProperty ("probability", p.steps[i].probability);
            stepObj->setProperty ("muted", p.steps[i].muted);
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

                // Serve index.html from BinaryData
                if (urlToRetrieve == "index.html")
                    return retrieveResource (BinaryData::index_html, BinaryData::index_htmlSize, "text/html");

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
                        // Mark localParams to force initial sync from C++ to JS
                        for (int i = 0; i < 8; ++i)
                            webViewInstance->localParams[i] = -1.0f;
                        webViewInstance->localStepCount = -1;
                        for (int i = 0; i < 15; ++i) {
                            webViewInstance->localSteps[i].pitchOffset = -999;
                            webViewInstance->localSteps[i].velocity = -1;
                            webViewInstance->localSteps[i].modwheel = -1;
                            webViewInstance->localSteps[i].probability = -1;
                            webViewInstance->localSteps[i].muted = !p.steps[i].muted;
                        }
                        
                        // Push full state back to the UI immediately
                        juce::String json = getFullStateJson (p);
                        webViewInstance->evaluateJavascript ("if (window.aetherUI) window.aetherUI.initializeState('" + json + "');");
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
                                juce::String prop = jsonVar.getProperty("property", "").toString();
                                juce::var val = jsonVar.getProperty("value", 0);
                                if (prop == "pitch")
                                {
                                    p.steps[idx].pitchOffset = (int)val;
                                    webViewInstance->localSteps[idx].pitchOffset = (int)val;
                                }
                                else if (prop == "velocity")
                                {
                                    p.steps[idx].velocity = (int)val;
                                    webViewInstance->localSteps[idx].velocity = (int)val;
                                }
                                else if (prop == "modwheel")
                                {
                                    p.steps[idx].modwheel = (int)val;
                                    webViewInstance->localSteps[idx].modwheel = (int)val;
                                }
                                else if (prop == "probability")
                                {
                                    p.steps[idx].probability = (int)val;
                                    webViewInstance->localSteps[idx].probability = (int)val;
                                }
                                else if (prop == "muted")
                                {
                                    p.steps[idx].muted = (bool)val;
                                    webViewInstance->localSteps[idx].muted = (bool)val;
                                }
                            }
                        }
                    }
                    else if (paramName == "randomizeLane" || paramName == "resetLane")
                    {
                        juce::String prop = args[1].toString();
                        juce::Random r;
                        for (int i = 0; i < 15; ++i)
                        {
                            auto& s = p.steps[i];
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
                    else
                    {
                        float paramValue = (float)args[1];
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