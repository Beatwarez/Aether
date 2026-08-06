#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

AetherAudioProcessor::AetherAudioProcessor()
    : AudioProcessor(BusesProperties()),
      apvts(*this, nullptr, "Parameters", createParameterLayout()) {
  for (int s = 0; s < 9; ++s) {
    snapshots[s].stepCount = 15;
    snapshots[s].enabled = true;
    snapshots[s].delayTimeMs = 500.0f;
    snapshots[s].syncDivision = 0;
    for (int i = 0; i < 15; ++i) {
      snapshots[s].steps[i].velocity = (int)(127 - (i * (126.0 / 14.0)));
      snapshots[s].steps[i].modwheel = 0;
      snapshots[s].steps[i].probability = 100;
      snapshots[s].steps[i].pitchOffset = 0;
      snapshots[s].steps[i].muted = false;
    }
  }

  // Register parameter change listeners
  apvts.addParameterListener ("enabled", this);
  apvts.addParameterListener ("delayTimeMs", this);
  apvts.addParameterListener ("syncDivision", this);
  apvts.addParameterListener ("stepCount", this);
  apvts.addParameterListener ("killOnStop", this);
  apvts.addParameterListener ("killOnSwitch", this);
  apvts.addParameterListener ("activeSnapshot", this);

  initFactoryPresets();
}

AetherAudioProcessor::~AetherAudioProcessor() {
  apvts.removeParameterListener ("enabled", this);
  apvts.removeParameterListener ("delayTimeMs", this);
  apvts.removeParameterListener ("syncDivision", this);
  apvts.removeParameterListener ("stepCount", this);
  apvts.removeParameterListener ("killOnStop", this);
  apvts.removeParameterListener ("killOnSwitch", this);
  apvts.removeParameterListener ("activeSnapshot", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout
AetherAudioProcessor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      "delayTimeMs", "Delay Time (ms)", 1.0f, 2000.0f, 500.0f));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      "enabled", "Enabled", true));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      "stepCount", "Step Count", 1, 15, 15));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      "killOnStop", "Kill On Stop", true));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      "killOnSwitch", "Kill On Switch", false));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      "syncDivision", "Sync Division", 0, 18, 0));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      "activeSnapshot", "Active Snapshot", 1, 9, 1));
  return layout;
}

void AetherAudioProcessor::prepareToPlay(double sampleRate,
                                         int samplesPerBlock) {
  lastSampleRate = (sampleRate > 0) ? sampleRate : 44100.0;
  totalSamplesProcessed = 0;
  midiQueue.clear();
  activeNotes.clear();
  noteTracker.clear();
  wasPlaying = false;

  float currentMs = *apvts.getRawParameterValue("delayTimeMs");
  smoothedDelaySamples.reset(lastSampleRate, 0.1);
  smoothedDelaySamples.setCurrentAndTargetValue(currentMs * 0.001f *
                                                (float)lastSampleRate);
}

double AetherAudioProcessor::getSyncTimeInMs() {
  auto* syncP = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("syncDivision"));
  int syncIdx = syncP ? syncP->get() : 0;
  if (syncIdx == 0)
    return (double)*apvts.getRawParameterValue("delayTimeMs");

  if (auto *ph = getPlayHead()) {
    if (auto pos = ph->getPosition()) {
      auto bpmOpt = pos->getBpm();
      double bpm = bpmOpt ? *bpmOpt : 120.0;
      double qnMs = (60.0 / bpm) * 1000.0;
      
      int step = (syncIdx - 1) / 3; // 0 to 5: 1/1, 1/2, 1/4, 1/8, 1/16, 1/32
      int type = (syncIdx - 1) % 3; // 0: dotted, 1: straight, 2: triplet
      
      double baseTime = qnMs * 4.0 * std::pow(0.5, step);
      double multiplier = (type == 0 ? 1.5 : (type == 2 ? (2.0 / 3.0) : 1.0));
      
      return baseTime * multiplier;
    }
  }
  return (double)*apvts.getRawParameterValue("delayTimeMs");
}

void AetherAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                        juce::MidiBuffer &midiMessages) {
  auto numSamples = buffer.getNumSamples();
  
  // Clear all audio outputs to prevent garbage/feedback noise
  for (int i = 0; i < buffer.getNumChannels(); ++i) {
    buffer.clear (i, 0, numSamples);
  }

  bool isPlaying = false;
  if (auto *ph = getPlayHead()) {
    if (auto pos = ph->getPosition())
      isPlaying = pos->getIsPlaying();
  }

  // If we just loaded a project, the host has sequentially restored the parameters.
  // We trigger an async update on the message thread to load the correct active snapshot parameters
  // back into the host's APVTS parameters.
  bool expected = true;
  if (isInitializing.compare_exchange_strong (expected, false)) {
    triggerAsyncUpdate();
  }

  auto* killP = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("killOnStop"));
  bool pKill = killP ? killP->get() : true;
  auto* enP = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("enabled"));
  bool pEnabled = enP ? enP->get() : true;
  auto* syncP = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("syncDivision"));
  int syncIdx = syncP ? syncP->get() : 0;
  auto* actP = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("activeSnapshot"));
  int activeSnap = (actP ? actP->get() : 1) - 1;
  activeSnap = juce::jlimit(0, 8, activeSnap);
  int pStepCount = snapshots[activeSnap].stepCount;

  auto killActiveMidiNotes = [&]() {
    for (const auto& note : activeNotes) {
      midiMessages.addEvent(juce::MidiMessage::noteOff(note.first, note.second, 0.0f), 0);
    }
    for (int ch = 1; ch <= 16; ++ch) {
      midiMessages.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
    }
    activeNotes.clear();
    midiQueue.clear();
    noteTracker.clear();
  };

  // Handle STOP button: immediately clear all loop state
  if (stopRequested.exchange(false)) {
    killActiveMidiNotes();
  }

  if (pKill && !isPlaying && wasPlaying) {
    killActiveMidiNotes();
  }
  wasPlaying = isPlaying;

  if (!pEnabled || (pKill && !isPlaying)) {
    if (!activeNotes.empty() || !midiQueue.empty()) {
      killActiveMidiNotes();
    }
    totalSamplesProcessed += numSamples;
    return;
  }

  double targetMs = (syncIdx == 0)
                        ? (double)*apvts.getRawParameterValue("delayTimeMs")
                        : getSyncTimeInMs();
  targetMs = juce::jmax(1.0, targetMs);
  smoothedDelaySamples.setTargetValue(
      (float)(targetMs * 0.001 * lastSampleRate));

  std::vector<QueuedEvent> additions;

  for (const auto metadata : midiMessages) {
    auto msg = metadata.getMessage();
    int localPos = metadata.samplePosition;
    int noteKey = (msg.getChannel() << 7) | msg.getNoteNumber();

    if (msg.isNoteOn()) {
      activityHits++;
      std::array<int, 15> cap;
      for (int i = 0; i < 15; ++i)
        cap[i] = snapshots[activeSnap].steps[i].pitchOffset;

      NoteState ns;
      ns.channel = msg.getChannel();
      ns.noteNumber = msg.getNoteNumber();
      ns.velocity = msg.getVelocity();
      ns.currentStepIndex = 0;
      ns.directionForward = true;
      ns.lastPlayedNote = -1;
      ns.pitchCaps = cap;
      noteTracker[noteKey] = ns;

      long long origin = totalSamplesProcessed + localPos;

      // Classic: schedule all steps simultaneously
      for (int i = 0; i < pStepCount; ++i) {
        if (snapshots[activeSnap].steps[i].muted || random.nextInt(100) >= snapshots[activeSnap].steps[i].probability)
          continue;
        int targetNote =
            juce::jlimit<int>(0, 127, msg.getNoteNumber() + cap[i]);
        additions.push_back(
            {juce::MidiMessage::noteOff(msg.getChannel(), targetNote), origin,
             i + 1, i, noteKey});
        auto dOn = msg;
        dOn.setNoteNumber(targetNote);
        dOn.setVelocity(snapshots[activeSnap].steps[i].velocity / 127.0f);
        additions.push_back({dOn, origin + 1, i + 1, i, noteKey});
        additions.push_back({juce::MidiMessage::controllerEvent(
                                 msg.getChannel(), 1, snapshots[activeSnap].steps[i].modwheel),
                              origin, i + 1, i, noteKey});
      }
    } else if (msg.isNoteOff()) {
      if (noteTracker.count(noteKey)) {
        // Classic mode: schedule note-offs for all taps, then clean up
        auto &ns = noteTracker[noteKey];
        long long origin = totalSamplesProcessed + localPos;
        for (int i = 0; i < pStepCount; ++i) {
          if (snapshots[activeSnap].steps[i].muted)
            continue;
          auto dOff = msg;
          dOff.setNoteNumber(juce::jlimit<int>(
              0, 127, msg.getNoteNumber() + ns.pitchCaps[i]));
          additions.push_back({dOff, origin, i + 1, i, noteKey});
        }
        noteTracker.erase(noteKey);
      }
    }
  }
  float delayVal = smoothedDelaySamples.getCurrentValue();
  for (int sample = 0; sample < numSamples; ++sample) {
    smoothedDelaySamples.getNextValue();
  }

  for (auto it = midiQueue.begin(); it != midiQueue.end();) {
    long long eventTargetTime =
        it->triggerSample + (long long)(delayVal * it->tapIndex);
    if (eventTargetTime < totalSamplesProcessed + numSamples) {
      int sampleOffset = (int)(eventTargetTime - totalSamplesProcessed);
      sampleOffset = juce::jlimit(0, numSamples - 1, sampleOffset);

      midiMessages.addEvent(it->message, sampleOffset);

      if (it->message.isNoteOn()) {
        activityHits++;
        int ch = it->message.getChannel();
        int note = it->message.getNoteNumber();
        if (std::find(activeNotes.begin(), activeNotes.end(), std::make_pair(ch, note)) == activeNotes.end()) {
          activeNotes.push_back({ch, note});
        }
      } else if (it->message.isNoteOff()) {
        int ch = it->message.getChannel();
        int note = it->message.getNoteNumber();
        activeNotes.erase(std::remove(activeNotes.begin(), activeNotes.end(), std::make_pair(ch, note)), activeNotes.end());
      }

      it = midiQueue.erase(it);
    } else {
      ++it;
    }
  }

  // Safely add all newly scheduled events to the main queue
  for (auto &e : additions)
    midiQueue.push_back(e);

  totalSamplesProcessed += numSamples;
  if (midiQueue.size() > 500000) {
    auto cutoff = totalSamplesProcessed - (long long)(lastSampleRate * 15.0);
    midiQueue.erase(std::remove_if(midiQueue.begin(), midiQueue.end(),
                                   [cutoff](const QueuedEvent &e) {
                                     return e.triggerSample < cutoff;
                                   }),
                    midiQueue.end());
  }
}

