# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Workflow

**IMPORTANT**: Before suggesting code changes:
1. **Explain the plan** - Describe what you intend to change and why
2. **Wait for approval** - User verifies the approach is correct
3. **Implement changes** - Make the approved code modifications
4. **Update documentation** - Update relevant requirement `.md` files to reflect changes

This ensures alignment before coding and keeps documentation in sync with implementation.

## Project Overview

This is a PlatformIO ESP32 project implementing a Simon Says memory game called "Shimon" for Midburn 2025. The game features 4 colored buttons (Red, Blue, Green, Yellow) and corresponding LEDs that play sequences for players to memorize and repeat.

## Architecture

### Hardware Setup
- **Platform**: ESP32 DevKitC-V4 (espressif32)
- **Framework**: Arduino
- **Pin Configuration** (`include/shimon.h` is the single source of truth for all pin/hw constants):
  - **LED Strips (MOSFET gates)**: BLUE=23, RED=19, GREEN=18, YELLOW=5
  - **Button Inputs**: BLUE=26, RED=13, GREEN=14, YELLOW=27 (INPUT_PULLUP; BLUE: GPIO21→32 Feb 2026, GPIO32→26 Mar 2026)
  - **Button LEDs**: ~~BLUE=25, RED=26, GREEN=32, YELLOW=33~~ **HARDWARE-CONTROLLED** (hardwired to mirror LED strips; GPIO26=BTN_BLUE, GPIO32/33 now used for I2S)
  - **I2S audio input**: BCLK=33, LRCK=25, DATA=32 (BCLK: GPIO26→33; DATA: GPIO22→32, Mar 2026)
  - **MIDI clock input**: RX=34, 31250 bps (UART1)
  - **DFPlayer Mini**: RX=16, TX=17 (Serial2)
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
  - **Hardware conditioning**: 10kΩ pull-up to 3.3V + 100nF capacitor to GND (eliminates ghost presses)
- **Button LED Circuit** (HARDWARE-CONTROLLED):
  - Button LEDs (12V) are **hardwired to mirror LED strips** via MOSFET channels
  - Button LED anode → +12V
  - Button LED cathode → Same MOSFET drain as corresponding LED strip
  - **Result**: Button LEDs automatically turn on/off when LED strips do (no firmware control needed)
  - **GPIOs 25, 26, 32, 33 are UNUSED/RESERVED** (previously assigned for button LEDs)
- **Power & Protection**:
  - Common ground shared between ESP32, PSU, and DFPlayer
  - TVS diode (SA5.0A) across +5V/GND after main fuse
  - Per-channel PTC fuses on LED+ lines (add after testing)

### Hardware Constraints (Critical - From As-Built Baseline)

**IMPORTANT**: These constraints are validated in hardware and **must be respected** in all firmware:

#### PWM Minimum Duty Limitation
- **Issue**: IRF540N MOSFETs driven at 3.3V gate voltage do not conduct linearly at very low PWM duty
- **Minimum Effective Duty**: ~70/255 (empirically validated)
- **Firmware Requirement**:
  - On button press or LED activation, **immediately jump PWM duty to minimum visible value** (70/255 or higher)
  - After initial jump, PWM may ramp linearly to full brightness
  - **Do NOT ramp from 0** - LEDs will remain dark or flicker inconsistently
  - Fade-out to zero duty does not require minimum clamp
- **Purpose**: Guarantees instant visual feedback on short button presses
- **Configuration**: See `PWM_MIN_EFFECTIVE_DUTY` in `shimon.h`

#### Power Budget Constraint
- **LED PSU**: 12V / 100W (~8.3A max continuous)
- **Per-wing current**: ~3A at full brightness
- **Constraint**: Sustained full-brightness operation of all four wings simultaneously is **not allowed**
- **Firmware Requirement**:
  - Any simultaneous multi-wing write must use `hw_led_all_set()` (shared HAL)
  - `hw_led_all_set()` enforces `HW_GLOBAL_DUTY_CAP = 320`: if sum of all 4 duties exceeds 320, all are scaled proportionally
  - Single-wing writes via `hw_led_duty()` are uncapped (safe by construction)
- **Configuration**: `HW_GLOBAL_DUTY_CAP = 320` in `shimon.h`; enforced by `hw_led_all_set()` in `hw.cpp`

#### PWM Configuration
- **Method**: `analogWrite()` / ESP32 LEDC backend
- **Stable Operating Envelope**:
  - Frequency: 12-12.5 kHz
  - Resolution: 8-bit (0-255)
