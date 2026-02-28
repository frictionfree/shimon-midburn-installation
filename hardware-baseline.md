# Shimon – As‑Built Hardware & Wiring Baseline (Frozen)

**Status:** VERIFIED, PHYSICALLY IMPLEMENTED, AND TESTED
**Purpose:** This document captures the exact as‑built hardware, wiring, passive components, and electrical topology of the Shimon installation.

This baseline is considered **frozen**. Future work must be additive and must not modify this document unless physical wiring or components change.

---

## 1. System Overview

Shimon is an ESP32‑based interactive light‑and‑sound installation consisting of four physical wings (Blue, Red, Green, Yellow). Each wing includes:
- A 12 V LED strip (low‑side PWM switched)
- A 60 mm illuminated arcade button

Audio feedback for interactive/game mode is provided by a DFPlayer Mini MP3 module and an external stereo amplifier. The system is designed for silent operation, electrical robustness, and unattended runtime.

---

## 2. Core Controller

- **Controller:** ESP32‑DevKitC‑32E (ESP32‑WROOM‑32E)
- Powered from 5 V (USB or buck converter)
- 3.3 V rail used for logic and button sensing

---

## 3. Power Architecture (As Built)

### 3.1 Power Sources

- **12 V LED PSU:** 12 V / 100 W (prototype)
- **Audio Amplifier PSU:** Separate 12 V / 5 A PSU (temporary noise‑mitigation solution)
- **Buck Converter:** 12 V → 5 V (up to 5 A)

### 3.2 Distribution Strategy

- **+12 V** distributed via WAGO‑style terminals to:
  - LED strips (via MOSFET module)
  - Button LED anodes
  - Buck converter input
- **+5 V** distributed from ESP32 5 V rail via WAGO to:
  - DFPlayer Mini
  - MOSFET module logic input
- **Common Ground:**
  - ESP32, DFPlayer, MOSFET logic, button logic, and LED PSU grounds are tied together
  - Audio amplifier ground is isolated by separate PSU

*No fuses are installed in the prototype at this stage.*

---

## 4. LED Strips & Switching

### 4.1 LED Strips

- 12 V COB LED strips
- 2 m per wing (prototype)
- Single‑color per wing (Blue / Red / Green / Yellow)

### 4.2 MOSFET Switch Module

- 4‑channel IRF540N MOSFET module (low‑side switching)
- Each channel switches LED strip negative

### 4.3 PWM Control Pins (Locked)

| Wing   | GPIO |
|--------|------|
| Blue   | 23   |
| Red    | 19   |
| Green  | 18   |
| Yellow | 5    |

PWM generation and fade behavior are handled in firmware; hardware provides low‑side switching only.

---

## 5. Arcade Buttons

### 5.1 Button Inputs

- 60 mm illuminated arcade buttons (12 V LED)
- Button contacts wired to ESP32 GPIOs using **INPUT_PULLUP** logic

**GPIO Assignments** (Blue moved from GPIO 21 → GPIO 32 in Feb 2026 — right-header grouping):

| Wing   | GPIO |
|--------|------|
| Blue   | 32   |
| Red    | 13   |
| Green  | 14   |
| Yellow | 27   |

### 5.2 Button Conditioning (Implemented)

Each button input includes:
- **10 kΩ pull‑up to 3.3 V**
- **100 nF ceramic capacitor to GND**
- **1 µF electrolytic capacitor to GND** (added Feb 2026, in parallel with 100 nF — broader low-frequency filtering)

This RC network eliminates noise and ghost presses caused by long cable runs and EMI. The 1 µF cap provides additional filtering of lower-frequency transients that the 100 nF alone cannot suppress.

### 5.3 Button LED Integration (Final, Implemented Design)

- Button LEDs are **12 V devices**
- Button LED **anodes** connect directly to +12 V
- Button LED **cathodes** are connected to the **same MOSFET channel** as the corresponding LED strip

**Result:**
- Button LEDs mirror LED strip PWM exactly
- No ESP32 GPIOs are used to drive button LEDs
- Previously assigned "button LED GPIOs" are **unused / reserved**

This behavior has been implemented and tested in hardware.

---

## 6. DFPlayer Mini Audio Subsystem

### 6.1 Power & Decoupling

- Powered from regulated 5 V
- Decoupling at DFPlayer VCC/GND:
  - 100 nF ceramic capacitor
  - 470 µF electrolytic capacitor

### 6.2 UART Connection

- ESP32 TX2 (GPIO17) → DFPlayer RX via **1 kΩ resistor**
- ESP32 RX2 (GPIO16) ← DFPlayer TX

### 6.3 Audio Signal Conditioning (As Built)

- DFPlayer DAC_R output → amplifier TRS ring via **1 kΩ series resistor**
- Additional analog conditioning network present (AC coupling / pop suppression), implemented during tuning

