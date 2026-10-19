# Shimon – Game Mode Requirements

**Status:** Implemented and Validated
**Scope:** Game Mode (Simon Says) specific requirements

This document defines requirements specific to Game Mode operation. System-level requirements (mode selection, boot sequence, failure handling) are defined in `SYSTEM_REQUIREMENTS.md`. Hardware constraints are defined in `hardware-baseline.md`.

> **Event branch (`event/midburn-oct-2026`):** Boot, mode selection, exit gesture, passive mode, debounce values and pending game changes for the game-only build are defined in `GAME_ONLY_REQUIREMENTS.md`, which takes precedence over this document and `SYSTEM_REQUIREMENTS.md` where they differ.

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

| Button | Difficulty | Starting Length | Mechanic |
|--------|------------|-----------------|----------|
| **Blue** | Novice | 3 | Standard — sequence extends by one color each round |
| **Red** | Intermediate | 3 | Audio confuser — spoken color may differ from the LED |
| **Green** | Advanced | 3 | New random sequence each round (one longer) — no memorizing the previous rounds |
| **Yellow** | Pro | 3 | Alternating presentation — even steps LED only (silent), odd steps voice only (dark) |

All difficulties start with a sequence of 3 colors. A sequence shorter than 3 is trivial and confusing — no difficulty starts below this floor.

All difficulties share the same timing curve (see §7); difficulty comes from the mechanic, not speed.

> **Planned swap (blocked on audio re-record):** Red and Green mechanics will be swapped — confuser moves to Green/Advanced, new-sequence-each-round moves to Red/Intermediate — because the confuser proved harder in field testing. See `GAME_ONLY_REQUIREMENTS.md §6.6`.

### Audio Confuser (Intermediate Only)

In Intermediate difficulty, the spoken color name may differ from the displayed LED color, adding cognitive challenge. (Moves to Advanced after the planned swap.)

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
| `INSTRUCTIONS` | Playing main instructions (`0011`); a button press skips and selects difficulty directly |
| `DIFFICULTY_SELECTION` | Clockwise LED rotation; waiting for a difficulty button |
| `DIFFICULTY_INSTRUCTIONS` | Playing difficulty instructions (`0012`–`0015`); a button press skips straight to game start |
| `AWAIT_START` | Waiting for button press to begin game |
| `SEQ_DISPLAY_INIT` | Initialize new sequence |
| `SEQ_DISPLAY_MYTURN` | Playing "My Turn" audio |
| `SEQ_DISPLAY` | Show sequence with LEDs and audio — **button presses are ignored**; ends when the last cue's LED turns off |
| `SEQ_DISPLAY_YOURTURN` | Unused — "Your Turn" now plays in the background inside `SEQ_INPUT` |
| `SEQ_INPUT` | Input accepted immediately; "Your Turn" plays one cue gap in (skipped if player already pressed); first-press timeout starts when "Your Turn" finishes |
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
  ├─ (button press = difficulty choice) ──────────┐
  ↓ (audio complete)                               │
DIFFICULTY_SELECTION                               │
  ↓ (difficulty button)                            │
DIFFICULTY_INSTRUCTIONS ◄──────────────────────────┘
  ├─ (button press = skip) ──────────┐
  ↓ (audio complete)                 │
AWAIT_START                          │
  ↓ (button press)                   │
SEQ_DISPLAY_INIT ◄───────────────────┘
  ↓