std::unique_ptr<juce::XmlElement> AetherAudioProcessor::createStateXml() {
  std::unique_ptr<juce::XmlElement> rootXml(new juce::XmlElement("AetherState"));
  rootXml->setAttribute("editSnapshot", editSnapshot);
  rootXml->setAttribute("currentBank", currentBank);
  rootXml->setAttribute("currentPreset", currentPreset);
  
  // 1. Explicitly serialize APVTS parameters as XML attributes inside Parameters element
  auto *paramsXml = rootXml->createNewChildElement("Parameters");
  if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("delayTimeMs")))
    paramsXml->setAttribute("delayTimeMs", (double)p->get());
    
  if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("enabled")))
    paramsXml->setAttribute("enabled", p->get() ? 1 : 0);
    
  if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("stepCount")))
    paramsXml->setAttribute("stepCount", p->get());
    
  if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("killOnStop")))
    paramsXml->setAttribute("killOnStop", p->get() ? 1 : 0);
    
  if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("killOnSwitch")))
    paramsXml->setAttribute("killOnSwitch", p->get() ? 1 : 0);
    
  if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("syncDivision")))
    paramsXml->setAttribute("syncDivision", p->get());
    
  if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("activeSnapshot")))
    paramsXml->setAttribute("activeSnapshot", p->get());
  
  // 2. Save Sequencer Snapshots
  auto *snapshotsXml = rootXml->createNewChildElement("SNAPSHOTS");
  for (int s = 0; s < 9; ++s) {
    auto *snapXml = snapshotsXml->createNewChildElement("SNAPSHOT");
    snapXml->setAttribute("id", s);
    snapXml->setAttribute("stepCount", snapshots[s].stepCount);
    snapXml->setAttribute("enabled", snapshots[s].enabled);
    snapXml->setAttribute("delayTimeMs", (double)snapshots[s].delayTimeMs);
    snapXml->setAttribute("syncDivision", snapshots[s].syncDivision);
    for (int i = 0; i < 15; ++i) {
      auto *stepXml = snapXml->createNewChildElement("STEP");
      stepXml->setAttribute("id", i);
      stepXml->setAttribute("pitch", snapshots[s].steps[i].pitchOffset);
      stepXml->setAttribute("velocity", snapshots[s].steps[i].velocity);
      stepXml->setAttribute("mod", snapshots[s].steps[i].modwheel);
      stepXml->setAttribute("prob", snapshots[s].steps[i].probability);
      stepXml->setAttribute("mute", snapshots[s].steps[i].muted);
    }
  }
  return rootXml;
}

