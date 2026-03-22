#pragma once

#include <JuceHeader.h>

#include "GirominController.h"
#include "QuatVisualizer.h"
#include "RangeKnob.h"

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/

class MainComponent
:
public juce::Component,
public juce::Timer
{
public:
    //==============================================================================
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    //==============================================================================
    // Barra de modo
    juce::TextButton oscModeBtn  { "OSC" };
    juce::TextButton midiModeBtn { "MIDI" };
    juce::ComboBox   midiInputSelector;

    // Sliders de acelerômetro (AX, AY, AZ)
    juce::Slider accelSliders[3];
    juce::Label  accelLabels[3];

    // Sliders de giroscópio (GX, GY, GZ)
    juce::Slider gyroSliders[3];
    juce::Label  gyroLabels[3];

    // Botões (B1, B2)
    juce::TextButton btnToggles[2];

    // ── Painel Note Output ───────────────────────────────────────────────────
    juce::Label    noteOutputLabel  { {}, "Note Output" };
    juce::Label    midiOutLabel     { {}, "Out" };
    juce::Label    noteChLabel      { {}, "Ch" };
    juce::Label    midiRateLabel_   { {}, "Hz" };
    juce::Label    noteB1Label      { {}, "B1" };
    juce::Label    noteB2Label      { {}, "B2" };
    juce::ComboBox midiOutputSelector;
    juce::ComboBox noteChannelBox;
    juce::Slider   midiRateSlider_;
    juce::ComboBox noteB1Box;
    juce::ComboBox noteB2Box;

    // ── CC Output panels (up to kMaxCCPanels) ────────────────────────────────
    static constexpr int kMaxCCPanels = 8;
    int numCCPanels_ = 3;

    juce::Label      ccOutLabels_[kMaxCCPanels];
    juce::TextButton ccOutEnableBtns_[kMaxCCPanels];
    juce::ComboBox   ccSourceBoxes_[kMaxCCPanels];
    juce::ComboBox   ccNumberBoxes_[kMaxCCPanels];
    juce::TextButton cc14bitBtns_[kMaxCCPanels];
    RangeKnob        ccRangeKnobs_[kMaxCCPanels];
    juce::Label      ccOutValueLabels_[kMaxCCPanels];
    juce::TextButton ccDeleteBtns_[kMaxCCPanels];

    void setupCCPanel (int i);
    void updateCC14bitButton (int i);
    void updateCCEnableButton (int i);
    void addCCPanel();
    void removeCCPanel (int i);
    int  computeContentHeight() const;

    // ── Euler angles display ─────────────────────────────────────────────────
    juce::ComboBox   eulerOrderBox_;
    juce::ComboBox   eulerSourceBox_;        // Raw / Remapped / With Yaw
    juce::Slider     eulerSliders_[3];       // [0]=first, [1]=last, [2]=mid(constrained)
    juce::Label      eulerLabels_[3];
    juce::TextButton eulerCenterResetBtns_[2]; // reset center for E1 and E2
    float            eulerCenter_[2] = { 0.f, 0.f };

    QuatVisualizer   quatViz_;
    juce::Slider     yawSlider_;
    juce::Label      yawLabel_    { {}, "Yaw offset" };
    juce::Label      quatLabel_;   // W X Y Z display (single line)
    juce::Slider     fpsSlider_;
    juce::Label      fpsLabel_  { {}, "FPS" };

    // ── Card bounds (set in resized, drawn in paint) ─────────────────────────
    juce::Rectangle<int> inputCard_, noteCard_;
    juce::Rectangle<int> b1Card_, b2Card_;
    juce::Rectangle<int> ccCards_[kMaxCCPanels + 1]; // [kMaxCCPanels] = "+" placeholder

    void mouseDown (const juce::MouseEvent&) override;

    GirominController::SensorDisplayData latestData_;

    void timerCallback() override;
    void setDisplayFPS (int fps);
    GirominController giromin_controller_;

    juce::ApplicationProperties appProperties_;

    void setupSlider (juce::Slider& s, juce::Label& l, const juce::String& name);
    void populateNoteBox (juce::ComboBox& box, int defaultNote);
    void updateModeButtons();
    void saveSettings();
    void loadSettings();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
