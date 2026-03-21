#include "MainComponent.h"

static const char* accelNames[] = { "AX", "AY", "AZ" };
static const char* gyroNames[]  = { "GX", "GY", "GZ" };
static const char* btnNames[]   = { "B1", "B2" };

//==============================================================================
MainComponent::MainComponent()
{
    setSize (700, 720);

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
    midiInputSelector.addItem ("(none)", 1);
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

    // ── Painel Note Output ───────────────────────────────────────────────────
    noteOutputLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.f).withStyle ("Bold")));
    addAndMakeVisible (noteOutputLabel);

    for (auto* l : { &midiOutLabel, &noteChLabel, &noteB1Label, &noteB2Label })
    {
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }

    // Output device selector
    {
        midiOutputSelector.addItem ("(none)", 1);
        int id = 2;
        int iacId = -1;
        for (const auto& dev : giromin_controller_.getMidiOutputDevices())
        {
            midiOutputSelector.addItem (dev.name, id);
            if (dev.name.containsIgnoreCase ("IAC Driver Bus 1"))
                iacId = id;
            ++id;
        }
        midiOutputSelector.setSelectedId (iacId > 0 ? iacId : 1);
        midiOutputSelector.onChange = [this]()
        {
            auto devices = giromin_controller_.getMidiOutputDevices();
            int sel = midiOutputSelector.getSelectedId() - 2;
            if (sel >= 0 && sel < devices.size())
                giromin_controller_.openMidiOutputDevice (devices[sel].identifier);
        };
        addAndMakeVisible (midiOutputSelector);
    }

    // Channel selector 1–16
    for (int ch = 1; ch <= 16; ++ch)
        noteChannelBox.addItem ("Ch " + juce::String(ch), ch);
    noteChannelBox.setSelectedId (giromin_controller_.getNoteChannel());
    noteChannelBox.onChange = [this]()
    {
        giromin_controller_.setNoteChannel (noteChannelBox.getSelectedId());
    };
    addAndMakeVisible (noteChannelBox);

    // Note selectors B1 / B2
    populateNoteBox (noteB1Box, giromin_controller_.getNoteForButton (0));
    noteB1Box.onChange = [this]()
    {
        giromin_controller_.setNoteForButton (0, noteB1Box.getSelectedId() - 1);
    };
    addAndMakeVisible (noteB1Box);

    populateNoteBox (noteB2Box, giromin_controller_.getNoteForButton (1));
    noteB2Box.onChange = [this]()
    {
        giromin_controller_.setNoteForButton (1, noteB2Box.getSelectedId() - 1);
    };
    addAndMakeVisible (noteB2Box);

    // ── Painel CC Output ─────────────────────────────────────────────────────
    ccOutLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.f).withStyle ("Bold")));
    addAndMakeVisible (ccOutLabel);

    for (auto* l : { &ccSourceLabel, &ccNumberLabel })
    {
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }

    // Enable toggle button
    ccOutEnableBtn.setClickingTogglesState (true);
    ccOutEnableBtn.onClick = [this]()
    {
        giromin_controller_.setCCOutEnabled (ccOutEnableBtn.getToggleState());
        updateCCEnableButton();
    };
    addAndMakeVisible (ccOutEnableBtn);
    updateCCEnableButton();

    // Source selector
    {
        struct { const char* name; GirominController::CCSource src; } sources[] = {
            { "Accel X",  GirominController::CCSource::AX },
            { "Accel Y",  GirominController::CCSource::AY },
            { "Accel Z",  GirominController::CCSource::AZ },
            { "Gyro X",   GirominController::CCSource::GX },
            { "Gyro Y",   GirominController::CCSource::GY },
            { "Gyro Z",   GirominController::CCSource::GZ },
        };
        int id = 1;
        for (auto& s : sources)
            ccSourceBox.addItem (s.name, id++);

        // Default: match cc_out_source_ (GX = index 4)
        ccSourceBox.setSelectedId (4);
        ccSourceBox.onChange = [this]()
        {
            GirominController::CCSource srcs[] = {
                GirominController::CCSource::AX, GirominController::CCSource::AY,
                GirominController::CCSource::AZ, GirominController::CCSource::GX,
                GirominController::CCSource::GY, GirominController::CCSource::GZ
            };
            int sel = ccSourceBox.getSelectedId() - 1;
            if (sel >= 0 && sel < 6)
                giromin_controller_.setCCOutSource (srcs[sel]);
        };
        addAndMakeVisible (ccSourceBox);
    }

    // CC number selector — standard 14-bit CCs (MSB numbers only)
    {
        struct { const char* name; int msb; } cc14s[] = {
            { "0 - Bank Select",       0  },
            { "1 - Mod Wheel",         1  },
            { "2 - Breath",            2  },
            { "4 - Foot",              4  },
            { "5 - Portamento Time",   5  },
            { "6 - Data Entry",        6  },
            { "7 - Channel Volume",    7  },
            { "8 - Balance",           8  },
            { "10 - Pan",              10 },
            { "11 - Expression",       11 },
            { "12 - Effect Ctrl 1",    12 },
            { "13 - Effect Ctrl 2",    13 },
            { "16 - Gen Purpose 1",    16 },
            { "17 - Gen Purpose 2",    17 },
            { "18 - Gen Purpose 3",    18 },
            { "19 - Gen Purpose 4",    19 },
        };
        int defaultId = 1;
        int id = 1;
        for (auto& c : cc14s)
        {
            ccNumberBox.addItem (c.name, id);
            if (c.msb == giromin_controller_.getCCOutMSB())
                defaultId = id;
            ++id;
        }
        ccNumberBox.setSelectedId (defaultId);

        // Store MSB values for lookup in onChange
        ccNumberBox.onChange = [this]()
        {
            int msbs[] = { 0,1,2,4,5,6,7,8,10,11,12,13,16,17,18,19 };
            int sel = ccNumberBox.getSelectedId() - 1;
            if (sel >= 0 && sel < 16)
                giromin_controller_.setCCOutMSB (msbs[sel]);
        };
        addAndMakeVisible (ccNumberBox);
    }

    // Rate slider
    ccRateLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (ccRateLabel);

    ccRateSlider.setRange (1, 200, 1);
    ccRateSlider.setValue (giromin_controller_.getCCOutRateHz(), juce::dontSendNotification);
    ccRateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    ccRateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 45, 20);
    ccRateSlider.onValueChange = [this]()
    {
        giromin_controller_.setCCOutRateHz ((int)ccRateSlider.getValue());
    };
    addAndMakeVisible (ccRateSlider);

    // 14-bit / 7-bit toggle
    cc14bitBtn.setClickingTogglesState (true);
    cc14bitBtn.setToggleState (giromin_controller_.getCCOut14bit(), juce::dontSendNotification);
    cc14bitBtn.onClick = [this]()
    {
        giromin_controller_.setCCOut14bit (cc14bitBtn.getToggleState());
        updateCC14bitButton();
    };
    addAndMakeVisible (cc14bitBtn);
    updateCC14bitButton();

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

