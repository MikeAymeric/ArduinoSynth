# ArduinoSynth

A groovebox built on the **Arduino Uno Q**, featuring an 8-step melodic sequencer with OLED display.

## Hardware

| Component | Details |
|-----------|---------|
| Board | Arduino Uno Q |
| Display | SH1106 128x64 OLED (I2C) |
| Audio | Passive buzzer |
| Control | 6 momentary buttons + 1 potentiometer |

## Wiring

| Component | Arduino Pin |
|-----------|-------------|
| OLED SDA | SDA |
| OLED SCL | SCL |
| OLED VCC | 3.3V |
| OLED GND | GND |
| Buzzer + | Pin 8 (via 100Ω resistor) |
| Buzzer − | GND |
| Potentiometer center | A0 |
| Potentiometer sides | 3.3V and GND |
| BTN Play | Pin 2 → GND |
| BTN Left | Pin 3 → GND |
| BTN Right | Pin 4 → GND |
| BTN Up | Pin 5 → GND |
| BTN Down | Pin 6 → GND |
| BTN Toggle | Pin 7 → GND |

> **Note:** On the Uno Q, analog pins are not 5V-tolerant. Always use 3.3V for the potentiometer.

## Features

- 8-step melodic sequencer
- C major pentatonic scale (2 octaves, 10 notes + silence)
- BPM control via potentiometer (40–200 BPM)
- OLED display showing steps, current note, BPM, and play position
- Step enable/disable toggle

## Controls

| Button | Action |
|--------|--------|
| PLAY | Start / Stop sequencer |
| LEFT / RIGHT | Navigate between steps |
| UP / DOWN | Change note for selected step |
| TOGGLE | Enable / disable selected step |
| Potentiometer | BPM (40–200) |

## Setup

1. Open [Arduino App Lab](https://app.arduino.cc)
2. Import the `groovebox` folder as a new app
3. Click **Run** — App Lab will install dependencies automatically (U8g2)
4. To auto-start on power-up: **Run → Run at startup**

## Project Structure

```
groovebox/
├── app.yaml              # App Lab configuration
├── sketch/
│   ├── sketch.ino        # Main Arduino sketch (MCU)
│   └── sketch.yaml       # Board and library config
└── python/
    └── main.py           # MPU side (unused for now)
```
