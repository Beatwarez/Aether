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
    snapshots[s].syncDivision = 14;
    snapshots[s].modwheelSlew = 0.0f;
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
  apvts.addParameterListener ("endSwitch", this);
  apvts.addParameterListener ("activeSnapshot", this);
  apvts.addParameterListener ("modwheelSlew", this);

  initFactoryPresets();
}

AetherAudioProcessor::~AetherAudioProcessor() {
  apvts.removeParameterListener ("enabled", this);
  apvts.removeParameterListener ("delayTimeMs", this);
  apvts.removeParameterListener ("syncDivision", this);
  apvts.removeParameterListener ("stepCount", this);
  apvts.removeParameterListener ("killOnStop", this);
  apvts.removeParameterListener ("killOnSwitch", this);
  apvts.removeParameterListener ("endSwitch", this);
  apvts.removeParameterListener ("activeSnapshot", this);
  apvts.removeParameterListener ("modwheelSlew", this);
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
      "killOnSwitch", "Kill On Switch", true));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      "endSwitch", "End Switch", false));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      "syncDivision", "Sync Division", 0, 18, 14));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      "activeSnapshot", "Active Snapshot", 1, 9, 1));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      "modwheelSlew", "Modwheel Slew", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
  return layout;
}

void AetherAudioProcessor::prepareToPlay(double sampleRate,
                                         int samplesPerBlock) {
  juce::ignoreUnused(samplesPerBlock);
  lastSampleRate = (sampleRate > 0) ? sampleRate : 44100.0;
  totalSamplesProcessed = 0;
  
  for (int i = 0; i < 9; ++i) {
    midiQueues[i].clear();
    activeNotes[i].clear();
    noteTrackers[i].clear();
    
    // Initialize delay times based on the snapshot's initial parameters
    float currentMs = snapshots[i].delayTimeMs;
    smoothedDelaySamples[i].reset(lastSampleRate, 0.1);
    smoothedDelaySamples[i].setCurrentAndTargetValue(currentMs * 0.001f * (float)lastSampleRate);
  }
  
  wasPlaying = false;
}

