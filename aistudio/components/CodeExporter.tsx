import React, { useState } from 'react';

interface CodeExporterProps {
  onClose: () => void;
}

export const CodeExporter: React.FC<CodeExporterProps> = ({ onClose }) => {
  const [activeTab, setActiveTab] = useState(0);
  const [copied, setCopied] = useState(false);

  const tabs = [
    { name: 'PluginProcessor.h', code: processorH },
    { name: 'PluginProcessor.cpp', code: processorCpp },
    { name: 'PluginEditor.h', code: editorH },
    { name: 'PluginEditor.cpp', code: editorCpp },
  ];

  const handleCopy = () => {
    navigator.clipboard.writeText(tabs[activeTab].code);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <div className="fixed inset-0 z-[1000] bg-black/90 flex flex-col items-center justify-center p-8 backdrop-blur-sm">
      <div className="w-full max-w-6xl h-full flex flex-col bg-[#1a1a1a] border border-[#333] shadow-2xl overflow-hidden">
        <div className="flex justify-between items-center px-6 py-4 border-b border-[#333] bg-[#222]">
          <div className="flex items-center gap-4">
            <h2 className="text-white font-bold tracking-tight text-sm">C++ EXPORTER (JUCE 8 / VST3)</h2>
            <div className="flex gap-1">
              {tabs.map((tab, i) => (
                <button
                  key={tab.name}
                  onClick={() => setActiveTab(i)}
                  className={`px-4 py-2 text-xs font-mono transition-colors ${
                    activeTab === i ? 'bg-[#00ff41] text-black font-bold' : 'text-gray-400 hover:text-white'
                  }`}
                >
                  {tab.name}
                </button>
              ))}
            </div>
          </div>
          <div className="flex gap-4">
            <button onClick={handleCopy} className="px-4 py-2 bg-white/5 hover:bg-white/10 text-white text-xs font-bold border border-white/10">
              {copied ? 'COPIED!' : 'COPY CODE'}
            </button>
            <button onClose={onClose} className="px-4 py-2 text-gray-400 hover:text-white text-xs font-bold">CLOSE</button>
          </div>
        </div>
        <div className="flex-1 overflow-auto bg-[#0a0a0a] p-6">
          <pre className="text-sm font-mono leading-relaxed text-gray-300 selection:bg-[#00ff41] selection:text-black">
            <code>{tabs[activeTab].code}</code>
          </pre>
        </div>
        <div className="px-6 py-3 border-t border-[#333] bg-[#1a1a1a] text-[10px] text-gray-500 uppercase flex justify-between">
          <span>Target: JUCE 8.x / C++17</span>
          <span>Unified Button Styling & 32-Stop Enhanced Vignette</span>
        </div>
      </div>
    </div>
  );
};

const processorH = `// PluginProcessor.h
#pragma once
#include <JuceHeader.h>
#include <map>

struct DelayStep {
    int pitchOffset = 0;
    int velocity = 100;
    int modwheel = 0;
    int probability = 100;
    bool muted = false;
};

struct QueuedEvent {
    juce::MidiMessage message;
    long long triggerSample; 
    int tapIndex;           
    int direction; // For pendulum mode: 1 for forward, -1 for backward
};

class AetherAudioProcessor : public juce::AudioProcessor {
public:
    AetherAudioProcessor();
    ~AetherAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AETHER"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 15.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    std::array<DelayStep, 15> steps;

private:
    double lastSampleRate = 44100.0;
    long long totalSamplesProcessed = 0;
    std::vector<QueuedEvent> midiQueue;
    std::map<int, std::array<int, 15>> noteTracker;
    
    juce::LinearSmoothedValue<float> smoothedDelaySamples;
    
    juce::Random random;
    bool wasPlaying = false; 
    double getSyncTimeInMs();
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JU_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AetherAudioProcessor)
};`;

