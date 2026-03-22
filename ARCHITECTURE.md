# Architecture & Glossary — imu-gesture-toolkit

## Overview

A C++ JUCE application that receives IMU sensor data from "Giromin" devices (via OSC or MIDI), processes it through a gesture layer, and outputs MIDI messages (Notes + CCs) for use in musical performance and other interactive contexts.

---

## Glossary

### Sensor
A physical IMU device ("Giromin") that measures:
- **Accelerometer** — linear acceleration on 3 axes (X, Y, Z), normalized to [-1, 1]
- **Gyroscope** — angular velocity on 3 axes (X, Y, Z), normalized to [-1, 1]
- **Quaternion** — orientation in 3D space (w, x, y, z), unit quaternion
- **Buttons** — two digital inputs (B1, B2), values 0.0 or 1.0

### Input
Raw normalized sensor data entering the system. Two input modes:
- **OSC** — float values received over UDP (port 1333) as OSC messages
- **MIDI 14-bit** — MSB/LSB CC pairs decoded to a 14-bit integer, then converted to float [-1, 1]

Normalization encoding: `raw_int16 = (value14 << 2) - 32768` → `float = raw_int16 * NORMALISATION_CONSTANT`

Constants:
- `A_NORMALISATION_CONSTANT = 0.006535947712` (accel, 16g range)
- `G_NORMALISATION_CONSTANT = 0.02895193978` (gyro, 500 dps range)

### Gesture
A **stateful transformation** of raw sensor data into a signal that carries gestural meaning. A gesture:
- Receives a raw sensor value or set of values
- Applies a transformation (may maintain internal state between calls)
- Returns a value that expresses something about the performer's movement or intent

Gestures are implemented as methods of `IMUGestureToolkit`. Each `IMUGestureToolkit` instance maintains its own state (filter history, toggle state, previous value).

| Gesture | Method | Description |
|---------|--------|-------------|
| **Button** | `processButtonSignal(input, action)` | PUSH / INVERTED_PUSH / TOGGLE; maintains `toggle_state_` |
| **Rotation Rate** | `processRotationRate(gyro[], axis, direction, gain, riseFilter, fallFilter)` | Extracts axis or magnitude, applies direction gate, gain, and asymmetric EMA filter |
| **Euler Orientation** | `convertQuaternionToEuler(w, x, y, z, order)` | Converts quaternion to Tait-Bryan angles in one of 6 orders → `{a_first, a_last, a_mid}` |
| **Scale & Clamp** | `scaleAndClamp(value, inMin, inMax, outMin, outMax)` | Maps and clips a value from one range to another — defines the active gesture region |
| **Change Detection** | `changed(int)` | Returns true only when the value differs from the last call — gates duplicate output |
| **EMA Filter** | `filterEMA` / `filterEMATwoWays` | Exponential moving average; asymmetric version uses separate rise/fall coefficients |

### Mapping
The chain that connects a sensor source to a MIDI output: **input → gesture → output**.

For CC outputs, a mapping consists of:
1. A **source** — which sensor signal to use (AX, AY, AZ, GX, GY, GZ, Euler 1/2/3)
2. A **range** — the active input window `[rangeMin, rangeMax]` within [-1, 1], optionally inverted
3. A **scale & clamp** — maps the windowed input to [0, 1] output
4. A **resolution** — 7-bit (0–127) or 14-bit (0–16383)
5. A **rate** — maximum send frequency in Hz
6. A **change gate** — only sends when the output value actually changed at the target resolution

For Note outputs, the mapping is: B1/B2 threshold (> 0.5) → Note On; else Note Off.

### Output
MIDI messages sent to a connected output device:
- **CC 7-bit** — `outputMidiMessage(channel, cc, 0–127)`
- **CC 14-bit** — `sendCC14(channel, msb_cc, value01)` — MSB on CC 16–27, LSB on CC (MSB+32) = 48–59
- **Note On/Off** — `sendNoteOn/Off(channel, note)`

Up to **8 independent CC outputs** can run simultaneously, each with its own source, range, resolution, and rate.

### Euler Center
An offset applied to Euler angles 1 and 2 to avoid the ±π discontinuity. When the user presses "ctr", the current angle becomes the new zero reference. Internally: `display = atan2(sin(raw - center), cos(raw - center))`, which wraps to [-π, π] with the reference angle at 0.

