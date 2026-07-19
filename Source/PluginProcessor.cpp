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
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      "enabled", "Enabled", 0.0f, 1.0f, 1.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      "stepCount", "Step Count", 1.0f, 15.0f, 15.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      "killOnStop", "Kill On Stop", 0.0f, 1.0f, 1.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      "killOnSwitch", "Kill On Switch", 0.0f, 1.0f, 0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      "syncDivision", "Sync Division", 0.0f, 18.0f, 0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      "activeSnapshot", "Active Snapshot", 1.0f, 9.0f, 1.0f));
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
  int syncIdx = (int)*apvts.getRawParameterValue("syncDivision");
  if (syncIdx == 0)
    return (double)*apvts.getRawParameterValue("delayTimeMs");

  if (auto *ph = getPlayHead()) {
    if (auto pos = ph->getPosition()) {
      auto bpmOpt = pos->getBpm();
      double bpm = bpmOpt ? *bpmOpt : 120.0;
      double qnMs = (60.0 / bpm) * 1000.0;
      int div = (syncIdx - 1) % 6;
      int type = (syncIdx - 1) / 6;
      double bt = qnMs;
      switch (div) {
      case 0:
        bt = qnMs * 4.0;
        break;
      case 1:
        bt = qnMs * 2.0;
        break;
      case 2:
        bt = qnMs;
        break;
      case 3:
        bt = qnMs * 0.5;
        break;
      case 4:
        bt = qnMs * 0.25;
        break;
      case 5:
        bt = qnMs * 0.125;
        break;
      }
      if (type == 1)
        bt *= 1.5;
      else if (type == 2)
        bt *= (2.0 / 3.0);
      return bt;
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

  bool pKill = *apvts.getRawParameterValue("killOnStop") > 0.5f;
  bool pEnabled = *apvts.getRawParameterValue("enabled") > 0.5f;
  int syncIdx = (int)std::round(*apvts.getRawParameterValue("syncDivision"));
  int activeSnap = (int)std::round(*apvts.getRawParameterValue("activeSnapshot")) - 1;
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

void AetherAudioProcessor::getStateInformation(juce::MemoryBlock &d) {
  std::unique_ptr<juce::XmlElement> rootXml(new juce::XmlElement("AetherState"));
  
  auto state = apvts.copyState();
  auto paramsXml = state.createXml();
  if (paramsXml != nullptr) {
    rootXml->addChildElement(paramsXml.release());
  }
  
  // Save Sequencer Snapshots
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
  
  // Debug log state saving
  AetherWebView::logToFile ("getStateInformation: saving XML = \n" + rootXml->toString());
  
  juce::AudioProcessor::copyXmlToBinary(*rootXml, d);
}

void AetherAudioProcessor::setStateInformation(const void *d, int s) {
  isInitializing = true;
  std::unique_ptr<juce::XmlElement> rootXml(
      juce::AudioProcessor::getXmlFromBinary(d, s));
  if (rootXml != nullptr) {
    AetherWebView::logToFile ("setStateInformation: loaded XML = \n" + rootXml->toString());
    AetherWebView::logToFile ("setStateInformation: xml tag name = " + rootXml->getTagName());

    // 1. Read step snapshots
    if (auto *snapshotsXml = rootXml->getChildByName("SNAPSHOTS")) {
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
    // Fallback: if old sequence tag exists, load it into snapshot 0
    else if (auto *sequenceXml = rootXml->getChildByName("SEQUENCE")) {
      for (auto *stepXml : sequenceXml->getChildIterator()) {
        int i = stepXml->getIntAttribute("id");
        if (i >= 0 && i < 15) {
          snapshots[0].steps[i].pitchOffset = stepXml->getIntAttribute("pitch");
          snapshots[0].steps[i].velocity = stepXml->getIntAttribute("velocity");
          snapshots[0].steps[i].modwheel = stepXml->getIntAttribute("mod");
          snapshots[0].steps[i].probability = stepXml->getIntAttribute("prob");
          snapshots[0].steps[i].muted = stepXml->getBoolAttribute("mute");
        }
      }
    }
    
    // 2. Load APVTS parameters cleanly from the dedicated Parameters child
    if (auto *paramsXml = rootXml->getChildByName("Parameters")) {
      apvts.replaceState (juce::ValueTree::fromXml (*paramsXml));
      AetherWebView::logToFile ("setStateInformation: replaceState was successfully called.");
    } else {
      AetherWebView::logToFile ("setStateInformation: WARNING - Parameters child not found!");
    }
  } else {
    AetherWebView::logToFile ("setStateInformation: rootXml was null (failed to parse binary data).");
  }
}

void AetherAudioProcessor::loadSnapshotParameters (int snapIdx) {
  isUpdatingSnapshotParameters = true;
  auto& snap = snapshots[snapIdx];
  
  if (auto* p = apvts.getParameter ("enabled"))
      p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (snap.enabled ? 1.0f : 0.0f));
      
  if (auto* p = apvts.getParameter ("delayTimeMs"))
      p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (snap.delayTimeMs));
      
  if (auto* p = apvts.getParameter ("syncDivision"))
      p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 ((float)snap.syncDivision));
      
  if (auto* p = apvts.getParameter ("stepCount"))
      p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 ((float)snap.stepCount));
      
  isUpdatingSnapshotParameters = false;
}

void AetherAudioProcessor::handleAsyncUpdate() {
  int activeSnap = (int)std::round(apvts.getRawParameterValue ("activeSnapshot")->load()) - 1;
  activeSnap = juce::jlimit (0, 8, activeSnap);
  loadSnapshotParameters (activeSnap);
}

void AetherAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue) {
  if (isInitializing)
    return;

  if (isUpdatingSnapshotParameters)
    return;

  int activeSnap = (int)std::round(apvts.getRawParameterValue ("activeSnapshot")->load()) - 1;
  activeSnap = juce::jlimit (0, 8, activeSnap);

  if (parameterID == "activeSnapshot") {
    int newActiveSnap = (int)std::round(newValue) - 1;
    newActiveSnap = juce::jlimit (0, 8, newActiveSnap);
    
    // Kill on switch logic
    if (apvts.getRawParameterValue ("killOnSwitch")->load() > 0.5f) {
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