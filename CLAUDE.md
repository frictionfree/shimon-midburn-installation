# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a PlatformIO ESP32 project implementing a Simon Says memory game called "Shimon" for Midburn 2025. The game features 4 colored buttons (Red, Blue, Green, Yellow) and corresponding LEDs that play sequences for players to memorize and repeat.

## Architecture

### Hardware Setup
- **Platform**: ESP32 DevKitC-V4 (espressif32)
- **Framework**: Arduino
- **Simulation**: Wokwi simulator support via `USE_WOKWI` build flag
- **Pin Configuration** (include/shimon.h):
  - **LED Strips (MOSFET gates)**: BLUE=23, RED=19, GREEN=18, YELLOW=5 (all on right header)
  - **Button Inputs**: BLUE=21, RED=13, GREEN=14, YELLOW=27 (INPUT_PULLUP, connected to GND)
  - **Button LEDs**: BLUE=25, RED=26, GREEN=32, YELLOW=33 (left-side pins, 220-470Ω resistors)
  - **DFPlayer Mini**: RX2=16, TX2=17 (Serial2)
  - **Service LED**: Pin 2 (heartbeat indicator)

### Circuit Design
- **LED Strip Circuit (MOSFET-driven)**:
  - `ESP32 GPIO → 330Ω resistor → MOSFET gate`
  - `10kΩ resistor: MOSFET gate → GND` (pull-down)
  - `MOSFET drain → LED strip "–" (cathode)`
  - `LED strip "+" (anode) → +5V PSU rail`
- **Button Input Circuit**:
  - `Button switch → ESP32 GPIO (INPUT_PULLUP)`
  - `Button other pin → GND`
- **Button LED Circuit**:
  - `ESP32 GPIO → 220-470Ω resistor → LED+ (anode)`
  - `LED– (cathode) → GND`
- **Power & Protection**:
  - Common ground shared between ESP32, PSU, and DFPlayer
  - TVS diode (SA5.0A) across +5V/GND after main fuse
  - Per-channel PTC fuses on LED+ lines (add after testing)

### Game Logic
The core game runs on a finite state machine (FSM) with these states:
- `IDLE`: Waiting for any button press to start, with ambient visual effects
- `INSTRUCTIONS`: Playing audio instructions
- `AWAIT_START`: Waiting for button press to begin game
- `SEQ_DISPLAY_INIT`: Initialize new sequence
- `SEQ_DISPLAY_MYTURN`: Playing "My Turn" audio
- `SEQ_DISPLAY`: Show the sequence with LEDs and audio
- `SEQ_DISPLAY_YOURTURN`: Playing "Your Turn" audio
- `SEQ_INPUT`: Wait for player input with timeout
- `CORRECT_FEEDBACK`: Playing correct feedback sound
- `WRONG_FEEDBACK`: Playing wrong input sound
- `TIMEOUT_FEEDBACK`: Playing timeout notification
- `GAME_OVER`: Personalized game over message (based on difficulty and score)
- `GENERAL_GAME_OVER`: General game over message (always plays after personalized)
- `POST_GAME_INVITE`: Invite player to play again after game over

### Audio System
**Dual Implementation:**
- **Simulation (`USE_WOKWI`)**: Audio calls print to Serial for debugging
- **Real Hardware**: Full DFPlayer Mini integration with MP3 playback

**Audio Finish Detection System:**
The game uses DFPlayer's `DFPlayerPlayFinished` event to detect when audio completes:
- `audioFinished` global flag set when DFPlayer reports playback complete (src/main.cpp:32, 192)
- `isAudioComplete()` helper function checks flag OR uses timeout as fallback (src/main.cpp:260-279)
- All audio-playing states wait for actual completion instead of fixed timeouts
- Timeout definitions in shimon.h remain as fallbacks for safety
- Prevents audio cutoff issues and enables responsive timing