- **Note**: Higher frequency + resolution combinations (e.g., 25 kHz @ 12-bit) are unstable or non-functional

#### Button LED Behavior
- **Hardware Controlled**: Button LEDs automatically mirror LED strip state (firmware has no direct control)
- **Implication**: All `setBtnLed()` calls in code are **ineffective** (hardware ignores GPIO 25, 26, 32, 33)
- **Visual Feedback**: Button LEDs provide automatic feedback when:
  - LED strips light during sequence display
  - Wing LEDs flash on button press (if pattern includes wing LED)
  - Any visual pattern activates corresponding LED strip

**Reference**: See `hardware-baseline.md` for complete as-built hardware documentation.

### Game Logic
The core game runs on a finite state machine (FSM) with these states:
- `IDLE`: Waiting for any button press to start, with ambient visual effects
- `INSTRUCTIONS`: Playing audio instructions (button press skips and selects difficulty)
- `DIFFICULTY_SELECTION`: Clockwise LED rotation, waiting for a difficulty button
- `DIFFICULTY_INSTRUCTIONS`: Playing difficulty-specific instructions (button press skips to game start)
- `AWAIT_START`: Waiting for button press to begin game
- `SEQ_DISPLAY_INIT`: Initialize new sequence
- `SEQ_DISPLAY_MYTURN`: Playing "My Turn" audio
- `SEQ_DISPLAY`: Show the sequence with LEDs and audio (button presses ignored)
- `SEQ_DISPLAY_YOURTURN`: Unused — "Your Turn" plays in the background inside `SEQ_INPUT`
- `SEQ_INPUT`: Input open from the last cue's LED-off; first-press timeout starts after "Your Turn" finishes
- `CORRECT_FEEDBACK`: Playing correct feedback sound
- `WRONG_FEEDBACK`: Playing wrong input sound
- `TIMEOUT_FEEDBACK`: Playing timeout notification
- `GAME_OVER`: Personalized game over message (based on difficulty and score)
- `GENERAL_GAME_OVER`: General game over message (always plays after personalized)
- `POST_GAME_INVITE`: Invite player to play again after game over

### Audio System
**Implementation**: DFPlayer Mini with MP3 playback (hardware only — the Wokwi simulation build was removed Oct 2026)

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
│   ├── 0061.mp3   # "Blue"
│   ├── 0062.mp3   # "Red"
│   ├── 0063.mp3   # "Green"
│   └── 0064.mp3   # "Yellow"
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
# Build
pio run -e hardware

# Upload to ESP32 (COM3; use -e hardware-com6 for COM6)
pio run -e hardware -t upload

