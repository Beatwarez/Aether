// PluginProcessor.h
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
};

class AetherAudioProcessor : public juce::AudioProcessor {
public:
    AetherAudioProcessor();
    ~AetherAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AETHER"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 15.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AetherAudioProcessor)
};