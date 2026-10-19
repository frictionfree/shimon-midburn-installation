# Shimon – Game-Only Build Requirements

**Branch:** `event/midburn-oct-2026`
**Status:** In progress
**Scope:** Game-only build for live event deployment. Party mode removed. Diagnostic and stress test simplified for field use.

This document covers requirements specific to the game-only build. For game logic and audio details see `GAME_MODE_REQUIREMENTS.md`. For electrical constraints see `hardware-baseline.md`.

---

## 1. Branch Scope

### What Changed vs `main`

| Area | Main branch | Game-only build |
|------|-------------|-----------------|
| Modes | Game + Party + Diagnostic | Game + Diagnostic + Stress Test |
| Party mode | Full multi-wing pattern system | **Removed** |
| Diagnostic | 3-phase (LED / Buttons / DFPlayer) | **Simplified — interactive button tester** |
| Stress test | Basic LED rotation + passive button log | **Replaced — automated click simulation** |
| Boot | Mode selection required | **Auto-boot to game mode; Yellow held at boot → mode selection** |
| Failure handling | None | **Passive mode with auto-restart** |
| Yellow exit hold | 5 seconds | **3 seconds** |
| Button debounce | HW_BTN_GHOST_MS_FAST = 0 ms | **20 ms** |
| Release detection | `hw_btn_raw()` | **`hw_btn_pressed()`** |

---

## 2. System Behavior

### Mode Architecture

Three operating modes:
- **Game Mode** (default) — Simon Says game
- **Diagnostic Mode** — Interactive button/LED/audio tester
- **Stress Test Mode** — Automated hardware endurance test

### Boot Behavior

| Reset type | Behavior |
|------------|----------|
| Cold start / brownout | Auto-boot directly to Game Mode (no operator action needed) |
| Yellow held at boot | Enter Mode Selection screen |
| Any other restart | Auto-boot to Game Mode |

**Entering Mode Selection:** Hold Yellow until the LEDs go off and the menu appears. No counting required — just hold until something happens. The 3s threshold triggers the restart; keeping Yellow held through the ~500ms reboot is what signals mode selection intent.

**Yellow-at-boot detection:** `setup()` checks `hw_btn_raw(YELLOW)` immediately after `hw_btn_init()`. If held, waits for release then enters `runModeSelection()`. Stuck button check is skipped for Yellow in this path.

**Implementation note:** `RTC_DATA_ATTR` boot intent flag was removed — it did not persist reliably through SW restart on this ESP32 chip. Yellow-held-at-boot is simpler and more reliable.

### Mode Selection

| Button | Mode |
|--------|------|
| Blue | Game Mode |
| Red | Diagnostic Mode |
| Yellow | Stress Test Mode |

Yellow 3s during Mode Selection → waits for Yellow release, then restarts directly to Game Mode (Yellow released = not held at boot = auto-starts game).

### Universal Exit Gesture

**Hold Yellow until LEDs go off** from any active mode → stops the current mode, restarts. Keep holding through the reboot → Mode Selection appears.

The 3-second threshold applies everywhere: Game Mode, Diagnostic Mode, Stress Test Mode, and Passive Mode.

---

## 3. Passive Mode (Failure Handler)

Passive mode is entered when a hardware fault is detected. It suspends all game logic and signals the fault visually.

### Entry Conditions

| Condition | Trigger |
|-----------|---------|
| Button stuck at boot | Any button held > 2s during `setup()` |
| DFPlayer init failed | `game_dfpOk()` returns false after `game_init()` |
| DFPlayer unresponsive | `game_audioFailCount() >= AUDIO_FAIL_THRESHOLD (3)` |
| Ghost storm | 10+ sub-debounce button events within 30s (currently disabled — see §6.3) |

### Passive Behavior

- All LEDs off
- RED LED slow breathing (sine wave) — "alive but unhealthy" signal
- Serial: reason, retry budget, countdown to next auto-restart

### Recovery

