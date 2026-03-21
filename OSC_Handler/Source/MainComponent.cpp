#include "MainComponent.h"

static const char* accelNames[] = { "AX", "AY", "AZ" };
static const char* gyroNames[]  = { "GX", "GY", "GZ" };
static const char* btnNames[]   = { "B1", "B2" };

// Returns {a_first, a_last, a_mid_constrained} for the given Tait-Bryan order index.
// Order indices: 0=XYZ, 1=XZY, 2=YXZ, 3=YZX, 4=ZXY, 5=ZYX
static std::array<float, 3> eulerFromQuat (float w, float x, float y, float z, int orderIdx)
{
    // Rotation matrix elements (row, col)
    float xx = x*x, yy = y*y, zz = z*z;
    float r00 = 1.f - 2.f*(yy+zz),  r01 = 2.f*(x*y - w*z),  r02 = 2.f*(x*z + w*y);
    float r10 = 2.f*(x*y + w*z),     r11 = 1.f - 2.f*(xx+zz), r12 = 2.f*(y*z - w*x);
    float r20 = 2.f*(x*z - w*y),     r21 = 2.f*(y*z + w*x),   r22 = 1.f - 2.f*(xx+yy);

    float a_first, a_last, a_mid;
    switch (orderIdx)
    {
        case 0: // XYZ
            a_mid   = std::asin  (juce::jlimit (-1.f, 1.f,  r02));
            a_first = std::atan2 (-r12, r22);
            a_last  = std::atan2 (-r01, r00);
            break;
        case 1: // XZY
            a_mid   = std::asin  (juce::jlimit (-1.f, 1.f, -r01));
            a_first = std::atan2 ( r21, r11);
            a_last  = std::atan2 ( r02, r00);
            break;
        case 2: // YXZ
            a_mid   = std::asin  (juce::jlimit (-1.f, 1.f, -r12));
            a_first = std::atan2 ( r02, r22);
            a_last  = std::atan2 ( r10, r11);
            break;
        case 3: // YZX
            a_mid   = std::asin  (juce::jlimit (-1.f, 1.f,  r10));
            a_first = std::atan2 (-r20, r00);
            a_last  = std::atan2 (-r12, r11);
            break;
        case 4: // ZXY
            a_mid   = std::asin  (juce::jlimit (-1.f, 1.f,  r21));
            a_first = std::atan2 (-r01, r11);
            a_last  = std::atan2 (-r20, r22);
            break;
        default: // ZYX (5)
            a_mid   = std::asin  (juce::jlimit (-1.f, 1.f, -r20));
            a_first = std::atan2 ( r10, r00);
            a_last  = std::atan2 ( r21, r22);
            break;
    }
    return { a_first, a_last, a_mid };
}

