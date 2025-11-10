# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a PlatformIO ESP32 project implementing a Simon Says memory game called "Shimon" for Midburn 2025. The game features 4 colored buttons (Red, Blue, Green, Yellow) and corresponding LEDs that play sequences for players to memorize and repeat.

## Architecture

### Hardware Setup
- **Platform**: ESP32 (espressif32)
- **Framework**: Arduino
- **Simulation**: Wokwi simulator support via `USE_WOKWI` build flag
- **Pin Configuration** (defined in include/shimon.h):
  - Buttons: RED=13, BLUE=21, GREEN=14, YELLOW=27 (INPUT_PULLUP)
  - Button LEDs: RED=23, BLUE=22, GREEN=32, YELLOW=33
  - Wing LED Strips: RED=19, BLUE=25, GREEN=18, YELLOW=26 (MOSFET gates)
  - Service LED: Pin 2 (heartbeat indicator)
  - DFPlayer: RX=16, TX=17

### Circuit Design
- **LED Circuit**: `ESP32 GPIO → 330Ω resistor → LED anode → LED cathode → GND`
- **Button Circuit**: `Button pin → ESP32 GPIO` with `INPUT_PULLUP` (other button pin to GND)
- **Current-limiting resistors**: 330Ω resistors are essential for LED protection

### Game Logic
The core game runs on a finite state machine (FSM) with these states:
- `IDLE`: Waiting for any button press to start, running ambient effects
- `INSTRUCTIONS`: Playing audio instructions
- `AWAIT_START`: Waiting for button press to begin game
- `SEQ_DISPLAY_INIT`: Initialize sequence display
- `SEQ_DISPLAY_MYTURN`: Playing "My Turn" audio announcement
- `SEQ_DISPLAY`: Show the sequence with LEDs and audio
- `SEQ_DISPLAY_YOURTURN`: Playing "Your Turn" audio announcement
- `SEQ_INPUT`: Wait for player input with timeout
- `CORRECT_FEEDBACK`: Playing correct sound, preparing next level
- `WRONG_FEEDBACK`: Playing wrong sound before game over
- `TIMEOUT_FEEDBACK`: Playing timeout sound before game over
- `GAME_OVER`: End game state
- `SCORE_DISPLAY`: Optional score announcement before returning to idle

### Audio System
**Dual Implementation:**
- **Simulation (`USE_WOKWI`)**: Audio calls print to Serial for debugging
- **Real Hardware**: Full DFPlayer Mini integration with MP3 playback

**Hardware Requirements:**
- DFPlayer Mini module
- MicroSD card with audio files (see file structure below)
- ESP32 UART1 connection: RX=16, TX=17
- Speaker/headphones connected to DFPlayer audio output

**Required SD Card File Structure and Audio Content:**

### **mp3 Directory (`/mp3/`) - Main Game Sounds**
```
mp3/
├── 0001.mp3    # Invite: "Come play with the butterfly!"
├── 0002.mp3    # Invite: "Test your memory skills!"
├── 0003.mp3    # Invite: "Ready for a challenge?"
├── 0004.mp3    # Invite: "The butterfly wants to play!"
├── 0005.mp3    # Invite: "Can you follow the pattern?"
├── 0006.mp3    # Instructions: "Watch the colors, then repeat the sequence"
├── 0007.mp3    # Timeout: "Time's up! Game over."
├── 0008.mp3    # Wrong: "Oops! That's not right."
├── 0009.mp3    # Game Over: "Game over! Thanks for playing!"
├── 0010.mp3    # Correct: "Great job! Next level!"
├── 0011.mp3    # My Turn: "My turn - watch carefully!"
└── 0012.mp3    # Your Turn: "Your turn - repeat the sequence!"
```

### **Folder 01 (`/01/`) - Color Names**
```
01/
├── 001.mp3     # "Red"
├── 002.mp3     # "Blue"
├── 003.mp3     # "Green"
└── 004.mp3     # "Yellow"
```
**Note**: These are used for the confuser mode where spoken color may differ from LED color.

