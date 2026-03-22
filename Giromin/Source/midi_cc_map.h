#pragma once

// ── Giromin 14-bit MIDI CC map ────────────────────────────────────────────────
// Each value uses a (MSB, LSB) pair following the MIDI 1.0 spec:
//   MSB controller: CC 0–31   → carries bits 13–7
//   LSB controller: CC 32–63  → carries bits 6–0  (= MSB number + 32)
//
// The range CC 16–31 / CC 48–63 covers:
//   CC 16–19  General Purpose Controllers 1–4  (MIDI 1.0 § 4)
//   CC 20–31  Undefined 14-bit controllers     (safe for custom use)
//
// Channel: (sender_id % 16) + 1  →  Giromin #1=ch1, #16=ch16, #17=ch1, etc.
//
// Value encoding for int16_t fields (accel, gyro, quat):
//   midi14 = (uint16_t)((int32_t)raw_int16 + 32768) >> 2
//   Range:  -32768 → 0     (full negative)
//           0      → 8192  (center / rest)
//           +32767 → 16383 (full positive)
//
// Value encoding for drops:
//   midi14 = min(totalDrops, 16383)
//
// Value encoding for timestamp:
//   midi14 = (timestamp_us >> 10) & 0x3FFF  →  ~1 ms resolution, wraps at 16 s
// ──────────────────────────────────────────────────────────────────────────────

#define MIDI_CC_AX    16   // Acelerômetro X  — General Purpose Controller 1
#define MIDI_CC_AY    17   // Acelerômetro Y  — General Purpose Controller 2
#define MIDI_CC_AZ    18   // Acelerômetro Z  — General Purpose Controller 3
#define MIDI_CC_GX    19   // Giroscópio X    — General Purpose Controller 4
#define MIDI_CC_GY    20   // Giroscópio Y    — Undefined
#define MIDI_CC_GZ    21   // Giroscópio Z    — Undefined
#define MIDI_CC_QW    22   // Quaternion W    — Undefined
#define MIDI_CC_QX    23   // Quaternion X    — Undefined
#define MIDI_CC_QY    24   // Quaternion Y    — Undefined
#define MIDI_CC_QZ    25   // Quaternion Z    — Undefined
#define MIDI_CC_DROPS 26   // Drop count      — Undefined
#define MIDI_CC_TS    27   // Timestamp       — Undefined
// LSBs = MSB + 32 → CC 48–59

// 7-bit button CCs (não usam par MSB/LSB): 0 = solto, 127 = pressionado
#define MIDI_CC_BTN1  28   // Botão 1         — Undefined
#define MIDI_CC_BTN2  29   // Botão 2         — Undefined

#define MIDI_BAUD          31250   // padrão MIDI (USB side)
#define MIDI_INTERNAL_BAUD 500000  // link interno M5Stick→Teensy (não precisa ser 31250)
