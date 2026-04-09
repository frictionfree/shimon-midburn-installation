# Shimon – Game Mode Requirements

**Status:** Implemented and Validated
**Scope:** Game Mode (Simon Says) specific requirements

This document defines requirements specific to Game Mode operation. System-level requirements (mode selection, boot sequence, failure handling) are defined in `SYSTEM_REQUIREMENTS.md`. Hardware constraints are defined in `hardware-baseline.md`.

---

## 1. Purpose of Game Mode

Game Mode implements an interactive Simon Says memory game where players memorize and repeat LED/audio sequences of increasing difficulty.

### Core Gameplay

1. System displays a sequence of colored LEDs with corresponding audio
2. Player must repeat the sequence by pressing buttons
3. Correct input extends the sequence
4. Wrong input or timeout ends the game
5. Difficulty increases (faster timing, longer sequences)

---

## 2. Difficulty Levels

### Level Selection

During difficulty selection, players choose by pressing a colored button:

| Button | Difficulty | Starting Length | Description |
|--------|------------|-----------------|-------------|
| **Blue** | Novice | 3 | Slowest timing, standard gameplay |
| **Red** | Intermediate | 3 | Faster timing, audio confuser enabled |
| **Green** | Advanced | 3 | Faster timing, no speed acceleration |
| **Yellow** | Pro | 3 | Fastest timing, maximum challenge |

All difficulties start with a sequence of 3 colors. A sequence shorter than 3 is trivial and confusing — no difficulty starts below this floor.

### Audio Confuser (Intermediate Only)

In Intermediate difficulty, the spoken color name may differ from the displayed LED color, adding cognitive challenge.

### Score Thresholds

| Difficulty | Strong Score | Message |
|------------|--------------|---------|
| Novice | ≥ 8 | "Ready for Intermediate!" |
| Intermediate | ≥ 8 | "Move up to Advanced!" |
| Advanced | ≥ 10 | "Join finals Friday!" |
| Pro | ≥ 10 | "Memory master! See you at finals!" |

---

## 3. State Machine

### FSM States

| State | Description |
|-------|-------------|
| `IDLE` | Waiting for button press, ambient visual effects |
| `INSTRUCTIONS` | Playing audio instructions |
| `AWAIT_START` | Waiting for button press to begin game |
| `SEQ_DISPLAY_INIT` | Initialize new sequence |
| `SEQ_DISPLAY_MYTURN` | Playing "My Turn" audio |
| `SEQ_DISPLAY` | Show sequence with LEDs and audio |
| `SEQ_DISPLAY_YOURTURN` | Playing "Your Turn" audio |
| `SEQ_INPUT` | Wait for player input with timeout |
| `CORRECT_FEEDBACK` | Playing correct feedback sound |
| `WRONG_FEEDBACK` | Playing wrong input sound |
| `TIMEOUT_FEEDBACK` | Playing timeout notification |
| `GAME_OVER` | Personalized game over message |
| `GENERAL_GAME_OVER` | General game over message |
| `POST_GAME_INVITE` | Invite player to play again |

### State Flow

```
IDLE
  ↓ (any button)
INSTRUCTIONS
  ↓ (audio complete)
AWAIT_START
  ↓ (button press)
SEQ_DISPLAY_INIT → SEQ_DISPLAY_MYTURN → SEQ_DISPLAY → SEQ_DISPLAY_YOURTURN
  ↓
SEQ_INPUT
  ├─→ CORRECT_FEEDBACK → (next level) → SEQ_DISPLAY_INIT
  ├─→ WRONG_FEEDBACK → GAME_OVER
  └─→ TIMEOUT_FEEDBACK → GAME_OVER
         ↓
     GENERAL_GAME_OVER
         ↓
     POST_GAME_INVITE
         ↓
       IDLE
```

---

## 4. Audio System

### Hardware

- **Module:** DFPlayer Mini MP3
- **Connection:** ESP32 UART2 (TX2=GPIO17, RX2=GPIO16)
- **Storage:** MicroSD card with MP3 files

See `hardware-baseline.md` Section 6 for electrical details.

### Audio Finish Detection

The `Audio` class owns all playback state. Callers follow one rule: **call `audio.play*()` to start, call `audio.isDone()` to wait.**