### **Folder 02 (`/02/`) - Score Announcements**
```
02/
├── 000.mp3     # "Zero points"
├── 001.mp3     # "One point" 
├── 002.mp3     # "Two points"
├── 003.mp3     # "Three points"
├── 004.mp3     # "Four points"
├── 005.mp3     # "Five points"
├── 010.mp3     # "Ten points"
├── 015.mp3     # "Fifteen points"
├── 020.mp3     # "Twenty points"
├── 025.mp3     # "Twenty-five points"
├── 030.mp3     # "Thirty points"
├── 050.mp3     # "Fifty points"
├── 075.mp3     # "Seventy-five points"
└── 100.mp3     # "One hundred points! Excellent!"
```
**Note**: Create files for common score ranges. Game scores are level-based: Level 1 = 1 point, Level 5 = 5 points, etc.

### **Audio File Specifications:**
- **Format**: MP3, 16-bit, mono recommended for smaller file size
- **Sample Rate**: 22kHz or 44.1kHz 
- **Bitrate**: 128kbps recommended
- **Volume**: Normalize to consistent levels across all files
- **Length**: Keep files concise (1-3 seconds for most sounds)
- **Voice**: Clear, friendly tone suitable for all ages
- **Language**: Adjust color names and narration as needed for target audience

### **Optional Enhancements:**
```
mp3/
├── 0013.mp3    # Welcome: "Welcome to Butterfly Simon!"
├── 0014.mp3    # Confuser Toggle: "Confuser mode enabled"
├── 0015.mp3    # Confuser Toggle: "Confuser mode disabled"
├── 0016.mp3    # High Score: "New high score!"
├── 0017.mp3    # Background Music (low volume ambient)
└── 03/         # Alternative voice sets or languages
    ├── 001.mp3 # "Rouge" (French)
    ├── 002.mp3 # "Bleu" (French)
    ├── 003.mp3 # "Vert" (French)
    └── 004.mp3 # "Jaune" (French)
```

## Development Commands

### Build & Upload
```bash
# Build for simulation (Wokwi)
pio run -e sim

# Build for real hardware (with DFPlayer Mini)
pio run -e hardware

# Upload to Wokwi simulator
pio run -e sim -t upload

# Upload to real ESP32 hardware
pio run -e hardware -t upload

# Monitor serial output
pio device monitor -e sim      # For simulation
pio device monitor -e hardware # For real hardware
```

### Testing
```bash
# Build native unit tests (if any exist)
pio run -e native

# Run unit tests  
pio test -e native
```

### Wokwi Simulation
The project includes complete Wokwi simulation setup:
- `wokwi.toml`: Simulation configuration
- `diagram.json`: Virtual hardware layout with ESP32, 4 buttons, 4 LEDs with 330Ω resistors
- Firmware built to `.pio/build/sim/firmware.bin`

**Important Wokwi Connection Notes**:
- **Pin Naming**: ESP32 pins must be referenced as `esp:D19`, `esp:D18`, etc. (not `esp:19`)
- **GND References**: Use specific GND pins like `esp:GND.1`, `esp:GND.2` (not `esp:GND`)
- **Resistors Required**: LEDs need 330Ω current-limiting resistors to function properly
- **Pin Compatibility**: Some ESP32 pins may not work in Wokwi simulation:
  - Pin 17: Had connection issues in Wokwi, moved Yellow LED to pin 4
  - Pin 16: Also had issues, pin 4 works reliably
  - Pins 19, 18, 5: Work correctly for RGB LEDs
- **Troubleshooting Checklist**:
  - If no connection wires visible → Check pin naming (use `esp:Dxx` format)
  - If LEDs don't light → Verify resistors are added to circuit
  - If specific LED doesn't work → Try different GPIO pin
  - Always refresh Wokwi page completely after changing diagram.json
  
**Current Hardware Pin Assignments** (defined in include/shimon.h):
```
Button Inputs:  RED=13, BLUE=21, GREEN=14, YELLOW=27
Button LEDs:    RED=23, BLUE=22, GREEN=32, YELLOW=33
Wing LED Strips: RED=19, BLUE=25, GREEN=18, YELLOW=26
Service LED:    2 (onboard heartbeat LED)
DFPlayer Audio: RX=16, TX=17
```

## Game States and Audio-Visual Sequences