void AetherAudioProcessor::loadStateFromXml(const juce::XmlElement& rootXml) {
  editSnapshot = rootXml.getIntAttribute("editSnapshot", 0);
  editSnapshot = juce::jlimit(0, 8, editSnapshot);
  currentBank = rootXml.getStringAttribute("currentBank", "Factory Presets");
  currentPreset = rootXml.getStringAttribute("currentPreset", "Init");

  // 1. Read step snapshots
  if (auto *snapshotsXml = rootXml.getChildByName("SNAPSHOTS")) {
    for (auto *snapXml : snapshotsXml->getChildIterator()) {
      int sId = snapXml->getIntAttribute("id");
      if (sId >= 0 && sId < 9) {
        snapshots[sId].stepCount = snapXml->getIntAttribute("stepCount", 15);
        snapshots[sId].enabled = snapXml->getBoolAttribute("enabled", true);
        snapshots[sId].delayTimeMs = (float)snapXml->getDoubleAttribute("delayTimeMs", 500.0);
        snapshots[sId].syncDivision = snapXml->getIntAttribute("syncDivision", 0);
        for (auto *stepXml : snapXml->getChildIterator()) {
          int i = stepXml->getIntAttribute("id");
          if (i >= 0 && i < 15) {
            snapshots[sId].steps[i].pitchOffset = stepXml->getIntAttribute("pitch");
            snapshots[sId].steps[i].velocity = stepXml->getIntAttribute("velocity");
            snapshots[sId].steps[i].modwheel = stepXml->getIntAttribute("mod");
            snapshots[sId].steps[i].probability = stepXml->getIntAttribute("prob");
            snapshots[sId].steps[i].muted = stepXml->getBoolAttribute("mute");
          }
        }
      }
    }
  }
  
  // 2. Load APVTS parameters cleanly from explicit Parameters XML attributes
  if (auto *paramsXml = rootXml.getChildByName("Parameters")) {
    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("delayTimeMs")))
      *p = (float)paramsXml->getDoubleAttribute("delayTimeMs", 500.0);

    if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("enabled")))
      *p = (paramsXml->getIntAttribute("enabled", 1) != 0);

    if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("stepCount")))
      *p = paramsXml->getIntAttribute("stepCount", 15);

    if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("killOnStop")))
      *p = (paramsXml->getIntAttribute("killOnStop", 1) != 0);

    if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("killOnSwitch")))
      *p = (paramsXml->getIntAttribute("killOnSwitch", 0) != 0);

    if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("syncDivision")))
      *p = paramsXml->getIntAttribute("syncDivision", 0);

    if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("activeSnapshot")))
      *p = paramsXml->getIntAttribute("activeSnapshot", 1);
  }
}

void AetherAudioProcessor::getStateInformation(juce::MemoryBlock &d) {
  auto rootXml = createStateXml();
  AetherWebView::logToFile ("getStateInformation: saving XML = \n" + rootXml->toString());
  juce::AudioProcessor::copyXmlToBinary(*rootXml, d);
}

void AetherAudioProcessor::setStateInformation(const void *d, int s) {
  isInitializing = true;
  std::unique_ptr<juce::XmlElement> rootXml(
      juce::AudioProcessor::getXmlFromBinary(d, s));
  if (rootXml != nullptr && rootXml->hasTagName("AetherState")) {
    AetherWebView::logToFile ("setStateInformation: loaded XML = \n" + rootXml->toString());
    loadStateFromXml(*rootXml);
    
    // Sync the loaded active snapshot parameters
    auto* actP = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("activeSnapshot"));
    int activeSnap = (actP ? actP->get() : 1) - 1;
    activeSnap = juce::jlimit (0, 8, activeSnap);
    editSnapshot = activeSnap;
    loadSnapshotParameters (activeSnap);
  } else {
    AetherWebView::logToFile ("setStateInformation: rootXml was null or invalid.");
  }
  isInitializing = false;
}