const processorCpp = `// PluginProcessor.cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"

AetherAudioProcessor::AetherAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    for (int i = 0; i < 15; ++i) {
        steps[i].velocity = (int)(127 - (i * (126.0/14.0)));
        steps[i].modwheel = 0;
        steps[i].probability = 100;
        steps[i].pitchOffset = 0;
        steps[i].muted = false;
    }
}

AetherAudioProcessor::~AetherAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout AetherAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>("delayTimeMs", "Delay Time (ms)", 1.0f, 2000.0f, 500.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>("enabled", "Enabled", true));
    layout.add(std::make_unique<juce::AudioParameterInt>("stepCount", "Step Count", 1, 15, 15));
    layout.add(std::make_unique<juce::AudioParameterBool>("killOnStop", "Kill On Stop", true));
    layout.add(std::make_unique<juce::AudioParameterInt>("syncDivision", "Sync Division", 0, 18, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>("isLooping", "Loop Active", false));
    layout.add(std::make_unique<juce::AudioParameterInt>("loopMode", "Loop Mode", 0, 2, 0)); // 0: Fwd, 1: Pend, 2: Rand
    layout.add(std::make_unique<juce::AudioParameterBool>("loopNoteRestart", "Loop Note Restart", false));
    return layout;
}

void AetherAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock) {
    lastSampleRate = (sampleRate > 0) ? sampleRate : 44100.0;
    totalSamplesProcessed = 0;
    midiQueue.clear();
    noteTracker.clear();
    wasPlaying = false;
    
    float currentMs = *apvts.getRawParameterValue("delayTimeMs");
    smoothedDelaySamples.reset(lastSampleRate, 0.1); 
    smoothedDelaySamples.setCurrentAndTargetValue(currentMs * 0.001f * (float)lastSampleRate);
}

double AetherAudioProcessor::getSyncTimeInMs() {
    int syncIdx = (int)*apvts.getRawParameterValue("syncDivision");
    if (syncIdx == 0) return (double)*apvts.getRawParameterValue("delayTimeMs");

    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            auto bpmOpt = pos->getBpm();
            double bpm = bpmOpt ? *bpmOpt : 120.0;
            double qnMs = (60.0 / bpm) * 1000.0;
            int div = (syncIdx - 1) % 6; 
            int type = (syncIdx - 1) / 6; 
            double bt = qnMs;
            switch (div) {
                case 0: bt = qnMs * 4.0; break; case 1: bt = qnMs * 2.0; break;
                case 2: bt = qnMs; break; case 3: bt = qnMs * 0.5; break;
                case 4: bt = qnMs * 0.25; break; case 5: bt = qnMs * 0.125; break;
            }
            if (type == 1) bt *= 1.5; else if (type == 2) bt *= (2.0/3.0);
            return bt;                             
        }
    }
    return (double)*apvts.getRawParameterValue("delayTimeMs");
}

void AetherAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    auto numSamples = buffer.getNumSamples();
    bool isPlaying = false;
    if (auto* ph = getPlayHead()) { if (auto pos = ph->getPosition()) isPlaying = pos->getIsPlaying(); }

    bool pKill = *apvts.getRawParameterValue("killOnStop");
    bool pEnabled = *apvts.getRawParameterValue("enabled");
    int pStepCount = (int)*apvts.getRawParameterValue("stepCount");
    int syncIdx = (int)*apvts.getRawParameterValue("syncDivision");
    bool pLoop = *apvts.getRawParameterValue("isLooping");
    int pLoopMode = (int)*apvts.getRawParameterValue("loopMode");
    bool pRestart = *apvts.getRawParameterValue("loopNoteRestart");

    if (pKill && !isPlaying && wasPlaying) {
        for (int ch = 1; ch <= 16; ++ch) midiMessages.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
        midiQueue.clear(); noteTracker.clear();
    }
    wasPlaying = isPlaying;

    if (!pEnabled || (pKill && !isPlaying)) {
        midiQueue.clear(); totalSamplesProcessed += numSamples; return;
    }

    double targetMs = (syncIdx == 0) ? (double)*apvts.getRawParameterValue("delayTimeMs") : getSyncTimeInMs();
    targetMs = juce::jmax(1.0, targetMs);
    smoothedDelaySamples.setTargetValue((float)(targetMs * 0.001 * lastSampleRate));

    for (const auto metadata : midiMessages) {
        auto msg = metadata.getMessage();
        int localPos = metadata.samplePosition;
        int noteKey = (msg.getChannel() << 7) | msg.getNoteNumber();

        if (msg.isNoteOn()) {
            if (pRestart) {
                midiQueue.erase(std::remove_if(midiQueue.begin(), midiQueue.end(),
                    [noteKey](const QueuedEvent& e) { 
                        return ((e.message.getChannel() << 7) | e.message.getNoteNumber()) == noteKey; 
                    }), midiQueue.end());
            }

            std::array<int, 15> cap; for(int i=0; i<15; ++i) cap[i] = steps[i].pitchOffset;
            noteTracker[noteKey] = cap;
            long long origin = totalSamplesProcessed + localPos;
            
            // Queue the first step
            int i = 0;
            if (!steps[i].muted && random.nextInt(100) < steps[i].probability) {
                int targetNote = juce::jlimit(0, 127, msg.getNoteNumber() + cap[i]);
                auto kill = juce::MidiMessage::noteOff(msg.getChannel(), targetNote);
                midiQueue.push_back({kill, origin, i + 1, 1});
                auto dOn = msg;
                dOn.setNoteNumber(targetNote);
                dOn.setVelocity(steps[i].velocity / 127.0f);
                midiQueue.push_back({dOn, origin + 1, i + 1, 1});
                auto mod = juce::MidiMessage::controllerEvent(msg.getChannel(), 1, steps[i].modwheel);
                midiQueue.push_back({mod, origin, i + 1, 1});
            } else {
                // If first step is muted/skipped, we still need to queue the "next" step trigger
                // but we'll handle that in the sample loop by checking if a step was skipped.
                // Actually, let's just queue a "dummy" event or handle it differently.
                // Simpler: if step 1 is muted, we don't play it, but we still want to trigger step 2 after delay.
                // Let's add a "silent" event to the queue to keep the chain going.
                midiQueue.push_back({juce::MidiMessage(), origin, i + 1, 1});
            }
        }
        else if (msg.isNoteOff()) {
            if (noteTracker.count(noteKey)) {
                auto cap = noteTracker[noteKey];
                long long origin = totalSamplesProcessed + localPos;
                // For NoteOff, we still want to queue the release for all active taps
                // This is tricky with one-by-one. 
                // Actually, the plugin is "polyphonic MIDI delay", so it delays the whole note.
                // The current implementation delays the NoteOff as well.
                // If we go one-by-one, we need to track which taps are "active" for each note.
                // Let's stick to the current NoteOff logic for now, it's safer.
                for (int i = 0; i < pStepCount; ++i) {
                    if (steps[i].muted) continue;
                    auto dOff = msg;
                    dOff.setNoteNumber(juce::jlimit(0, 127, msg.getNoteNumber() + cap[i]));
                    midiQueue.push_back({dOff, origin, i + 1, 1});
                }
                noteTracker.erase(noteKey);
            }
        }
    }

    for (int sample = 0; sample < numSamples; ++sample) {
        float currentDelaySamples = smoothedDelaySamples.getNextValue();
        long long currentTime = totalSamplesProcessed + sample;

        for (int i = 0; i < (int)midiQueue.size(); ++i) {
            auto& ev = midiQueue[i];
            long long eventTargetTime = ev.triggerSample + (long long)(currentDelaySamples * ev.tapIndex);
            
            if (eventTargetTime <= currentTime) {
                if (!ev.message.isNoteOff() && ev.message.getRawDataSize() > 0) {
                    midiMessages.addEvent(ev.message, sample);
                }

                // If it was a NoteOn or Controller or Dummy, trigger the next step
                if (ev.message.isNoteOn() || ev.message.isController() || ev.message.getRawDataSize() == 0) {
                    // Only trigger next step once per tap (e.g. on the NoteOn event)
                    if (ev.message.isNoteOn() || (ev.message.getRawDataSize() == 0)) {
                        int currentIdx = ev.tapIndex - 1;
                        int nextIdx = -1;
                        int nextDir = ev.direction;

                        if (pLoop) {
                            if (pLoopMode == 0) { // Forward
                                nextIdx = (currentIdx + 1) % pStepCount;
                            } else if (pLoopMode == 1) { // Pendulum
                                nextIdx = currentIdx + nextDir;
                                if (nextIdx >= pStepCount) { nextIdx = pStepCount - 2; nextDir = -1; }
                                else if (nextIdx < 0) { nextIdx = 1; nextDir = 1; }
                                nextIdx = juce::jlimit(0, pStepCount - 1, nextIdx);
                            } else { // Random
                                nextIdx = random.nextInt(pStepCount);
                            }
                        } else {
                            if (currentIdx < pStepCount - 1) nextIdx = currentIdx + 1;
                        }

                        if (nextIdx != -1) {
                            int noteKey = (ev.message.getChannel() << 7) | ev.message.getNoteNumber();
                            if (noteTracker.count(noteKey)) {
                                auto cap = noteTracker[noteKey];
                                if (!steps[nextIdx].muted && random.nextInt(100) < steps[nextIdx].probability) {
                                    int targetNote = juce::jlimit(0, 127, ev.message.getNoteNumber() + cap[nextIdx]);
                                    auto kill = juce::MidiMessage::noteOff(ev.message.getChannel(), targetNote);
                                    midiQueue.push_back({kill, ev.triggerSample, nextIdx + 1, nextDir});
                                    auto dOn = ev.message;
                                    dOn.setNoteNumber(targetNote);
                                    dOn.setVelocity(steps[nextIdx].velocity / 127.0f);
                                    midiQueue.push_back({dOn, ev.triggerSample + 1, nextIdx + 1, nextDir});
                                    auto mod = juce::MidiMessage::controllerEvent(ev.message.getChannel(), 1, steps[nextIdx].modwheel);
                                    midiQueue.push_back({mod, ev.triggerSample, nextIdx + 1, nextDir});
                                } else {
                                    // Silent step, but keep loop going
                                    midiQueue.push_back({juce::MidiMessage(), ev.triggerSample, nextIdx + 1, nextDir});
                                }
                            }
                        }
                    }
                } else if (ev.message.isNoteOff()) {
                    midiMessages.addEvent(ev.message, sample);
                }

                midiQueue.erase(midiQueue.begin() + i);
                --i;
            }
        }
    }

    totalSamplesProcessed += numSamples;
    if (midiQueue.size() > 500000) {
        auto cutoff = totalSamplesProcessed - (long long)(lastSampleRate * 15.0); 
        midiQueue.erase(std::remove_if(midiQueue.begin(), midiQueue.end(),
            [cutoff](const QueuedEvent& e) { return e.triggerSample < cutoff; }), midiQueue.end());
    }
}

void AetherAudioProcessor::getStateInformation (juce::MemoryBlock& d) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    auto* sequenceXml = xml->createNewChildElement("SEQUENCE");
    for (int i = 0; i < 15; ++i) {
        auto* stepXml = sequenceXml->createNewChildElement("STEP");
        stepXml->setAttribute("id", i);
        stepXml->setAttribute("pitch", steps[i].pitchOffset);
        stepXml->setAttribute("velocity", steps[i].velocity);
        stepXml->setAttribute("mod", steps[i].modwheel);
        stepXml->setAttribute("prob", steps[i].probability);
        stepXml->setAttribute("mute", steps[i].muted);
    }
    juce::AudioProcessor::copyXmlToBinary (*xml, d);
}

void AetherAudioProcessor::setStateInformation (const void* d, int s) {
    std::unique_ptr<juce::XmlElement> xml (juce::AudioProcessor::getXmlFromBinary (d, s));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType())) {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        if (auto* sequenceXml = xml->getChildByName("SEQUENCE")) {
            for (auto* stepXml : sequenceXml->getChildIterator()) {
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
juce::AudioProcessorEditor* AetherAudioProcessor::createEditor() { return new AetherAudioProcessorEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AetherAudioProcessor(); }`;

