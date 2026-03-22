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
    setSize (700, 800);    // placeholder — corrected after loadSettings()

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

    for (auto* l : { &midiOutLabel, &noteChLabel, &midiRateLabel_, &noteB1Label, &noteB2Label })
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

    // Global MIDI output rate (applies to all CC channels)
    midiRateSlider_.setRange (1, 200, 1);
    midiRateSlider_.setValue (10, juce::dontSendNotification);
    midiRateSlider_.setDoubleClickReturnValue (true, 10.0);
    midiRateSlider_.setSliderStyle (juce::Slider::LinearHorizontal);
    midiRateSlider_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 36, 20);
    midiRateSlider_.onValueChange = [this]()
    {
        int hz = (int) midiRateSlider_.getValue();
        for (int i = 0; i < 3; ++i)
            giromin_controller_.setCCOutRateHz (i, hz);
        saveSettings();
    };
    addAndMakeVisible (midiRateSlider_);

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

    // ── CC Output panels (initial count = numCCPanels_) ──────────────────────
    for (int i = 0; i < numCCPanels_; ++i)
        setupCCPanel (i);

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

    // ── Euler center-reset buttons (E1 and E2 only) ───────────────────────────
    for (int i = 0; i < 2; ++i)
    {
        eulerCenterResetBtns_[i].setButtonText ("ctr");
        eulerCenterResetBtns_[i].setTooltip ("Set current angle as center (avoids the +-pi discontinuity)");
        eulerCenterResetBtns_[i].onClick = [this, i]()
        {
            eulerCenter_[i] = (float) eulerSliders_[i].getValue();
            saveSettings();
        };
        addAndMakeVisible (eulerCenterResetBtns_[i]);
    }

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
    setSize (700, computeContentHeight());
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

void MainComponent::updateCC14bitButton (int i)
{
    bool is14 = cc14bitBtns_[i].getToggleState();
    cc14bitBtns_[i].setButtonText (is14 ? "14b" : "7b");
    cc14bitBtns_[i].setColour (juce::TextButton::buttonColourId,
                                is14 ? juce::Colours::steelblue : juce::Colours::slategrey);
}

void MainComponent::updateCCEnableButton (int i)
{
    bool on = ccOutEnableBtns_[i].getToggleState();
    ccOutEnableBtns_[i].setButtonText (on ? "ON" : "OFF");
    ccOutEnableBtns_[i].setColour (juce::TextButton::buttonColourId,
                                    on ? juce::Colours::limegreen : juce::Colours::darkgrey);
}