double AetherAudioProcessor::getSyncTimeInMs(int snapshotIndex) {
  int syncIdx = snapshots[snapshotIndex].syncDivision;
  if (syncIdx == 0)
    return (double)snapshots[snapshotIndex].delayTimeMs;

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
  return (double)snapshots[snapshotIndex].delayTimeMs;
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
  auto* actP = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("activeSnapshot"));
  int targetActiveSnap = (actP ? actP->get() : 1) - 1;
  targetActiveSnap = juce::jlimit(0, 8, targetActiveSnap);
  
  auto* esP = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("endSwitch"));
  bool pEndSwitch = esP ? esP->get() : false;
    
  if (!pEndSwitch) {
    actualActiveSnap.store(targetActiveSnap);
    lastPpqPosition = -1.0;
  } else {
    if (auto* playhead = getPlayHead()) {
      if (auto posInfo = playhead->getPosition()) {
        if (posInfo->getPpqPosition().hasValue() && posInfo->getTimeSignature().hasValue()) {
          double ppq = *posInfo->getPpqPosition();
          auto sig = *posInfo->getTimeSignature();
          double barLength = (4.0 * sig.numerator) / sig.denominator;
          
          if (lastPpqPosition >= 0.0) {
            double prevBarPhase = std::fmod(lastPpqPosition, barLength);
            double currBarPhase = std::fmod(ppq, barLength);
            // Check for loop wrap or bar boundary crossing
            if (currBarPhase < prevBarPhase || (ppq - lastPpqPosition) >= barLength) {
              actualActiveSnap.store(targetActiveSnap);
            }
          }
          lastPpqPosition = ppq;
        } else {
          actualActiveSnap.store(targetActiveSnap);
          lastPpqPosition = -1.0;
        }
      } else {
        actualActiveSnap.store(targetActiveSnap);
        lastPpqPosition = -1.0;
      }
    } else {
      actualActiveSnap.store(targetActiveSnap);
      lastPpqPosition = -1.0;
    }
  }

  int activeSnap = actualActiveSnap.load();
  
  auto* kswP = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("killOnSwitch"));
  bool pKillOnSwitch = kswP ? kswP->get() : false;

  auto killActiveMidiNotes = [&](int snapIndex = -1) {
    if (snapIndex == -1) {
      // Kill all
      for (int s = 0; s < 9; ++s) {
        for (const auto& note : activeNotes[s]) {
          midiMessages.addEvent(juce::MidiMessage::noteOff(note.first, note.second, 0.0f), 0);
        }
        activeNotes[s].clear();
        midiQueues[s].clear();
        noteTrackers[s].clear();
      }
      for (int ch = 1; ch <= 16; ++ch) {
        midiMessages.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
      }
    } else {
      // Kill specific snapshot
      for (const auto& note : activeNotes[snapIndex]) {
        midiMessages.addEvent(juce::MidiMessage::noteOff(note.first, note.second, 0.0f), 0);
      }
      activeNotes[snapIndex].clear();
      midiQueues[snapIndex].clear();
      noteTrackers[snapIndex].clear();
    }
  };

  if (lastActiveSnap != activeSnap) {
    if (lastActiveSnap != -1 && pKillOnSwitch) {
      killActiveMidiNotes(lastActiveSnap);
    }
    lastActiveSnap = activeSnap;
  }

  // Handle STOP button: immediately clear all loop state
  if (stopRequested.exchange(false)) {
    killActiveMidiNotes();
  }

  if (pKill && !isPlaying && wasPlaying) {
    killActiveMidiNotes();
  }
  wasPlaying = isPlaying;

  if (!pEnabled || (pKill && !isPlaying)) {
    bool hasNotes = false;
    for (int s = 0; s < 9; ++s) {
      if (!activeNotes[s].empty() || !midiQueues[s].empty()) hasNotes = true;
    }
    if (hasNotes) {
      killActiveMidiNotes();
    }
    totalSamplesProcessed += numSamples;
    return;
  }

  // Update target delays for all 9 snapshots
  std::array<float, 9> currentDelayVals;
  for (int s = 0; s < 9; ++s) {
    double targetMs = (snapshots[s].syncDivision == 0)
                          ? (double)snapshots[s].delayTimeMs
                          : getSyncTimeInMs(s);
    targetMs = juce::jmax(1.0, targetMs);
    smoothedDelaySamples[s].setTargetValue((float)(targetMs * 0.001 * lastSampleRate));
    currentDelayVals[s] = smoothedDelaySamples[s].getCurrentValue();
    for (int sample = 0; sample < numSamples; ++sample) {
      smoothedDelaySamples[s].getNextValue();
    }
  }

  std::array<std::vector<QueuedEvent>, 9> additions;

  juce::MidiBuffer filteredMessages;
  int currentPercent = lastEmittedPercent.load();

  for (const auto metadata : midiMessages) {
    auto msg = metadata.getMessage();
    int localPos = metadata.samplePosition;
    int noteKey = (msg.getChannel() << 7) | msg.getNoteNumber();
    long long origin = totalSamplesProcessed + localPos;

    if (msg.isController() && msg.getControllerNumber() == 1) {
        lastBaseModwheel = msg.getControllerValue();
        int finalVal = lastBaseModwheel + (int)(((127 - lastBaseModwheel) * currentPercent) / 100.0f);
        finalVal = juce::jlimit(0, 127, finalVal);
        filteredMessages.addEvent(juce::MidiMessage::controllerEvent(msg.getChannel(), 1, finalVal), localPos);
    } else {
        filteredMessages.addEvent(msg, localPos);
    }

    if (msg.isNoteOn()) {
        lastMidiChannel = msg.getChannel();
        activityHits++;
      for (int s = 0; s < 9; ++s) {
        if (!snapshots[s].enabled) continue;
        
        std::array<int, 15> cap;
        for (int i = 0; i < 15; ++i)
          cap[i] = snapshots[s].steps[i].pitchOffset;

        NoteState ns;
        ns.channel = msg.getChannel();
        ns.noteNumber = msg.getNoteNumber();
        ns.velocity = msg.getVelocity();
        ns.currentStepIndex = 0;
        ns.directionForward = true;
        ns.lastPlayedNote = -1;
        ns.pitchCaps = cap;
        noteTrackers[s][noteKey] = ns;

          int pStepCount = snapshots[s].stepCount;
          for (int i = 0; i < pStepCount; ++i) {
            if (snapshots[s].steps[i].muted || random.nextInt(100) >= snapshots[s].steps[i].probability)
              continue;
            int targetNote = juce::jlimit<int>(0, 127, msg.getNoteNumber() + cap[i]);
            additions[s].push_back(
                {juce::MidiMessage::noteOff(msg.getChannel(), targetNote), origin,
                 i + 1, i, noteKey, s});
            auto dOn = msg;
            dOn.setNoteNumber(targetNote);
            dOn.setVelocity(snapshots[s].steps[i].velocity / 127.0f);
            additions[s].push_back({dOn, origin + 1, i + 1, i, noteKey, s});
            
            int targetMod = snapshots[s].steps[i].modwheel;
            additions[s].push_back({juce::MidiMessage::controllerEvent(
                                   msg.getChannel(), 1, targetMod),
                                  origin, i + 1, i, noteKey, s});
          }
        }
      }
    } else if (msg.isNoteOff()) {
      for (int s = 0; s < 9; ++s) {
        if (noteTrackers[s].count(noteKey)) {
          auto &ns = noteTrackers[s][noteKey];
          int pStepCount = snapshots[s].stepCount;
          for (int i = 0; i < pStepCount; ++i) {
            if (snapshots[s].steps[i].muted)
              continue;
            auto dOff = msg;
            dOff.setNoteNumber(juce::jlimit<int>(0, 127, msg.getNoteNumber() + ns.pitchCaps[i]));
            additions[s].push_back({dOff, origin, i + 1, i, noteKey, s});
          }
          noteTrackers[s].erase(noteKey);
        }
      }
    }
  }

  midiMessages = filteredMessages;

  // Process all 9 queues
  for (int s = 0; s < 9; ++s) {
    float delayVal = currentDelayVals[s];
    for (auto it = midiQueues[s].begin(); it != midiQueues[s].end();) {
      long long eventTargetTime = it->triggerSample + (long long)(delayVal * it->tapIndex);
      
      if (eventTargetTime < totalSamplesProcessed + numSamples) {
        int sampleOffset = (int)(eventTargetTime - totalSamplesProcessed);
        sampleOffset = juce::jlimit(0, numSamples - 1, sampleOffset);

          if (s == activeSnap) {
              auto outMsg = it->message;
              if (outMsg.isController() && outMsg.getControllerNumber() == 1) {
                  targetModwheelPercent.store(outMsg.getControllerValue());
                  lastMidiChannel = outMsg.getChannel();
              } else {
                  midiMessages.addEvent(outMsg, sampleOffset);
                  if (outMsg.isNoteOn()) {
                    activityHits++;
                    int ch = it->message.getChannel();
                    int note = it->message.getNoteNumber();
                    if (std::find(activeNotes[s].begin(), activeNotes[s].end(), std::make_pair(ch, note)) == activeNotes[s].end()) {
                      activeNotes[s].push_back({ch, note});
                    }
                  } else if (it->message.isNoteOff()) {
                    int ch = it->message.getChannel();
                    int note = it->message.getNoteNumber();
                    activeNotes[s].erase(std::remove(activeNotes[s].begin(), activeNotes[s].end(), std::make_pair(ch, note)), activeNotes[s].end());
                  }
              }
          } else {
          // If not active snapshot, we only emit Note Offs for notes that were ACTUALLY turned on by this snapshot in the past
          if (it->message.isNoteOff()) {
            int ch = it->message.getChannel();
            int note = it->message.getNoteNumber();
            auto activeIt = std::find(activeNotes[s].begin(), activeNotes[s].end(), std::make_pair(ch, note));
            if (activeIt != activeNotes[s].end()) {
              midiMessages.addEvent(it->message, sampleOffset);
              activeNotes[s].erase(activeIt);
            }
          }
        }

        it = midiQueues[s].erase(it);
      } else {
        ++it;
      }
    }

    for (auto &e : additions[s])
      midiQueues[s].push_back(e);

    if (midiQueues[s].size() > 500000) {
      auto cutoff = totalSamplesProcessed - (long long)(lastSampleRate * 15.0);
      midiQueues[s].erase(std::remove_if(midiQueues[s].begin(), midiQueues[s].end(),
                                     [cutoff](const QueuedEvent &e) {
                                       return e.triggerSample < cutoff;
                                     }),
                      midiQueues[s].end());
    }
  }
  
  // Real-Time Modwheel Smoother
  int targetPct = targetModwheelPercent.load();
  float slewParam = snapshots[activeSnap].modwheelSlew;
  
  if (slewParam <= 0.001f) {
      currentModwheelPercentFloat = (float)targetPct;
  } else {
      float delayVal = currentDelayVals[activeSnap];
      long long slewSamples = (long long)(delayVal * slewParam * 2.0f);
      if (slewSamples < 1) slewSamples = 1;
      
      float maxDeltaPerSample = 100.0f / (float)slewSamples;
      float deltaForBlock = maxDeltaPerSample * numSamples;
      
      if (currentModwheelPercentFloat < targetPct) {
          currentModwheelPercentFloat += deltaForBlock;
          if (currentModwheelPercentFloat > targetPct)
              currentModwheelPercentFloat = (float)targetPct;
      } else if (currentModwheelPercentFloat > targetPct) {
          currentModwheelPercentFloat -= deltaForBlock;
          if (currentModwheelPercentFloat < targetPct)
              currentModwheelPercentFloat = (float)targetPct;
      }
  }
  
  int currentPercentInt = (int)std::round(currentModwheelPercentFloat);
  if (currentPercentInt != lastEmittedPercent.load()) {
      lastEmittedPercent.store(currentPercentInt);
      int finalVal = lastBaseModwheel + (int)(((127 - lastBaseModwheel) * currentPercentInt) / 100.0f);
      finalVal = juce::jlimit(0, 127, finalVal);
      midiMessages.addEvent(juce::MidiMessage::controllerEvent(lastMidiChannel, 1, finalVal), numSamples - 1);
  }

  totalSamplesProcessed += numSamples;
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
    
  if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("endSwitch")))
    paramsXml->setAttribute("endSwitch", p->get() ? 1 : 0);
    
  if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("syncDivision")))
    paramsXml->setAttribute("syncDivision", p->get());
    
  if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("activeSnapshot")))
    paramsXml->setAttribute("activeSnapshot", p->get());

  if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("modwheelSlew")))
    paramsXml->setAttribute("modwheelSlew", (double)p->get());
  
  // 2. Save Sequencer Snapshots
  auto *snapshotsXml = rootXml->createNewChildElement("SNAPSHOTS");
  for (int s = 0; s < 9; ++s) {
    auto *snapXml = snapshotsXml->createNewChildElement("SNAPSHOT");
    snapXml->setAttribute("id", s);
    snapXml->setAttribute("stepCount", snapshots[s].stepCount);
    snapXml->setAttribute("enabled", snapshots[s].enabled);
    snapXml->setAttribute("delayTimeMs", (double)snapshots[s].delayTimeMs);
    snapXml->setAttribute("syncDivision", snapshots[s].syncDivision);
    snapXml->setAttribute("modSlew", snapshots[s].modwheelSlew);
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
  currentBank = rootXml.getStringAttribute("currentBank", "");
  currentPreset = rootXml.getStringAttribute("currentPreset", "Init");

  // 1. Read step snapshots
  if (auto *snapshotsXml = rootXml.getChildByName("SNAPSHOTS")) {
    for (auto *snapXml : snapshotsXml->getChildIterator()) {
      int sId = snapXml->getIntAttribute("id");
      if (sId >= 0 && sId < 9) {
        snapshots[sId].stepCount = snapXml->getIntAttribute("stepCount", 15);
        snapshots[sId].enabled = snapXml->getBoolAttribute("enabled", true);
        if (snapXml->hasAttribute("delayTimeMs"))
          snapshots[sId].delayTimeMs = (float)snapXml->getDoubleAttribute("delayTimeMs", 500.0);
        if (snapXml->hasAttribute("syncDivision"))
          snapshots[sId].syncDivision = snapXml->getIntAttribute("syncDivision", 0);
        if (snapXml->hasAttribute("stepCount"))
          snapshots[sId].stepCount = snapXml->getIntAttribute("stepCount", 15);
        if (snapXml->hasAttribute("modSlew"))
          snapshots[sId].modwheelSlew = (float)snapXml->getDoubleAttribute("modSlew", 0.0);
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

    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("modwheelSlew")))
      *p = (float)paramsXml->getDoubleAttribute("modwheelSlew", 0.0);

    if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("killOnStop")))
      *p = (paramsXml->getIntAttribute("killOnStop", 1) != 0);

    if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("killOnSwitch")))
      *p = (paramsXml->getIntAttribute("killOnSwitch", 1) != 0);

    if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("endSwitch"))) {
      bool val = (paramsXml->getIntAttribute("endSwitch", 0) != 0);
      *p = val;
    }

    if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("syncDivision")))
      *p = paramsXml->getIntAttribute("syncDivision", 14);

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
    juce::File desktop = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
    juce::File dir = desktop.getChildFile("aether_presets");
    if (!dir.exists()) dir.createDirectory();
    return dir;
}

