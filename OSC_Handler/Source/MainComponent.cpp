#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    setSize (700, 400);

    // ── Rotary knob ──────────────────────────────────────────────────────────
    rotaryKnob.setRange (0, 1.0);
    rotaryKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    rotaryKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 150, 25);
    rotaryKnob.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (rotaryKnob);

    giromin_controller_.update_UI = [&](float data)
    {
        juce::MessageManager::callAsync ([this, data]()
        {
            rotaryKnob.setValue (static_cast<double>(data));
        });
    };

    // ── Botões de modo ───────────────────────────────────────────────────────
    oscModeBtn.setClickingTogglesState (false);
    midiModeBtn.setClickingTogglesState (false);

    oscModeBtn.onClick = [this]()
    {
        giromin_controller_.setInputMode (GirominController::InputMode::OSC);
        updateModeButtons();
    };

    midiModeBtn.onClick = [this]()
    {
        giromin_controller_.setInputMode (GirominController::InputMode::MIDI);
        updateModeButtons();
    };

    addAndMakeVisible (oscModeBtn);
    addAndMakeVisible (midiModeBtn);

    // ── Seletor de dispositivo MIDI input ────────────────────────────────────
    midiInputSelector.addItem ("(nenhum)", 1);
    int itemId = 2;
    for (const auto& dev : giromin_controller_.getMidiInputDevices())
    {
        midiInputSelector.addItem (dev.name, itemId);
        midiInputSelector.setItemEnabled (itemId, true);
        ++itemId;
    }
    midiInputSelector.setSelectedId (1);

    midiInputSelector.onChange = [this]()
    {
        auto devices = giromin_controller_.getMidiInputDevices();
        int selected = midiInputSelector.getSelectedId() - 2;  // offset de 2 (item 1 = nenhum)
        if (selected >= 0 && selected < devices.size())
            giromin_controller_.openMidiInputDevice (devices[selected].identifier);
    };

    addAndMakeVisible (midiInputSelector);

    updateModeButtons();
}

MainComponent::~MainComponent()
{
}

void MainComponent::updateModeButtons()
{
    bool isMidi = giromin_controller_.getInputMode() == GirominController::InputMode::MIDI;
    oscModeBtn .setColour (juce::TextButton::buttonColourId,
                           isMidi ? juce::Colours::darkgrey : juce::Colours::steelblue);
    midiModeBtn.setColour (juce::TextButton::buttonColourId,
                           isMidi ? juce::Colours::steelblue : juce::Colours::darkgrey);
    midiInputSelector.setEnabled (isMidi);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (10);

    // Botões de modo no topo
    auto topBar = area.removeFromTop (30);
    oscModeBtn .setBounds (topBar.removeFromLeft (80));
    topBar.removeFromLeft (6);
    midiModeBtn.setBounds (topBar.removeFromLeft (80));
    topBar.removeFromLeft (10);
    midiInputSelector.setBounds (topBar.removeFromLeft (220));

    // Knob ocupa o restante
    rotaryKnob.setBounds (area);
}