**Hardware Requirements:**
- DFPlayer Mini module
- MicroSD card with audio files (see file structure below)
- ESP32 UART1 connection: RX=16, TX=17
- Speaker/headphones connected to DFPlayer audio output

**Required SD Card File Structure and Audio Content:**

### **mp3 Directory (`/mp3/`) - All Game Sounds**
All audio files are now in the `/mp3/` directory for simplicity.

```
mp3/
├── 0001-0005.mp3  # Invitation messages (5 variations to play when idle)
│   ├── 0001.mp3   # "Come play with me!"
│   ├── 0002.mp3   # "Want to test your memory?"
│   ├── 0003.mp3   # "Press any button to begin!"
│   ├── 0004.mp3   # "Let's play Simon Says!"
│   └── 0005.mp3   # "Can you remember the pattern?"
│
├── 0011.mp3       # Main game instructions with difficulty selection explanation
├── 0012.mp3       # Blue/Novice difficulty instructions
├── 0013.mp3       # Red/Intermediate difficulty instructions
├── 0014.mp3       # Green/Advanced difficulty instructions
├── 0015.mp3       # Yellow/Pro difficulty instructions
│
├── 0021-0025.mp3  # "My Turn" variations (5 different announcements)
│   ├── 0021.mp3   # "My turn!"
│   ├── 0022.mp3   # "Watch carefully!"
│   ├── 0023.mp3   # "Pay attention!"
│   ├── 0024.mp3   # "Here's the sequence!"
│   └── 0025.mp3   # "Follow along!"
│
├── 0031-0035.mp3  # "Your Turn" variations (5 different announcements)
│   ├── 0031.mp3   # "Your turn!"
│   ├── 0032.mp3   # "Now you try!"
│   ├── 0033.mp3   # "Can you repeat it?"
│   ├── 0034.mp3   # "Show me what you remember!"
│   └── 0035.mp3   # "Go ahead!"
│
├── 0041-0045.mp3  # Positive feedback variations (5 different celebrations)
│   ├── 0041.mp3   # "Correct! Well done!"
│   ├── 0042.mp3   # "Excellent memory!"
│   ├── 0043.mp3   # "Perfect! Keep going!"
│   ├── 0044.mp3   # "You got it!"
│   └── 0045.mp3   # "Great job!"
│
├── 0051.mp3       # Wrong button press ("Oops! Wrong button")
├── 0052.mp3       # Timeout notification ("Too slow! Time's up")
│
├── 0053-0058.mp3  # Game over messages (personalized + general)
│   ├── 0053.mp3   # Novice strong scorer (score >= 8): "Ready for Intermediate!"
│   ├── 0054.mp3   # Intermediate strong scorer (score >= 8): "Move up to Advanced!"
│   ├── 0055.mp3   # Advanced strong scorer (score >= 10): "Join finals Friday!"
│   ├── 0056.mp3   # Pro strong scorer (score >= 10): "Memory master! See you at finals!"
│   ├── 0057.mp3   # Mediocre scorer (below threshold): "Good try! Practice makes perfect!"
│   └── 0058.mp3   # General game over (plays after personalized): "Thanks for playing!"
│
├── 0061-0064.mp3  # Color names for sequence display
│   ├── 0061.mp3   # "Red"
│   ├── 0062.mp3   # "Blue"
│   ├── 0063.mp3   # "Green"
│   └── 0064.mp3   # "Yellow"
│
├── 0065-0068.mp3  # Color names for button feedback (can be same as 61-64)
│   ├── 0065.mp3   # "Red"
│   ├── 0066.mp3   # "Blue"
│   ├── 0067.mp3   # "Green"
│   └── 0068.mp3   # "Yellow"
│
└── 0070-0170.mp3  # Score announcements (base 70 + score value)
    ├── 0070.mp3   # "Zero points"
    ├── 0071.mp3   # "One point"
    ├── 0072.mp3   # "Two points"
    ├── 0075.mp3   # "Five points"
    ├── 0080.mp3   # "Ten points"
    ├── 0085.mp3   # "Fifteen points"
    ├── 0090.mp3   # "Twenty points"
    └── 0170.mp3   # "One hundred points! Amazing!"
```