juce::File AetherAudioProcessor::getSettingsFolder() {
    return getPresetsFolder();
}

juce::File AetherAudioProcessor::getPreferencesFile() {
    return getSettingsFolder().getChildFile("Settings.xml");
}

void AetherAudioProcessor::initFactoryPresets() {
    // Factory presets generation has been removed as per user request.
}

void AetherAudioProcessor::loadSnapshotParameters (int snapIdx) {
  isUpdatingSnapshotParameters = true;
  auto& snap = snapshots[snapIdx];
  
  if (auto* raw = apvts.getRawParameterValue("enabled"))
      raw->store (snap.enabled ? 1.0f : 0.0f);
  if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter ("enabled")))
      p->setValueNotifyingHost (p->convertTo0to1 (snap.enabled ? 1.0f : 0.0f));
      
  if (auto* raw = apvts.getRawParameterValue("delayTimeMs"))
      raw->store (snap.delayTimeMs);
  if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter ("delayTimeMs")))
      p->setValueNotifyingHost (p->convertTo0to1 (snap.delayTimeMs));
      
  if (auto* raw = apvts.getRawParameterValue("syncDivision"))
      raw->store ((float)snap.syncDivision);
  if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter ("syncDivision")))
      p->setValueNotifyingHost (p->convertTo0to1 ((float)snap.syncDivision));
      
  if (auto* raw = apvts.getRawParameterValue("stepCount"))
      raw->store ((float)snap.stepCount);
  if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter ("stepCount")))
      p->setValueNotifyingHost (p->convertTo0to1 ((float)snap.stepCount));
      
  if (auto* raw = apvts.getRawParameterValue("modwheelSlew"))
      raw->store (snap.modwheelSlew);
  if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter ("modwheelSlew")))
      p->setValueNotifyingHost (p->convertTo0to1 (snap.modwheelSlew));
      
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
    
    // Kill on switch is now handled safely inside processBlock()
    
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
    else if (parameterID == "modwheelSlew")
        snapshots[activeSnap].modwheelSlew = newValue;
  }
}
juce::AudioProcessorEditor *AetherAudioProcessor::createEditor() {
  return new AetherAudioProcessorEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new AetherAudioProcessor();
}