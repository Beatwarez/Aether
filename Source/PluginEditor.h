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
    float localParams[7] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
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
        auto* actP = dynamic_cast<juce::AudioParameterInt*>(p.apvts.getParameter("activeSnapshot"));
        int activeSnap = (actP ? actP->get() : 1) - 1;
        activeSnap = juce::jlimit (0, 8, activeSnap);

        p.editSnapshot = activeSnap;
        int editSnap = p.editSnapshot;
        auto& snap = p.snapshots[editSnap];

        juce::DynamicObject::Ptr stateObj = new juce::DynamicObject();
        
        stateObj->setProperty ("isEnabled", snap.enabled);
        stateObj->setProperty ("delayMode", snap.syncDivision == 0 ? "ms" : "sync");
        stateObj->setProperty ("delayTimeMs", (double)snap.delayTimeMs);
        
        juce::StringArray SYNC_DIVISIONS = {
          "1/1d", "1/1", "1/1t",
          "1/2d", "1/2", "1/2t",
          "1/4d", "1/4", "1/4t",
          "1/8d", "1/8", "1/8t",
          "1/16d", "1/16", "1/16t",
          "1/32d", "1/32", "1/32t"
        };
        juce::String syncDivisionStr = (snap.syncDivision == 0) ? "ms" : SYNC_DIVISIONS[snap.syncDivision - 1];
        stateObj->setProperty ("syncDivision", syncDivisionStr);
        
        stateObj->setProperty ("stepCount", snap.stepCount);
        
        auto* kosP = dynamic_cast<juce::AudioParameterBool*>(p.apvts.getParameter("killOnStop"));
        stateObj->setProperty ("killOnStop", kosP ? kosP->get() : true);
        
        auto* kswP = dynamic_cast<juce::AudioParameterBool*>(p.apvts.getParameter("killOnSwitch"));
        stateObj->setProperty ("killOnSwitch", kswP ? kswP->get() : false);
        
        stateObj->setProperty ("activeSnapshot", activeSnap);
        stateObj->setProperty ("editSnapshot", editSnap);
        
        juce::var stepsArray;
        for (int i = 0; i < 15; ++i)
        {
            juce::DynamicObject::Ptr stepObj = new juce::DynamicObject();
            stepObj->setProperty ("pitch", snap.steps[i].pitchOffset);
            stepObj->setProperty ("velocity", snap.steps[i].velocity);
            stepObj->setProperty ("modwheel", snap.steps[i].modwheel);
            stepObj->setProperty ("probability", snap.steps[i].probability);
            stepObj->setProperty ("muted", snap.steps[i].muted);
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
                        for (int i = 0; i < 7; ++i)
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
                                int editSnap = p.editSnapshot;
                                
                                juce::String prop = jsonVar.getProperty("property", "").toString();
                                juce::var val = jsonVar.getProperty("value", 0);
                                if (prop == "pitch")
                                {
                                    p.snapshots[editSnap].steps[idx].pitchOffset = (int)val;
                                    webViewInstance->localSteps[idx].pitchOffset = (int)val;
                                }
                                else if (prop == "velocity")
                                {
                                    p.snapshots[editSnap].steps[idx].velocity = (int)val;
                                    webViewInstance->localSteps[idx].velocity = (int)val;
                                }
                                else if (prop == "modwheel")
                                {
                                    p.snapshots[editSnap].steps[idx].modwheel = (int)val;
                                    webViewInstance->localSteps[idx].modwheel = (int)val;
                                }
                                else if (prop == "probability")
                                {
                                    p.snapshots[editSnap].steps[idx].probability = (int)val;
                                    webViewInstance->localSteps[idx].probability = (int)val;
                                }
                                else if (prop == "muted")
                                {
                                    p.snapshots[editSnap].steps[idx].muted = (bool)val;
                                    webViewInstance->localSteps[idx].muted = (bool)val;
                                }
                            }
                        }
                    }
                    else if (paramName == "randomizeLane" || paramName == "resetLane")
                    {
                        juce::String prop = args[1].toString();
                        int editSnap = p.editSnapshot;
                        
                        juce::Random r;
                        for (int i = 0; i < 15; ++i)
                        {
                            auto& s = p.snapshots[editSnap].steps[i];
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
                    else if (paramName == "loadPreset")
                    {
                        auto jsonVar = juce::JSON::parse (args[1].toString());
                        if (auto* obj = jsonVar.getDynamicObject())
                        {
                            juce::String bank = obj->getProperty ("bank").toString();
                            juce::String preset = obj->getProperty ("preset").toString();
                            webViewInstance->loadPreset (bank, preset);
                        }
                    }
                    else if (paramName == "savePreset")
                    {
                        auto jsonVar = juce::JSON::parse (args[1].toString());
                        if (auto* obj = jsonVar.getDynamicObject())
                        {
                            juce::String bank = obj->getProperty ("bank").toString();
                            juce::String preset = obj->getProperty ("preset").toString();
                            webViewInstance->savePreset (bank, preset);
                        }
                    }
                    else if (paramName == "deletePreset")
                    {
                        auto jsonVar = juce::JSON::parse (args[1].toString());
                        if (auto* obj = jsonVar.getDynamicObject())
                        {
                            juce::String bank = obj->getProperty ("bank").toString();
                            juce::String preset = obj->getProperty ("preset").toString();
                            webViewInstance->deletePreset (bank, preset);
                        }
                    }
                    else if (paramName == "togglePresetFavorite")
                    {
                        auto jsonVar = juce::JSON::parse (args[1].toString());
                        if (auto* obj = jsonVar.getDynamicObject())
                        {
                            juce::String bank = obj->getProperty ("bank").toString();
                            juce::String preset = obj->getProperty ("preset").toString();
                            webViewInstance->togglePresetFavorite (bank, preset);
                        }
                    }
                    else if (paramName == "createBank")
                    {
                        juce::String bankName = args[1].toString();
                        webViewInstance->createBank (bankName);
                    }
                    else if (paramName == "refreshPresetManager")
                    {
                        webViewInstance->triggerPresetManagerUpdate();
                    }
                    else if (paramName == "copyActiveSnapshot")
                    {
                        int editSnap = p.editSnapshot;
                        p.copiedSnapshot = p.snapshots[editSnap];
                        p.hasCopiedSnapshot = true;
                        logToFile ("copyActiveSnapshot: snapshot " + juce::String (editSnap + 1) + " copied.");
                    }
                    else if (paramName == "pasteActiveSnapshot")
                    {
                        if (p.hasCopiedSnapshot)
                        {
                            int editSnap = p.editSnapshot;
                            p.snapshots[editSnap] = p.copiedSnapshot;
                            
                            // Reset all caches so the timer pushes the active snapshot's unique parameters to the JS UI on the next tick
                            for (int i = 0; i < 7; ++i)
                                webViewInstance->localParams[i] = -1.0f;
                            webViewInstance->localStepCount = -1;
                            webViewInstance->localActiveSnapshot = -1;
                            logToFile ("pasteActiveSnapshot: copied state pasted into snapshot " + juce::String (editSnap + 1) + ".");
                        }
                        else
                        {
                            logToFile ("pasteActiveSnapshot: WARNING - no copied snapshot in buffer!");
                        }
                    }
                    else if (paramName == "activeSnapshot")
                    {
                        float paramValue = (float)args[1];
                        p.editSnapshot = (int)paramValue - 1; // Also set edit snapshot
                        
                        // Reset all caches so the timer pushes the active snapshot's unique parameters to the JS UI on the next tick
                        for (int i = 0; i < 7; ++i)
                            webViewInstance->localParams[i] = -1.0f;
                        webViewInstance->localStepCount = -1;
                        webViewInstance->localActiveSnapshot = -1;
                        
                        if (auto* rawVal = p.apvts.getRawParameterValue ("activeSnapshot"))
                            rawVal->store (paramValue);
                             
                        if (auto* param = p.apvts.getParameter ("activeSnapshot"))
                        {
                            param->beginChangeGesture();
                            if (auto* intParam = dynamic_cast<juce::AudioParameterInt*> (param))
                                *intParam = (int)paramValue;
                            else if (auto* rangedParam = dynamic_cast<juce::RangedAudioParameter*> (param))
                                rangedParam->setValueNotifyingHost (rangedParam->getNormalisableRange().convertTo0to1 (paramValue));
                            else
                                param->setValueNotifyingHost (paramValue);
                            param->endChangeGesture();
                        }
                    }
                    else if (paramName == "editSnapshot")
                    {
                        float paramValue = (float)args[1];
                        p.editSnapshot = (int)paramValue - 1;
                        
                        // Reset caches to trigger a push
                        for (int i = 0; i < 7; ++i)
                            webViewInstance->localParams[i] = -1.0f;
                        webViewInstance->localStepCount = -1;
                        
                        webViewInstance->evaluateJavascript ("if (window.aetherUI) window.aetherUI.updateParamFromCpp('editSnapshot', " + juce::String ((int)paramValue) + ");");
                    }
                    else
                    {
                        float paramValue = (float)args[1];
                        int editSnap = p.editSnapshot;
                        
                        if (paramName == "stepCount")
                            p.snapshots[editSnap].stepCount = (int)paramValue;
                        else if (paramName == "enabled")
                            p.snapshots[editSnap].enabled = (paramValue > 0.5f);
                        else if (paramName == "delayTimeMs")
                            p.snapshots[editSnap].delayTimeMs = paramValue;
                        else if (paramName == "syncDivision")
                            p.snapshots[editSnap].syncDivision = (int)paramValue;
                        // killOnStop is global now, just APVTS update

                        if (auto* rawVal = p.apvts.getRawParameterValue (paramName))
                        {
                            rawVal->store (paramValue);
                        }
                        if (auto* param = p.apvts.getParameter (paramName))
                        {
                            param->beginChangeGesture();
                            if (auto* intParam = dynamic_cast<juce::AudioParameterInt*> (param))
                                *intParam = (int)paramValue;
                            else if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*> (param))
                                *boolParam = (paramValue > 0.5f);
                            else if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (param))
                                *floatParam = paramValue;
                            else if (auto* rangedParam = dynamic_cast<juce::RangedAudioParameter*> (param))
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
        triggerPresetManagerUpdate();
    }

    juce::String getPresetManagerStatusJson()
    {
        juce::DynamicObject::Ptr statusObj = new juce::DynamicObject();
        statusObj->setProperty ("currentBank", processor.currentBank);
        statusObj->setProperty ("currentPreset", processor.currentPreset);

        juce::Array<juce::var> banksArray;
        juce::Array<juce::var> presetsArray;

        auto presetsFolder = processor.getPresetsFolder();
        auto files = presetsFolder.findChildFiles (juce::File::findFiles, false, "*.xml");
        
        for (const auto& f : files)
        {
            juce::String bankName = f.getFileNameWithoutExtension();
            banksArray.add (bankName);

            if (bankName == processor.currentBank)
            {
                juce::XmlDocument doc (f);
                if (auto root = doc.getDocumentElement())
                {
                    for (auto* child : root->getChildIterator())
                    {
                        if (child->hasAttribute ("name"))
                        {
                            juce::DynamicObject::Ptr presetObj = new juce::DynamicObject();
                            presetObj->setProperty ("name", child->getStringAttribute ("name"));
                            presetObj->setProperty ("favorite", child->getBoolAttribute ("favorite", false));
                            presetsArray.add (juce::var (presetObj.get()));
                        }
                    }
                }
            }
        }

        statusObj->setProperty ("banks", banksArray);
        statusObj->setProperty ("presets", presetsArray);

        return juce::JSON::toString (juce::var (statusObj.get()), true).replace ("'", "\\'");
    }

    void triggerPresetManagerUpdate()
    {
        juce::String jsonStr = getPresetManagerStatusJson();
        evaluateJavascript ("if (window.aetherUI) window.aetherUI.updatePresetManager('" + jsonStr + "');");
    }

    void loadPreset (const juce::String& bank, const juce::String& preset)
    {
        if (preset.isEmpty())
        {
            processor.currentBank = bank;
            triggerPresetManagerUpdate();
            return;
        }
        juce::File file = processor.getPresetsFolder().getChildFile (bank + ".xml");
        juce::XmlDocument doc (file);
        if (auto root = doc.getDocumentElement())
        {
            for (auto* child : root->getChildIterator())
            {
                if (child->getStringAttribute ("name") == preset)
                {
                    processor.currentBank = bank;
                    processor.currentPreset = preset;
                    processor.loadStateFromXml (*child);
                    
                    // Trigger reload of active snapshot parameter in C++
                    auto* actP = dynamic_cast<juce::AudioParameterInt*> (processor.apvts.getParameter ("activeSnapshot"));
                    int activeSnap = (actP ? actP->get() : 1) - 1;
                    activeSnap = juce::jlimit (0, 8, activeSnap);
                    processor.editSnapshot = activeSnap;
                    processor.loadSnapshotParameters (activeSnap);
                    
                    // Update JS UI with new full state and trigger preset status update
                    juce::String fullStateJson = getFullStateJson (processor);
                    evaluateJavascript ("if (window.aetherUI) { window.aetherUI.initializeState('" + fullStateJson + "'); }");
                    
                    // Reset all caches in the editor so that they update correctly on next timer tick
                    for (int i = 0; i < 7; ++i)
                        localParams[i] = -1.0f;
                    localStepCount = -1;
                    localActiveSnapshot = -1;
                    
                    triggerPresetManagerUpdate();
                    return;
                }
            }
        }
    }

    void savePreset (const juce::String& bank, const juce::String& preset)
    {
        juce::File file = processor.getPresetsFolder().getChildFile (bank + ".xml");
        juce::XmlDocument doc (file);
        std::unique_ptr<juce::XmlElement> root = doc.getDocumentElement();
        if (root == nullptr || root->getTagName() != "AetherPresets")
        {
            root = std::make_unique<juce::XmlElement> ("AetherPresets");
        }

        auto presetXml = processor.createStateXml();
        presetXml->setAttribute ("name", preset);

        bool wasFav = false;
        juce::XmlElement* existingChild = root->getChildByName (presetXml->getTagName());
        while (existingChild != nullptr)
        {
            if (existingChild->getStringAttribute ("name") == preset)
            {
                wasFav = existingChild->getBoolAttribute ("favorite", false);
                root->removeChildElement (existingChild, true);
                existingChild = root->getChildByName (presetXml->getTagName());
            }
            else
            {
                existingChild = existingChild->getNextElement();
            }
        }

        presetXml->setAttribute ("favorite", wasFav);
        root->addChildElement (presetXml.release());
        root->writeTo (file);

        processor.currentBank = bank;
        processor.currentPreset = preset;

        triggerPresetManagerUpdate();
    }

    void deletePreset (const juce::String& bank, const juce::String& preset)
    {
        juce::File file = processor.getPresetsFolder().getChildFile (bank + ".xml");
        if (!file.existsAsFile()) return;

        juce::XmlDocument doc (file);
        std::unique_ptr<juce::XmlElement> root = doc.getDocumentElement();
        if (root != nullptr)
        {
            juce::XmlElement* childToRemove = nullptr;
            for (auto* child : root->getChildIterator())
            {
                if (child->getStringAttribute ("name") == preset)
                {
                    childToRemove = child;
                    break;
                }
            }
            if (childToRemove != nullptr)
            {
                root->removeChildElement (childToRemove, true);
                root->writeTo (file);
                if (processor.currentPreset == preset)
                {
                    processor.currentPreset = "Init";
                }
                triggerPresetManagerUpdate();
            }
        }
    }

    void togglePresetFavorite (const juce::String& bank, const juce::String& preset)
    {
        juce::File file = processor.getPresetsFolder().getChildFile (bank + ".xml");
        if (!file.existsAsFile()) return;

        juce::XmlDocument doc (file);
        std::unique_ptr<juce::XmlElement> root = doc.getDocumentElement();
        if (root != nullptr)
        {
            for (auto* child : root->getChildIterator())
            {
                if (child->getStringAttribute ("name") == preset)
                {
                    bool isFav = child->getBoolAttribute ("favorite", false);
                    child->setAttribute ("favorite", !isFav);
                    root->writeTo (file);
                    triggerPresetManagerUpdate();
                    return;
                }
            }
        }
    }

    void createBank (const juce::String& bankName)
    {
        juce::File file = processor.getPresetsFolder().getChildFile (bankName + ".xml");
        if (!file.exists())
        {
            juce::XmlElement root ("AetherPresets");
            root.writeTo (file);
        }
        processor.currentBank = bankName;
        processor.currentPreset = "Init";
        triggerPresetManagerUpdate();
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

    void loadWindowSize();
    void saveWindowSize();

    // Helper to serialize all 15 steps to JSON
    juce::String getStepsJson();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AetherAudioProcessorEditor)
};