const editorH = `// PluginEditor.h
#pragma once
#include "PluginProcessor.h"

class AetherLookAndFeel : public juce::LookAndFeel_V4 {
public:
    AetherLookAndFeel() {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xFF121212));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF121212));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF00FF41));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF00FF41));
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xFF000000));
    }
    
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour, bool, bool) override {
        auto area = button.getLocalBounds().toFloat();
        if (button.getToggleState()) {
            g.setColour(juce::Colour(0xFF00FF41)); g.fillRect(area);
        } else {
            g.setColour(juce::Colour(0xFF121212)); g.fillRect(area);
            g.setColour(juce::Colour(0xFF00FF41).withAlpha(0.4f)); g.drawRect(area, 1.0f);
        }
    }
};

class AetherAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    AetherAudioProcessorEditor (AetherAudioProcessor&);
    ~AetherAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
private:
    void handleInteraction(const juce::MouseEvent& e, bool isClick);
    void drawDice(juce::Graphics& g, int x, int y, int size, bool isPressed);
    void drawReset(juce::Graphics& g, int x, int y, int size, bool isPressed);
    AetherLookAndFeel lookAndFeel;
    AetherAudioProcessor& audioProcessor;
    std::vector<std::unique_ptr<juce::TextButton>> radioButtons;
    juce::TextButton msButton;
    juce::TextButton killOnStopButton;
    juce::TextButton loopButton;
    juce::TextButton loopNoteRestartButton;
    std::vector<std::unique_ptr<juce::TextButton>> loopModeButtons;
    int pressedDiceIdx = -1, pressedResetIdx = -1, draggingLaneIdx = -1;
    bool draggingMsBox = false; float dragStartMsVal = 0.0f;
    struct Lane { juce::String label; int min, max; bool isBipolar; juce::String propertyId; };
    std::vector<Lane> lanes { 
        {"Pitch Shift", -24, 24, true, "pitch"}, {"Velocity", 1, 127, false, "velocity"}, 
        {"Modwheel", 0, 127, false, "modwheel"}, {"Probability", 0, 100, false, "probability"}, 
        {"Mute", 0, 1, false, "muted"} 
    };
    JU_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AetherAudioProcessorEditor)
};`;