*(Exact analog topology to be preserved as‑built; not modified in this baseline.)*

### 6.4 DFPlayer Mini – Operational Constraints (Baseline)

**SD Card Structure (Locked)**
- All audio files must reside in the `/mp3` directory
- Files must be named using **three-digit numeric format** (e.g., `061.mp3`)

**Track Mapping (Interactive Mode)**

| Wing   | File       |
|--------|------------|
| Blue   | 061.mp3    |
| Red    | 062.mp3    |
| Green  | 063.mp3    |
| Yellow | 064.mp3    |

**Playback Method (Locked)**
- Playback function: `dfp.playMp3Folder(trackNumber)`
- Playback must be guarded to prevent re-entrant commands
- Stopping previous playback before starting a new one improves stability

---

## 7. ESP32 Decoupling

- 470 µF electrolytic capacitor between ESP32 5 V and GND
- 100 nF ceramic capacitor between ESP32 3.3 V and GND

---

## 8. Wiring & Interconnects

- WAGO‑style terminals used for:
  - 12 V distribution
  - 5 V distribution
  - Common ground star points
- USB‑A → micro‑USB cable used to power ESP32 during development or via buck converter

---

## 9. Audio Input: SPDIF → I2S

### Primary Musical Analysis Source

Party Mode relies on digital audio analysis via I2S as the primary source for detecting musical context.

### Hardware Path (Validated & In Use)

```
Mixer SPDIF (coax) output
    ↓
SPDIF → I2S converter
    ↓
ESP32 I2S peripheral (RX, slave mode)
```

### Assumed Audio Format (Frozen)

| Parameter    | Value                          |
|--------------|--------------------------------|
| Sample rate  | 48 kHz                         |
| Sample width | 24-bit                         |
| Packing      | Right-justified in 32-bit words |

All analysis code assumes this format. Any change requires full re-validation.

### ESP32 I2S Pin Mapping (Validated & Frozen)

The ESP32 is configured as an I2S slave receiver with the following GPIO mapping:

| Signal       | GPIO |
|--------------|------|
| BCLK (Bit Clock) | 26 |
| LRCK / WS (Word Select) | 25 |
| DATA IN      | 22   |
| MCLK         | Not used |

This mapping has been validated in practice and is part of the stable hardware baseline.
Any change requires full clocking and signal-integrity re-validation.

### Critical: Correct 24-bit Right-Justified Decode

The SPDIF-to-I2S converter outputs 24-bit right-justified samples packed into 32-bit words.

**Correct decoding is mandatory.** Any sketch that does not implement this decode path is invalid by default.

```cpp
int32_t sample = raw >> 8;
float x = sample / 8388608.0f;
```

**Incorrect decoding causes:**
- False silence
- Broken RMS calculations
- Invalid state transitions

DSP behavior, beat detection, musical timing, or audio-synchronized visuals are explicitly out of scope for this baseline and are documented separately in post-baseline materials.

---

## 10. MIDI Clock Input

### Timing Authority

**MIDI clock is the authoritative timing source in Party Mode.**

### MIDI Transport Paths

**Validated (Development & Testing)**
- USB-MIDI from Ableton (full START / STOP / CONTINUE / CLOCK semantics)
- DIN MIDI via optocoupler (see below)

**Defined but Not Yet Fully Validated (Field Path)**
- Mixer USB-MIDI output
- External USB MIDI Host
- ESP32 UART RX (GPIO 34)

### ESP32 MIDI Input (Recommended & Reserved)

| Parameter | Value |
|-----------|-------|
| UART RX pin | GPIO 34 |
| Baud rate | 31,250 bps (standard MIDI) |

GPIO 34 is RX-only and has no internal pull-up.

Two mutually exclusive electrical input options exist:
1. **DIN MIDI via optocoupler** (preferred, field-safe)
2. **USB-UART input** for development only

**Rule:** DIN MIDI IN and USB-UART MIDI input must never be connected at the same time.

### DIN MIDI IN — Optocoupler Interface

**Validated & Rebuild-Critical**

This interface is not generic. The wiring and pull-up configuration documented here are required for reliable operation with the ESP32 and must be preserved for any future rebuild.

**Input (DIN Connector)**
- Left MIDI pin (viewed from back of connector) → 220 Ω → 6N138 pin 2
- Right MIDI pin → 6N138 pin 3

**Optocoupler: 6N138**

| Pin | Connection |
|-----|------------|
| 8   | 3.3 V |
| 8   | 100 nF capacitor → Pin 5 |
| 5   | GND |
| 7   | GND via 4.7 kΩ |
| 6   | ESP32 GPIO 34 (UART RX) |
| 6   | 10 kΩ pull-up to 3.3 V (required; GPIO34 has no internal pull-up) |

**Validation**
- Tested using Ableton MIDI output
- Source: Focusrite Scarlett 16i16 MIDI OUT
- Stable MIDI clock ingestion confirmed