//==============================================================================
MainComponent::MainComponent()
{
    setSize (700, 960);

    // ── Persistência de configurações ────────────────────────────────────────
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "GirominToolkit";
    opts.filenameSuffix      = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    appProperties_.setStorageParameters (opts);

    // ── Botões de modo ───────────────────────────────────────────────────────
    oscModeBtn .setClickingTogglesState (false);
    midiModeBtn.setClickingTogglesState (false);

    oscModeBtn.onClick = [this]()
    {
        giromin_controller_.setInputMode (GirominController::InputMode::OSC);
        updateModeButtons();
        saveSettings();
    };

    midiModeBtn.onClick = [this]()
    {
        giromin_controller_.setInputMode (GirominController::InputMode::MIDI);
        updateModeButtons();
        saveSettings();
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
        saveSettings();
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
            saveSettings();
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
        saveSettings();
    };
    addAndMakeVisible (noteChannelBox);

    // Note selectors B1 / B2
    populateNoteBox (noteB1Box, giromin_controller_.getNoteForButton (0));
    noteB1Box.onChange = [this]()
    {
        giromin_controller_.setNoteForButton (0, noteB1Box.getSelectedId() - 1);
        saveSettings();
    };
    addAndMakeVisible (noteB1Box);

    populateNoteBox (noteB2Box, giromin_controller_.getNoteForButton (1));
    noteB2Box.onChange = [this]()
    {
        giromin_controller_.setNoteForButton (1, noteB2Box.getSelectedId() - 1);
        saveSettings();
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
        saveSettings();
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
            saveSettings();
        };
        addAndMakeVisible (ccSourceBox);
    }

    // CC number selector — all 14-bit CCs 0-31 (item ID = msb + 1)
    {
        static const char* ccNames[32] = {
            "0  - Bank Select",         "1  - Mod Wheel",
            "2  - Breath Controller",   "3  - Undefined",
            "4  - Foot Pedal",          "5  - Portamento Time",
            "6  - Data Entry",          "7  - Volume",
            "8  - Balance",             "9  - Undefined",
            "10 - Pan",                 "11 - Expression",
            "12 - Effect Controller 1", "13 - Effect Controller 2",
            "14 - Undefined",           "15 - Undefined",
            "16 - General Purpose 1",   "17 - General Purpose 2",
            "18 - General Purpose 3",   "19 - General Purpose 4",
            "20 - Undefined",           "21 - Undefined",
            "22 - Undefined",           "23 - Undefined",
            "24 - Undefined",           "25 - Undefined",
            "26 - Undefined",           "27 - Undefined",
            "28 - Undefined",           "29 - Undefined",
            "30 - Undefined",           "31 - Undefined",
        };
        for (int msb = 0; msb < 32; ++msb)
            ccNumberBox.addItem (ccNames[msb], msb + 1);  // ID = msb + 1

        ccNumberBox.setSelectedId (giromin_controller_.getCCOutMSB() + 1);
        ccNumberBox.onChange = [this]()
        {
            giromin_controller_.setCCOutMSB (ccNumberBox.getSelectedId() - 1);
            saveSettings();
        };
        addAndMakeVisible (ccNumberBox);
    }

    // Rate slider
    ccRateLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (ccRateLabel);

    ccRateSlider.setRange (1, 200, 1);
    ccRateSlider.setValue (giromin_controller_.getCCOutRateHz(), juce::dontSendNotification);
    ccRateSlider.setDoubleClickReturnValue (true, 10.0);
    ccRateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    ccRateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 45, 20);
    ccRateSlider.onValueChange = [this]()
    {
        giromin_controller_.setCCOutRateHz ((int)ccRateSlider.getValue());
        saveSettings();
    };
    addAndMakeVisible (ccRateSlider);

    // 14-bit / 7-bit toggle
    cc14bitBtn.setClickingTogglesState (true);
    cc14bitBtn.setToggleState (giromin_controller_.getCCOut14bit(), juce::dontSendNotification);
    cc14bitBtn.onClick = [this]()
    {
        giromin_controller_.setCCOut14bit (cc14bitBtn.getToggleState());
        updateCC14bitButton();
        saveSettings();
    };
    addAndMakeVisible (cc14bitBtn);
    updateCC14bitButton();

    // ── Callback de dados do sensor — apenas cacheia, timer faz o display ────
    giromin_controller_.update_UI = [this](const GirominController::SensorDisplayData& d)
    {
        juce::MessageManager::callAsync ([this, d]() { latestData_ = d; });
    };

    addAndMakeVisible (quatViz_);

    // ── Euler angles ─────────────────────────────────────────────────────────
    eulerOrderBox_.addItem ("XYZ", 1);
    eulerOrderBox_.addItem ("XZY", 2);
    eulerOrderBox_.addItem ("YXZ", 3);
    eulerOrderBox_.addItem ("YZX", 4);
    eulerOrderBox_.addItem ("ZXY", 5);
    eulerOrderBox_.addItem ("ZYX", 6);
    eulerOrderBox_.setSelectedId (1);
    addAndMakeVisible (eulerOrderBox_);

    eulerSourceBox_.addItem ("Raw",         1);   // d.qw, d.qx, d.qy, d.qz
    eulerSourceBox_.addItem ("Remapped",    2);   // (qw, qx, qz, -qy)
    eulerSourceBox_.addItem ("With Yaw",    3);   // remapped + yaw offset
    eulerSourceBox_.setSelectedId (1);
    addAndMakeVisible (eulerSourceBox_);

    for (int i = 0; i < 3; ++i)
    {
        eulerSliders_[i].setRange (-juce::MathConstants<double>::pi,
                                    juce::MathConstants<double>::pi);
        eulerSliders_[i].setSliderStyle (juce::Slider::LinearVertical);
        eulerSliders_[i].setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        eulerSliders_[i].setInterceptsMouseClicks (false, false);
        addAndMakeVisible (eulerSliders_[i]);
        eulerLabels_[i].setJustificationType (juce::Justification::centred);
        addAndMakeVisible (eulerLabels_[i]);
    }
    // Middle angle (index 2) constrained to [-pi/2, pi/2]
    eulerSliders_[2].setRange (-juce::MathConstants<double>::pi / 2.0,
                                juce::MathConstants<double>::pi / 2.0);

    // ── Yaw offset slider ────────────────────────────────────────────────────
    yawSlider_.setRange (-180.0, 180.0, 1.0);
    yawSlider_.setValue (0.0, juce::dontSendNotification);
    yawSlider_.setDoubleClickReturnValue (true, 0.0);
    yawSlider_.setSliderStyle (juce::Slider::LinearHorizontal);
    yawSlider_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 45, 20);
    yawSlider_.onValueChange = [this]()
    {
        quatViz_.setYawOffset ((float)yawSlider_.getValue());
    };
    addAndMakeVisible (yawSlider_);

    yawLabel_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (yawLabel_);

    // ── FPS slider ────────────────────────────────────────────────────────────
    fpsSlider_.setRange (10.0, 60.0, 1.0);
    fpsSlider_.setValue (30.0, juce::dontSendNotification);
    fpsSlider_.setDoubleClickReturnValue (true, 30.0);
    fpsSlider_.setSliderStyle (juce::Slider::LinearHorizontal);
    fpsSlider_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 35, 20);
    fpsSlider_.onValueChange = [this]() { setDisplayFPS ((int)fpsSlider_.getValue()); };
    addAndMakeVisible (fpsSlider_);

    fpsLabel_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (fpsLabel_);

    // ── Quaternion display label (single monospace line) ─────────────────────
    quatLabel_.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 13.f, juce::Font::plain));
    quatLabel_.setText ("Quat: +1.0  +0.0  +0.0  +0.0", juce::dontSendNotification);
    addAndMakeVisible (quatLabel_);

    updateModeButtons();
    loadSettings();
    setDisplayFPS (30);
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