| Path | Action |
|------|--------|
| Auto-restart | Every 5 minutes, up to 3 attempts (`PASSIVE_MAX_RETRIES`). Each attempt restarts directly to Game Mode (Yellow not held at boot → auto-start). |
| Manual exit | Yellow 3s → Mode Selection |
| Max retries reached | Stay dark indefinitely until power-cycle or Yellow 3s |

Retry counter (`rtcPassiveRetries`) is reset to 0 on cold start or brownout.

---

## 4. Diagnostic Mode

### Purpose

Interactive tester for verifying button, LED, and audio hardware in the field. No phases, no pass/fail scoring — just immediate feedback for each button press.

### Behavior

On entry:
- Serial: `=== DIAGNOSTIC MODE ===` with usage instructions
- Blue LED blinks 2× as entry confirmation
- DFPlayer initialized (failure is non-fatal — button/LED test still works without audio)

During operation (per button):
- **Press detected** → LED on at duty 200, color audio plays, serial log: `[DIAG] PRESS: <color>`
- **While held** → LED stays on
- **Release detected** → LED off, serial log: `[DIAG] RELEASE: <color>`

### Ghost Click Monitoring

Raw GPIO state is tracked alongside debounced press/release detection. Any raw LOW that resolves without reaching the debounce threshold is logged as a ghost:

```
[DIAG] GHOST: <color>  dur=<N>ms
```

Used to isolate the BLUE microswitch defect (§6.2). A healthy button shows no ghosts, or an occasional 0–2 ms ghost (normal electrical noise). Repeated ghosts approaching 20 ms on a single channel indicate a failing switch.

Yellow button: short tap works normally (LED on + audio). 3-second hold exits to Mode Selection (handled by main loop).

### Audio

- Uses DFPlayer Mini with color name files (0061–0064.mp3)
- Initialized at `diag_init()`, shut down at `diag_stop()`
- Volume: 20
- If DFPlayer init fails: logs warning, continues in LED-only mode

### Debounce Mode

Diagnostic runs with the fast debounce setting (`hw_btn_set_fast(true)`) to match game mode's input behavior during investigation of click detection issues.

### Serial Log Format

```
=== DIAGNOSTIC MODE ===
  Press any button: LED on + color audio plays.
  Hold YELLOW 3s: return to mode selection.
  DFPlayer OK.
[DIAG] Ready.
[DIAG] PRESS: BLUE
[DIAG] AUDIO: BLUE (track 61)
[DIAG] RELEASE: BLUE
[DIAG] GHOST: BLUE  dur=3ms
```

### Future (Full Version)

When returning to the full build with Party Mode, Diagnostic Mode will offer a selection screen at entry:
- **Blue** → Game hardware diagnostic (this mode)
- **Red/Green/Yellow** → Party Mode diagnostic (LED patterns, wing sync, MIDI/I2S)

No implementation needed now — keep the entry point clean for this hook.

---

## 5. Stress Test Mode

### Purpose

Automated hardware endurance test. Simulates button clicks at a fixed rate to drive LEDs and audio continuously, while monitoring all button GPIO lines for unexpected activity (ghost clicks, EMI coupling, stuck buttons).

### Click Simulation

| Parameter | Constant | Default |
|-----------|----------|---------|
| Click duration | `STRESS_CLICK_DURATION_MS` | 1000 ms |
| Gap between clicks | `STRESS_CLICK_GAP_MS` | 2000 ms |
| Click order | fixed | BLUE → RED → GREEN → YELLOW → repeat |

Each simulated click:
1. LED turns on at duty 200 for the active color
2. Corresponding color audio plays (0061–0064.mp3)
3. Serial log (one line per click): `[STRESS] #N  <color>`
4. After `STRESS_CLICK_DURATION_MS`: LED off (not logged)
5. Gap of `STRESS_CLICK_GAP_MS`, then next color

No human input is expected during a stress test run. All button GPIO activity is therefore unexpected and logged.

### Ghost / Unexpected Activity Detection

The test monitors all 4 button GPIO lines every tick. Since no physical presses occur, any raw LOW reading is anomalous.

