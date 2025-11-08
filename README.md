# 🦋 Shimon – An Interactive Butterfly

**An immersive light, sound, and memory game installation by [Gal Triffon](#credits)**  
Inspired by the classic *Simon* game, reimagined as a large, responsive butterfly for the [Midburn Festival](https://midburn.org).

---

## 🎮 Concept

**Shimon** transforms the timeless color–sequence memory game into a multi-sensory art experience.  
Players interact with a 3.6 m–wide butterfly sculpture whose four wings glow with vivid LED colors — red, blue, green, and yellow.

When idle, Shimon sparkles gently and occasionally “invites” players with voice prompts.  
Once a button is pressed, the butterfly begins a *confuser* memory challenge —  
the lights and spoken color names intentionally mismatch, testing perception and memory.

Players must replicate the true color sequence using four illuminated buttons.  
Each correct round extends the sequence and speeds up the playback; a wrong press triggers a playful *Game Over*.

---

## ⚙️ Technical Overview

| Subsystem | Description |
|------------|--------------|
| **Controller** | ESP32 DevKitC-V4 (Arduino framework, 5 V / 3.3 V logic) |
| **Audio** | DFPlayer Mini MP3 module + VISATON K50WP waterproof 8 Ω speaker |
| **Lighting** | Four 5 V COB LED strips (Red, Blue, Green, Yellow), driven by IRLZ44N MOSFETs |
| **Input** | Four illuminated arcade buttons, each color-matched to its LED wing |
| **Power** | Mean Well LRS-100-5 PSU (5 V / 18 A) with fuse, TVS, Schottky, and PTC protection |
| **Enclosure** | IP65 polycarbonate box mounted behind the butterfly’s central column |
| **Framework** | [PlatformIO](https://platformio.org/) + Arduino core for ESP32 |
| **Board** | `board = esp32dev` (compatible with ESP32-DevKitC-V4) |

---

## 🧠 Firmware Architecture

The firmware implements a **non-blocking finite state machine (FSM)** controlling lighting, sound, and input logic.

**Main states:**
```
IDLE → INSTRUCTIONS → AWAIT_START
SEQ_DISPLAY_INIT → SEQ_DISPLAY → SEQ_INPUT
GAME_OVER → IDLE
```

**Key features:**
- Confuser mode (`ENABLE_AUDIO_CONFUSER`) mismatches spoken vs. shown colors.
- Adjustable timing: `CUE_ON_MS`, `CUE_GAP_MS`, `INPUT_TIMEOUT_MS`.
- PWM-driven LED channels (4 × 4 kHz).
- UART1 connection to DFPlayer (`TX = 17`, `RX = 16`).
- Debounced button inputs with internal pull-ups (`GPIO 13 – 21 – 14 – 33`).
- Illuminated arcade buttons with LED feedback on separate pins.

---

## 🧰 Hardware Pin Mapping

| Function | GPIO | Notes |
|-----------|------|-------|
| LED – Red | 19 | PWM channel 0 |
| LED – Blue | 25 | PWM channel 1 |
| LED – Green | 18 | PWM channel 2 |
| LED – Yellow | 26 | PWM channel 3 |
| Button – Red (Input) | 13 | Input_PULLUP |
| Button – Blue (Input) | 21 | Input_PULLUP |
| Button – Green (Input) | 14 | Input_PULLUP |
| Button – Yellow (Input) | 33 | Input_PULLUP |
| Button – Red (LED) | 23 | Button feedback LED |
| Button – Blue (LED) | 22 | Button feedback LED |
| Button – Green (LED) | 32 | Button feedback LED |
| Button – Yellow (LED) | 27 | Button feedback LED |
| DFPlayer TX/RX | 16 / 17 | UART1 |
| Service LED | 22 | On-board indicator |

---

## 🔊 Audio Assets

Place the following MP3 files on the DFPlayer’s SD card:

| Folder | Files | Purpose |
|---------|--------|----------|
| `/mp3/` | `0001–0005.mp3` | Invite phrases |
| `/mp3/0006.mp3` | Instructions |
| `/mp3/0007.mp3` | Timeout → Game Over |
| `/mp3/0008.mp3` | Wrong |
| `/mp3/0009.mp3` | Game Over |
| `/mp3/0010.mp3` | Correct |
| `/01/001–004.mp3` | Spoken color names |
| `/02/000–100.mp3` | Optional numeric scoring |

---

## 🛠️ Development & Simulation

You can develop and test the firmware **without hardware** using **PlatformIO** and the **Wokwi Simulator**.

### 🧩 Quickstart (macOS)

```bash
brew install platformio
git clone https://github.com/<your-username>/shimon-midburn.git
cd shimon-midburn
code .
```

**Build and simulate:**

```bash
pio run -e sim
```

Then in VS Code → **Wokwi: Start Simulation** to visualize LEDs and button inputs.  
To always show serial output in Wokwi (web or VS Code), add this to your `diagram.json`:

```json
"serialMonitor": { "display": "always" }
```

---

## 🧾 Bill of Materials (Highlights)

- ESP32-DevKitC-V4 (Digi-Key # 1965-ESP32-DEVKITC-32E-ND)  
- DFPlayer Mini (DFR0299)  
- VISATON K50WP Speaker  
- Mean Well LRS-100-5 PSU  
- IRLZ44N MOSFET × 4 (+ heatsinks)  
- KXZM COB LED strips (5 V IP65)  
- 4 × Illuminated arcade buttons  
- WAGO connectors / IDEC terminal blocks  
- PTC fuses and Schottky diodes  

(Full BOM available in [`docs/Shimon.pdf`](docs/Shimon.pdf).)

---

## 🧱 Physical Design

- **Wings:** 3.6 m span × 2.6 m height  
- **Wing colors:**  
  - Green – top-left  
  - Yellow – top-right  
  - Red – bottom-left  
  - Blue – bottom-right  
- **Speaker:** Center-front mount  
- **Electronics:** Rear-mounted IP65 enclosure, 1.2 m above base  

---

## 💡 License

This project is released under the **MIT License**.  
See the [`LICENSE`](LICENSE) file for details.

---

## 👤 Credits

- **Artist & Concept:** Gal Triffon  
- **Firmware Development:** Yossi Attas  
- **Sound Design:** TBD  
- **Mechanical Design:** TBD  
- **Built for:** [Midburn Festival 2025](https://midburn.org)
