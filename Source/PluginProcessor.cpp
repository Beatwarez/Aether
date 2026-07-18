#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

AetherAudioProcessor::AetherAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()) {
  for (int i = 0; i < 15; ++i) {
    steps[i].velocity = (int)(127 - (i * (126.0 / 14.0)));
    steps[i].modwheel = 0;
    steps[i].probability = 100;
    steps[i].pitchOffset = 0;
    steps[i].muted = false;
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
  layout.add(std::make_unique<juce::AudioParameterBool>("loopEnabled",
                                                        "Loop Enabled", false));
  juce::StringArray modes = {"Forward", "Pendulum", "Random"};
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      "loopMode", "Loop Mode", modes, 0));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      "loopRestart", "Loop Note Restart", false));
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
  int pStepCount = (int)*apvts.getRawParameterValue("stepCount");
  int syncIdx = (int)*apvts.getRawParameterValue("syncDivision");

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

  bool pLoopEnabled = *apvts.getRawParameterValue("loopEnabled");
  bool pLoopRestart = *apvts.getRawParameterValue("loopRestart");
  int pLoopMode = (int)*apvts.getRawParameterValue("loopMode");

  // Helper: advance step index according to current mode
  auto advanceStep = [&](NoteState &ns, int fromStep) -> int {
    if (pLoopMode == 0) { // Forward
      return (fromStep + 1) % pStepCount;
    } else if (pLoopMode == 1) { // Pendulum
      if (ns.directionForward) {
        if (fromStep >= pStepCount - 1) {
          ns.directionForward = false;
          return std::max(0, pStepCount - 2);
        }
        return fromStep + 1;
      } else {
        if (fromStep <= 0) {
          ns.directionForward = true;
          return std::min(pStepCount - 1, 1);
        }
        return fromStep - 1;
      }
    } else { // Random
      return random.nextInt(pStepCount);
    }
  };

  // Helper: find next non-muted step, returns {stepIndex, tapMultiple}
  auto findNextStep = [&](NoteState &ns, int fromStep) -> std::pair<int, int> {
    int nextI = advanceStep(ns, fromStep);
    int taps = 1;
    for (int guard = 0; guard < pStepCount && steps[nextI].muted; ++guard) {
      nextI = advanceStep(ns, nextI);
      ++taps;
    }
    return {nextI, taps};
  };

  std::vector<QueuedEvent> additions;

  // Schedule a loop chain event for nextI, taps delay-periods from now
  auto scheduleLoopEvents = [&](NoteState &ns, int nextI, int taps,
                                long long origin, int noteKey) {
    if (steps[nextI].muted)
      return; // all steps muted
    int targetNote = juce::jlimit(0, 127, ns.noteNumber + ns.pitchCaps[nextI]);
    // Pre-kill target pitch
    additions.push_back({juce::MidiMessage::noteOff(ns.channel, targetNote),
                         origin, taps, nextI, noteKey});
    if (random.nextInt(100) < steps[nextI].probability) {
      auto on = juce::MidiMessage::noteOn(ns.channel, targetNote,
                                          steps[nextI].velocity / 127.0f);
      additions.push_back({on, origin + 1, taps, nextI, noteKey});
      additions.push_back({juce::MidiMessage::controllerEvent(
                               ns.channel, 1, steps[nextI].modwheel),
                           origin, taps, nextI, noteKey});
    }
    ns.currentStepIndex = nextI;
  };

  for (const auto metadata : midiMessages) {
    auto msg = metadata.getMessage();
    int localPos = metadata.samplePosition;
    int noteKey = (msg.getChannel() << 7) | msg.getNoteNumber();

    if (msg.isNoteOn()) {
      if (pLoopRestart && pLoopEnabled) {
        // Clear ALL queued loop events to restart from scratch
        midiQueue.clear();
        noteTracker.clear();
      }

      std::array<int, 15> cap;
      for (int i = 0; i < 15; ++i)
        cap[i] = steps[i].pitchOffset;

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

      if (pLoopEnabled) {
        // Find first non-muted step, skip with accumulated delay
        auto &ns2 = noteTracker[noteKey];
        int startI = 0;
        int taps = 1;
        for (int guard = 0; guard < pStepCount && steps[startI].muted;
             ++guard) {
          startI = advanceStep(ns2, startI);
          ++taps;
        }
        scheduleLoopEvents(ns2, startI, taps, origin, noteKey);
      } else {
        // Classic: schedule all steps simultaneously
        for (int i = 0; i < pStepCount; ++i) {
          if (steps[i].muted || random.nextInt(100) >= steps[i].probability)
            continue;
          int targetNote =
              juce::jlimit<int>(0, 127, msg.getNoteNumber() + cap[i]);
          additions.push_back(
              {juce::MidiMessage::noteOff(msg.getChannel(), targetNote), origin,
               i + 1, i, noteKey});
          auto dOn = msg;
          dOn.setNoteNumber(targetNote);
          dOn.setVelocity(steps[i].velocity / 127.0f);
          additions.push_back({dOn, origin + 1, i + 1, i, noteKey});
          additions.push_back({juce::MidiMessage::controllerEvent(
                                   msg.getChannel(), 1, steps[i].modwheel),
                               origin, i + 1, i, noteKey});
        }
      }
    } else if (msg.isNoteOff()) {
      if (noteTracker.count(noteKey)) {
        if (!pLoopEnabled) {
          // Classic mode: schedule note-offs for all taps, then clean up
          auto &ns = noteTracker[noteKey];
          long long origin = totalSamplesProcessed + localPos;
          for (int i = 0; i < pStepCount; ++i) {
            if (steps[i].muted)
              continue;
            auto dOff = msg;
            dOff.setNoteNumber(juce::jlimit<int>(
                0, 127, msg.getNoteNumber() + ns.pitchCaps[i]));
            additions.push_back({dOff, origin, i + 1, i, noteKey});
          }
          noteTracker.erase(noteKey);
        }
        // In loop mode: ignore note-off — loop keeps running until STOP /
        // transport stop / note restart
      }
    }
  }

  for (int sample = 0; sample < numSamples; ++sample) {
    float currentDelaySamples = smoothedDelaySamples.getNextValue();
    long long currentTime = totalSamplesProcessed + sample;

    for (auto it = midiQueue.begin(); it != midiQueue.end();) {
      long long eventTargetTime =
          it->triggerSample + (long long)(currentDelaySamples * it->tapIndex);
      if (eventTargetTime <= currentTime) {
        midiMessages.addEvent(it->message, sample);

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

        // Loop continuation: when a looped note-on fires, schedule the next
        if (it->message.isNoteOn() && pLoopEnabled &&
            noteTracker.count(it->noteKey)) {
          auto &ns = noteTracker[it->noteKey];

          // Kill the note that just fired (schedule kill 1 delay period from
          // now)
          int thisNote =
              juce::jlimit(0, 127, ns.noteNumber + ns.pitchCaps[it->stepIndex]);
          additions.push_back({juce::MidiMessage::noteOff(ns.channel, thisNote),
                               currentTime, 1, it->stepIndex, it->noteKey});

          // Find and schedule the next step
          auto [nextI, taps] = findNextStep(ns, ns.currentStepIndex);
          scheduleLoopEvents(ns, nextI, taps, currentTime, it->noteKey);
        }

        it = midiQueue.erase(it);
      } else {
        ++it;
      }
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
  auto state = apvts.copyState();
  std::unique_ptr<juce::XmlElement> xml(state.createXml());
  auto *sequenceXml = xml->createNewChildElement("SEQUENCE");
  for (int i = 0; i < 15; ++i) {
    auto *stepXml = sequenceXml->createNewChildElement("STEP");
    stepXml->setAttribute("id", i);
    stepXml->setAttribute("pitch", steps[i].pitchOffset);
    stepXml->setAttribute("velocity", steps[i].velocity);
    stepXml->setAttribute("mod", steps[i].modwheel);
    stepXml->setAttribute("prob", steps[i].probability);
    stepXml->setAttribute("mute", steps[i].muted);
  }
  juce::AudioProcessor::copyXmlToBinary(*xml, d);
}

void AetherAudioProcessor::setStateInformation(const void *d, int s) {
  std::unique_ptr<juce::XmlElement> xml(
      juce::AudioProcessor::getXmlFromBinary(d, s));
  if (xml != nullptr && xml->hasTagName(apvts.state.getType())) {
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    if (auto *sequenceXml = xml->getChildByName("SEQUENCE")) {
      for (auto *stepXml : sequenceXml->getChildIterator()) {
        int i = stepXml->getIntAttribute("id");
        if (i >= 0 && i < 15) {
          steps[i].pitchOffset = stepXml->getIntAttribute("pitch");
          steps[i].velocity = stepXml->getIntAttribute("velocity");
          steps[i].modwheel = stepXml->getIntAttribute("mod");
          steps[i].probability = stepXml->getIntAttribute("prob");
          steps[i].muted = stepXml->getBoolAttribute("mute");
        }
      }
    }
  }
}
juce::AudioProcessorEditor *AetherAudioProcessor::createEditor() {
  return new AetherAudioProcessorEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new AetherAudioProcessor();
}