- **`play*()`** — stops any active track (`dfPlayer.stop()` + 20 ms), starts the new track, records `_playStartMs` and a per-message `_fallbackMs` timeout. Resets the finished flag internally. Callers never touch `audioFinished`.
  - Exception: `playColorName()` is fire-and-forget — no stop, no state reset. SEQ_DISPLAY is timer-driven; DFPlayer handles track interruption natively.
  - If DFPlayer is not initialized, `play*()` returns immediately with `_finished` left `true` — FSM continues without stalling (no fallback timeout wait).
- **`audio.isDone()`** — returns `true` when `DFPlayerPlayFinished` is received (primary path) OR when the fallback timeout expires (logged as a warning — should be rare). No timeout constant needed at call sites.
- **`audio.update()`** — called once per `game_tick()` to consume DFPlayer events:
  - Accepts any `DFPlayerPlayFinished` event (track-ID matching removed).
  - Suppresses stale stop-response events via `_ignoreUntilMs` (300 ms window after each `play*()`).
  - Suppresses duplicate `DFPlayerPlayFinished` events for the same track within 50 ms (known DFPlayer hardware quirk).
  - Handles `DFPlayerCardOnline` / `DFPlayerUSBOnline` / `DFPlayerCardUSBOnline` (module reset): re-applies volume, sets `_finished = true` to unblock waiting states.
- **`audio.stop()`** — called on user interaction or mode exit to interrupt playback cleanly.

### Playback Rules

- Every tracked `play*()` calls `dfPlayer.stop()` internally before the new command — no inter-state `delay()` needed for audio transitions.
- FSM states do not pass timeout constants — fallback timeouts live inside `play*()` and are set generously (5–15 s) as a safety net, not as tuned estimates.
- No blocking `delay()` calls in FSM states for audio purposes.
- `POST_GAME_INVITE` waits 2500 ms after entry before playing the invite, then plays immediately.
- Invite variations use anti-repetition tracking (`lastInviteVar`) matching the pattern used by My Turn, Your Turn, and Correct feedback variations.

### DFPlayer Power Lifecycle

| Event | Action |
|-------|--------|
| `game_init()` | `audio.begin()` → `dfPlayer.begin(stream, true, true)` → sets volume + EQ → `initialized=true` |
| `game_stop()` | `audio.shutdown()` → `dfPlayer.stop()` → sets `initialized=false` |

DFPlayer draws ~45 mA in active/idle state and is left running across mode switches. Sleep mode was investigated but removed: the DFPlayer Mini's UART wake circuit is unreliable after `ESP.restart()` — a hardware MOSFET on VCC is required for reliable power-cycle-based sleep. A 10kΩ pull-up on GPIO17 (ESP32 TX → DFPlayer RX) is installed to keep the line HIGH during UART reassignment and restarts.

---

## 5. Audio File Structure

All files in `/mp3/` directory on SD card.

### Invitation Messages (0001-0005)

| File | Content |
|------|---------|
| 0001.mp3 | "Come play with me!" |
| 0002.mp3 | "Want to test your memory?" |
| 0003.mp3 | "Press any button to begin!" |
| 0004.mp3 | "Let's play Simon Says!" |
| 0005.mp3 | "Can you remember the pattern?" |

### Instructions (0011-0015)

| File | Content |
|------|---------|
| 0011.mp3 | Main instructions with difficulty explanation |
| 0012.mp3 | Blue/Novice difficulty instructions |
| 0013.mp3 | Red/Intermediate difficulty instructions |
| 0014.mp3 | Green/Advanced difficulty instructions |
| 0015.mp3 | Yellow/Pro difficulty instructions |

### "My Turn" Variations (0021-0025)

| File | Content |
|------|---------|
| 0021.mp3 | "My turn!" |
| 0022.mp3 | "Watch carefully!" |
| 0023.mp3 | "Pay attention!" |
| 0024.mp3 | "Here's the sequence!" |
| 0025.mp3 | "Follow along!" |

### "Your Turn" Variations (0031-0035)