void MainComponent::populateNoteBox (juce::ComboBox& box, int defaultNote)
{
    for (int n = 0; n < 128; ++n)
    {
        // ID = note + 1 (IDs devem ser >= 1)
        box.addItem (juce::MidiMessage::getMidiNoteName (n, true, true, 4), n + 1);
    }
    box.setSelectedId (defaultNote + 1);
}

void MainComponent::updateCC14bitButton()
{
    bool is14 = cc14bitBtn.getToggleState();
    cc14bitBtn.setButtonText (is14 ? "14-bit" : "7-bit");
    cc14bitBtn.setColour (juce::TextButton::buttonColourId,
                          is14 ? juce::Colours::steelblue : juce::Colours::slategrey);
}

void MainComponent::updateCCEnableButton()
{
    bool on = ccOutEnableBtn.getToggleState();
    ccOutEnableBtn.setButtonText (on ? "Enabled" : "Enable");
    ccOutEnableBtn.setColour (juce::TextButton::buttonColourId,
                              on ? juce::Colours::limegreen : juce::Colours::darkgrey);
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

    // ── Barra de modo ────────────────────────────────────────────────────────
    auto topBar = area.removeFromTop (30);
    oscModeBtn .setBounds (topBar.removeFromLeft (60));
    topBar.removeFromLeft (6);
    midiModeBtn.setBounds (topBar.removeFromLeft (60));
    topBar.removeFromLeft (10);
    midiInputSelector.setBounds (topBar.removeFromLeft (220));

    area.removeFromTop (10);

    const int labelW = 30;
    const int rowH   = 34;
    const int gap    = 4;

    // ── Acelerômetro ─────────────────────────────────────────────────────────
    for (int i = 0; i < 3; ++i)
    {
        auto row = area.removeFromTop (rowH);
        area.removeFromTop (gap);
        accelLabels[i].setBounds (row.removeFromLeft (labelW));
        accelSliders[i].setBounds (row);
    }

    area.removeFromTop (10);

    // ── Giroscópio ───────────────────────────────────────────────────────────
    for (int i = 0; i < 3; ++i)
    {
        auto row = area.removeFromTop (rowH);
        area.removeFromTop (gap);
        gyroLabels[i].setBounds (row.removeFromLeft (labelW));
        gyroSliders[i].setBounds (row);
    }

    area.removeFromTop (10);

    // ── Botões ───────────────────────────────────────────────────────────────
    {
        auto row = area.removeFromTop (36);
        btnToggles[0].setBounds (row.removeFromLeft (80));
        row.removeFromLeft (10);
        btnToggles[1].setBounds (row.removeFromLeft (80));
    }

    area.removeFromTop (14);

    // ── Painel Note Output ───────────────────────────────────────────────────
    noteOutputLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);

    const int colLabelW = 46;
    const int colW      = 180;
    const int smallW    = 70;

    // Linha 1: Device
    {
        auto row = area.removeFromTop (26);
        area.removeFromTop (gap);
        midiOutLabel.setBounds (row.removeFromLeft (colLabelW));
        row.removeFromLeft (4);
        midiOutputSelector.setBounds (row.removeFromLeft (colW));
    }

    // Linha 2: Channel
    {
        auto row = area.removeFromTop (26);
        area.removeFromTop (gap);
        noteChLabel.setBounds (row.removeFromLeft (colLabelW));
        row.removeFromLeft (4);
        noteChannelBox.setBounds (row.removeFromLeft (smallW));
    }

    // Linha 3: B1 note / B2 note
    {
        auto row = area.removeFromTop (26);
        noteB1Label.setBounds (row.removeFromLeft (colLabelW));
        row.removeFromLeft (4);
        noteB1Box.setBounds (row.removeFromLeft (smallW));
        row.removeFromLeft (16);
        noteB2Label.setBounds (row.removeFromLeft (colLabelW));
        row.removeFromLeft (4);
        noteB2Box.setBounds (row.removeFromLeft (smallW));
    }

    area.removeFromTop (14);

    // ── Painel CC Output ─────────────────────────────────────────────────────
    {
        auto headerRow = area.removeFromTop (20);
        ccOutLabel.setBounds (headerRow.removeFromLeft (100));
        headerRow.removeFromLeft (8);
        ccOutEnableBtn.setBounds (headerRow.removeFromLeft (70));
        area.removeFromTop (6);

        // Source
        {
            auto row = area.removeFromTop (26);
            area.removeFromTop (gap);
            ccSourceLabel.setBounds (row.removeFromLeft (colLabelW));
            row.removeFromLeft (4);
            ccSourceBox.setBounds (row.removeFromLeft (colW));
        }

        // CC number
        {
            auto row = area.removeFromTop (26);
            area.removeFromTop (gap);
            ccNumberLabel.setBounds (row.removeFromLeft (colLabelW));
            row.removeFromLeft (4);
            ccNumberBox.setBounds (row.removeFromLeft (colW));
        }

        // Rate + 14/7-bit toggle
        {
            auto row = area.removeFromTop (26);
            ccRateLabel.setBounds (row.removeFromLeft (colLabelW));
            row.removeFromLeft (4);
            ccRateSlider.setBounds (row.removeFromLeft (colW));
            row.removeFromLeft (8);
            cc14bitBtn.setBounds (row.removeFromLeft (60));
        }
    }
}