SEQ_DISPLAY_MYTURN → SEQ_DISPLAY
  ↓ (last cue's LED off — input open; "Your Turn" follows in background)
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
- No blocking LED visual sequences during invite or instructions playback — all visual patterns that used `delay()` internally (`inviteSequence`, `instructionsSequence`) have been removed. Audio plays non-blocking; ambient effects provide visual engagement in IDLE.
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

**Pending re-recordings** (see `GAME_ONLY_REQUIREMENTS.md §6.6`):
- `0013.mp3` — describe new-sequence-each-round (after level swap)
- `0014.mp3` — describe the audio confuser (after level swap)

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

**Note:** There is no common LED pattern in `setup()`. On the event branch, Game Mode auto-starts at boot (no mode selection). The Game Mode splash below runs inside `game_init()`.

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

**Audio:** Periodic invites — first immediately on entering IDLE after boot, then every 20–45 s (random)

### Invite Sequence

**Trigger:** Timer expires

**Why random intervals:** Keeps the installation from sounding like a fixed timer, prevents people nearby from habituating to a predictable beat, and avoids phase-locking with surrounding sound. Combined with 5 invite variations (anti-repetition), idle audio stays varied.

**Visual:** None — ambient effects continue uninterrupted. The blocking LED pattern (`inviteSequence`) was removed; it caused a ~3s input blackout during every invite, preventing players from pressing to start.

### Instructions Sequence

**Trigger:** Button press from idle

**Visual:** None — audio plays non-blocking. The blocking LED pattern (`instructionsSequence`) was removed; it caused a ~3.6s input blackout, preventing players from pressing to skip instructions.

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

Audio-only invite after game over. Encourages replay. No blocking LED pattern.

**Cooldown:** 2500ms silent pause after `GENERAL_GAME_OVER` completes before the invite plays. This prevents the invite from sounding rushed immediately after the game over message. Button press during cooldown skips directly to `IDLE`.

---

## 7. Timing Parameters

### Sequence Timing

| Parameter | Default | Min | Description |
|-----------|---------|-----|-------------|
| `CUE_ON_MS` | 450ms | 250ms | LED on-time per cue |
| `CUE_GAP_MS` | 250ms | 120ms | Gap between cues |
| `INPUT_TIMEOUT_MS` | 3000ms | 1800ms | Player response limit per press (first press: measured from end of "Your Turn"; later presses: from previous release) |

### Progression

| Parameter | Value | Description |
|-----------|-------|-------------|
| `SEQUENCE_SPEED_STEP` | 0.97 | Sequence display acceleration (every 3 levels) |
| `INPUT_SPEED_STEP` | 0.97 | Input timeout acceleration (every 3 levels) |
| `MAX_SAME_COLOR` | 2 | Max consecutive same colors |

### Invite Timing

| Parameter | Value |
|-----------|-------|
| First invite | Immediate (first IDLE tick after boot splash) |
| Subsequent | 20-45 seconds (random), measured from the previous invite |
| Post-game cooldown | 2500ms (before first post-game invite) |

### Blocking Visual Pattern Constraint

Visual patterns that use `delay()` internally (e.g. `diagonalCrossPattern`, `clockwiseRotation`, `sparkleBurstSequence`) must record their end time using `millis()` — not the stale `now` captured at the top of `game_tick()`. Failure to do so causes immediate re-triggering on the next tick because `now - timer` already exceeds the repeat threshold.

---

## 8. Button Behavior

### Input Handling

- **Edge-based:** Actions on press/release only
- **No hold logic:** No action while button held
- **Visual feedback:** LED stays on while pressed
- **Input window:** Input opens the moment the last cue's LED turns off — players may answer before or during "Your Turn" and it counts. Presses during the presentation itself are discarded (edges reset when input opens). See `GAME_ONLY_REQUIREMENTS.md §6.5`
- **Debounce:** Fast mode (`HW_BTN_GHOST_MS_FAST = 20 ms`) during `SEQ_INPUT`; mid-sequence release confirmed via `hw_btn_pressed()` (see `GAME_ONLY_REQUIREMENTS.md §6.1`)

### Button Assignments

| Button | GPIO | Wing |
|--------|------|------|
| Blue | 26 | W1 |
| Red | 13 | W2 |
| Green | 14 | W3 |
| Yellow | 27 | W4 |

### Special Gestures

- **Yellow held 3 s** (event branch) → stop current mode and restart; keep holding through the reboot to reach Mode Selection. See `GAME_ONLY_REQUIREMENTS.md §2`.

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
- ✅ Invite and instructions skip lag (~3s blackout) — blocking `inviteSequence()` and `instructionsSequence()` removed; `INSTRUCTIONS_MIN_DURATION_MS` guard removed; `stateTimer` now set with `millis()` at transition point
- ✅ Second idle invite fired 5 s after the first, truncating it (fixed `dc10342`: leftover `firstInvite` flag removed from `scheduleNextInvite()`; validated 2026-10-19)
- ✅ False input edge after button release (fixed: 20 ms fast ghost hold + debounced release detection)

- ✅ Early answers lost/penalized + "Your Turn" eating first-press time (fixed: input opens at last LED-off; first-press timeout starts after "Your Turn"; validated 2026-10-19)

### Pending

- ⬜ Swap Red/Green mechanics after re-recording `0013.mp3` / `0014.mp3`

---

## 10. Configuration Constants

### Defined in `shimon.h`

```cpp
// Timing
CUE_ON_MS_DEFAULT      = 450
CUE_GAP_MS_DEFAULT     = 250
INPUT_TIMEOUT_MS_DEFAULT = 3000

// Progression
SEQUENCE_SPEED_STEP    = 0.97
INPUT_SPEED_STEP       = 0.97
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
| `GAME_ONLY_REQUIREMENTS.md` | Event build: boot, passive mode, debounce, pending game changes (takes precedence) |
| `SYSTEM_REQUIREMENTS.md` | Mode selection, boot sequence (full build) |
| `PARTY_MODE_REQUIREMENTS.md` | Party Mode specific requirements |
| `CLAUDE.md` | Developer guide, build commands |
