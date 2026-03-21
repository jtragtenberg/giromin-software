/*
  ==============================================================================

    MidiHandler.h
    Created: 26 Jul 2024 4:53:20pm
    Author:  Solomon Moulang Lewis

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class MidiHandler : public juce::MidiInputCallback
{
public:
    // Callback invocado pelo GirominController para cada CC recebido
    std::function<void(int channel, int cc, int value)> midiCCCallback;

    MidiHandler()
    {
        // ── MIDI Output ──────────────────────────────────────────────────────
        auto midiOutputs = juce::MidiOutput::getAvailableDevices();
        if (midiOutputs.isEmpty())
        {
            std::cout << "no MIDI output devices found" << std::endl;
        }
        else
        {
            for (const auto& d : midiOutputs)
                std::cout << "MIDI Output: " << d.name << " (" << d.identifier << ")" << std::endl;

            auto outDev = findDeviceByName (midiOutputs, "IAC Driver Bus 1");
            midiOutputDevice = juce::MidiOutput::openDevice(outDev.identifier);
            if (midiOutputDevice != nullptr)
                DBG("MIDI output opened: " + outDev.name);
            else
                DBG("Failed to open MIDI output: " + outDev.name);
        }

        // ── MIDI Input ───────────────────────────────────────────────────────
        auto midiInputs = juce::MidiInput::getAvailableDevices();
        if (midiInputs.isEmpty())
        {
            std::cout << "no MIDI input devices found" << std::endl;
        }
        else
        {
            for (const auto& d : midiInputs)
                std::cout << "MIDI Input: " << d.name << " (" << d.identifier << ")" << std::endl;

            auto inDev = findDeviceByName (midiInputs, "Teensy MIDI Port 1");
            midiInputDevice = juce::MidiInput::openDevice(inDev.identifier, this);
            if (midiInputDevice != nullptr)
            {
                DBG("MIDI input opened: " + inDev.name);
                midiInputDevice->start();
            }
            else
            {
                DBG("Failed to open MIDI input: " + inDev.name);
            }
        }
    }

    ~MidiHandler()
    {
        if (midiInputDevice != nullptr)
            midiInputDevice->stop();
    }

    // ── Output: CC ───────────────────────────────────────────────────────────
    void outputMidiMessage (const int midi_ch, const int midi_cc, const int midi_cc_value)
    {
        if (midiOutputDevice != nullptr)
        {
            juce::MidiMessage msg = juce::MidiMessage::controllerEvent(midi_ch, midi_cc, midi_cc_value);
            midiOutputDevice->sendMessageNow(msg);
        }
    }

    // ── Output: Note On / Note Off ────────────────────────────────────────────
    void sendNoteOn (int channel, int note, int velocity = 127)
    {
        if (midiOutputDevice != nullptr)
            midiOutputDevice->sendMessageNow (juce::MidiMessage::noteOn  (channel, note, (juce::uint8)velocity));
    }

    void sendNoteOff (int channel, int note)
    {
        if (midiOutputDevice != nullptr)
            midiOutputDevice->sendMessageNow (juce::MidiMessage::noteOff (channel, note));
    }

    // ── Dispositivos output ───────────────────────────────────────────────────
    juce::Array<juce::MidiDeviceInfo> getAvailableOutputDevices() const
    {
        return juce::MidiOutput::getAvailableDevices();
    }

    void openOutputDevice (const juce::String& identifier)
    {
        midiOutputDevice = juce::MidiOutput::openDevice(identifier);
        if (midiOutputDevice != nullptr)
            DBG("MIDI output switched to: " + identifier);
    }

    // ── Dispositivos input ────────────────────────────────────────────────────
    juce::Array<juce::MidiDeviceInfo> getAvailableInputDevices() const
    {
        return juce::MidiInput::getAvailableDevices();
    }

    void openInputDevice (const juce::String& identifier)
    {
        if (midiInputDevice != nullptr)
            midiInputDevice->stop();

        midiInputDevice = juce::MidiInput::openDevice(identifier, this);
        if (midiInputDevice != nullptr)
        {
            DBG("MIDI input switched to: " + identifier);
            midiInputDevice->start();
        }
    }

private:
    // Procura dispositivo pelo nome; retorna o primeiro que contém 'name', ou o [0] como fallback
    static juce::MidiDeviceInfo findDeviceByName (const juce::Array<juce::MidiDeviceInfo>& devices,
                                                   const juce::String& name)
    {
        for (const auto& d : devices)
            if (d.name.containsIgnoreCase (name))
                return d;
        return devices[0];
    }

    // juce::MidiInputCallback interface
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& msg) override
    {
        if (msg.isController() && midiCCCallback)
            midiCCCallback(msg.getChannel(), msg.getControllerNumber(), msg.getControllerValue());
    }

    std::unique_ptr<juce::MidiOutput> midiOutputDevice;
    std::unique_ptr<juce::MidiInput>  midiInputDevice;
};
