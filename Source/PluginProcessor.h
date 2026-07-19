// PluginProcessor.h
#pragma once
#include <JuceHeader.h>
#include <array>
#include <map>
#include <vector>

struct DelayStep {
  int pitchOffset = 0;
  int velocity = 100;
  int modwheel = 0;
  int probability = 100;
  bool muted = false;
};

struct NoteState {
  int channel;
  int noteNumber;
  float velocity;
  int currentStepIndex;
  bool directionForward;
  int lastPlayedNote = -1;
  std::array<int, 15> pitchCaps;
};

struct QueuedEvent {
  juce::MidiMessage message;
  long long triggerSample;
  int tapIndex;
  int stepIndex;
  int noteKey;
};

class AetherAudioProcessor : public juce::AudioProcessor {
public:
  AetherAudioProcessor();
  ~AetherAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override { return true; }
  const juce::String getName() const override { return "AETHER"; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return true; }
  double getTailLengthSeconds() const override { return 15.0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  juce::AudioProcessorValueTreeState apvts;
  std::array<std::array<DelayStep, 15>, 9> steps;
  std::array<int, 9> stepCounts;
  std::atomic<bool> stopRequested{false};

private:
  double lastSampleRate = 44100.0;
  long long totalSamplesProcessed = 0;
  std::vector<QueuedEvent> midiQueue;
  std::vector<std::pair<int, int>> activeNotes;
  std::map<int, NoteState> noteTracker;

  juce::LinearSmoothedValue<float> smoothedDelaySamples;

  juce::Random random;
  bool wasPlaying = false;
  double getSyncTimeInMs();
  juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AetherAudioProcessor)
};