| Event type | Log tag | Meaning |
|------------|---------|---------|
| Sub-debounce spike | `UNEXPECTED GHOST` | Raw LOW resolved before debounce threshold |
| Debounce-confirmed LOW | `UNEXPECTED PRESS` | Raw LOW held long enough to pass debounce |
| Stuck / sustained LOW | `STUCK` | Raw LOW persists > 500ms (logged every 1s) |

Log format:
```
[STRESS] UNEXPECTED GHOST: RED  dur=2ms  ghost#1  @0h05m12s
[STRESS] UNEXPECTED PRESS: BLUE  dur=95ms  @0h05m15s
[STRESS] STUCK: GREEN  1500ms  @0h10m30s
```

Ghost counts are tracked per channel and included in status logs and the stop summary.

### Status Reporting

| Parameter | Constant | Default |
|-----------|----------|---------|
| Periodic status interval | `STRESS_STATUS_INTERVAL_MS` | 600000 ms (10 min) |

Status log format (preceded by a blank line so it stands out when scrolling):
```

[STRESS ===] @0h10m00s  clicks=200  ghosts: BLUE=0 RED=1 GREEN=0 YELLOW=0
```

**Validation run (2026-10-18):** ~1 hour continuous run after the BLUE microswitch replacement — clean, no unexpected activity.

Runtime is expressed as elapsed time since `stress_init()` in `H:MM:SS` format.

### Exit

Yellow held 3s → `stress_stop()` (called by main loop) → logs final status + "Mode stopped." → restarts to Mode Selection.

`stress_stop()` always logs a final status summary before stopping.

### Audio

- Uses DFPlayer Mini with color name files (0061–0064.mp3)
- Initialized at `stress_init()`, shut down at `stress_stop()`
- If DFPlayer init fails: logs warning, continues in LED-only mode

### Serial Log Format

```
=== STRESS TEST MODE ===
  Click: 1000ms on / 2000ms gap, order: BLUE->RED->GREEN->YELLOW.
  All button activity logged. Hold YELLOW 3s to exit.
  DFPlayer OK.
[STRESS] #1  BLUE
[STRESS] #2  RED
[STRESS] UNEXPECTED GHOST: YELLOW  dur=3ms  ghost#1  @0h00m09s
[STRESS] #3  GREEN
...

[STRESS ===] @0h10m00s  clicks=80  ghosts: BLUE=0 RED=0 GREEN=0 YELLOW=1
```

One line per click (`#N  COLOR`). Errors (`UNEXPECTED GHOST`, `UNEXPECTED PRESS`, `STUCK`) print inline. Status uses `[STRESS ===]` tag with a leading blank line to stand out when scrolling.

---

## 6. Game Mode — Game-Only Build Changes

### 6.1 Button Debounce Fix

**Problem:** The game-only binary is smaller (no Party Mode code) → faster loop execution → debounce accumulation time shortened → contact bounce that was harmless under old firmware now fires false edges, especially immediately after button release.

**Fix (two layers):**

1. `HW_BTN_GHOST_MS_FAST` raised from 0 ms → 20 ms. Requires button to stay consistently HIGH for 20 ms before state can invert back to released. Typical contact bounce resolves in < 5 ms; real taps last > 50 ms.

2. `awaitingRelease` in `SEQ_INPUT` changed from `!hw_btn_raw(releaseColor)` to `!hw_btn_pressed(releaseColor)`. Waits for 3 consistent HIGH reads before accepting the release, giving a second bounce-protection layer on top of the ghost hold.

**Result:** Input quality reported as better than original firmware.

### 6.2 Click Detection Improvement (Resolved)

**Status:** Resolved. Root cause was a defective microswitch in the BLUE arcade button.

**Investigation (Diagnostic Mode):** Ghost monitoring in Diagnostic Mode (`[DIAG] GHOST:` log) was used to isolate the source. BLUE showed repeated ghosts of 2–19ms while RED/GREEN/YELLOW were clean. A GPIO swap test (rewiring BLUE↔YELLOW at the button box) confirmed the bounce traveled with the physical button, not the GPIO environment — ruling out PCB layout, trace coupling, and capacitor quality as factors.