### USB-UART MIDI Clock Ingress (Development Harness Only)

- Not MIDI-spec, but acceptable for development
- Hairless forwards raw MIDI bytes
- Baud rate used successfully: 115,200
- MIDI bytes (e.g. 0xF8) forwarded verbatim

This path is intended for:
- Development
- Debug
- Early timing validation

**Not for final deployment.**

---

## 11. Validated Electrical & Control Findings (Baseline-Critical)

The following findings were discovered empirically during development and are **critical to correct operation** of the existing hardware. They are considered part of the frozen baseline.

### 11.1 MOSFET Minimum Effective Duty Limitation

- The IRF540N-based MOSFET switch module does **not conduct linearly at very low PWM duty** when driven at 3.3 V gate voltage.
- Below a minimum duty threshold, LED strips remain dark or respond inconsistently.

**Validated solution (locked):**
- On button press, firmware must **immediately jump PWM duty to a minimum visible value** (empirically ~70/255).
- After the initial jump, PWM may ramp linearly to full brightness.
- Fade-out to zero duty does **not** use a minimum clamp.

This behavior is required to guarantee instant visual feedback on short button presses.

### 11.2 PWM Configuration Constraints (ESP32)

- PWM method: `analogWrite()` / ESP32 LEDC backend
- Stable, silent operating envelope:
  - Frequency: ~12–12.5 kHz
  - Resolution: 8-bit (0–255)

Attempts at higher frequency + resolution combinations (e.g., 25 kHz @ 12-bit) were unstable or non-functional.

### 11.3 Power Budget & LED Usage Constraints

- LED PSU (prototype): **12 V / 100 W (~8.3 A max continuous)**

**Implications:**
- Sustained full-brightness operation of all four wings simultaneously is **not allowed**.
- Lighting patterns must:
  - Prefer one dominant wing at a time
  - Allow short overlaps of two wings
  - Reserve four-wing activation for brief, low-duty accents

Firmware patterns must respect a global brightness cap to avoid PSU overload.

### 11.4 Audio Noise Root Cause & Mitigation (Baseline)

- Background audio noise during LED activity was traced to **conducted supply/ground noise** caused by high LED current switching.
- The issue is **not** caused by PWM frequency, software timing, or DFPlayer logic.

**Validated mitigation:**
- Powering the audio amplifier from a **separate 12 V PSU** eliminates noise completely.

This solution is currently in use and is considered part of the baseline electrical topology.

---

## 12. Firmware Toolchain & Version Constraints

- Development environment: Arduino IDE
- ESP32 boards package: **Espressif ESP32 v3.3.5**

This version is required for:
- Correct `analogWrite()` behavior
- Stable LEDC PWM operation

### ESP32 LEDC API (Core 3.x – Locked Requirement)

With ESP32 boards package v3.x, the legacy LEDC API `ledcSetup()` / `ledcAttachPin()` is no longer available.

All PWM control **must** use:

```cpp
ledcAttach(pin, frequency, resolutionBits)
ledcWrite(pin, duty)
```

Use of deprecated LEDC functions will result in compilation failure.
This constraint applies to all baseline-compatible firmware.

---

## 13. Bill of Materials (As-Built)

### Core Electronics

- ESP32-DevKitC-32E (ESP32-WROOM-32E)
- ESP32 expansion / carrier board
- 4-channel IRF540N MOSFET switch module

### Audio Output

- DFPlayer Mini MP3 module
- Lepy LP-2020A amplifier
- Pyle PDWR30B speakers

### Audio Analysis

- SPDIF → I2S Converter
- MIDI DIN Input port
- 6N138 Optocoupler for MIDI signal
- DORMIDI USB MIDI Host Box – bridge between Mixer MIDI USB Output and system MIDI DIN Input

### Power

- 12 V / 100 W LED PSU
- 12 V → 5 V buck converter (5 A)
- Separate 12 V / 5 A PSU for amplifier

### Lighting & Inputs

- 12 V COB LED strips (2 m per wing)
- 60 mm 12 V illuminated arcade buttons (4 colors)

### Interconnects & Passives

- WAGO-style distribution terminals
- Assorted ceramic (100 nF) and electrolytic capacitors
- Resistors (1 kΩ, 10 kΩ, 4.7 kΩ, 220 Ω)

---

## 14. What Is Explicitly OUT OF SCOPE

The following are **not** part of this baseline and must not be assumed:
- Musical state machine logic
- Threshold values
- MIDI clock interpretation semantics
- Visual pattern behavior
- Party Mode firmware logic

All of the above will be introduced after this baseline.

---

## 15. Baseline Status

✅ Hardware wiring validated
✅ Button noise eliminated
✅ LED PWM switching stable and silent
✅ DFPlayer audio stable under PWM load

**This document represents the authoritative ground truth for the Shimon installation prior to audio‑sync expansion.**
