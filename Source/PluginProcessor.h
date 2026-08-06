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

struct Snapshot {
  std::array<DelayStep, 15> steps;
  int stepCount = 15;
  bool enabled = true;
  float delayTimeMs = 500.0f;
  int syncDivision = 0;
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

class AetherAudioProcessor : public juce::AudioProcessor,
                             public juce::AudioProcessorValueTreeState::Listener,
                             public juce::AsyncUpdater {
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

  // APVTS Listener
  void parameterChanged (const juce::String& parameterID, float newValue) override;

  // AsyncUpdater override
  void handleAsyncUpdate() override;
  void loadSnapshotParameters (int snapIdx);

  juce::AudioProcessorValueTreeState apvts;
  std::array<Snapshot, 9> snapshots;
  int editSnapshot = 0;
  Snapshot copiedSnapshot;
  bool hasCopiedSnapshot = false;
  bool isUpdatingSnapshotParameters = false;
  std::atomic<bool> isInitializing{ false };
  std::atomic<bool> stopRequested{false};
  std::atomic<int> activityHits{0};

  juce::String currentBank{ "Factory Presets" };
  juce::String currentPreset{ "Init" };

  juce::File getAppFolder();
  juce::File getPresetsFolder();
  juce::File getSettingsFolder();
  juce::File getPreferencesFile();
  void initFactoryPresets();

  std::unique_ptr<juce::XmlElement> createStateXml();
  void loadStateFromXml(const juce::XmlElement& rootXml);

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