void MainComponent::saveSettings()
{
    auto* p = appProperties_.getUserSettings();
    if (p == nullptr) return;

    p->setValue ("inputMode",    (int)giromin_controller_.getInputMode());

    auto inDevs = giromin_controller_.getMidiInputDevices();
    int inSel = midiInputSelector.getSelectedId() - 2;
    if (inSel >= 0 && inSel < inDevs.size())
        p->setValue ("midiInputDevice", inDevs[inSel].name);

    auto outDevs = giromin_controller_.getMidiOutputDevices();
    int outSel = midiOutputSelector.getSelectedId() - 2;
    if (outSel >= 0 && outSel < outDevs.size())
        p->setValue ("midiOutputDevice", outDevs[outSel].name);

    p->setValue ("noteChannel",  giromin_controller_.getNoteChannel());
    p->setValue ("noteB1",       giromin_controller_.getNoteForButton (0));
    p->setValue ("noteB2",       giromin_controller_.getNoteForButton (1));

    p->setValue ("ccOutEnabled", giromin_controller_.getCCOutEnabled());
    p->setValue ("ccOut14bit",   giromin_controller_.getCCOut14bit());
    p->setValue ("ccOutSource",  (int)giromin_controller_.getCCOutSource());
    p->setValue ("ccOutMSB",     giromin_controller_.getCCOutMSB());
    p->setValue ("ccOutRateHz",  giromin_controller_.getCCOutRateHz());

    p->saveIfNeeded();
}

