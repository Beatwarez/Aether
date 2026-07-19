// PluginEditor.h
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

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour, bool, bool) override {
        auto area = button.getLocalBounds().toFloat();
        if (button.getToggleState()) {
            g.setColour(juce::Colour(0xFF00FF41)); g.fillRect(area);
        }
        else {
            g.setColour(juce::Colour(0xFF121212)); g.fillRect(area);
            g.setColour(juce::Colour(0xFF00FF41).withAlpha(0.4f)); g.drawRect(area, 1.0f);
        }
    }
};

class AetherAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    AetherAudioProcessorEditor(AetherAudioProcessor&);
    ~AetherAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
private:
    void handleInteraction(const juce::MouseEvent& e, bool isClick);
    void drawDice(juce::Graphics& g, int x, int y, int size, bool isPressed);
    void drawReset(juce::Graphics& g, int x, int y, int size, bool isPressed);
    AetherLookAndFeel lookAndFeel;
    AetherAudioProcessor& audioProcessor;
    std::vector<std::unique_ptr<juce::TextButton>> radioButtons;
    juce::TextButton msButton;
    juce::TextButton killOnStopButton;
    int pressedDiceIdx = -1, pressedResetIdx = -1, draggingLaneIdx = -1;
    bool draggingMsBox = false; float dragStartMsVal = 0.0f;
    struct Lane { juce::String label; int min, max; bool isBipolar; juce::String propertyId; };
    std::vector<Lane> lanes{
        {"Pitch Shift", -24, 24, true, "pitch"}, {"Velocity", 1, 127, false, "velocity"},
        {"Modwheel", 0, 127, false, "modwheel"}, {"Probability", 0, 100, false, "probability"},
        {"Mute", 0, 1, false, "muted"}
    };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AetherAudioProcessorEditor)
};