const editorCpp = `// PluginEditor.cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"

AetherAudioProcessorEditor::AetherAudioProcessorEditor (AetherAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel(&lookAndFeel);
    const juce::StringArray names = {
        "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
        "1/1d", "1/2d", "1/4d", "1/8d", "1/16d", "1/32d",
        "1/1t", "1/2t", "1/4t", "1/8t", "1/16t", "1/32t"
    };
    for (int i = 0; i < names.size(); ++i) {
        auto* b = radioButtons.emplace_back(std::make_unique<juce::TextButton>(names[i])).get();
        b->setRadioGroupId(101);
        b->setClickingTogglesState(true);
        b->onClick = [this, i] { 
            auto* param = audioProcessor.apvts.getParameter("syncDivision");
            param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(i + 1));
            repaint(); 
        };
        addAndMakeVisible(b);
    }
    msButton.setButtonText("MANUAL (MS)");
    msButton.setRadioGroupId(101);
    msButton.setClickingTogglesState(true);
    msButton.onClick = [this] { 
        auto* param = audioProcessor.apvts.getParameter("syncDivision");
        param->setValueNotifyingHost(0.0f);
        repaint(); 
    };
    addAndMakeVisible(msButton);
    killOnStopButton.setButtonText("KILL ON STOP");
    killOnStopButton.setClickingTogglesState(true);
    killOnStopButton.onClick = [this] {
        auto* p = audioProcessor.apvts.getParameter("killOnStop");
        p->setValueNotifyingHost(killOnStopButton.getToggleState() ? 1.0f : 0.0f);
        repaint();
    };
    addAndMakeVisible(killOnStopButton);

    loopButton.setButtonText("LOOP");
    loopButton.setClickingTogglesState(true);
    loopButton.onClick = [this] {
        auto* p = audioProcessor.apvts.getParameter("isLooping");
        p->setValueNotifyingHost(loopButton.getToggleState() ? 1.0f : 0.0f);
        repaint();
    };
    addAndMakeVisible(loopButton);

    loopNoteRestartButton.setButtonText("RESTART");
    loopNoteRestartButton.setClickingTogglesState(true);
    loopNoteRestartButton.onClick = [this] {
        auto* p = audioProcessor.apvts.getParameter("loopNoteRestart");
        p->setValueNotifyingHost(loopNoteRestartButton.getToggleState() ? 1.0f : 0.0f);
        repaint();
    };
    addAndMakeVisible(loopNoteRestartButton);

    const juce::StringArray modes = {"FWD", "PEND", "RAND"};
    for (int i = 0; i < modes.size(); ++i) {
        auto* b = loopModeButtons.emplace_back(std::make_unique<juce::TextButton>(modes[i])).get();
        b->setRadioGroupId(102);
        b->setClickingTogglesState(true);
        b->onClick = [this, i] {
            auto* p = audioProcessor.apvts.getParameter("loopMode");
            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1((float)i));
            repaint();
        };
        addAndMakeVisible(b);
    }

    setSize (1040, 1000); 
    startTimerHz(30);
}

AetherAudioProcessorEditor::~AetherAudioProcessorEditor() { setLookAndFeel(nullptr); }

void AetherAudioProcessorEditor::timerCallback() {
    int syncIdx = (int)*audioProcessor.apvts.getRawParameterValue("syncDivision");
    if (syncIdx == 0) {
        if (!msButton.getToggleState()) msButton.setToggleState(true, juce::dontSendNotification);
    } else {
        int btnIdx = syncIdx - 1;
        if (btnIdx >= 0 && btnIdx < (int)radioButtons.size()) {
            if (!radioButtons[btnIdx]->getToggleState()) radioButtons[btnIdx]->setToggleState(true, juce::dontSendNotification);
        }
    }
    bool k = *audioProcessor.apvts.getRawParameterValue("killOnStop");
    if (killOnStopButton.getToggleState() != k) killOnStopButton.setToggleState(k, juce::dontSendNotification);
    
    bool l = *audioProcessor.apvts.getRawParameterValue("isLooping");
    if (loopButton.getToggleState() != l) loopButton.setToggleState(l, juce::dontSendNotification);
    
    bool r = *audioProcessor.apvts.getRawParameterValue("loopNoteRestart");
    if (loopNoteRestartButton.getToggleState() != r) loopNoteRestartButton.setToggleState(r, juce::dontSendNotification);
    
    int lm = (int)*audioProcessor.apvts.getRawParameterValue("loopMode");
    if (lm >= 0 && lm < (int)loopModeButtons.size()) {
        if (!loopModeButtons[lm]->getToggleState()) loopModeButtons[lm]->setToggleState(true, juce::dontSendNotification);
    }
    
    repaint();
}

void AetherAudioProcessorEditor::drawDice(juce::Graphics& g, int x, int y, int size, bool isPressed) {
    auto green = juce::Colour(0xFF00FF41);
    if (isPressed) { g.setColour(green); g.fillRect(x, y, size, size); g.setColour(juce::Colour(0xFF000000)); }
    else { g.setColour(green.withAlpha(0.6f)); g.drawRect(x, y, size, size, 2); }
    float ds = size * 0.15f;
    g.fillEllipse(x + size*0.2, y + size*0.2, ds, ds); g.fillEllipse(x + size*0.7, y + size*0.2, ds, ds);
    g.fillEllipse(x + size*0.45, y + size*0.45, ds, ds); g.fillEllipse(x + size*0.2, y + size*0.7, ds, ds);
    g.fillEllipse(x + size*0.7, y + size*0.7, ds, ds);
}

void AetherAudioProcessorEditor::drawReset(juce::Graphics& g, int x, int y, int size, bool isPressed) {
    auto green = juce::Colour(0xFF00FF41);
    if (isPressed) { g.setColour(green); g.fillRect(x, y, size, size); g.setColour(juce::Colour(0xFF000000)); }
    else { g.setColour(green.withAlpha(0.6f)); g.drawRect(x, y, size, size, 2); }
    g.setFont(juce::Font(size * 0.8f, juce::Font::bold)); g.drawText("R", x, y, size, size, juce::Justification::centred);
}

void AetherAudioProcessorEditor::handleInteraction(const juce::MouseEvent& e, bool isClick) {
    int laneStartY = 200, spacing = 150;
    int startX = 610, w = 62, h = 28;
    juce::Rectangle<int> msBoxRect(startX + 5 * w, 40 + 3 * h, w, h);
    int syncIdx = (int)*audioProcessor.apvts.getRawParameterValue("syncDivision");
    if (syncIdx == 0) {
        if (isClick && msBoxRect.contains(e.x, e.y)) {
            draggingMsBox = true; dragStartMsVal = *audioProcessor.apvts.getRawParameterValue("delayTimeMs");
            return;
        }
        if (draggingMsBox && !isClick) {
            float delta = (float)e.getDistanceFromDragStartY() * -0.5f;
            float newVal = juce::jlimit(1.0f, 2000.0f, dragStartMsVal + delta);
            audioProcessor.apvts.getParameter("delayTimeMs")->setValueNotifyingHost(audioProcessor.apvts.getParameter("delayTimeMs")->getNormalisableRange().convertTo0to1(newVal));
            repaint(); return;
        }
    }
    if (isClick && juce::Rectangle<int>(40, 40, 50, 50).contains(e.x, e.y)) {
        auto* p = audioProcessor.apvts.getParameter("enabled");
        p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f); repaint(); return;
    }
    if (draggingLaneIdx != -1 && !isClick) {
        int l = draggingLaneIdx, curY = laneStartY + (l * spacing);
        float sw = 950.0f / 15.0f; int idx = juce::jlimit(0, 14, (int)((e.x - 40) / sw));
        auto& s = audioProcessor.steps[idx]; auto& lane = lanes[l];
        if (lane.propertyId != "muted") {
            float nY = 1.0f - (float)(e.y - curY) / 80.0f; nY = juce::jlimit(0.0f, 1.0f, nY);
            int val = (int)(lane.min + nY * (lane.max - lane.min));
            if (lane.propertyId == "pitch") s.pitchOffset = val; else if (lane.propertyId == "velocity") s.velocity = val;
            else if (lane.propertyId == "modwheel") s.modwheel = val; else if (lane.propertyId == "probability") s.probability = val;
            repaint();
        }
        return;
    }
    for (int l = 0; l < (int)lanes.size(); ++l) {
        int curY = laneStartY + (l * spacing);
        if (isClick && juce::Rectangle<int>(40, curY - 30, 24, 24).contains(e.x, e.y)) {
            pressedDiceIdx = l; repaint(); juce::Random r; auto& lane = lanes[l];
            for(auto& s : audioProcessor.steps) {
                if (lane.propertyId == "pitch") s.pitchOffset = r.nextInt(juce::Range<int>(-24, 25));
                else if (lane.propertyId == "velocity") s.velocity = r.nextInt(juce::Range<int>(1, 128));
                else if (lane.propertyId == "modwheel") s.modwheel = r.nextInt(juce::Range<int>(0, 128));
                else if (lane.propertyId == "probability") s.probability = r.nextInt(juce::Range<int>(0, 101));
                else if (lane.propertyId == "muted") s.muted = r.nextBool();
            }
            return;
        }
        if (isClick && juce::Rectangle<int>(70, curY - 30, 24, 24).contains(e.x, e.y)) {
            pressedResetIdx = l; repaint(); auto& lane = lanes[l];
            for(int i=0; i<15; ++i) {
                auto& s = audioProcessor.steps[i];
                if (lane.propertyId == "pitch") s.pitchOffset = 0;
                else if (lane.propertyId == "velocity") s.velocity = (int)(127 - (i * (126.0/14.0)));
                else if (lane.propertyId == "modwheel") s.modwheel = 0;
                else if (lane.propertyId == "probability") s.probability = 100;
                else if (lane.propertyId == "muted") s.muted = false;
            }
            return;
        }
        if (juce::Rectangle<int>(40, curY, 950, 100).contains(e.x, e.y)) {
            draggingLaneIdx = l; float sw = 950.0f / 15.0f; int idx = juce::jlimit(0, 14, (int)((e.x - 40) / sw));
            auto& s = audioProcessor.steps[idx]; auto& lane = lanes[l];
            if (lane.propertyId == "muted") { if (isClick) { s.muted = !s.muted; repaint(); } }
            else {
                float nY = 1.0f - (float)(e.y - curY) / 80.0f; nY = juce::jlimit(0.0f, 1.0f, nY);
                int val = (int)(lane.min + nY * (lane.max - lane.min));
                if (lane.propertyId == "pitch") s.pitchOffset = val; else if (lane.propertyId == "velocity") s.velocity = val;
                else if (lane.propertyId == "modwheel") s.modwheel = val; else if (lane.propertyId == "probability") s.probability = val;
                repaint();
            }
            return;
        }
    }
    if (juce::Rectangle<int>(40, 930, 950, 30).contains(e.x, e.y)) {
        float n = (float)(e.x - 40) / 950.0f; audioProcessor.apvts.getParameter("stepCount")->setValueNotifyingHost(n);
        repaint();
    }
}

void AetherAudioProcessorEditor::mouseDown (const juce::MouseEvent& e) { handleInteraction(e, true); }
void AetherAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e) { handleInteraction(e, false); }
void AetherAudioProcessorEditor::mouseUp (const juce::MouseEvent& e) { 
    draggingLaneIdx = -1; pressedDiceIdx = -1; pressedResetIdx = -1; draggingMsBox = false; repaint();
}

void AetherAudioProcessorEditor::paint (juce::Graphics& g) {
    bool en = *audioProcessor.apvts.getRawParameterValue("enabled");
    int scCount = (int)*audioProcessor.apvts.getRawParameterValue("stepCount");
    float tmVal = *audioProcessor.apvts.getRawParameterValue("delayTimeMs");
    int syncIdx = (int)*audioProcessor.apvts.getRawParameterValue("syncDivision");
    g.fillAll (juce::Colour(0xFF121212)); 
    if (!en) g.setOpacity(0.4f);
    g.setColour(juce::Colour(0xFF00FF41));
    g.drawEllipse(40, 40, 50, 50, 2);
    if (en) { g.fillEllipse(50, 50, 30, 30); g.setColour(juce::Colour(0xFF000000)); g.drawText("I", 50, 50, 30, 30, juce::Justification::centred); }
    else { g.drawText("O", 50, 50, 30, 30, juce::Justification::centred); }
    g.setColour(juce::Colour(0xFF00FF41)); g.setFont(juce::Font("IBM Plex Mono", 64.0f, juce::Font::bold));
    g.drawText("AETHER", 110, 30, 400, 70, juce::Justification::left);
    g.setFont(14.0f); g.setOpacity(0.6f); g.drawText("Polyphonic MIDI Delay", 115, 95, 400, 20, juce::Justification::left);
    g.setFont(juce::Font("IBM Plex Mono", 10.0f, juce::Font::bold)); g.setOpacity(0.6f);
    g.drawText("DELAY TIME MATRIX", 610, 20, 200, 15, juce::Justification::left);
    int startX = 610; int w = 62, h = 28;
    juce::Rectangle<int> msBox(startX + 5 * w, 40 + 3 * h, w, h);
    bool isMsMode = (syncIdx == 0);
    
    // MS VALUE BOX Rendering (Unified styling with buttons)
    if (isMsMode) {
        g.setColour(juce::Colour(0xFF00FF41));
        g.fillRect(msBox);
        g.setColour(juce::Colour(0xFF000000));
    } else {
        g.setColour(juce::Colour(0xFF121212));
        g.fillRect(msBox);
        g.setColour(juce::Colour(0xFF00FF41).withAlpha(0.4f));
        g.drawRect(msBox, 1);
        g.setColour(juce::Colour(0xFF00FF41).withAlpha(0.2f));
    }
    g.setFont(juce::Font("IBM Plex Mono", 14.0f, juce::Font::bold));
    g.drawText(juce::String((int)tmVal), msBox, juce::Justification::centred);
    
    g.setOpacity(1.0f);
    int laneStartY = 200, spacing = 150;
    for (int l = 0; l < (int)lanes.size(); ++l) {
        int lY = laneStartY + (l * spacing); auto& lane = lanes[l];
        drawDice(g, 40, lY - 28, 20, pressedDiceIdx == l);
        drawReset(g, 70, lY - 28, 20, pressedResetIdx == l);
        g.setOpacity(0.8f); g.setColour(juce::Colour(0xFF00FF41)); g.setFont(juce::Font("IBM Plex Mono", 18.0f, juce::Font::bold));
        g.drawText(lane.label.toUpperCase(), 105, lY - 30, 200, 25, juce::Justification::left);
        float sw = 950.0f / 15.0f; int bh = 80;
        for (int i = 0; i < 15; ++i) {
            auto& s = audioProcessor.steps[i]; float op = (i >= scCount) ? 0.2f : 1.0f;
            g.setColour(juce::Colour(0xFF121212)); g.fillRect(40.0f + i * sw + (sw * 0.1f), (float)lY, sw * 0.8f, (float)bh);
            g.setColour(juce::Colour(0xFF00FF41).withAlpha(0.3f * op)); g.drawRect(40.0f + i * sw + (sw * 0.1f), (float)lY, sw * 0.8f, (float)bh, 1.0f);
            g.setColour(juce::Colour(0xFF00FF41).withAlpha(op));
            int val = 0; juce::String vs = "";
            if (lane.propertyId == "pitch") { val = s.pitchOffset; vs = (val > 0 ? "+" : "") + juce::String(val); }
            else if (lane.propertyId == "velocity") { val = s.velocity; vs = juce::String(val); }
            else if (lane.propertyId == "modwheel") { val = s.modwheel; vs = juce::String(val); }
            else if (lane.propertyId == "probability") { val = s.probability; vs = juce::String(val) + "%"; }
            if (lane.propertyId == "muted") {
                juce::Rectangle<float> mb(40.0f + i * sw + 8, (float)lY + 8, sw - 16, (float)bh - 16);
                if (!s.muted) { g.fillRect(mb); g.setColour(juce::Colour(0xFF000000)); g.fillEllipse(mb.getCentreX()-3, mb.getCentreY()-3, 6, 6); }
                else { g.setColour(juce::Colour(0xFF00FF41).withAlpha(0.1f * op)); g.drawRect(mb, 1.0f); }
            } else if (lane.isBipolar) {
                float hh = bh / 2.0f; float h = (std::abs((float)val) / (float)lane.max) * hh;
                if (val >= 0) g.fillRect(40.0f + i * sw + (sw * 0.1f), (float)lY + hh - h, sw * 0.8f, h);
                else g.fillRect(40.0f + i * sw + (sw * 0.1f), (float)lY + hh, sw * 0.8f, h);
            } else {
                float h = ((float)(val - lane.min) / (float)(lane.max - lane.min)) * bh;
                g.fillRect(40.0f + i * sw + (sw * 0.1f), (float)lY + (bh - h), sw * 0.8f, h);
            }
            if (lane.propertyId != "muted") {
                g.setColour(juce::Colour(0xFF00FF41).withAlpha(op)); g.setFont(juce::Font("IBM Plex Mono", 16.5f, juce::Font::bold));
                g.drawText(vs, (int)(40.0f + i * sw), lY + bh + 4, (int)sw, 24, juce::Justification::centred);
            }
        }
    }
    g.setOpacity(1.0f); g.setColour(juce::Colour(0xFF00FF41)); g.setFont(juce::Font("IBM Plex Mono", 14.0f, juce::Font::bold));
    g.drawText("STEP COUNT: " + juce::String(scCount), 40, 910, 400, 20, juce::Justification::left);
    float sew = 950.0f / 15.0f;
    for (int i = 0; i < 15; ++i) {
        juce::Rectangle<float> seg(40.0f + i * sew + 1, 935, sew - 2, 25);
        bool isActive = (i < scCount); g.setColour(isActive ? juce::Colour(0xFF00FF41) : juce::Colour(0xFF003311));
        g.fillRect(seg); g.setColour(isActive ? juce::Colour(0xFF00FF41).withAlpha(0.4f) : juce::Colour(0xFF004411)); g.drawRect(seg, 1.0f);
    }
}

void AetherAudioProcessorEditor::paintOverChildren (juce::Graphics& g) {
    auto bBounds = getLocalBounds();

    // 1. PHOSPHOR BLOOM / HAZE (Enhanced Blur simulation)
    for (int i = 0; i < 3; ++i) {
        float sizeMult = 1.0f + (i * 0.3f);
        juce::ColourGradient bloom(juce::Colour(0xFF00FF41).withAlpha(0.05f / (i+1)), 
                                   (float)bBounds.getCentreX(), (float)bBounds.getCentreY(), 
                                   juce::Colours::transparentBlack, 0.0f, 0.0f, true);
        bloom.addColour(0.4 * sizeMult, juce::Colour(0xFF00FF41).withAlpha(0.02f));
        g.setGradientFill(bloom);
        g.fillRect(bBounds);
    }

    // 2. SCANLINES (Refined prominence)
    g.setOpacity(1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.22f));
    for (int y = 0; y < getHeight(); y += 3) {
        g.fillRect(0, y, getWidth(), 1);
    }

    // 3. ULTRA-SMOOTH VIGNETTE (Radius 1.3x bigger, Intensity 0.7x)
    float cX = (float)bBounds.getCentreX();
    float cY = (float)bBounds.getCentreY();
    float vignetteIntensity = 0.595f; // Original 0.85f * 0.7f

    // Pushing end-point 1.3x further out from the center increases the effective radius
    juce::ColourGradient vignette(juce::Colours::transparentBlack, cX, cY,
                                  juce::Colours::black.withAlpha(vignetteIntensity), 
                                  cX - (cX * 1.3f), cY - (cY * 1.3f), true);
    
    // Cubic falloff over 32 stops
    for (int i = 0; i <= 32; ++i) {
        float pos = (float)i / 32.0f;
        float alpha = 0.0f;
        if (pos > 0.5f) {
            float t = (pos - 0.5f) / 0.5f;
            alpha = (t * t * t) * vignetteIntensity;
        }
        vignette.addColour(pos, juce::Colours::black.withAlpha(alpha));
    }
    
    g.setGradientFill(vignette);
    g.fillRect(bBounds);

    // 4. RETRO STATIC / GRAIN
    g.setColour(juce::Colour(0xFF00FF41).withAlpha(0.04f));
    juce::Random& r = juce::Random::getSystemRandom();
    for (int i = 0; i < 1500; ++i) {
        int rx = r.nextInt(getWidth());
        int ry = r.nextInt(getHeight());
        g.fillRect(rx, ry, 1, 1);
    }

    // 5. SCREEN DISTORTION BORDER
    g.setColour(juce::Colour(0xFF00FF41).withAlpha(0.08f));
    g.drawRect(bBounds, 4);
}

void AetherAudioProcessorEditor::resized() {
    int startX = 610, startY = 40, w = 62, h = 28, gap = 0;
    for (int i = 0; i < 18; ++i) {
        int col = i % 6, row = i / 6;
        radioButtons[i]->setBounds(startX + col * (w + gap), startY + row * (h + gap), w, h);
    }
    killOnStopButton.setBounds(startX, startY + 3 * h, 2 * w, h);
    msButton.setBounds(startX + 2 * w, startY + 3 * h, 3 * w, h);
    
    // Position loop controls (between logo and matrix in web, but here we place them logically)
    // Let's place them below the delay matrix controls
    int loopY = startY + 4 * h + 10;
    loopButton.setBounds(startX, loopY, w * 1.5, h);
    loopNoteRestartButton.setBounds(startX + w * 1.5, loopY, w * 1.5, h);
    for (int i = 0; i < (int)loopModeButtons.size(); ++i) {
        loopModeButtons[i]->setBounds(startX + (3 + i) * w, loopY, w, h);
    }
}
`;