### **Boot Sequence (Setup)**
**Duration**: ~5-6 seconds  
**Audio**: `[AUDIO] DFPlayer initialized (simulation)`  
**Visual**: 
- Rainbow wave: LEDs light Red→Blue→Green→Yellow (3 cycles)
- Flash finale: All 4 LEDs flash together 4 times
- Serial: `"Playing boot LED sequence..." → "Boot sequence complete!"`

### **Idle Mode - Continuous Ambient Effects**
**Duration**: Indefinite (until player interaction)  
**Audio**: Periodic invites every 5 seconds (first), then 20-45 seconds  
**Visual**: 4 rotating ambient effects, each lasting 30 seconds:

1. **BREATHING (0-30s)**: All LEDs pulse gently together (sine wave, 100ms updates)
2. **SLOW_CHASE (30-60s)**: Single LED circles Red→Blue→Green→Yellow (800ms each)
3. **TWINKLE (60-90s)**: 1-2 random LEDs sparkle every 500ms
4. **PULSE_WAVE (90-120s)**: Dual wave patterns flow through LEDs (200ms updates)

**Debug Output**: `"IDLE: Invite in X seconds (Effect: N)"`

### **Invite Sequence** 
**Trigger**: Automatic timer expires  
**Duration**: ~3-4 seconds  
**Audio**: `[AUDIO] Playing invite X` (random 1-5)  
**Visual**:
- Double flash: All LEDs flash twice (200ms on/off)
- Spinning chase: 8 spins through colors (150ms each)  
- Final flash: All LEDs bright for 300ms
- Serial: `"Playing invite LED sequence..."`
- **Returns to ambient effects** after completion

### **Instructions Sequence**
**Trigger**: Any button press (except YELLOW in idle)  
**Duration**: ~2 seconds + 2 second pause  
**Audio**: `[AUDIO] Playing instructions (0006.mp3)`  
**Visual**:
- Alternating pairs: Red+Green ↔ Blue+Yellow (6 cycles, 300ms each)
- All LEDs OFF for 2 seconds
- Serial: `"Instructions playing..."`

### **Ready to Start State**
**Duration**: Indefinite (until button press)  
**Audio**: None  
**Visual**:
- Gentle pulsing: All LEDs pulse together (150ms updates, faster than breathing)
- Sine wave pattern with higher threshold for cleaner on/off
- Serial: `"Press any button to start game..."`

### **Game Start Confirmation**
**Trigger**: Any button press in ready state  
**Duration**: ~1 second  
**Audio**: None  
**Visual**:
- Triple burst: All LEDs flash 3 times (100ms on/off)
- Final flash: All LEDs bright for 200ms, then OFF
- Serial: `"Game starting visual confirmation!" + "Game starting!"`

### **Sequence Display Phase**
**Duration**: Variable (based on level and timing)  
**Sub-states**:

#### **"My Turn" Audio**
**Duration**: 1 second  
**Audio**: `[AUDIO] My Turn!`  
**Visual**: All LEDs OFF (preparation)

#### **LED Sequence Display**
**Duration**: `(CUE_ON_MS + CUE_GAP_MS) × level`  
**Audio**: `[AUDIO] Color name: [Color] (/01/00X.mp3)` (with confuser logic)  
**Visual**: 
- Each color LED lights for `CUE_ON_MS` (default 450ms, decreases each level)
- Gap of `CUE_GAP_MS` between cues (default 250ms, decreases each level)
- Serial: `"Step X: LED=Y, Voice=Z (CONFUSER!)"` if colors differ

#### **"Your Turn" Audio**
**Duration**: 1 second  
**Audio**: `[AUDIO] Your Turn!`  
**Visual**: All LEDs OFF

### **Player Input Phase**
**Duration**: Up to `INPUT_TIMEOUT_MS` (default 3000ms)  
**Audio**: None during input  
**Visual**: 
- Brief LED flash on correct button press
- All LEDs OFF otherwise
- Serial: `"Waiting for player input (timeout: Xms)"`

### **Feedback Sequences**

#### **Correct Input**
**Duration**: 1 second  
**Audio**: `[AUDIO] Correct! (0010.mp3)`  
**Visual**: Pressed LED stays on during feedback  
**Progression**: Sequence extends, speeds increase, next level begins

#### **Wrong Input**
**Duration**: 1 second + 2 seconds  
**Audio**: `[AUDIO] Wrong (0008.mp3)` → `[AUDIO] Game Over (0009.mp3)`  
**Visual**: All LEDs OFF  
**Result**: Game ends

