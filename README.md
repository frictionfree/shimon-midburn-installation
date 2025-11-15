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
| **LED – Blue**   | 23 | PWM (right header) |
| **LED – Red**    | 19 | PWM (right header) |
| **LED – Green**  | 18 | PWM (right header) |
| **LED – Yellow** | 5  | PWM (right header) |
| **Button – Blue (Input)**  | 21 | INPUT_PULLUP |
| **Button – Red (Input)**   | 13 | INPUT_PULLUP |
| **Button – Green (Input)** | 14 | INPUT_PULLUP |
| **Button – Yellow (Input)**| 27 | INPUT_PULLUP |
| **Button – Blue (LED)**  | 25 | Feedback LED (+ via 220–470 Ω) |
| **Button – Red (LED)**   | 26 | Feedback LED (+ via 220–470 Ω) |
| **Button – Green (LED)** | 32 | Feedback LED (+ via 220–470 Ω) |
| **Button – Yellow (LED)**| 33 | Feedback LED (+ via 220–470 Ω) |
| **DFPlayer RX/TX** | 16 / 17 | UART2 (TX2 → DF RX via 1 kΩ resistor) |

### Power & Protection Notes
- PSU powers LED strips through main fuse → +WAGO → LED + lines.  
- ESP32 logic powered via USB (5 V pin → logic rail).  
- Both breadboards share common ground (GND WAGO).  
- TVS diode (SA5.0A) across PSU +/– after fuse.  
- Add **PTC fuses** per LED + channel after successful basic tests.
---

## 🔊 Audio Assets

Place the following MP3 files on the DFPlayer’s SD card:

| Path | Purpose |
|------|----------|
| /mp3/0001.mp3 | Invitation 1 to Play (Idle) |
| /mp3/0002.mp3 | Invitation 2 to Play |
| /mp3/0003.mp3 | Invitation 3 to Play |
| /mp3/0004.mp3 | Invitation 4 to Play |
| /mp3/0005.mp3 | Invitation 5 to Play |
| /mp3/0006.mp3 | Game Instructions |
| /mp3/0007.mp3 | Announcement: “My Turn” |
| /mp3/0008.mp3 | Announcement: “Your Turn” |
| /mp3/0009.mp3 | Wrong Button Press |
| /mp3/0010.mp3 | Game Over |
| /mp3/0011.mp3 | Positive Feedback / Level Complete |
| /mp3/0012.mp3 | Timeout Notification |
| /01/001.mp3  | Color: Blue |
| /01/002.mp3  | Color: Red |
| /01/003.mp3  | Color: Green |
| /01/004.mp3  | Color: Yellow |
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
