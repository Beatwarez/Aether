#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

AetherAudioProcessor::AetherAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()) {
  for (int s = 0; s < 9; ++s) {
    stepCounts[s] = 15;
    for (int i = 0; i < 15; ++i) {
      steps[s][i].velocity = (int)(127 - (i * (126.0 / 14.0)));
      steps[s][i].modwheel = 0;
      steps[s][i].probability = 100;
      steps[s][i].pitchOffset = 0;
      steps[s][i].muted = false;
    }
  }
}

AetherAudioProcessor::~AetherAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout
AetherAudioProcessor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      "delayTimeMs", "Delay Time (ms)", 1.0f, 2000.0f, 500.0f));
  layout.add(
      std::make_unique<juce::AudioParameterBool>("enabled", "Enabled", true));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      "stepCount", "Step Count", 1, 15, 15));
  layout.add(std::make_unique<juce::AudioParameterBool>("killOnStop",
                                                         "Kill On Stop", true));
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
  bool isPlaying = false;
  if (auto *ph = getPlayHead()) {
    if (auto pos = ph->getPosition())
      isPlaying = pos->getIsPlaying();
  }

  bool pKill = *apvts.getRawParameterValue("killOnStop");
  bool pEnabled = *apvts.getRawParameterValue("enabled");
  int syncIdx = (int)*apvts.getRawParameterValue("syncDivision");
  int activeSnap = (int)*apvts.getRawParameterValue("activeSnapshot") - 1;
  activeSnap = juce::jlimit(0, 8, activeSnap);
  int pStepCount = stepCounts[activeSnap];

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
      std::array<int, 15> cap;
      for (int i = 0; i < 15; ++i)
        cap[i] = steps[activeSnap][i].pitchOffset;

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
        if (steps[activeSnap][i].muted || random.nextInt(100) >= steps[activeSnap][i].probability)
          continue;
        int targetNote =
            juce::jlimit<int>(0, 127, msg.getNoteNumber() + cap[i]);
        additions.push_back(
            {juce::MidiMessage::noteOff(msg.getChannel(), targetNote), origin,
             i + 1, i, noteKey});
        auto dOn = msg;
        dOn.setNoteNumber(targetNote);
        dOn.setVelocity(steps[activeSnap][i].velocity / 127.0f);
        additions.push_back({dOn, origin + 1, i + 1, i, noteKey});
        additions.push_back({juce::MidiMessage::controllerEvent(
                                 msg.getChannel(), 1, steps[activeSnap][i].modwheel),
                              origin, i + 1, i, noteKey});
      }
    } else if (msg.isNoteOff()) {
      if (noteTracker.count(noteKey)) {
        // Classic mode: schedule note-offs for all taps, then clean up
        auto &ns = noteTracker[noteKey];
        long long origin = totalSamplesProcessed + localPos;
        for (int i = 0; i < pStepCount; ++i) {
          if (steps[activeSnap][i].muted)
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
    snapXml->setAttribute("stepCount", stepCounts[s]);
    for (int i = 0; i < 15; ++i) {
      auto *stepXml = snapXml->createNewChildElement("STEP");
      stepXml->setAttribute("id", i);
      stepXml->setAttribute("pitch", steps[s][i].pitchOffset);
      stepXml->setAttribute("velocity", steps[s][i].velocity);
      stepXml->setAttribute("mod", steps[s][i].modwheel);
      stepXml->setAttribute("prob", steps[s][i].probability);
      stepXml->setAttribute("mute", steps[s][i].muted);
    }
  }
  
  // Debug log state saving
  AetherWebView::logToFile ("getStateInformation: saving XML = \n" + rootXml->toString());
  
  juce::AudioProcessor::copyXmlToBinary(*rootXml, d);
}

void AetherAudioProcessor::setStateInformation(const void *d, int s) {
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
          stepCounts[sId] = snapXml->getIntAttribute("stepCount", 15);
          for (auto *stepXml : snapXml->getChildIterator()) {
            int i = stepXml->getIntAttribute("id");
            if (i >= 0 && i < 15) {
              steps[sId][i].pitchOffset = stepXml->getIntAttribute("pitch");
              steps[sId][i].velocity = stepXml->getIntAttribute("velocity");
              steps[sId][i].modwheel = stepXml->getIntAttribute("mod");
              steps[sId][i].probability = stepXml->getIntAttribute("prob");
              steps[sId][i].muted = stepXml->getBoolAttribute("mute");
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
          steps[0][i].pitchOffset = stepXml->getIntAttribute("pitch");
          steps[0][i].velocity = stepXml->getIntAttribute("velocity");
          steps[0][i].modwheel = stepXml->getIntAttribute("mod");
          steps[0][i].probability = stepXml->getIntAttribute("prob");
          steps[0][i].muted = stepXml->getBoolAttribute("mute");
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
juce::AudioProcessorEditor *AetherAudioProcessor::createEditor() {
  return new AetherAudioProcessorEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new AetherAudioProcessor();
}