juce::File AetherAudioProcessor::getAppFolder() {
#if JUCE_MAC
    juce::File dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Application Support").getChildFile("Algebra Within").getChildFile("Aether");
#else
    juce::File dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Algebra Within").getChildFile("Aether");
#endif
    if (!dir.exists()) dir.createDirectory();
    return dir;
}

juce::File AetherAudioProcessor::getPresetsFolder() {
    juce::File dir = getAppFolder().getChildFile("Presets");
    if (!dir.exists()) dir.createDirectory();
    return dir;
}

juce::File AetherAudioProcessor::getSettingsFolder() {
    juce::File dir = getAppFolder().getChildFile("Settings");
    if (!dir.exists()) dir.createDirectory();
    return dir;
}

juce::File AetherAudioProcessor::getPreferencesFile() {
    return getSettingsFolder().getChildFile("Settings.xml");
}

void AetherAudioProcessor::initFactoryPresets() {
    juce::File factoryFile = getPresetsFolder().getChildFile("Factory Presets.xml");
    if (!factoryFile.existsAsFile()) {
        juce::XmlElement root("AetherPresets");
        
        // 1. Init
        auto* pInit = root.createNewChildElement("AetherState");
        pInit->setAttribute("name", "Init");
        pInit->setAttribute("editSnapshot", 0);
        auto* pInitParams = pInit->createNewChildElement("Parameters");
        pInitParams->setAttribute("activeSnapshot", 1);
        pInitParams->setAttribute("enabled", 1);
        pInitParams->setAttribute("delayTimeMs", 500.0);
        pInitParams->setAttribute("syncDivision", 8); // 1/4
        pInitParams->setAttribute("stepCount", 15);
        pInitParams->setAttribute("killOnStop", 1);
        pInitParams->setAttribute("killOnSwitch", 0);
        
        auto* pInitSnaps = pInit->createNewChildElement("SNAPSHOTS");
        for (int s = 0; s < 9; ++s) {
            auto* snapXml = pInitSnaps->createNewChildElement("SNAPSHOT");
            snapXml->setAttribute("id", s);
            snapXml->setAttribute("stepCount", 15);
            snapXml->setAttribute("enabled", 1);
            snapXml->setAttribute("delayTimeMs", 500.0);
            snapXml->setAttribute("syncDivision", 8);
            for (int i = 0; i < 15; ++i) {
                auto* stepXml = snapXml->createNewChildElement("STEP");
                stepXml->setAttribute("id", i);
                stepXml->setAttribute("pitch", 0);
                stepXml->setAttribute("velocity", (int)std::round(127 - (i * (126.0 / 14.0))));
                stepXml->setAttribute("mod", 0);
                stepXml->setAttribute("prob", 100);
                stepXml->setAttribute("mute", 0);
            }
        }
        
        // 2. Dotted Chord
        auto* pDotted = root.createNewChildElement("AetherState");
        pDotted->setAttribute("name", "Dotted Chord");
        pDotted->setAttribute("editSnapshot", 0);
        auto* pDottedParams = pDotted->createNewChildElement("Parameters");
        pDottedParams->setAttribute("activeSnapshot", 1);
        pDottedParams->setAttribute("enabled", 1);
        pDottedParams->setAttribute("delayTimeMs", 375.0);
        pDottedParams->setAttribute("syncDivision", 10); // 1/8d
        pDottedParams->setAttribute("stepCount", 15);
        pDottedParams->setAttribute("killOnStop", 1);
        pDottedParams->setAttribute("killOnSwitch", 1);
        
        auto* pDottedSnaps = pDotted->createNewChildElement("SNAPSHOTS");
        for (int s = 0; s < 9; ++s) {
            auto* snapXml = pDottedSnaps->createNewChildElement("SNAPSHOT");
            snapXml->setAttribute("id", s);
            snapXml->setAttribute("stepCount", 15);
            snapXml->setAttribute("enabled", 1);
            snapXml->setAttribute("delayTimeMs", 375.0);
            snapXml->setAttribute("syncDivision", 10);
            for (int i = 0; i < 15; ++i) {
                auto* stepXml = snapXml->createNewChildElement("STEP");
                stepXml->setAttribute("id", i);
                int offsets[15] = { 0, 3, 7, 12, 15, 19, 24, 0, 3, 7, 12, 15, 19, 24, 0 };
                stepXml->setAttribute("pitch", offsets[i]);
                stepXml->setAttribute("velocity", 100);
                stepXml->setAttribute("mod", 0);
                stepXml->setAttribute("prob", i % 2 == 0 ? 100 : 70);
                stepXml->setAttribute("mute", 0);
            }
        }
        
        // 3. Classic 8th
        auto* pClassic = root.createNewChildElement("AetherState");
        pClassic->setAttribute("name", "Classic 8th");
        pClassic->setAttribute("editSnapshot", 0);
        auto* pClassicParams = pClassic->createNewChildElement("Parameters");
        pClassicParams->setAttribute("activeSnapshot", 1);
        pClassicParams->setAttribute("enabled", 1);
        pClassicParams->setAttribute("delayTimeMs", 250.0);
        pClassicParams->setAttribute("syncDivision", 11); // 1/8
        pClassicParams->setAttribute("stepCount", 15);
        pClassicParams->setAttribute("killOnStop", 1);
        pClassicParams->setAttribute("killOnSwitch", 0);
        
        auto* pClassicSnaps = pClassic->createNewChildElement("SNAPSHOTS");
        for (int s = 0; s < 9; ++s) {
            auto* snapXml = pClassicSnaps->createNewChildElement("SNAPSHOT");
            snapXml->setAttribute("id", s);
            snapXml->setAttribute("stepCount", 15);
            snapXml->setAttribute("enabled", 1);
            snapXml->setAttribute("delayTimeMs", 250.0);
            snapXml->setAttribute("syncDivision", 11);
            for (int i = 0; i < 15; ++i) {
                auto* stepXml = snapXml->createNewChildElement("STEP");
                stepXml->setAttribute("id", i);
                stepXml->setAttribute("pitch", 0);
                stepXml->setAttribute("velocity", 100);
                stepXml->setAttribute("mod", 0);
                stepXml->setAttribute("prob", 100);
                stepXml->setAttribute("mute", 0);
            }
        }
        
        root.writeTo(factoryFile);
    }
}