---

## Architecture Layers

```
┌──────────────────────────────────────────────────────┐
│                    UI Layer                          │
│  MainComponent.h/cpp — cards, sliders, knobs, timer  │
│  QuatVisualizer.h — OpenGL 3D orientation display    │
│  RangeKnob.h — custom dual-arc range control         │
└──────────────────┬───────────────────────────────────┘
                   │ timer callback (FPS-driven)
┌──────────────────▼───────────────────────────────────┐
│              Orchestration Layer                     │
│  GirominController.h                                 │
│  — manages input mode, device connections            │
│  — normalizes sensor data → SensorDisplayData        │
│  — drives gesture processing per sensor update       │
│  — runs CC output pipeline per timer tick            │
└──────┬─────────────────────────────────────┬─────────┘
       │                                     │
┌──────▼──────────┐               ┌──────────▼─────────┐
│  Gesture Layer  │               │    I/O Layer        │
│ IMUGestureToolkit│              │ OSCHandler.h        │
│ — processButton │               │ MidiHandler.h       │
│ — rotationRate  │               │ midi_cc_map.h       │
│ — eulerConvert  │               └──────────┬──────────┘
│ — scaleAndClamp │                          │
│ — changed()     │               ┌──────────▼──────────┐
│ — filterEMA     │               │    Data Layer        │
└─────────────────┘               │ GirominData.h        │
                                  │ — raw sensor state   │
                                  └─────────────────────┘
```

---

## Data Flow

```
Giromin IMU device
    │
    ├─[OSC UDP 1333]──► OSCHandler ──► GirominController::UpdateGiromin()
    │                                             │
    └─[MIDI 14-bit]──► MidiHandler ──► GirominController::UpdateGirominFromMIDI()
                                                  │
                                        GirominData (state store)
                                                  │
                                        ProcessGestures()
                                        ├─ normalize → SensorDisplayData [-1,1]
                                        ├─ update_UI callback → MainComponent
                                        ├─ button toggle gesture → CC 10
                                        └─ button threshold → NoteOn/NoteOff
                                                  │
                                        MainComponent::timerCallback() [FPS]
                                        ├─ update display sliders/labels
                                        ├─ convertQuaternionToEuler (6 orders)
                                        ├─ apply eulerCenter offset
                                        └─ processCCOutputs(sensor, e1, e2, e3)
                                                  │
                                          per CC output (up to 8):
                                          ├─ extract source value [-1,1]
                                          ├─ scaleAndClamp → [0,1]
                                          ├─ rate limit (interval_ms)
                                          ├─ changeTracker.changed(sentVal)
                                          └─ sendCC14 / outputMidiMessage
                                                  │
                                          MidiHandler → MIDI output device
```

---

## Source Files

| File | Layer | Role |
|------|-------|------|
| `GirominData.h` | Data | Raw sensor state container |
| `IMUGestureToolkit.h` | Gesture | Stateful gesture transformations |
| `GirominController.h` | Orchestration | Input → gesture → mapping → output |
| `OSCHandler.h` | I/O | OSC receiver (UDP, port 1333) |
| `MidiHandler.h` | I/O | MIDI input/output device management |
| `midi_cc_map.h` | I/O | Fixed CC number constants per sensor field |
| `MainComponent.h/cpp` | UI | GUI panels, timer loop, settings persistence |
| `QuatVisualizer.h` | UI | OpenGL 3D quaternion orientation display |
| `RangeKnob.h` | UI | Custom rotary control for input range mapping |
| `Main.cpp` | Entry | Application entry point, scrollable window wrapper |

---

## CC Mapping Protocol (14-bit)

| Sensor Field | MSB CC | LSB CC |
|---|---|---|
| Accel X | 16 | 48 |
| Accel Y | 17 | 49 |
| Accel Z | 18 | 50 |
| Gyro X | 19 | 51 |
| Gyro Y | 20 | 52 |
| Gyro Z | 21 | 53 |
| Quat W | 22 | 54 |
| Quat X | 23 | 55 |
| Quat Y | 24 | 56 |
| Quat Z | 25 | 57 |
| Drops | 26 | 58 |
| Timestamp | 27 | 59 |
| Button 1 | 28 (7-bit) | — |
| Button 2 | 29 (7-bit) | — |
