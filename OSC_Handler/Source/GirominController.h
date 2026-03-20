/*
  ==============================================================================

    GirominController.h
    Created: 26 Jul 2024 4:52:51pm
    Author:  Solomon Moulang Lewis

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "GirominData.h"
#include "OSCHandler.h"
#include "MidiHandler.h"
#include "IMUGestureToolkit.h"
#include "midi_cc_map.h"

#define MAX_GIROMINS 6

#define A_NORMALISATION_CONSTANT 0.006535947712     // 1/15.3   - accelerometer in 16g resolution
#define G_NORMALISATION_CONSTANT 0.02895193978      // 1/34.54  - gyroscope in 500dps resolution

class GirominController
{
public:
    enum class InputMode { OSC, MIDI };

    std::function<void(float)> update_UI;

    GirominController()
    {
        for (int i = 0; i < MAX_GIROMINS; i++)
            giromins_.emplace_back (GirominData());

        giromins_[0].setID (23);

        // Inicializa tabela de MSBs pendentes como "sem MSB"
        for (auto& row : msb_pending_)
            row.fill (-1);

        // ── OSC callback ─────────────────────────────────────────────────────
        osc_handler_.oscMessageCallback = [this](std::string addr, float* values)
        {
            if (input_mode_ != InputMode::OSC) return;
            UpdateGiromin (addr, values);
            ProcessGestures();
        };

        // ── MIDI input callback (chamado na thread de audio/MIDI do JUCE) ────────
        midi_handler_.midiCCCallback = [this](int channel, int cc, int value)
        {
            if (input_mode_ != InputMode::MIDI) return;
            // Despacha para a message thread para manter thread-safety com o OSC
            juce::MessageManager::callAsync ([this, channel, cc, value]()
            {
                UpdateGirominFromMIDI (channel, cc, value);
            });
        };
    }

    void setInputMode (InputMode mode)
    {
        input_mode_ = mode;
    }

    InputMode getInputMode() const { return input_mode_; }

    juce::Array<juce::MidiDeviceInfo> getMidiInputDevices() const
    {
        return midi_handler_.getAvailableInputDevices();
    }

    void openMidiInputDevice (const juce::String& identifier)
    {
        midi_handler_.openInputDevice (identifier);
    }
    
    GirominData* getGiromin (int index)
    {
        //TODO: use ID instead
        return &giromins_[index];
    }

private:
    // ── Processamento de gestos (compartilhado pelos dois modos de input) ─────
    void ProcessGestures()
    {
        //===============================================================================================
        // GIROSCÓPIO — dado bruto normalizado conforme midi_cc_map.h
        // Encoding:  midi14 = (uint16_t)((int32_t)raw_int16 + 32768) >> 2
        // Decoding:  float armazenado = raw_int16 * G_NORMALISATION_CONSTANT
        // Para [0,1]: inverte o encoding → value14 = (raw_int16 + 32768) / 4
        //             normalizado = value14 / 16383.0  (range 14-bit)
        //===============================================================================================
        auto* g = getGiromin(0);
        auto raw_to_01 = [](float stored, float norm_constant) -> float
        {
            float raw_int16 = stored / norm_constant;
            float value14   = (raw_int16 + 32768.f) / 4.f;
            return value14 / 16383.f;
        };

        float gx_01 = raw_to_01 (g->getGX(), G_NORMALISATION_CONSTANT);
        update_UI (gx_01);
        std::cout << "GX: " << gx_01 << std::endl;

        //===============================================================================================
        // BUTTON ACTIONS
        //===============================================================================================
        float giromin_data_value = getGiromin(0)->getB1();

        if (giromin_data_value != previous_giromin_data_value_)
        {
            float output_value = gesture1.processButtonSignal (giromin_data_value,
                                                               IMUGestureToolkit::ButtonAction::TOGGLE);

            if (gesture1.changed (static_cast<int>(output_value)))
            {
                std::cout << "giromin_data_value: " << output_value << std::endl;
                midi_handler_.outputMidiMessage (1, 10, (int)output_value);
            }

            previous_giromin_data_value_ = giromin_data_value;
        }
    }

    // ── Decodificação de MIDI CC 14-bit → float normalizado ──────────────────
    // Protocolo definido em midi_cc_map.h:
    //   MSB = CC 16-27, LSB = CC (MSB+32) = 48-59
    //   value14 = (msb << 7) | lsb
    //   raw_int16 = (int32_t)(value14 << 2) - 32768
    //   float = raw_int16 * NORMALIZATION_CONSTANT
    void UpdateGirominFromMIDI (int channel, int cc, int value)
    {
        // Canal MIDI → índice do giromin (canal 1 = índice 0)
        int idx = (channel - 1) % MAX_GIROMINS;

        // ── Armazena MSB para montar par 14-bit depois ────────────────────────
        if (cc >= 16 && cc <= 27)
        {
            msb_pending_[idx][cc] = value;
            return;  // aguarda o LSB chegar
        }

        // ── LSB chegou: monta valor 14-bit e aplica ao GirominData ────────────
        if (cc >= 48 && cc <= 59)
        {
            int msb_cc = cc - 32;

            if (msb_pending_[idx][msb_cc] < 0)
                return;  // MSB ainda não chegou, descarta

            int value14  = (msb_pending_[idx][msb_cc] << 7) | value;
            msb_pending_[idx][msb_cc] = -1;  // limpa MSB pendente

            // Reconstrói int16 a partir do valor 14-bit
            // Encoding: midi14 = (uint16_t)((int32_t)raw_int16 + 32768) >> 2
            // Decoding: raw_int16 = (int32_t)(value14 << 2) - 32768
            int raw_int16 = (value14 << 2) - 32768;

            switch (msb_cc)
            {
                case MIDI_CC_AX: giromins_[idx].setAX ((float)raw_int16 * A_NORMALISATION_CONSTANT); break;
                case MIDI_CC_AY: giromins_[idx].setAY ((float)raw_int16 * A_NORMALISATION_CONSTANT); break;
                case MIDI_CC_AZ: giromins_[idx].setAZ ((float)raw_int16 * A_NORMALISATION_CONSTANT); break;
                case MIDI_CC_GX: giromins_[idx].setGX ((float)raw_int16 * G_NORMALISATION_CONSTANT); break;
                case MIDI_CC_GY: giromins_[idx].setGY ((float)raw_int16 * G_NORMALISATION_CONSTANT); break;
                case MIDI_CC_GZ: giromins_[idx].setGZ ((float)raw_int16 * G_NORMALISATION_CONSTANT); break;
                // Quaternions: range físico é [-1, +1]
                case MIDI_CC_QW: giromins_[idx].setQ1 ((float)raw_int16 / 32767.f); break;
                case MIDI_CC_QX: giromins_[idx].setQ2 ((float)raw_int16 / 32767.f); break;
                case MIDI_CC_QY: giromins_[idx].setQ3 ((float)raw_int16 / 32767.f); break;
                case MIDI_CC_QZ: giromins_[idx].setQ4 ((float)raw_int16 / 32767.f); break;
                default: break;
            }

            ProcessGestures();
            return;
        }

        // ── Botões: 7-bit simples ─────────────────────────────────────────────
        if (cc == MIDI_CC_BTN1)
        {
            giromins_[idx].setB1 (value == 127 ? 1.f : 0.f);
            ProcessGestures();
        }
        else if (cc == MIDI_CC_BTN2)
        {
            giromins_[idx].setB2 (value == 127 ? 1.f : 0.f);
            ProcessGestures();
        }
    }

    // ── OSC parsing (inalterado) ──────────────────────────────────────────────
    // /giromin/25/a/x
    void UpdateGiromin (std::string addr, float *values)
    {
        // TODO: add a condition to check for 'giromin' - could be a different address.
        const char* start = strchr (addr.c_str() + 1, '/');
        const char* next = strchr (start + 1, '/');
        int address_id = std::atoi (std::string (start + 1, next).c_str());
        
        for (int i = 0; i < MAX_GIROMINS; i ++)
        {
            if (giromins_[i].getId() == address_id)
            {
                start = next;
                next = strchr (start + 1, '/');
                std::string param_group_id (start + 1, next ? next - start - 1 : strlen (start + 1));
                std::string param;
                
                if (next) { param = next + 1; }
                
                switch (param_group_id[0])
                {
                    case 'a':
                        if (param == "x")
                        {
                            giromins_[i].setAX (values[0] * A_NORMALISATION_CONSTANT);
                        }
                        else if (param == "y")
                        {
                            giromins_[i].setAY (values[0] * A_NORMALISATION_CONSTANT);
                        }
                        else if (param == "z")
                        {
                            giromins_[i].setAZ (values[0] * A_NORMALISATION_CONSTANT);
                        }
                        break;
                    case 'g':
                        if (param == "x")
                        {
                            giromins_[i].setGX (values[0] * G_NORMALISATION_CONSTANT);
                        }
                        else if (param == "y")
                        {
                            giromins_[i].setGY (values[0] * G_NORMALISATION_CONSTANT);
                        }
                        else if (param == "z")
                        {
                            giromins_[i].setGZ (values[0] * G_NORMALISATION_CONSTANT);
                        }
                        break;
                    case 'q':
                        giromins_[i].setQ1 (values[0]);
                        giromins_[i].setQ2 (values[1]);
                        giromins_[i].setQ3 (values[2]);
                        giromins_[i].setQ4 (values[3]);
                        break;
                    case 'b':
                        if (param_group_id == "b1")
                        {
                            giromins_[i].setB1 (values[0]);
                        }
                        else if (param_group_id == "b2")
                        {
                            giromins_[i].setB2 (values[0]);
                        }
                        break;
                    default:
                        std::cout << "unknown param_group_id: " << param_group_id << "\n";
                        break;
                }
            }
        }
    }
    
    IMUGestureToolkit gesture1, gesture2;
    float previous_giromin_data_value_ = 0;
    float previous_giromin_output_value_ = 0;

    // MSBs pendentes para montagem de pares 14-bit: msb_pending_[giromin_idx][cc_number]
    // -1 = nenhum MSB pendente para este campo
    std::array<std::array<int, 128>, MAX_GIROMINS> msb_pending_;

    InputMode input_mode_ = InputMode::OSC;

    std::vector<GirominData> giromins_;
    OSCHandler osc_handler_;
    MidiHandler midi_handler_;
};
