# ArduinoSynth

A polyphonic groovebox built on the **Arduino Uno Q**, featuring a 2-track 8-step melodic sequencer with OLED display, effects, arpeggiator, and hardware PWM waveform control.

## Hardware

| Component | Details |
|-----------|---------|
| Board | Arduino Uno Q |
| Display | SH1106 128x64 OLED (I2C) |
| Audio | 3× passive buzzers (voices A, CHR, B) |
| Control | 7 momentary buttons + 1 potentiometer |

## Wiring

| Component | Arduino Pin |
|-----------|-------------|
| OLED SDA | SDA |
| OLED SCL | SCL |
| OLED VCC | 3.3V |
| OLED GND | GND |
| Buzzer A + | Pin 8 (via 100Ω resistor) |
| Buzzer CHR + | Pin 9 (via 100Ω resistor) |
| Buzzer B + | Pin 10 (via 100Ω resistor) |
| Buzzer − (all) | GND |
| Potentiometer center | A0 |
| Potentiometer sides | 3.3V and GND |
| BTN Play | Pin 2 → GND |
| BTN Left | Pin 3 → GND |
| BTN Right | Pin 4 → GND |
| BTN Up | Pin 5 → GND |
| BTN Down | Pin 6 → GND |
| BTN Toggle | Pin 7 → GND |
| BTN Track | Pin 11 → GND |

> **Note:** On the Uno Q, analog pins are not 5V-tolerant. Always use 3.3V for the potentiometer.

## Features

- **2-track polyphonic sequencer** — Track A and Track B play simultaneously
- **Chord mode** — adds a harmony note (+3rd) on top of Track A
- **8 steps** per track, independently enabled/disabled
- **C major pentatonic scale** across 2 octaves (C1–G5, 25 notes + silence)
- **BPM control** via potentiometer (40–200 BPM)
- **Per-step effects**: None, Vibrato, Tremolo, Staccato, Portamento
- **Per-step arpeggiator** with 4 patterns: Up, Down, Up-Down, Random
- **Hardware PWM waveform** selector per track: Square (50%), Pulse 25%, Pulse 12%
  — uses STM32 TIM3/TIM4 via Zephyr PWM API (D8/D9/D10)
- **OLED display** showing steps, notes, BPM, play position, and long-press progress bar

## Controls

All button actions fire on **release**. Holding a button for 3 seconds triggers a long press — a progress bar on the display shows how close you are.

| Button | Short press | Long press (3s, hold to adjust) |
|--------|-------------|----------------------------------|
| PLAY | Start / Stop sequencer | — |
| LEFT | Step ← | Open **Waveform** selector (pot) — releases to confirm |
| RIGHT | Step → | Open **Arp pattern** selector (pot) — releases to confirm |
| UP | Note +1 | Toggle **arpeggiator** on/off for current step |
| DOWN | Note −1 | — |
| TOGGLE | Enable / disable step | Toggle **chord** on/off (Track A only) |
| TRACK | Switch Track A / B | Open **Effect** selector (pot) — releases to confirm |
| Potentiometer | BPM (40–200) | Selects value in modal menus |

### Modal menus (Waveform / Effect / Arp pattern)
1. Hold the corresponding button until the progress bar fills (3s)
2. The menu opens — rotate the potentiometer to choose a value
3. Release the button to confirm and close the menu

## Display layout

```
>A  [■][■][ ][■][■][ ][■][ ]
 B  [■][ ][■][ ][■][ ][■][ ]
                        ▼
A: C4 +CHR VIBRATO
SQUARE >> 120
████████████░░░░░░░░░░░░░░░░  ← long press progress bar
```

- **■** = active step, **□** = inactive step
- **▼** = current play position
- **▪** top-left of step = chord active
- **▸** top-right of step = arpeggiator active
- **─** under step = effect active

## Setup

1. Open [Arduino App Lab](https://app.arduino.cc)
2. Import the `groovebox` folder as a new app
3. Click **Run** — App Lab will install dependencies automatically (U8g2)
4. To auto-start on power-up: **Run → Run at startup**

> **Note:** The Uno Q takes ~43 seconds to boot (Linux side must start first before the sketch runs).

## Project Structure

```
groovebox/
├── app.yaml              # App Lab configuration
├── sketch/
│   ├── sketch.ino        # Main Arduino sketch (MCU — STM32U585)
│   └── sketch.yaml       # Board and library config
└── python/
    └── main.py           # MPU side (unused for now)
```

## Technical notes

- **Board**: Arduino Uno Q has two cores — STM32U585 MCU (runs Zephyr OS + Arduino sketch) and Qualcomm QRB2210 MPU (runs Linux). App Lab is required to deploy sketches.
- **Hardware PWM**: Waveform duty cycle is controlled via Zephyr's PWM API using the `zephyr_user` node from the system overlay. D8=TIM3_CH1, D9=TIM4_CH3, D10=TIM4_CH4. No custom `.overlay` file needed.
- **GPIO**: All header pins are 3.3V logic. Analog pins are **not** 5V-tolerant.
