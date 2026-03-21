#pragma once

#include <JuceHeader.h>

#include "GirominController.h"

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/

class MainComponent
:
public juce::Component
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

    // ── Painel CC Output ─────────────────────────────────────────────────────
    juce::Label      ccOutLabel     { {}, "CC Output" };
    juce::TextButton ccOutEnableBtn { "Enable" };
    juce::Label      ccSourceLabel  { {}, "Source" };
    juce::ComboBox   ccSourceBox;
    juce::Label      ccNumberLabel  { {}, "CC" };
    juce::ComboBox   ccNumberBox;
    juce::Label      ccRateLabel    { {}, "Rate (Hz)" };
    juce::Slider     ccRateSlider;
    juce::TextButton cc14bitBtn     { "14-bit" };

    void updateCC14bitButton();

    GirominController giromin_controller_;

    juce::ApplicationProperties appProperties_;

    void setupSlider (juce::Slider& s, juce::Label& l, const juce::String& name);
    void populateNoteBox (juce::ComboBox& box, int defaultNote);
    void updateModeButtons();
    void updateCCEnableButton();
    void saveSettings();
    void loadSettings();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