**Notes**:
- All files are in `/mp3/` directory (no `/01/` or `/02/` subfolders)
- Game uses variations for "My Turn", "Your Turn", and positive feedback to reduce repetition
- **Personalized game over messages** (0053-0058): System evaluates difficulty level and score, then plays appropriate message
  - Strong performers encouraged to advance to next difficulty level
  - Top performers (Advanced/Pro) invited to finals on Friday
  - General game over message (0058) always plays after personalized message
- Score files (0070-0170) reserved for future use (not currently announced in-game)

### **Audio File Specifications:**
- **Format**: MP3, 16-bit, mono recommended for smaller file size
- **Sample Rate**: 22kHz or 44.1kHz
- **Bitrate**: 128kbps recommended
- **Volume**: Normalize to consistent levels across all files
- **Length**: Keep files concise (1-3 seconds for most sounds)
- **Voice**: Clear, friendly tone suitable for all ages
- **Language**: Hebrew recommended for Midburn audience

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
- **Pin Compatibility**: Some ESP32 pins may not work in Wokwi simulation
- **Troubleshooting Checklist**:
  - If no connection wires visible → Check pin naming (use `esp:Dxx` format)
  - If LEDs don't light → Verify resistors are added to circuit
  - If specific LED doesn't work → Try different GPIO pin
  - Always refresh Wokwi page completely after changing diagram.json

**Hardware Pin Assignments** (from include/shimon.h):
```
LED Strips (MOSFET gates): BLUE=D23, RED=D19, GREEN=D18, YELLOW=D5
Button Inputs:             BLUE=D21, RED=D13, GREEN=D14, YELLOW=D27
Button LEDs:               BLUE=D25, RED=D26, GREEN=D32, YELLOW=D33
DFPlayer Mini:             RX2=D16, TX2=D17 (Serial2)
Service LED:               D2 (heartbeat)
```

**Note**: Wokwi simulation may use simplified pin assignments. Always refer to `include/shimon.h` for the authoritative hardware configuration.

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
- Wing LED flashes briefly on button press
- **Button LED stays ON while button is physically held** (src/main.cpp:915, 927-937)
- Button LED turns OFF when button is released
- All LEDs OFF otherwise
- Serial: `"Waiting for player input (timeout: Xms)"`

**Button LED Behavior**: The illuminated button LEDs now stay lit while buttons are pressed, providing tactile feedback to players. This was implemented to fix the issue where button LEDs were only flashing briefly.

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
**Duration**: Wait for audio completion (with 6000ms timeout fallback)
**Audio Phase 1**: Personalized message based on difficulty and score:
- Novice (score ≥ 8): `[AUDIO] 0053.mp3` - "Ready for Intermediate!"
- Intermediate (score ≥ 8): `[AUDIO] 0054.mp3` - "Move up to Advanced!"
- Advanced (score ≥ 10): `[AUDIO] 0055.mp3` - "Join finals Friday!"
- Pro (score ≥ 10): `[AUDIO] 0056.mp3` - "Memory master! See you at finals!"
- Below threshold: `[AUDIO] 0057.mp3` - "Good try! Practice makes perfect!"

**Delay**: 300ms between personalized and general message

**Audio Phase 2**: General game over message
- `[AUDIO] 0058.mp3` - "Thanks for playing!"

**Visual**: All LEDs OFF
**Result**: Proceeds to General Game Over → Post-Game Invite

### **General Game Over**
**Duration**: Wait for audio completion (with 6000ms timeout fallback)
**Delay**: 6000ms pause after audio completes (let message sink in)
**Result**: Proceeds to Post-Game Invite