#### **Input Timeout**
**Duration**: 1 second + 2 seconds  
**Audio**: `[AUDIO] Timeout -> Game Over (0007.mp3)` → `[AUDIO] Game Over (0009.mp3)`  
**Visual**: All LEDs OFF  
**Result**: Game ends

### **Game Over Sequence**
**Duration**: 2 seconds (+ optional score)  
**Audio**: `[AUDIO] Game Over (0009.mp3)`  
**Visual**: All LEDs OFF  
**If Score > 0**: Proceeds to Score Display  
**If Score = 0**: Returns directly to Idle Mode

### **Score Display** (Optional)
**Duration**: 2 seconds  
**Audio**: `[AUDIO] Score: X (/02/XXX.mp3)`  
**Visual**: All LEDs OFF  
**Result**: Returns to Idle Mode with ambient effects

## Key Configuration

### Game Tuning Parameters (src/main.cpp:21-32)
- `CUE_ON_MS_DEFAULT`: 450ms (min: 250ms) - LED on-time
- `CUE_GAP_MS_DEFAULT`: 250ms (min: 120ms) - Gap between cues  
- `INPUT_TIMEOUT_MS_DEFAULT`: 3000ms (min: 1800ms) - Player response limit
- `SPEED_STEP`: 0.97 - Acceleration factor applied every 3 levels for gradual difficulty progression
- `INVITE_INTERVAL`: 20-45 seconds (first invite: 5 seconds)
- `MAX_SAME_COLOR`: 2 - Maximum consecutive same colors
- `ENABLE_AUDIO_CONFUSER`: Toggle with YELLOW button in idle

### Build Environments

#### **`[env:sim]` - Wokwi Simulation**
- **Build Flag**: `-D USE_WOKWI`
- **Purpose**: Simulation with audio stubs printing to Serial
- **Dependencies**: None (uses built-in Arduino libraries)
- **Usage**: `pio run -e sim`

#### **`[env:hardware]` - Real Hardware**  
- **Build Flag**: None (disables `USE_WOKWI`)
- **Purpose**: Real ESP32 with DFPlayer Mini integration
- **Dependencies**: `dfrobot/DFRobotDFPlayerMini@^1.0.6`
- **Usage**: `pio run -e hardware`
- **Requires**: DFPlayer Mini + SD card with audio files

#### **`[env:native]` - Unit Testing**
- **Build Flag**: `-D UNIT_TESTS`  
- **Purpose**: Native unit tests on development machine
- **Usage**: `pio test -e native`

## Quick Reference for Testing

### **What to Expect in Wokwi Simulation:**
1. **Boot**: Rainbow wave + flashing (5-6 seconds)
2. **Idle**: Continuous ambient effects (breathing, chase, twinkle, waves)
3. **Every 5s (first) then 20-45s**: Invite sequences (double flash + spin)
4. **Press any button**: Instructions (alternating pairs) → Ready pulse
5. **Press again**: Game start burst → "My Turn" → LED sequence → "Your Turn" 
6. **Match sequence**: Correct feedback → next level (faster & longer)
7. **Wrong/Timeout**: Game over → back to idle with ambient

### **Special Controls:**
- **YELLOW button in IDLE**: Toggle confuser mode (LED flashes 6 times to confirm)
- **Serial Monitor**: Essential for understanding audio cues and game state
- **Service LED**: Always blinking (heartbeat indicator)

### **Testing Scenarios:**
- **Visual Engagement**: Wait and observe ambient effects cycling
- **Invite System**: Wait 5 seconds for first invite, then longer intervals
- **Confuser Mode**: Toggle with YELLOW, observe "CONFUSER!" in serial during sequence
- **Progression**: Complete several levels, notice increasing speed
- **Error Handling**: Test wrong buttons, timeouts, and game over sequences

## File Structure

- `src/main.cpp`: Main game logic, FSM, and all visual effects (690+ lines)
- `platformio.ini`: Build environments (sim + native testing)
- `wokwi.toml` + `diagram.json`: Wokwi simulation configuration with proper pin connections
- `CLAUDE.md`: This comprehensive documentation file
- Standard PlatformIO directories (`lib/`, `include/`, `test/`) currently unused