| File | Content |
|------|---------|
| 0031.mp3 | "Your turn!" |
| 0032.mp3 | "Now you try!" |
| 0033.mp3 | "Can you repeat it?" |
| 0034.mp3 | "Show me what you remember!" |
| 0035.mp3 | "Go ahead!" |

### Positive Feedback (0041-0045)

| File | Content |
|------|---------|
| 0041.mp3 | "Correct! Well done!" |
| 0042.mp3 | "Excellent memory!" |
| 0043.mp3 | "Perfect! Keep going!" |
| 0044.mp3 | "You got it!" |
| 0045.mp3 | "Great job!" |

### Error Messages (0051-0052)

| File | Content |
|------|---------|
| 0051.mp3 | "Oops! Wrong button" |
| 0052.mp3 | "Too slow! Time's up" |

### Game Over Messages (0053-0058)

| File | Condition | Content |
|------|-----------|---------|
| 0053.mp3 | Novice, score ≥ 8 | "Ready for Intermediate!" |
| 0054.mp3 | Intermediate, score ≥ 8 | "Move up to Advanced!" |
| 0055.mp3 | Advanced, score ≥ 10 | "Join finals Friday!" |
| 0056.mp3 | Pro, score ≥ 10 | "Memory master! See you at finals!" |
| 0057.mp3 | Below threshold | "Good try! Practice makes perfect!" |
| 0058.mp3 | Always (general) | "Thanks for playing!" |

### Color Names (0061-0064)

| File | Color |
|------|-------|
| 0061.mp3 | "Blue" |
| 0062.mp3 | "Red" |
| 0063.mp3 | "Green" |
| 0064.mp3 | "Yellow" |

### Score Announcements (0070-0170)

Reserved for future use. Base 70 + score value.

### Audio File Specifications

| Parameter | Recommendation |
|-----------|----------------|
| Format | MP3, 16-bit, mono |
| Sample Rate | 22kHz or 44.1kHz |
| Bitrate | 128kbps |
| Volume | Normalized across files |
| Length | 1-3 seconds (most sounds) |
| Language | Hebrew (for Midburn) |

---

## 6. Visual Sequences

### Boot Sequence

**Note:** A common 4-color sequential sweep runs in `setup()` before mode selection (see `SYSTEM_REQUIREMENTS.md §10`). The Game Mode splash below runs after mode selection, inside `game_init()`.

**Duration:** ~3 seconds

**Visual:**
1. Rainbow wave: LEDs light in sequence (3 cycles)
2. Flash finale: All 4 LEDs flash together 4 times

### Idle Mode - Ambient Effects

**Duration:** Indefinite (until interaction)

Four rotating effects, 30 seconds each:

| Effect | Description |
|--------|-------------|
| BREATHING | All LEDs pulse together (sine wave) |
| SLOW_CHASE | Single LED circles through colors |
| TWINKLE | Random LEDs sparkle |
| PULSE_WAVE | Dual wave patterns flow through |

**Audio:** Periodic invites (first at 5s, then 20-45s intervals)

### Invite Sequence

**Trigger:** Timer expires

**Visual:**
1. Double flash: All LEDs flash twice
2. Spinning chase: 8 spins through colors
3. Final flash: All LEDs bright

### Instructions Sequence

**Trigger:** Button press from idle

**Visual:** Alternating pairs (Red+Green ↔ Blue+Yellow)

### Sequence Display

**Per cue:**
- LED lights for `CUE_ON_MS`
- Gap of `CUE_GAP_MS`
- Color audio plays (with optional confuser)

### Player Input Phase

- Wing LED flashes on button press
- LED stays ON while button held
- All LEDs OFF otherwise

### Feedback Sequences

| Feedback | Visual |
|----------|--------|
| Correct | Pressed LED stays on |
| Wrong | All LEDs OFF |
| Timeout | All LEDs OFF |

### Post-Game Invite

Same as idle invite sequence. Encourages replay.

**Cooldown:** 2500ms silent pause after `GENERAL_GAME_OVER` completes before the invite plays. This prevents the invite from sounding rushed immediately after the game over message. Button press during cooldown skips directly to `IDLE`.

---

## 7. Timing Parameters

### Sequence Timing