void MainComponent::setupCCPanel (int i)
{
    static const char* ccNames[32] = {
        "0  Bank Sel",   "1  Mod Wheel",  "2  Breath",     "3  Undef",
        "4  Foot",       "5  Portamento", "6  Data Entry", "7  Volume",
        "8  Balance",    "9  Undef",      "10 Pan",        "11 Expr",
        "12 FX Ctrl 1",  "13 FX Ctrl 2",  "14 Undef",      "15 Undef",
        "16 Gen Pur 1",  "17 Gen Pur 2",  "18 Gen Pur 3",  "19 Gen Pur 4",
        "20 Undef",      "21 Undef",      "22 Undef",      "23 Undef",
        "24 Undef",      "25 Undef",      "26 Undef",      "27 Undef",
        "28 Undef",      "29 Undef",      "30 Undef",      "31 Undef",
    };

    ccOutLabels_[i].setText ("CC " + juce::String (i + 1), juce::dontSendNotification);
    ccOutLabels_[i].setFont (juce::Font (juce::FontOptions().withHeight (13.f).withStyle ("Bold")));
    addAndMakeVisible (ccOutLabels_[i]);

    ccOutEnableBtns_[i].setClickingTogglesState (true);
    ccOutEnableBtns_[i].onClick = [this, i]()
    {
        giromin_controller_.setCCOutEnabled (i, ccOutEnableBtns_[i].getToggleState());
        updateCCEnableButton (i);
        saveSettings();
    };
    addAndMakeVisible (ccOutEnableBtns_[i]);
    updateCCEnableButton (i);

    // Source: AX/AY/AZ/GX/GY/GZ/E1/E2/E3
    ccSourceBoxes_[i].addItem ("Accel X",   1);
    ccSourceBoxes_[i].addItem ("Accel Y",   2);
    ccSourceBoxes_[i].addItem ("Accel Z",   3);
    ccSourceBoxes_[i].addItem ("Gyro X",    4);
    ccSourceBoxes_[i].addItem ("Gyro Y",    5);
    ccSourceBoxes_[i].addItem ("Gyro Z",    6);
    ccSourceBoxes_[i].addItem ("Euler 1",   7);
    ccSourceBoxes_[i].addItem ("Euler 2",   8);
    ccSourceBoxes_[i].addItem ("Euler 3",   9);
    ccSourceBoxes_[i].setSelectedId ((int)giromin_controller_.getCCOutSource (i) + 1);
    static const char* srcShortNames[] = { "AX","AY","AZ","GX","GY","GZ","E1","E2","E3" };
    auto updateKnobLabel = [this, i]()
    {
        int sel = ccSourceBoxes_[i].getSelectedId() - 1;
        if (sel >= 0 && sel < 9)
            ccRangeKnobs_[i].setCentreLabel (srcShortNames[sel]);
    };

    ccSourceBoxes_[i].onChange = [this, i, updateKnobLabel]()
    {
        using S = GirominController::CCSource;
        static const S srcs[] = { S::AX, S::AY, S::AZ, S::GX, S::GY, S::GZ,
                                   S::EULER1, S::EULER2, S::EULER3 };
        int sel = ccSourceBoxes_[i].getSelectedId() - 1;
        if (sel >= 0 && sel < 9)
            giromin_controller_.setCCOutSource (i, srcs[sel]);
        updateKnobLabel();
        saveSettings();
    };
    updateKnobLabel();
    addAndMakeVisible (ccSourceBoxes_[i]);

    for (int msb = 0; msb < 32; ++msb)
        ccNumberBoxes_[i].addItem (ccNames[msb], msb + 1);
    ccNumberBoxes_[i].setSelectedId (giromin_controller_.getCCOutMSB (i) + 1);
    ccNumberBoxes_[i].onChange = [this, i]()
    {
        giromin_controller_.setCCOutMSB (i, ccNumberBoxes_[i].getSelectedId() - 1);
        saveSettings();
    };
    addAndMakeVisible (ccNumberBoxes_[i]);

    cc14bitBtns_[i].setClickingTogglesState (true);
    cc14bitBtns_[i].setToggleState (giromin_controller_.getCCOut14bit (i), juce::dontSendNotification);
    cc14bitBtns_[i].onClick = [this, i]()
    {
        giromin_controller_.setCCOut14bit (i, cc14bitBtns_[i].getToggleState());
        updateCC14bitButton (i);
        saveSettings();
    };
    addAndMakeVisible (cc14bitBtns_[i]);
    updateCC14bitButton (i);

    // ── Range knob ────────────────────────────────────────────────────────
    ccRangeKnobs_[i].setNormalizedRange (giromin_controller_.getCCOutRangeMin (i),
                                         giromin_controller_.getCCOutRangeMax (i));
    ccRangeKnobs_[i].onRangeChanged = [this, i](float lo, float hi)
    {
        giromin_controller_.setCCOutRange (i, lo, hi);
        saveSettings();
    };
    addAndMakeVisible (ccRangeKnobs_[i]);

    ccOutValueLabels_[i].setJustificationType (juce::Justification::centred);
    ccOutValueLabels_[i].setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 13.f, juce::Font::plain));
    ccOutValueLabels_[i].setText ("---", juce::dontSendNotification);
    addAndMakeVisible (ccOutValueLabels_[i]);
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

    p->setValue ("numCCPanels", numCCPanels_);

    for (int i = 0; i < numCCPanels_; ++i)
    {
        juce::String k = "cc" + juce::String (i);
        p->setValue (k + "Enabled", giromin_controller_.getCCOutEnabled (i));
        p->setValue (k + "14bit",   giromin_controller_.getCCOut14bit   (i));
        p->setValue (k + "Source",  (int)giromin_controller_.getCCOutSource (i));
        p->setValue (k + "MSB",     giromin_controller_.getCCOutMSB     (i));
        p->setValue (k + "RangeMin", giromin_controller_.getCCOutRangeMin (i));
        p->setValue (k + "RangeMax", giromin_controller_.getCCOutRangeMax (i));
    }

    p->setValue ("midiRateHz",   (int) midiRateSlider_.getValue());
    p->setValue ("eulerOrder",   eulerOrderBox_.getSelectedId());
    p->setValue ("eulerSource",  eulerSourceBox_.getSelectedId());
    p->setValue ("eulerCenter0", eulerCenter_[0]);
    p->setValue ("eulerCenter1", eulerCenter_[1]);

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

    // Restore CC panel count and setup any extra panels
    {
        int saved = juce::jlimit (3, kMaxCCPanels, p->getIntValue ("numCCPanels", 3));
        for (int i = numCCPanels_; i < saved; ++i)
            setupCCPanel (i);
        numCCPanels_ = saved;
    }

    // CC outputs
    for (int i = 0; i < numCCPanels_; ++i)
    {
        juce::String k = "cc" + juce::String (i);

        bool en = p->getBoolValue (k + "Enabled", false);
        giromin_controller_.setCCOutEnabled (i, en);
        ccOutEnableBtns_[i].setToggleState (en, juce::dontSendNotification);
        updateCCEnableButton (i);

        bool b14 = p->getBoolValue (k + "14bit", true);
        giromin_controller_.setCCOut14bit (i, b14);
        cc14bitBtns_[i].setToggleState (b14, juce::dontSendNotification);
        updateCC14bitButton (i);

        int srcIdx = p->getIntValue (k + "Source", i == 0 ? 4 : (i == 1 ? 5 : 6));
        if (srcIdx >= 0 && srcIdx < 9)
        {
            using S = GirominController::CCSource;
            static const S srcs[] = { S::AX, S::AY, S::AZ, S::GX, S::GY, S::GZ,
                                       S::EULER1, S::EULER2, S::EULER3 };
            giromin_controller_.setCCOutSource (i, srcs[srcIdx]);
            ccSourceBoxes_[i].setSelectedId (srcIdx + 1, juce::dontSendNotification);
        }

        int msb = p->getIntValue (k + "MSB", i + 1);
        giromin_controller_.setCCOutMSB (i, msb);
        ccNumberBoxes_[i].setSelectedId (msb + 1, juce::dontSendNotification);

        float rMin = (float) p->getDoubleValue (k + "RangeMin", 0.0);
        float rMax = (float) p->getDoubleValue (k + "RangeMax", 1.0);
        giromin_controller_.setCCOutRange (i, rMin, rMax);
        ccRangeKnobs_[i].setNormalizedRange (rMin, rMax);
    }

    {
        int hz = p->getIntValue ("midiRateHz", 10);
        midiRateSlider_.setValue (hz, juce::dontSendNotification);
        for (int i = 0; i < 3; ++i)
            giromin_controller_.setCCOutRateHz (i, hz);
    }

    eulerOrderBox_ .setSelectedId (p->getIntValue ("eulerOrder",  1), juce::dontSendNotification);
    eulerSourceBox_.setSelectedId (p->getIntValue ("eulerSource", 1), juce::dontSendNotification);
    eulerCenter_[0] = (float) p->getDoubleValue ("eulerCenter0", 0.0);
    eulerCenter_[1] = (float) p->getDoubleValue ("eulerCenter1", 0.0);
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

    // Apply center offset for E1 and E2: subtract offset, then wrap to [-π, π]
    // This prevents the active range from being split by the ±π discontinuity.
    for (int i = 0; i < 2; ++i)
        euler[i] = std::atan2 (std::sin (euler[i] - eulerCenter_[i]),
                               std::cos (euler[i] - eulerCenter_[i]));

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

    giromin_controller_.processCCOutputs (d, euler[0], euler[1], euler[2]);

    // Update knob values and output labels
    const float pi = juce::MathConstants<float>::pi;
    for (int i = 0; i < numCCPanels_; ++i)
    {
        // Raw input value [0,1] for this CC channel's source
        float srcVal = 0.5f;
        switch (giromin_controller_.getCCOutSource (i))
        {
            case GirominController::CCSource::AX:     srcVal = d.ax; break;
            case GirominController::CCSource::AY:     srcVal = d.ay; break;
            case GirominController::CCSource::AZ:     srcVal = d.az; break;
            case GirominController::CCSource::GX:     srcVal = d.gx; break;
            case GirominController::CCSource::GY:     srcVal = d.gy; break;
            case GirominController::CCSource::GZ:     srcVal = d.gz; break;
            case GirominController::CCSource::EULER1: srcVal = juce::jlimit (0.f, 1.f, (euler[0] + pi)       / (2.f * pi)); break;
            case GirominController::CCSource::EULER2: srcVal = juce::jlimit (0.f, 1.f, (euler[1] + pi)       / (2.f * pi)); break;
            case GirominController::CCSource::EULER3: srcVal = juce::jlimit (0.f, 1.f, (euler[2] + pi * 0.5f) / pi);        break;
        }

        // Mapped output value [0,1] — handles inverted ranges
        float rMin = giromin_controller_.getCCOutRangeMin (i);
        float rMax = giromin_controller_.getCCOutRangeMax (i);
        float span = rMax - rMin;
        float outVal = (std::abs (span) > 0.001f)
                        ? juce::jlimit (0.f, 1.f, (srcVal - rMin) / span)
                        : 0.f;

        ccRangeKnobs_[i].setInputValue  (srcVal);
        ccRangeKnobs_[i].setOutputValue (outVal);

        // Output value label
        int v14 = giromin_controller_.getCCOutLastVal14 (i);
        if (v14 < 0)
            ccOutValueLabels_[i].setText ("---", juce::dontSendNotification);
        else if (giromin_controller_.getCCOut14bit (i))
            ccOutValueLabels_[i].setText (juce::String (v14), juce::dontSendNotification);
        else
            ccOutValueLabels_[i].setText (juce::String (v14 >> 7), juce::dontSendNotification);
    }
}