void MainComponent::loadSettings()
{
    auto* p = appProperties_.getUserSettings();
    if (p == nullptr) return;

    // Input mode
    auto mode = (GirominController::InputMode) p->getIntValue ("inputMode", 1);
    giromin_controller_.setInputMode (mode);
    updateModeButtons();

    // MIDI input device
    {
        juce::String saved = p->getValue ("midiInputDevice", "Teensy MIDI Port 1");
        auto devs = giromin_controller_.getMidiInputDevices();
        for (int i = 0; i < devs.size(); ++i)
        {
            if (devs[i].name == saved)
            {
                midiInputSelector.setSelectedId (i + 2, juce::dontSendNotification);
                giromin_controller_.openMidiInputDevice (devs[i].identifier);
                break;
            }
        }
    }

    // MIDI output device
    {
        juce::String saved = p->getValue ("midiOutputDevice", "IAC Driver Bus 1");
        auto devs = giromin_controller_.getMidiOutputDevices();
        for (int i = 0; i < devs.size(); ++i)
        {
            if (devs[i].name == saved)
            {
                midiOutputSelector.setSelectedId (i + 2, juce::dontSendNotification);
                giromin_controller_.openMidiOutputDevice (devs[i].identifier);
                break;
            }
        }
    }

    // Note channel
    int ch = p->getIntValue ("noteChannel", 2);
    giromin_controller_.setNoteChannel (ch);
    noteChannelBox.setSelectedId (ch, juce::dontSendNotification);

    // Notes per button
    int n1 = p->getIntValue ("noteB1", 60);
    int n2 = p->getIntValue ("noteB2", 62);
    giromin_controller_.setNoteForButton (0, n1);
    giromin_controller_.setNoteForButton (1, n2);
    noteB1Box.setSelectedId (n1 + 1, juce::dontSendNotification);
    noteB2Box.setSelectedId (n2 + 1, juce::dontSendNotification);

    // CC out enabled
    bool ccEnabled = p->getBoolValue ("ccOutEnabled", false);
    giromin_controller_.setCCOutEnabled (ccEnabled);
    ccOutEnableBtn.setToggleState (ccEnabled, juce::dontSendNotification);
    updateCCEnableButton();

    // CC 14-bit
    bool cc14 = p->getBoolValue ("ccOut14bit", true);
    giromin_controller_.setCCOut14bit (cc14);
    cc14bitBtn.setToggleState (cc14, juce::dontSendNotification);
    updateCC14bitButton();

    // CC source (0=AX..5=GZ)
    int srcIdx = p->getIntValue ("ccOutSource", 3);
    GirominController::CCSource srcs[] = {
        GirominController::CCSource::AX, GirominController::CCSource::AY,
        GirominController::CCSource::AZ, GirominController::CCSource::GX,
        GirominController::CCSource::GY, GirominController::CCSource::GZ
    };
    if (srcIdx >= 0 && srcIdx < 6)
    {
        giromin_controller_.setCCOutSource (srcs[srcIdx]);
        ccSourceBox.setSelectedId (srcIdx + 1, juce::dontSendNotification);
    }

    // CC MSB (item ID = msb + 1)
    int msb = p->getIntValue ("ccOutMSB", 1);
    giromin_controller_.setCCOutMSB (msb);
    ccNumberBox.setSelectedId (msb + 1, juce::dontSendNotification);

    // CC rate
    int rateHz = p->getIntValue ("ccOutRateHz", 10);
    giromin_controller_.setCCOutRateHz (rateHz);
    ccRateSlider.setValue (rateHz, juce::dontSendNotification);
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
void MainComponent::setDisplayFPS (int fps)
{
    fps = juce::jlimit (10, 60, fps);
    quatViz_.setUpdateFPS (fps);
    startTimerHz (fps);
}

void MainComponent::timerCallback()
{
    const auto& d = latestData_;

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

    // Remap sensor frame (X=right, Y=up, Z=forward)
    //   → visualizer frame (X=right, Y=forward, Z=up)
    // Swap sensor Y↔Z; negate new Z to preserve right-handedness
    quatViz_.setQuaternion (d.qw, d.qx, d.qz, -d.qy);

    quatLabel_.setText (
        juce::String::formatted ("Quat:%+5.1f %+5.1f %+5.1f %+5.1f",
                                 d.qw, d.qx, d.qy, d.qz),
        juce::dontSendNotification);

    // Select quaternion source for Euler decomposition
    int oi = eulerOrderBox_.getSelectedId() - 1;
    float ew, ex, ey, ez;
    switch (eulerSourceBox_.getSelectedId())
    {
        case 2: // Remapped: sensor (X=right,Y=up,Z=fwd) → viz (X=right,Y=fwd,Z=up)
            ew =  d.qw; ex =  d.qx; ey =  d.qz; ez = -d.qy;
            break;
        case 3: // Remapped + yaw offset (rotation around viz Y = world Z/up)
        {
            float rw =  d.qw, rx =  d.qx, ry =  d.qz, rz = -d.qy;
            float yawRad = (float)(yawSlider_.getValue() * juce::MathConstants<double>::pi / 180.0);
            float cY = std::cos (yawRad * 0.5f), sY = std::sin (yawRad * 0.5f);
            ew = cY*rw - sY*ry;
            ex = cY*rx + sY*rz;
            ey = cY*ry + sY*rw;
            ez = cY*rz - sY*rx;
            break;
        }
        default: // Raw
            ew = d.qw; ex = d.qx; ey = d.qy; ez = d.qz;
            break;
    }
    auto euler = eulerFromQuat (ew, ex, ey, ez, oi);
    eulerSliders_[0].setValue (euler[0], juce::dontSendNotification);
    eulerSliders_[1].setValue (euler[1], juce::dontSendNotification);
    eulerSliders_[2].setValue (euler[2], juce::dontSendNotification);

    // Axis labels per order: {first, last, mid}
    // Axis labels per order: columns are {first, last, mid}
    static const char* orderLabels[6][3] = {
        { "X", "Z", "Y" },   // XYZ
        { "X", "Y", "Z" },   // XZY
        { "Y", "Z", "X" },   // YXZ
        { "Y", "X", "Z" },   // YZX
        { "Z", "Y", "X" },   // ZXY
        { "Z", "X", "Y" },   // ZYX
    };
    for (int i = 0; i < 3; ++i)
        eulerLabels_[i].setText (orderLabels[oi][i], juce::dontSendNotification);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (10);

    // ── INPUT section ────────────────────────────────────────────────────────

    // Mode bar (full width, inside input)
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

    // Height for 6 sliders + buttons + euler section (10 + 26 combo + 6 + 120 sliders)
    const int inputAreaH = 3 * (rowH + gap) + 10 + 3 * (rowH + gap) + 10 + 36 + 10 + 26 + 6 + 120;

    auto inputArea = area.removeFromTop (inputAreaH);

    // Left: quaternion visualizer + yaw slider below it
    auto vizArea = inputArea.removeFromLeft (280);
    inputArea.removeFromLeft (10);

    const int yawRowH  = 28;
    const int quatRowH = 20;
    const int fpsRowH  = 28;

    // Reserve space bottom-up: fps, 4 quat labels, yaw slider
    {
        auto fpsRow = vizArea.removeFromBottom (fpsRowH);
        fpsLabel_.setBounds (fpsRow.removeFromLeft (30));
        fpsSlider_.setBounds (fpsRow);
    }

    quatLabel_.setBounds (vizArea.removeFromBottom (quatRowH));

    auto yawRow = vizArea.removeFromBottom (yawRowH);
    yawLabel_.setBounds (yawRow.removeFromLeft (70));
    yawSlider_.setBounds (yawRow);

    quatViz_.setBounds (vizArea);

    // Right: sliders + buttons
    // ── Acelerômetro ─────────────────────────────────────────────────────────
    for (int i = 0; i < 3; ++i)
    {
        auto row = inputArea.removeFromTop (rowH);
        inputArea.removeFromTop (gap);
        accelLabels[i].setBounds (row.removeFromLeft (labelW));
        accelSliders[i].setBounds (row);
    }

    inputArea.removeFromTop (10);

    // ── Giroscópio ───────────────────────────────────────────────────────────
    for (int i = 0; i < 3; ++i)
    {
        auto row = inputArea.removeFromTop (rowH);
        inputArea.removeFromTop (gap);
        gyroLabels[i].setBounds (row.removeFromLeft (labelW));
        gyroSliders[i].setBounds (row);
    }

    inputArea.removeFromTop (10);

    // ── Botões ───────────────────────────────────────────────────────────────
    {
        auto row = inputArea.removeFromTop (36);
        btnToggles[0].setBounds (row.removeFromLeft (80));
        row.removeFromLeft (10);
        btnToggles[1].setBounds (row.removeFromLeft (80));
    }

    // ── Euler angles ─────────────────────────────────────────────────────────
    inputArea.removeFromTop (10);
    {
        auto row = inputArea.removeFromTop (26);
        int half = row.getWidth() / 2;
        eulerOrderBox_ .setBounds (row.removeFromLeft (half - 2));
        row.removeFromLeft (4);
        eulerSourceBox_.setBounds (row);
    }
    inputArea.removeFromTop (6);

    {
        auto eulerArea = inputArea.removeFromTop (120);
        int sliderW = eulerArea.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto col = (i < 2) ? eulerArea.removeFromLeft (sliderW)
                                : eulerArea;
            auto labelRow = col.removeFromBottom (18);
            eulerSliders_[i].setBounds (col);
            eulerLabels_[i].setBounds (labelRow);
        }
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
