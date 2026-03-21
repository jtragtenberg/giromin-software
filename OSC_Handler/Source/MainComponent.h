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

    GirominController giromin_controller_;

    void setupSlider (juce::Slider& s, juce::Label& l, const juce::String& name);
    void updateModeButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