void MainComponent::addCCPanel()
{
    if (numCCPanels_ >= kMaxCCPanels) return;
    setupCCPanel (numCCPanels_);
    ++numCCPanels_;
    setSize (getWidth(), computeContentHeight());
    resized();
    repaint();
    saveSettings();
}

int MainComponent::computeContentHeight() const
{
    const int P = 10, CG = 8, rowH = 34, gap = 4;
    const int inputContentH = 30 + 10 + 3*(rowH+gap) + 10 + 3*(rowH+gap) + 10 + 36 + 10 + 26 + 6 + 138;
    const int noteContentH  = 20 + 6 + 26;
    const int btnCardH      = 26;
    const int ccContentH    = 24 + 28 + 28 + 8 + 90 + 2 + 18;

    int numSlots = (numCCPanels_ < kMaxCCPanels) ? numCCPanels_ + 1 : numCCPanels_;
    int ccRows   = (numSlots > 4) ? 2 : 1;

    int h = 16;   // reduced(8) border on each side
    h += (inputContentH + P*2) + CG;
    h += (noteContentH  + P*2) + CG;
    h += (btnCardH      + P*2) + CG;
    h += ccRows * (ccContentH + P*2) + (ccRows > 1 ? CG : 0);
    h += 20;      // bottom padding
    return h;
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    if (numCCPanels_ < kMaxCCPanels
        && ccCards_[kMaxCCPanels].contains (e.getPosition()))
        addCCPanel();
}