**Fix:** Replaced the click mechanism (microswitch) in the BLUE arcade button. Post-replacement log showed one 0ms ghost across ~30 presses — within normal electrical noise levels, well below the 20ms threshold. `HW_BTN_GHOST_MS_FAST` remains at 20ms; no firmware change needed.

### 6.3 Ghost Storm Monitor

The ghost storm monitor (`ghostMonitorTick()` in `main.cpp`) was implemented to detect hardware ghost clicks and enter Passive Mode after 10 events in 30 seconds. It has been **disabled** (function exists but not called from `loop()`) because:

- The faster game-only loop made it too sensitive under normal conditions
- It was not the root cause of the button input regression (§6.1 was)
- It added extra `digitalRead()` calls per tick

**Future:** Can be re-enabled with logging-only mode (no passive entry trigger) to surface hardware ghost data during live operation without risking false shutdown.

### 6.4 Idle Invite Timing Fix (Resolved)

**Problem:** After boot, the first invite played immediately, then the second invite fired only 5 s later. With invite tracks ~5 s long, the second invite started right as the first ended — or cut it off mid-playback if the first track was longer than 5 s.

**Root cause:** `scheduleNextInvite()` contained a leftover `static bool firstInvite` flag (from early testing) that applied `FIRST_INVITE_DELAY_SEC = 5` on its first call. `game_init()` already handles the immediate first invite (`nextInviteDelay = 0`), so the 5 s delay was applied to the *second* invite.

**Fix (`dc10342`):** Removed the special case. `scheduleNextInvite()` always uses the random `INVITE_INTERVAL_MIN_SEC`–`INVITE_INTERVAL_MAX_SEC` interval.

**Invite timing (current):**

| Invite | Timing |
|--------|--------|
| First (after boot splash) | Immediate |
| All subsequent idle invites | Random 20–45 s after the previous one |
| Post-game invite | 2500 ms after general game over message, then random 20–45 s to the next |

The interval is randomized so the installation does not sound like a fixed timer, passers-by and staff don't habituate to a predictable beat, and invites don't lock into phase with surrounding sound sources. Combined with the 5 random invite variations (anti-repetition), the idle audio stays varied.

**Validated on hardware 2026-10-19.** Note: `FIRST_INVITE_DELAY_SEC` in `shimon.h` is now unused (dead constant).

### 6.5 Presses During Sequence Display (Open — Audio Content)

**Observation:** Players press buttons while the sequence is still being displayed. Presses during `SEQ_DISPLAY` are silently ignored — input is only accepted from `SEQ_INPUT` onward (edges are reset at its start). "Your Turn" audio plays in the background at the start of `SEQ_INPUT`, so input is live as soon as the last cue ends.

**Planned fix (no firmware change):** Re-record instructions track `0011.mp3` to tell players to watch the whole sequence and wait for "Your Turn" before pressing.

