#pragma once

#include <JuceHeader.h>

#include "GirominController.h"
#include "QuatVisualizer.h"

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
    juce::Label    midiOutLabel     { {}, "Device" };
    juce::Label    noteChLabel      { {}, "Ch" };
    juce::Label    noteB1Label      { {}, "B1" };
    juce::Label    noteB2Label      { {}, "B2" };
    juce::ComboBox midiOutputSelector;
    juce::ComboBox noteChannelBox;
    juce::ComboBox noteB1Box;
    juce::ComboBox noteB2Box;

    // ── 3 painéis CC Output ──────────────────────────────────────────────────
    juce::Label      ccOutLabels_[3];
    juce::TextButton ccOutEnableBtns_[3];
    juce::ComboBox   ccSourceBoxes_[3];
    juce::ComboBox   ccNumberBoxes_[3];
    juce::Slider     ccRateSliders_[3];
    juce::TextButton cc14bitBtns_[3];

    void setupCCPanel (int i);
    void updateCC14bitButton (int i);
    void updateCCEnableButton (int i);

    // ── Euler angles display ─────────────────────────────────────────────────
    juce::ComboBox eulerOrderBox_;
    juce::ComboBox eulerSourceBox_;    // Raw / Remapped / With Yaw
    juce::Slider   eulerSliders_[3];   // [0]=first, [1]=last, [2]=mid(constrained)
    juce::Label    eulerLabels_[3];

    QuatVisualizer   quatViz_;
    juce::Slider     yawSlider_;
    juce::Label      yawLabel_    { {}, "Yaw offset" };
    juce::Label      quatLabel_;   // W X Y Z display (single line)
    juce::Slider     fpsSlider_;
    juce::Label      fpsLabel_  { {}, "FPS" };

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