| Parameter | Default | Min | Description |
|-----------|---------|-----|-------------|
| `CUE_ON_MS` | 450ms | 250ms | LED on-time per cue |
| `CUE_GAP_MS` | 250ms | 120ms | Gap between cues |
| `INPUT_TIMEOUT_MS` | 3000ms | 1800ms | Player response limit |

### Progression

| Parameter | Value | Description |
|-----------|-------|-------------|
| `SPEED_STEP` | 0.97 | Acceleration factor (every 3 levels) |
| `MAX_SAME_COLOR` | 2 | Max consecutive same colors |

### Invite Timing

| Parameter | Value |
|-----------|-------|
| First invite | 5 seconds |
| Subsequent | 20-45 seconds (random) |
| Post-game cooldown | 2500ms (before first post-game invite) |

### Blocking Visual Pattern Constraint

Visual patterns that use `delay()` internally (e.g. `diagonalCrossPattern`, `clockwiseRotation`, `sparkleBurstSequence`) must record their end time using `millis()` — not the stale `now` captured at the top of `game_tick()`. Failure to do so causes immediate re-triggering on the next tick because `now - timer` already exceeds the repeat threshold.

---

## 8. Button Behavior

### Input Handling

- **Edge-based:** Actions on press/release only
- **No hold logic:** No action while button held
- **Visual feedback:** LED stays on while pressed

### Button Assignments

| Button | GPIO | Wing |
|--------|------|------|
| Blue | 32 | W1 |
| Red | 13 | W2 |
| Green | 14 | W3 |
| Yellow | 27 | W4 |

### Special Combinations

See `SYSTEM_REQUIREMENTS.md` for:
- Blue + Red → Game Mode selection
- Red + Yellow → Universal reboot

---

## 9. Implementation Status

### Validated Features

- ✅ Full FSM implementation
- ✅ All 4 difficulty levels
- ✅ Audio confuser (Intermediate)
- ✅ Personalized game over messages
- ✅ Audio finish detection
- ✅ Ambient idle effects
- ✅ Post-game invite

### Known Issues (Resolved)

- ✅ Audio cutoff (fixed with finish detection)
- ✅ Button LED flash-only (fixed with hold detection)
- ✅ No post-game prompt (fixed with POST_GAME_INVITE)
- ✅ Confuser color names only first plays (fixed: `playColorName()` fire-and-forget)
- ✅ 6s stall before general game over (fixed: stale `now` replaced with `millis()` after blocking patterns)
- ✅ Invite plays immediately after game over (fixed: 2500ms cooldown in POST_GAME_INVITE)
- ✅ Novice starts at 1-color sequence (fixed: all difficulties start at 3)
- ✅ DFPlayer not initialized stalls FSM for 5-12s (fixed: `_stopAndStart()` fast-fail, `_finished` stays true)
- ✅ Same invite variation can repeat back-to-back (fixed: `selectVariationWithFallback()` with `lastInviteVar`)
- ✅ Duplicate `DFPlayerPlayFinished` events (fixed: 50 ms dedup window per track)
- ✅ DFPlayer volume lost after power glitch (fixed: re-apply on `DFPlayerCardOnline`/`USBOnline` events)
- ✅ DFPlayer restart failure after mode switch (investigated sleep/wake — removed in favour of always-on; MOSFET on VCC needed for true power management)

---

## 10. Configuration Constants

### Defined in `shimon.h`

```cpp
// Timing
CUE_ON_MS_DEFAULT      = 450
CUE_GAP_MS_DEFAULT     = 250
INPUT_TIMEOUT_MS_DEFAULT = 3000

// Progression
SPEED_STEP             = 0.97
MAX_SAME_COLOR         = 2

// Invites
INVITE_INTERVAL_MIN_SEC = 20
INVITE_INTERVAL_MAX_SEC = 45

// PWM
PWM_MIN_EFFECTIVE_DUTY = 70
```

---

## 11. References

| Document | Content |
|----------|---------|
| `hardware-baseline.md` | DFPlayer config (Section 6), PWM constraints (Section 11) |
| `SYSTEM_REQUIREMENTS.md` | Mode selection, boot sequence |
| `PARTY_MODE_REQUIREMENTS.md` | Party Mode specific requirements |
| `CLAUDE.md` | Developer guide, build commands |