**Under discussion:** A visual cue marking the start of input (backup signal for players who don't hear the audio).

### 6.6 Swap Difficulty Levels 2 and 3 (Planned — Blocked on Audio)

**Rationale:** Field testing showed the audio confuser is harder than new-sequence-each-turn, so it should be the higher level.

| Button | Difficulty | Current mechanic | After swap |
|--------|------------|------------------|------------|
| Red | Intermediate | Audio confuser | New sequence each turn |
| Green | Advanced | New sequence each turn | Audio confuser |

**Code change:** Swap `difficultyConfigs[1]` and `difficultyConfigs[2]` in `mode_game.cpp`:
- Red (INTERMEDIATE): `{3, false, true, false}`
- Green (ADVANCED): `{3, true, false, false}`

Also update the `DifficultyLevel` enum comments and the `"Green level"` serial log in `CORRECT_FEEDBACK`.

**No change needed:** Game over thresholds and messages — Red/Intermediate stays at ≥ 8 ("Move up to Advanced!"), Green/Advanced stays at ≥ 10 ("Join finals Friday!").

**Blocked on:** Re-recording difficulty instruction tracks:
- `0013.mp3` (Red) — must describe new-sequence-each-turn
- `0014.mp3` (Green) — must describe the audio confuser

Firmware and audio must be deployed together; swapping either alone would make the spoken instructions contradict the gameplay.

---

## 7. Configuration Constants

All constants in `include/shimon.h`.

### Game-Only Build Specific

| Constant | Value | Description |
|----------|-------|-------------|
| `YELLOW_EXIT_HOLD_MS` | 3000 ms | Yellow hold to exit to mode selection |
| `HW_BTN_GHOST_MS_FAST` | 20 ms | Ghost hold for fast-input states (SEQ_INPUT, Diagnostic) |
| `HW_BTN_GHOST_MS_STANDARD` | 15 ms | Ghost hold for all other states |
| `PASSIVE_MAX_RETRIES` | 3 | Max auto-restart attempts from passive mode |
| `PASSIVE_RETRY_INTERVAL_MS` | 300000 ms | Interval between passive auto-restarts (5 min) |
| `STUCK_BUTTON_MS` | 2000 ms | Button held at boot to trigger passive mode |
| `INVITE_INTERVAL_MIN_SEC` / `MAX_SEC` | 20 / 45 s | Random idle invite interval (§6.4) |

### Stress Test

| Constant | Value | Description |
|----------|-------|-------------|
| `STRESS_CLICK_DURATION_MS` | 1000 ms | Simulated click on-time |
| `STRESS_CLICK_GAP_MS` | 2000 ms | Gap between clicks |
| `STRESS_STATUS_INTERVAL_MS` | 600000 ms | Periodic status log interval |

---

## 8. Implementation Status

### Done

- ✅ Party mode removed from build
- ✅ Auto-boot to Game Mode on cold start / brownout
- ✅ Passive mode failure handler (stuck button, DFPlayer fail, audio timeout)
- ✅ Button debounce fix: `HW_BTN_GHOST_MS_FAST = 20 ms`
- ✅ Release detection fix: `awaitingRelease` uses `hw_btn_pressed()`
- ✅ `YELLOW_EXIT_HOLD_MS = 3000` added to `shimon.h`, all exit points updated
- ✅ Diagnostic mode rewrite — interactive button/LED/audio tester (§4)
- ✅ Stress test rewrite — automated click simulation with ghost monitoring (§5)
- ✅ Mode selection via Yellow-held-at-boot (replaces unreliable `rtcBootIntent`)
- ✅ Ghost click monitoring in Diagnostic Mode (§4)
- ✅ Click detection investigation resolved — BLUE button microswitch replaced; 20ms debounce threshold confirmed sufficient (§6.2)
- ✅ Stress test logging simplified; ~1 hour clean run (§5)
- ✅ Idle invite timing — second invite waits the full random interval; validated on hardware (§6.4)

### Pending

- ⬜ Re-record instructions `0011.mp3` — tell players to wait for "Your Turn" (§6.5); decide on visual input cue
- ⬜ Re-record difficulty instructions `0013.mp3` / `0014.mp3`, then swap levels 2 and 3 (§6.6)
- ⬜ Cleanup: remove unused `FIRST_INVITE_DELAY_SEC` and `POST_GAME_INVITE_DELAY_MS` from `shimon.h`; fix stale "hold 5s" text in Mode Selection serial banner (code uses 3 s)

---

## 9. References

| Document | Content |
|----------|---------|
| `GAME_MODE_REQUIREMENTS.md` | Full game FSM, audio system, difficulty levels |
| `SYSTEM_REQUIREMENTS.md` | Shared system behaviors (full build; this document overrides it on this branch) |
| `hardware-baseline.md` | Pin assignments, PWM constraints, electrical design |
| `CLAUDE.md` | Developer guide, build commands, file structure |