# Monitor serial output
pio device monitor -e hardware
```

**Hardware Pin Assignments** (from include/shimon.h):
```
LED Strips (MOSFET gates): BLUE=D23, RED=D19, GREEN=D18, YELLOW=D5
Button Inputs:             BLUE=D26, RED=D13, GREEN=D14, YELLOW=D27
I2S audio input:           BCLK=D33, LRCK=D25, DATA=D32
MIDI clock input:          RX=D34 (UART1, 31250 bps)
DFPlayer Mini:             RX2=D16, TX2=D17 (Serial2)
Service LED:               D2 (heartbeat)
```

## Game States and Audio-Visual Sequences

### **Boot Sequence**

#### Common (`setup()`) — always runs at power-on
**Duration**: ~0 ms (no LED pattern)
**Serial**: `=== Shimon v1.0 ===`, `[BOOT] Reset reason=N ...`, then `[BOOT] Auto-start: Game Mode.` (or `[BOOT] Yellow held: entering mode selection.`)
**Visual**: None — LED sweep removed; mode-specific splashes handle visual confirmation.
**Event branch**: Auto-boots to Game Mode after a stuck-button check; Mode Selection only if Yellow is held at boot. See `GAME_ONLY_REQUIREMENTS.md §2`.

#### Game Mode splash (`game_init()` → `bootSequence()`)
**Duration**: ~3 seconds
**Audio**: None (DFPlayer initialized)
**Visual**:
- Rainbow wave: LEDs light Red→Blue→Green→Yellow (3 cycles)
- Flash finale: All 4 LEDs flash together 4 times
- Serial: `"Playing boot LED sequence..." → "Boot sequence complete!"`

### **Idle Mode - Continuous Ambient Effects**
**Duration**: Indefinite (until player interaction)  
**Audio**: First invite immediately after the boot splash, then every 20-45 seconds (random — avoids a mechanical, predictable rhythm)  
**Visual**: 4 rotating ambient effects, each lasting 30 seconds:

1. **BREATHING (0-30s)**: All LEDs pulse gently together (sine wave, 100ms updates)
2. **SLOW_CHASE (30-60s)**: Single LED circles Red→Blue→Green→Yellow (800ms each)
3. **TWINKLE (60-90s)**: 1-2 random LEDs sparkle every 500ms
4. **PULSE_WAVE (90-120s)**: Dual wave patterns flow through LEDs (200ms updates)

**Debug Output**: `"IDLE: Invite in X seconds (Effect: N)"`

### **Invite Sequence** 
**Trigger**: Automatic timer expires  
**Audio**: `[AUDIO] Invite N -> /mp3/000N.mp3` (random 1-5, no back-to-back repeat)  
**Visual**: None — ambient effects continue (blocking invite LED pattern removed; it blocked input for ~3 s)  
**Next**: `Next invite scheduled in NN seconds` (20-45 s)

### **Instructions Sequence**
**Trigger**: Any button press in idle  
**Audio**: `[AUDIO] Instructions -> /mp3/0011.mp3`  
**Visual**: None (blocking pattern removed)  
**Skip**: A button press during instructions selects that difficulty directly  

### **Difficulty Selection / Instructions**
**Visual**: Clockwise LED rotation (400 ms per wing) while waiting for a choice  
**Audio**: `0012`-`0015` (Blue/Red/Green/Yellow difficulty instructions); a button press skips straight to game start

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

**Input**: Button presses during the display are ignored. Input opens the moment the last cue's LED turns off (no trailing gap).

#### **"Your Turn" Audio**
**Audio**: `0031`-`0035` variation, started one `CUE_GAP_MS` after the last LED-off (so it doesn't cut the last color name); skipped if the player already pressed  
**Visual**: All LEDs OFF

### **Player Input Phase**
**Duration**: Up to `INPUT_TIMEOUT_MS` (default 3000ms) per press — first press measured from the end of "Your Turn", later presses from the previous release
**Audio**: "Your Turn" (background, at start only)
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
**Audio**: Positive feedback variation (`0041`-`0045`)  
**Visual**: Pressed LED stays on during feedback  
**Progression**: Sequence extends, speeds increase, next level begins

#### **Wrong Input**
**Duration**: 1 second + 2 seconds  
**Audio**: `0051.mp3` (wrong) → personalized game over  
**Visual**: All LEDs OFF  
**Result**: Game ends

#### **Input Timeout**
**Duration**: 1 second + 2 seconds  
**Audio**: `0052.mp3` (timeout) → personalized game over  
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

**Transition**: General message starts as soon as the personalized one finishes (a button press skips ahead)

**Audio Phase 2**: General game over message
- `[AUDIO] 0058.mp3` - "Thanks for playing!"

**Visual**: All LEDs OFF
**Result**: Proceeds to General Game Over → Post-Game Invite

### **General Game Over**
**Duration**: Wait for audio completion (fallback timeout inside `play*()`)
**Result**: Proceeds to Post-Game Invite

### **Post-Game Invite**
**Cooldown**: 2500 ms silent pause, then the invite plays (button press during cooldown → straight to IDLE)
**Audio**: Random invite (1-5) — same as idle invites
**Visual**: None (blocking pattern removed)
**Result**: Returns to Idle Mode; next invite in 20-45 s

**Note**: The post-game invite was added to provide closure and encourage players to play again, rather than silently returning to idle mode.

## Key Configuration

### Game Tuning Parameters (include/shimon.h)
- `CUE_ON_MS_DEFAULT`: 450ms (min: 250ms) - LED on-time
- `CUE_GAP_MS_DEFAULT`: 250ms (min: 120ms) - Gap between cues
- `INPUT_TIMEOUT_MS_DEFAULT`: 3000ms (min: 1800ms) - Player response limit
- `SEQUENCE_SPEED_STEP` / `INPUT_SPEED_STEP`: 0.97 - Acceleration factor applied every 3 levels (same for all difficulties)
- `INVITE_INTERVAL_MIN_SEC` / `MAX_SEC`: 20-45 seconds random (first invite: immediate after boot)
- `MAX_SAME_COLOR`: 2 - Maximum consecutive same colors
- `ENABLE_AUDIO_CONFUSER`: Configured automatically based on difficulty level (Intermediate/Red only — moves to Advanced/Green after the planned level swap)
- `HW_BTN_GHOST_MS_FAST` / `STANDARD`: 20 / 15 ms debounce hold
- `YELLOW_EXIT_HOLD_MS`: 3000 ms universal exit

### Build Environments

#### **`[env:hardware]` - Real Hardware**  
- **Purpose**: Real ESP32 with DFPlayer Mini integration
- **Dependencies**: `dfrobot/DFRobotDFPlayerMini@^1.0.6`
- **Usage**: `pio run -e hardware`
- **Requires**: DFPlayer Mini + SD card with audio files

#### **`[env:hardware-com6]` - Real Hardware on COM6**
- Same as `hardware` but `upload_port = COM6` (`hardware` uses COM3)
- **Usage**: `pio run -e hardware-com6 -t upload`

## Quick Reference for Testing

### **What to Expect on Hardware:**
1. **Boot**: Rainbow wave + flashing (~3 seconds)
2. **Idle**: Continuous ambient effects (breathing, chase, twinkle, waves)
3. **Immediately, then every 20-45s**: Invite audio (ambient effects continue)
4. **Press any button**: Instructions → difficulty selection (clockwise rotation) → difficulty instructions → Ready pulse
5. **Press again**: Game start burst → "My Turn" → LED sequence → "Your Turn" 
6. **Match sequence**: Correct feedback → next level (faster & longer)
7. **Wrong/Timeout**: Game over → back to idle with ambient

### **Special Controls:**
- **Serial Monitor**: Essential for understanding audio cues and game state
- **Service LED**: Always blinking (heartbeat indicator)
- **Difficulty Selection**: Press any colored button during difficulty selection to choose game mode

### **Testing Scenarios:**
- **Visual Engagement**: Wait and observe ambient effects cycling
- **Invite System**: First invite right after boot; serial should then show `Next invite scheduled in NN seconds` (20-45) with no invite before that
- **Difficulty Levels**: Test all 4 difficulties (Blue/Red/Green/Yellow) for different game mechanics
- **Confuser Mode**: Play Intermediate (Red) difficulty to see voice mismatch with LED
- **Progression**: Complete several levels, notice increasing speed every 3 levels
- **Error Handling**: Test wrong buttons, timeouts, and game over sequences

## Recent Changes & Implementation Notes

### Midburn Event Build (October 2026, Branch: `event/midburn-oct-2026`)

Full details in `GAME_ONLY_REQUIREMENTS.md` (authoritative for this branch).

- **Game-only build**: Party Mode removed. Modes: Game (default), Diagnostic, Stress Test.
- **Auto-boot to Game Mode**: Mode Selection only when Yellow is held at boot (RTC boot-intent flag dropped — unreliable on this chip). Mode menu: Blue=Game, Red=Diagnostic, Yellow=Stress.
- **Universal exit**: Yellow held **3 s** (`YELLOW_EXIT_HOLD_MS`); keep holding through reboot for Mode Selection.
- **Passive Mode**: stuck button at boot / DFPlayer init fail / DFPlayer unresponsive → RED breathing, auto-restart every 5 min (max 3). Ghost-storm trigger exists but is disabled.
- **Uptime logging**: `[SYS] Uptime` every 5 min; passive entry logs uptime.
- **Debounce fix**: `HW_BTN_GHOST_MS_FAST` 0 → 20 ms; `SEQ_INPUT` release uses `hw_btn_pressed()` (fixed false edge after release).
- **Diagnostic Mode**: interactive tester — press = LED + color audio; logs PRESS / RELEASE / `GHOST dur=Nms`.
- **Stress Test Mode**: automated click simulation (1 s on / 2 s gap, B→R→G→Y); one log line per click; errors (`UNEXPECTED GHOST/PRESS`, `STUCK`) inline; `[STRESS ===]` status every 10 min. ~1 h clean run.
- **BLUE ghost clicks**: hardware — failing microswitch, replaced (see `hardware-baseline.md §5.2.1`).
- **Idle invite timing** (`dc10342`): second invite no longer fires 5 s after the first (which truncated it); all subsequent invites wait 20-45 s.
- **Cleanup**: removed unused `FIRST_INVITE_DELAY_SEC` / `POST_GAME_INVITE_DELAY_MS`; "5s" Yellow texts in `main.cpp` corrected to 3 s.
- **Eager input**: input opens at the last cue's LED-off (early answers count instead of being dropped → false "Wrong"); "Your Turn" deferred one cue gap and skipped if the player already pressed; first-press timeout starts after "Your Turn" finishes. Instructions unchanged (waiting for "Your Turn" is still preferred). See `GAME_ONLY_REQUIREMENTS.md §6.5`.
- **Sim build removed**: `[env:sim]`, `wokwi.toml`, `diagram.json` and all `USE_WOKWI` code deleted — hardware envs only.

**Pending:**
- Remove unused `shimon.h` constants (incl. dead `playScore()` / `AUDIO_SCORE_BASE`)
- Move `POST_GAME_COOLDOWN_MS` into `shimon.h`
- Swap Red/Green mechanics (confuser → Green/Advanced) — `0013`/`0014` re-recorded, ready
- Per-difficulty high scores + game counter (NVS), logged at game start/end; new high score → `0071.mp3` + fast LED rotation

### Updates (November 2025)
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

### Diagnostic Mode Fixes (Pre-HAL, Branch: `feat/party-mode-poc`)

6. **Phase B minimum LED visibility window**
   - Phase B now waits 300 ms after lighting the wing LED before accepting a button press
   - Prevents false PASS if operator's finger is already on the button when the LED lights
   - Constant: `PHASE_B_MIN_VIS_MS = 300`

7. **Phase skip + exit navigation**
   - Any button skips the current diagnostic phase (except B — all four buttons under test)
   - YELLOW 5 s exits to Mode Selection at any time (standard universal gesture)
   - Skip uses partial results (Phase C evaluates ticks/bytes collected before skip)

### Shared Hardware Abstraction Layer (February 2026, Branch: `feat/party-mode-poc`)

8. **hw.h / hw.cpp — Shared HAL**
   - Single `Color` enum, LED PWM, and button debounce used by all three modes and main.cpp
   - Ghost filter: 3 consistent reads + 50 ms hold for PRESS; release immediate
   - `hw_btn_update()` called once per loop tick in `loop()` before mode dispatch
   - `hw_btn_edge()` for single-tick press events; `hw_btn_pressed()` for held state; `hw_btn_raw()` for blocking release-wait loops
   - `hw_led_all_set(duties[4])`: safe multi-wing write — scales all 4 duties proportionally if sum > `HW_GLOBAL_DUTY_CAP` (320); use this for any simultaneous multi-wing write
   - Removed per-mode duplicates: `SEL_*` (main), `DIAG_LED/BTN/LEDC` (diagnostic), `PIN_LED/BTN/LEDC_CH` (party), `ledPins/btnPins/btnLedPins` (game)
   - `Wing` enum in party mode renamed to `Color`; all `W_BLUE/RED/GREEN/YELLOW` → `BLUE/RED/GREEN/YELLOW`
   - `PWM_FREQUENCY_HZ` corrected: 12000 → 12500 in shimon.h (code always ran at 12500; doc fix)

9. **Three diagnostic edge-detection bug fixes** (side-effect of hw.h migration)
   - **Bug fix 1 — C_PROMPT instant-confirm**: `phCPrompt_tick` now uses `hw_btn_edge(BLUE)`; a BLUE held from Phase B can no longer confirm C_PROMPT on the very first tick, which would transition directly to Phase C with 0 collected bytes → false FAIL
   - **Bug fix 2 — Phase C instant-skip**: `phC_tick` now uses `hw_btn_any_edge`; a button still held from C_PROMPT can no longer skip Phase C with 0 bytes → false FAIL
   - **Bug fix 3 — DS_DONE ghost restart**: `DS_DONE` now uses `hw_btn_any_edge`; the blinking LED drives the MOSFET at 12.5 kHz which can couple ghost LOW readings onto button pins; edge detection (requiring a new press) suppresses these

### Boot Cleanup + Global Power Cap (March 2026, Branch: `feat/party-mode-poc`)

10. **Common boot in `setup()`**
    - `hw_led_init()`, `hw_btn_init()` run once in `setup()` before `runModeSelection()`; prints `[BOOT] Hardware init done. Entering mode selection.`
    - No LED pattern during common boot (LED sweep removed)
    - Removed redundant `hw_led_init()`/`hw_btn_init()` calls from `runModeSelection()`, `game_init()`, `party_init()`, and `diag_init()`

11. **`game_init()` cleanup**
    - Removed: `Serial.begin()` + wait loop, 5-second boot countdown, per-LED flash test
    - `bootSequence()` (rainbow wave splash) now starts immediately — reduces game mode entry from ~8 s to ~3 s

12. **Mode-specific entry splashes**
    - **Diagnostic mode**: BLUE blinks 2× — was nothing before
    - **Game mode**: existing `bootSequence()` unchanged

13. **`hw_led_all_set(duties[4])` — global power cap in shared HAL**
    - Replaces the private `applyGlobalCap()` + `writeWingDuty()` pair that existed only in `mode_party.cpp`
    - All simultaneous 4-wing writes now go through this function: `selConfirmAnimation()` (main), PASS summary (diag), `commitRequests()` and `visualsRender()` failure path (party)
    - Constant `HW_GLOBAL_DUTY_CAP = 320` (sum of all 4 channel duties); previously only party mode enforced a cap — `selConfirmAnimation` (sum=720) and diagnostic PASS (sum=800) were both over the PSU limit
    - Removed dead code from `mode_party.cpp`: `GLOBAL_DUTY_CAP`, `applyGlobalCap()`, `writeWingDuty()`, `outDuty[]`, `allOff()`
    - Removed dead constants from `shimon.h`: `MAX_SIMULTANEOUS_WINGS_FULL_BRIGHTNESS`, `POWER_BUDGET_SAFETY_MARGIN`

### Hardware Pin Consolidation (March 2026, Branch: `feat/party-mode-poc`)

17. **ESP32 pin changes**
    - `BTN_BLUE`: GPIO 32 → GPIO 26
    - `I2S BCLK`: GPIO 26 → GPIO 33 (freed by BTN_BLUE move)
    - `I2S DATA`: GPIO 22 → GPIO 32 (freed by BTN_BLUE move)

18. **`shimon.h` — single source of truth for all hardware constants**
    - Added `I2S_PIN_BCLK`, `I2S_PIN_LRCK`, `I2S_PIN_DATA`, `I2S_SAMPLE_RATE`, `MIDI_PIN_RX`, `MIDI_BAUD_RATE`
    - Removed redundant `DFP_RX2` / `DFP_TX2` (unused duplicates of `DFPLAYER_RX` / `DFPLAYER_TX`)
    - Both `mode_party.cpp` and `mode_diagnostic.cpp` now reference `shimon.h` constants — no more per-mode hardcoded pins
    - Fixed diagnostic I2S WARN bug: `mode_diagnostic.cpp` had stale GPIO 26/22 that were never updated after the Mar 2026 pin move

### Known Issues (Resolved)
- ✅ Audio messages getting cut off (fixed with finish detection)
- ✅ No post-game prompt (fixed with POST_GAME_INVITE state)
- ✅ Button LEDs flashing instead of staying on (fixed with hold detection)
- ✅ DS_DONE spurious restart from PWM ghost clicks (fixed with edge detection in hw.h)
- ✅ `selConfirmAnimation` and diag PASS exceeding PSU limit (fixed with `hw_led_all_set()`)
- ✅ Redundant hw init calls in every mode (cleaned up — now only in `setup()`)
- ✅ False input edge after button release on event build (20 ms fast ghost hold + debounced release)
- ✅ BLUE ghost clicks (hardware: microswitch replaced)
- ✅ Second idle invite 5 s after the first, truncating it (`dc10342`)

### PlatformIO Access
PlatformIO can be accessed at: `~/.platformio/penv/Scripts/pio.exe`
Or add to system PATH: `C:\Users\galtr\.platformio\penv\Scripts`

## File Structure

- `src/main.cpp`: Top-level mode selection FSM and loop dispatcher (BLUE=Game, RED=Diag, YELLOW=Stress)
- `include/hw.h` + `src/hw.cpp`: **Shared hardware abstraction layer** — Color enum, LED PWM, button debounce
- `include/shimon.h`: **Single source of truth** for all hardware pin assignments and tunable parameters
- `src/mode_game.cpp`: Simon Says game logic
- `src/mode_stress.cpp` + `src/mode_stress.h`: Stress test mode — LED rotation, DFPlayer audio, ghost-click monitoring
- `src/mode_diagnostic.cpp` + `src/mode_diagnostic.h`: 3-phase diagnostic (A=LED / B=Buttons / E=DFPlayer)
- `src/mode_game.h`: Game mode interface header
- `platformio.ini`: Build environments (hardware = COM3, hardware-com6 = COM6)
- `CLAUDE.md`: This documentation file
- Standard PlatformIO directories (`lib/`, `test/`) currently unused