void AetherAudioProcessor::loadSnapshotParameters (int snapIdx) {
  isUpdatingSnapshotParameters = true;
  auto& snap = snapshots[snapIdx];
  
  if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter ("enabled")))
      *p = snap.enabled;
      
  if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter ("delayTimeMs")))
      *p = snap.delayTimeMs;
      
  if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter ("syncDivision")))
      *p = snap.syncDivision;
      
  if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter ("stepCount")))
      *p = snap.stepCount;
      
  isUpdatingSnapshotParameters = false;
}

void AetherAudioProcessor::handleAsyncUpdate() {
  auto* actP = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("activeSnapshot"));
  int activeSnap = (actP ? actP->get() : 1) - 1;
  activeSnap = juce::jlimit (0, 8, activeSnap);
  editSnapshot = activeSnap;
  loadSnapshotParameters (activeSnap);
}

void AetherAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue) {
  if (isInitializing)
    return;

  if (isUpdatingSnapshotParameters)
    return;

  auto* actP = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("activeSnapshot"));
  int activeSnap = (actP ? actP->get() : 1) - 1;
  activeSnap = juce::jlimit (0, 8, activeSnap);

  if (parameterID == "activeSnapshot") {
    int newActiveSnap = (int)std::round(newValue) - 1;
    newActiveSnap = juce::jlimit (0, 8, newActiveSnap);
    editSnapshot = newActiveSnap;
    
    // Kill on switch logic
    auto* kswP = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("killOnSwitch"));
    if (kswP && kswP->get()) {
      midiQueue.clear();
      activeNotes.clear();
      noteTracker.clear();
    }
    
    loadSnapshotParameters (newActiveSnap);
  } else {
    if (parameterID == "enabled")
        snapshots[activeSnap].enabled = (newValue > 0.5f);
    else if (parameterID == "delayTimeMs")
        snapshots[activeSnap].delayTimeMs = newValue;
    else if (parameterID == "syncDivision")
        snapshots[activeSnap].syncDivision = (int)std::round(newValue);
    else if (parameterID == "stepCount")
        snapshots[activeSnap].stepCount = (int)std::round(newValue);
  }
}
juce::AudioProcessorEditor *AetherAudioProcessor::createEditor() {
  return new AetherAudioProcessorEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new AetherAudioProcessor();
}