### **Post-Game Invite**
**Duration**: ~3-4 seconds
**Audio**: `[AUDIO] Playing invite X` (random 1-5) - Same as idle invites
**Visual**:
- Double flash: All LEDs flash twice (200ms on/off)
- Spinning chase: 8 spins through colors (150ms each)
- Final flash: All LEDs bright for 300ms
- Serial: `"Post-game invite: encouraging player to play again"`
**Result**: Returns to Idle Mode with ambient effects

**Note**: The post-game invite was added to provide closure and encourage players to play again, rather than silently returning to idle mode.

## Key Configuration

### Game Tuning Parameters (include/shimon.h)
- `CUE_ON_MS_DEFAULT`: 450ms (min: 250ms) - LED on-time
- `CUE_GAP_MS_DEFAULT`: 250ms (min: 120ms) - Gap between cues
- `INPUT_TIMEOUT_MS_DEFAULT`: 3000ms (min: 1800ms) - Player response limit
- `SPEED_STEP`: 0.97 - Acceleration factor applied every 3 levels for gradual difficulty progression
- `INVITE_INTERVAL_MIN_SEC` / `MAX_SEC`: 20-45 seconds (first invite: 5 seconds)
- `MAX_SAME_COLOR`: 2 - Maximum consecutive same colors
- `ENABLE_AUDIO_CONFUSER`: Toggle with YELLOW button in idle (runtime configurable)

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

## Recent Changes & Implementation Notes

### Latest Updates (November 2025)
**Branch**: `feat/logic-change`

1. **Audio Finish Detection System** (Commits: 11095ad, latest)
   - Implemented DFPlayer `DFPlayerPlayFinished` event detection
   - Added `audioFinished` flag and `isAudioComplete()` helper function
   - Fixed audio cutoff issues by waiting for actual playback completion
   - Timeout fallbacks remain for safety

2. **Post-Game Invite Feature**
   - Added `POST_GAME_INVITE` state to FSM
   - Plays invite message after score display to encourage replay
   - Provides better player engagement and clearer game flow

3. **Button LED Behavior Fix**
   - Button LEDs now stay ON while buttons are physically held
   - Provides tactile feedback during gameplay
   - Fixed issue where button LEDs only flashed briefly

4. **Audio Timing Improvements**
   - Increased feedback duration from 2000ms to 2800ms
   - Increased game over duration from 2500ms to 3500ms
   - Added delays between audio transitions (250ms, 200ms)
   - Tuned for DFPlayer Mini hardware behavior

5. **Personalized Game Over Messages** (Latest)
   - Added difficulty-based and score-based game over messages (0053-0058.mp3)
   - Strong scorers encouraged to advance: Novice/Intermediate (≥8), Advanced/Pro (≥10)
   - Top performers invited to finals on Friday afternoon
   - Two-phase game over: Personalized message → General message (0058.mp3)
   - Removed SCORE_DISPLAY state (no individual score announcements)
   - Added GENERAL_GAME_OVER state with 6-second pause before invite
   - Score thresholds: Novice/Int ≥8, Advanced/Pro ≥10

### Known Issues (Resolved)
- ✅ Audio messages getting cut off (fixed with finish detection)
- ✅ No post-game prompt (fixed with POST_GAME_INVITE state)
- ✅ Button LEDs flashing instead of staying on (fixed with hold detection)

### PlatformIO Access
PlatformIO can be accessed at: `~/.platformio/penv/Scripts/pio.exe`
Or add to system PATH: `C:\Users\galtr\.platformio\penv\Scripts`

## File Structure

- `src/main.cpp`: Main game logic, FSM, and all visual effects (1050+ lines)
- `include/shimon.h`: Configuration header with all tunable parameters
- `platformio.ini`: Build environments (sim, hardware, native testing)
- `wokwi.toml` + `diagram.json`: Wokwi simulation configuration with proper pin connections
- `CLAUDE.md`: This comprehensive documentation file
- Standard PlatformIO directories (`lib/`, `test/`) currently unused