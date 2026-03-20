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
    juce::Slider    rotaryKnob;

    juce::TextButton oscModeBtn  { "OSC" };
    juce::TextButton midiModeBtn { "MIDI" };
    juce::ComboBox   midiInputSelector;

    GirominController giromin_controller_;

    void updateModeButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