//==============================================================================
static void drawCard (juce::Graphics& g, juce::Rectangle<int> r, const char* title)
{
    g.setColour (juce::Colour (0xff222222));
    g.fillRoundedRectangle (r.toFloat(), 8.f);
    g.setColour (juce::Colour (0xff3a3a3a));
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.f, 1.f);

    if (title && title[0])
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::Font (juce::FontOptions().withHeight (11.f)));
        g.drawText (title,
                    r.removeFromTop (18).reduced (8, 0),
                    juce::Justification::centredLeft);
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    drawCard (g, inputCard_, "Input");
    drawCard (g, noteCard_,  "MIDI Output");
    drawCard (g, b1Card_, "");
    drawCard (g, b2Card_, "");

    for (int i = 0; i < numCCPanels_; ++i)
    {
        char buf[8];
        snprintf (buf, sizeof (buf), "CC %d", i + 1);
        drawCard (g, ccCards_[i], buf);
    }

    // "+" placeholder card
    if (numCCPanels_ < kMaxCCPanels)
    {
        drawCard (g, ccCards_[kMaxCCPanels], "");
        auto r = ccCards_[kMaxCCPanels].toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.setFont (juce::Font (juce::FontOptions().withHeight (28.f)));
        g.drawText ("+", r, juce::Justification::centred);
        g.setFont (juce::Font (juce::FontOptions().withHeight (11.f)));
        g.drawText ("Add mapping",
                    r.withTrimmedTop (r.getHeight() * 0.6f),
                    juce::Justification::centred);
    }
}

