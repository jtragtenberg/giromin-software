#include "MainComponent.h"

static const char* accelNames[] = { "AX", "AY", "AZ" };
static const char* gyroNames[]  = { "GX", "GY", "GZ" };
static const char* btnNames[]   = { "B1", "B2" };

//==============================================================================
MainComponent::MainComponent()
{
    setSize (700, 480);

    // ── Botões de modo ───────────────────────────────────────────────────────
    oscModeBtn .setClickingTogglesState (false);
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
    int teensyItemId = -1;
    for (const auto& dev : giromin_controller_.getMidiInputDevices())
    {
        midiInputSelector.addItem (dev.name, itemId);
        if (dev.name.containsIgnoreCase ("Teensy MIDI Port 1"))
            teensyItemId = itemId;
        ++itemId;
    }
    midiInputSelector.setSelectedId (teensyItemId > 0 ? teensyItemId : 1);

    midiInputSelector.onChange = [this]()
    {
        auto devices = giromin_controller_.getMidiInputDevices();
        int selected = midiInputSelector.getSelectedId() - 2;
        if (selected >= 0 && selected < devices.size())
            giromin_controller_.openMidiInputDevice (devices[selected].identifier);
    };

    addAndMakeVisible (midiInputSelector);

    // ── Sliders de acelerômetro ───────────────────────────────────────────────
    for (int i = 0; i < 3; ++i)
        setupSlider (accelSliders[i], accelLabels[i], accelNames[i]);

    // ── Sliders de giroscópio ─────────────────────────────────────────────────
    for (int i = 0; i < 3; ++i)
        setupSlider (gyroSliders[i], gyroLabels[i], gyroNames[i]);

    // ── Botões toggle ────────────────────────────────────────────────────────
    for (int i = 0; i < 2; ++i)
    {
        btnToggles[i].setButtonText (btnNames[i]);
        btnToggles[i].setClickingTogglesState (false);
        btnToggles[i].setEnabled (false);
        addAndMakeVisible (btnToggles[i]);
    }

    // ── Callback de dados do sensor ──────────────────────────────────────────
    giromin_controller_.update_UI = [this](const GirominController::SensorDisplayData& d)
    {
        juce::MessageManager::callAsync ([this, d]()
        {
            accelSliders[0].setValue (d.ax, juce::dontSendNotification);
            accelSliders[1].setValue (d.ay, juce::dontSendNotification);
            accelSliders[2].setValue (d.az, juce::dontSendNotification);

            gyroSliders[0].setValue (d.gx, juce::dontSendNotification);
            gyroSliders[1].setValue (d.gy, juce::dontSendNotification);
            gyroSliders[2].setValue (d.gz, juce::dontSendNotification);

            auto btnColour = [](bool on) {
                return on ? juce::Colours::limegreen : juce::Colours::darkgrey;
            };
            btnToggles[0].setColour (juce::TextButton::buttonColourId, btnColour (d.b1 > 0.5f));
            btnToggles[1].setColour (juce::TextButton::buttonColourId, btnColour (d.b2 > 0.5f));
        });
    };

    updateModeButtons();
}

MainComponent::~MainComponent() {}

void MainComponent::setupSlider (juce::Slider& s, juce::Label& l, const juce::String& name)
{
    s.setRange (0.0, 1.0);
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, true, 55, 20);
    s.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (s);

    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (l);
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

    // Separador entre accel e gyro
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (40);  // topBar
    int rowH = 36;
    int sepY  = area.getY() + 3 * rowH + 8;
    g.setColour (juce::Colours::grey.withAlpha (0.4f));
    g.drawHorizontalLine (sepY, (float)area.getX(), (float)area.getRight());
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (10);

    // ── Barra de modo ────────────────────────────────────────────────────────
    auto topBar = area.removeFromTop (30);
    oscModeBtn .setBounds (topBar.removeFromLeft (60));
    topBar.removeFromLeft (6);
    midiModeBtn.setBounds (topBar.removeFromLeft (60));
    topBar.removeFromLeft (10);
    midiInputSelector.setBounds (topBar.removeFromLeft (220));

    area.removeFromTop (10);

    const int labelW = 30;
    const int rowH   = 36;
    const int gap    = 4;

    // ── Acelerômetro ─────────────────────────────────────────────────────────
    for (int i = 0; i < 3; ++i)
    {
        auto row = area.removeFromTop (rowH);
        area.removeFromTop (gap);
        accelLabels[i].setBounds (row.removeFromLeft (labelW));
        accelSliders[i].setBounds (row);
    }

    area.removeFromTop (12);  // espaço entre grupos

    // ── Giroscópio ───────────────────────────────────────────────────────────
    for (int i = 0; i < 3; ++i)
    {
        auto row = area.removeFromTop (rowH);
        area.removeFromTop (gap);
        gyroLabels[i].setBounds (row.removeFromLeft (labelW));
        gyroSliders[i].setBounds (row);
    }

    area.removeFromTop (12);

    // ── Botões ───────────────────────────────────────────────────────────────
    auto btnRow = area.removeFromTop (40);
    btnToggles[0].setBounds (btnRow.removeFromLeft (80));
    btnRow.removeFromLeft (10);
    btnToggles[1].setBounds (btnRow.removeFromLeft (80));
}
