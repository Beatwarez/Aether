// PluginEditor.cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"

AetherAudioProcessorEditor::AetherAudioProcessorEditor(AetherAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
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
    setSize(1040, 1000);
    startTimerHz(30);
}

AetherAudioProcessorEditor::~AetherAudioProcessorEditor() { setLookAndFeel(nullptr); }

void AetherAudioProcessorEditor::timerCallback() {
    int syncIdx = (int)*audioProcessor.apvts.getRawParameterValue("syncDivision");
    if (syncIdx == 0) {
        if (!msButton.getToggleState()) msButton.setToggleState(true, juce::dontSendNotification);
    }
    else {
        int btnIdx = syncIdx - 1;
        if (btnIdx >= 0 && btnIdx < (int)radioButtons.size()) {
            if (!radioButtons[btnIdx]->getToggleState()) radioButtons[btnIdx]->setToggleState(true, juce::dontSendNotification);
        }
    }
    bool k = *audioProcessor.apvts.getRawParameterValue("killOnStop");
    if (killOnStopButton.getToggleState() != k) killOnStopButton.setToggleState(k, juce::dontSendNotification);
    repaint();
}

void AetherAudioProcessorEditor::drawDice(juce::Graphics& g, int x, int y, int size, bool isPressed) {
    auto green = juce::Colour(0xFF00FF41);
    if (isPressed) { g.setColour(green); g.fillRect(x, y, size, size); g.setColour(juce::Colour(0xFF000000)); }
    else { g.setColour(green.withAlpha(0.6f)); g.drawRect(x, y, size, size, 2); }
    float ds = size * 0.15f;
    g.fillEllipse(x + size * 0.2, y + size * 0.2, ds, ds); g.fillEllipse(x + size * 0.7, y + size * 0.2, ds, ds);
    g.fillEllipse(x + size * 0.45, y + size * 0.45, ds, ds); g.fillEllipse(x + size * 0.2, y + size * 0.7, ds, ds);
    g.fillEllipse(x + size * 0.7, y + size * 0.7, ds, ds);
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
            for (auto& s : audioProcessor.steps) {
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
            for (int i = 0; i < 15; ++i) {
                auto& s = audioProcessor.steps[i];
                if (lane.propertyId == "pitch") s.pitchOffset = 0;
                else if (lane.propertyId == "velocity") s.velocity = (int)(127 - (i * (126.0 / 14.0)));
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

void AetherAudioProcessorEditor::mouseDown(const juce::MouseEvent& e) { handleInteraction(e, true); }
void AetherAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e) { handleInteraction(e, false); }
void AetherAudioProcessorEditor::mouseUp(const juce::MouseEvent& e) {
    draggingLaneIdx = -1; pressedDiceIdx = -1; pressedResetIdx = -1; draggingMsBox = false; repaint();
}

void AetherAudioProcessorEditor::paint(juce::Graphics& g) {
    bool en = *audioProcessor.apvts.getRawParameterValue("enabled");
    int scCount = (int)*audioProcessor.apvts.getRawParameterValue("stepCount");
    float tmVal = *audioProcessor.apvts.getRawParameterValue("delayTimeMs");
    int syncIdx = (int)*audioProcessor.apvts.getRawParameterValue("syncDivision");
    g.fillAll(juce::Colour(0xFF121212));
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
    }
    else {
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
                if (!s.muted) { g.fillRect(mb); g.setColour(juce::Colour(0xFF000000)); g.fillEllipse(mb.getCentreX() - 3, mb.getCentreY() - 3, 6, 6); }
                else { g.setColour(juce::Colour(0xFF00FF41).withAlpha(0.1f * op)); g.drawRect(mb, 1.0f); }
            }
            else if (lane.isBipolar) {
                float hh = bh / 2.0f; float h = (std::abs((float)val) / (float)lane.max) * hh;
                if (val >= 0) g.fillRect(40.0f + i * sw + (sw * 0.1f), (float)lY + hh - h, sw * 0.8f, h);
                else g.fillRect(40.0f + i * sw + (sw * 0.1f), (float)lY + hh, sw * 0.8f, h);
            }
            else {
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

void AetherAudioProcessorEditor::paintOverChildren(juce::Graphics& g) {
    auto bBounds = getLocalBounds();

    // 1. PHOSPHOR BLOOM / HAZE (Enhanced Blur simulation)
    for (int i = 0; i < 3; ++i) {
        float sizeMult = 1.0f + (i * 0.3f);
        juce::ColourGradient bloom(juce::Colour(0xFF00FF41).withAlpha(0.05f / (i + 1)),
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
}