void MainComponent::resized()
{
    const int P   = 10;   // card inner padding
    const int CG  = 8;    // gap between cards
    const int rowH = 34, gap = 4, labelW = 30;

    auto area = getLocalBounds().reduced (8);

    // ── Input Card ────────────────────────────────────────────────────────────
    const int inputContentH = 30 + 10
                              + 3 * (rowH + gap) + 10
                              + 3 * (rowH + gap) + 10
                              + 36 + 10 + 26 + 6 + 138;
    inputCard_ = area.removeFromTop (inputContentH + P * 2);
    area.removeFromTop (CG);
    {
        auto inner = inputCard_.reduced (P);

        auto topBar = inner.removeFromTop (30);
        oscModeBtn .setBounds (topBar.removeFromLeft (60));
        topBar.removeFromLeft (6);
        midiModeBtn.setBounds (topBar.removeFromLeft (60));
        topBar.removeFromLeft (10);
        midiInputSelector.setBounds (topBar.removeFromLeft (220));

        inner.removeFromTop (10);

        auto inputArea = inner.removeFromTop (inputContentH - 30 - 10);

        auto vizArea = inputArea.removeFromLeft (280);
        inputArea.removeFromLeft (10);

        {
            auto fpsRow = vizArea.removeFromBottom (28);
            fpsLabel_.setBounds (fpsRow.removeFromLeft (30));
            fpsSlider_.setBounds (fpsRow);
        }
        quatLabel_.setBounds (vizArea.removeFromBottom (20));
        auto yawRow = vizArea.removeFromBottom (28);
        yawLabel_.setBounds (yawRow.removeFromLeft (70));
        yawSlider_.setBounds (yawRow);
        quatViz_.setBounds (vizArea);

        for (int i = 0; i < 3; ++i)
        {
            auto row = inputArea.removeFromTop (rowH);
            inputArea.removeFromTop (gap);
            accelLabels[i].setBounds (row.removeFromLeft (labelW));
            accelSliders[i].setBounds (row);
        }
        inputArea.removeFromTop (10);
        for (int i = 0; i < 3; ++i)
        {
            auto row = inputArea.removeFromTop (rowH);
            inputArea.removeFromTop (gap);
            gyroLabels[i].setBounds (row.removeFromLeft (labelW));
            gyroSliders[i].setBounds (row);
        }
        inputArea.removeFromTop (10);
        {
            auto row = inputArea.removeFromTop (36);
            btnToggles[0].setBounds (row.removeFromLeft (80));
            row.removeFromLeft (10);
            btnToggles[1].setBounds (row.removeFromLeft (80));
        }
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
            auto eulerArea = inputArea.removeFromTop (138);
            int sliderW = eulerArea.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
            {
                auto col = (i < 2) ? eulerArea.removeFromLeft (sliderW) : eulerArea;
                if (i < 2)
                {
                    auto btnRow = col.removeFromBottom (20);
                    col.removeFromBottom (2);
                    eulerCenterResetBtns_[i].setBounds (btnRow);
                }
                auto labelRow = col.removeFromBottom (18);
                eulerSliders_[i].setBounds (col);
                eulerLabels_[i].setBounds (labelRow);
            }
        }
    }

    // ── Note Output Card ──────────────────────────────────────────────────────
    const int noteContentH = 20 + 6 + 26;
    noteCard_ = area.removeFromTop (noteContentH + P * 2);
    area.removeFromTop (CG);
    {
        auto inner = noteCard_.reduced (P);
        const int colLabelW = 30, smallW = 62;

        noteOutputLabel.setBounds (inner.removeFromTop (20));
        inner.removeFromTop (6);

        {
            auto row = inner.removeFromTop (26);
            midiOutLabel.setBounds (row.removeFromLeft (colLabelW));
            row.removeFromLeft (4);
            midiOutputSelector.setBounds (row.removeFromLeft (160));
            row.removeFromLeft (8);
            noteChLabel.setBounds (row.removeFromLeft (colLabelW));
            row.removeFromLeft (4);
            noteChannelBox.setBounds (row.removeFromLeft (smallW));
            row.removeFromLeft (8);
            midiRateLabel_.setBounds (row.removeFromLeft (22));
            row.removeFromLeft (2);
            midiRateSlider_.setBounds (row);
        }
    }

    // ── B1 / B2 mini-cards ────────────────────────────────────────────────────
    {
        const int btnCardH = 26;
        auto btnRow = area.removeFromTop (btnCardH + P * 2);
        area.removeFromTop (CG);
        int half = (btnRow.getWidth() - CG) / 2;

        b1Card_ = btnRow.removeFromLeft (half);
        btnRow.removeFromLeft (CG);
        b2Card_ = btnRow;

        {
            auto inner = b1Card_.reduced (P);
            noteB1Label.setBounds (inner.removeFromLeft (30));
            inner.removeFromLeft (4);
            noteB1Box.setBounds (inner);
        }
        {
            auto inner = b2Card_.reduced (P);
            noteB2Label.setBounds (inner.removeFromLeft (30));
            inner.removeFromLeft (4);
            noteB2Box.setBounds (inner);
        }
    }

    // ── CC Cards (dynamic) + Placeholder Card ────────────────────────────────
    const int ccContentH = 24 + 28 + 28 + 8 + 90 + 2 + 18;

    // Helper: lay out widgets for one CC panel inside its card rect
    auto layoutCCPanel = [&](int i, juce::Rectangle<int> cardRect)
    {
        ccCards_[i] = cardRect;
        auto col = cardRect.reduced (P);

        {
            auto row = col.removeFromTop (20);
            col.removeFromTop (4);
            ccOutLabels_[i].setBounds (row.removeFromLeft (34));
            row.removeFromLeft (4);
            ccOutEnableBtns_[i].setBounds (row.removeFromLeft (46));
        }

        ccSourceBoxes_[i].setBounds (col.removeFromTop (24));
        col.removeFromTop (gap);

        {
            auto row = col.removeFromTop (24);
            col.removeFromTop (gap);
            cc14bitBtns_[i].setBounds (row.removeFromRight (34));
            row.removeFromRight (4);
            ccNumberBoxes_[i].setBounds (row);
        }

        col.removeFromTop (8);
        {
            int knobSize = juce::jmin (col.getWidth(), 90);
            int knobX = col.getX() + (col.getWidth() - knobSize) / 2;
            ccRangeKnobs_[i].setBounds (knobX, col.getY(), knobSize, knobSize);
            col.removeFromTop (knobSize + 2);
            ccOutValueLabels_[i].setBounds (col.removeFromTop (18));
        }
    };

    // Helper: lay out one row of up to 4 slots starting at startSlot
    auto layoutCCRow = [&](juce::Rectangle<int> rowArea, int startSlot)
    {
        int numSlots  = (numCCPanels_ < kMaxCCPanels) ? numCCPanels_ + 1 : numCCPanels_;
        int slotsLeft = juce::jmin (4, numSlots - startSlot);
        int cw        = (rowArea.getWidth() - (slotsLeft - 1) * CG) / slotsLeft;

        for (int col = 0; col < slotsLeft; ++col)
        {
            int slot = startSlot + col;
            juce::Rectangle<int> cardRect = (col < slotsLeft - 1)
                ? rowArea.removeFromLeft (cw)
                : rowArea;
            if (col < slotsLeft - 1) rowArea.removeFromLeft (CG);

            if (slot < numCCPanels_)
                layoutCCPanel (slot, cardRect);
            else
                ccCards_[kMaxCCPanels] = cardRect;   // "+" placeholder
        }
    };

    {
        int numSlots = (numCCPanels_ < kMaxCCPanels) ? numCCPanels_ + 1 : numCCPanels_;
        bool twoRows = (numSlots > 4);

        auto ccRow1 = area.removeFromTop (ccContentH + P * 2);
        layoutCCRow (ccRow1, 0);

        if (twoRows)
        {
            area.removeFromTop (CG);
            auto ccRow2 = area.removeFromTop (ccContentH + P * 2);
            layoutCCRow (ccRow2, 4);
        }
    }
}
