# Shimon – System Requirements

**Status:** Validated
**Scope:** Shared system behaviors for Game Mode, Party Mode, and Diagnostic Mode

This document defines system-level requirements that apply consistently across all operating modes. Mode-specific requirements are documented separately in `GAME_MODE_REQUIREMENTS.md` and `PARTY_MODE_REQUIREMENTS.md`.

Hardware interfaces and electrical constraints are defined in the frozen `hardware-baseline.md`.

---

## 1. Document Hierarchy

```
hardware-baseline.md        ← Frozen hardware truth
        ↓
SYSTEM_REQUIREMENTS.md      ← This document (shared behaviors)
        ↓
    ┌───┼───────────┐
    ↓   ↓           ↓
GAME  PARTY_MODE  DIAG_MODE ← Mode-specific requirements
```

---

## 2. Interaction Model

### Design Evolution

Early sketches used level-based button logic, which led to:
- Re-entrant actions
- Overlapping fades
- Audio contention

### Final Interaction Model (Validated)

- **Edge-based handling only**
- Actions occur on press and release
- No logic executes while a button is held

Button presses trigger:
- Immediate visual response
- Asynchronous fade progression
- A single audio event (Game Mode only)

This model significantly reduced instability and improved perceived responsiveness.

---

## 3. System Mode Lifecycle

### Purpose

This chapter defines system-level interaction rules that govern:
- Operating mode selection
- Mode exit
- Recovery behavior

These rules operate **above** individual modes and are independent of internal mode logic, visual patterns, or synchronization mechanisms.

The goal is to provide a simple, deterministic, and recoverable system lifecycle.

---

### 3.1 Mode Selection State

#### Entry Conditions

The system enters `MODE_SELECTION` under the following conditions:
- Initial power-up
- Any system reboot (manual or software-initiated)
- Forced reboot triggered by the universal reboot gesture

`MODE_SELECTION` is a **blocking state**; the system remains here until the user explicitly selects an operating mode.

#### Visual Feedback

While in `MODE_SELECTION`:
- Blue, Red, and Green LEDs rotate in sequence (1 second each, cyclically)
- Yellow LED is not included in the rotation (reserved for global reset)
- The pattern continues indefinitely (no timeout)

No audio feedback is produced in this state.

#### User Input

Mode selection is performed using a **single button press**:

| Button | Action |
|--------|--------|
| **Blue** | Enter Game Mode |
| **Green** | Enter Party Mode |
| **Red** | Enter Diagnostic Mode |
| **Yellow hold ≥5 s** | Global reboot (see Section 3.2) |

**Notes:**
- A single press on Blue, Green, or Red immediately selects the corresponding mode
- There is no timeout; the system waits indefinitely for a valid selection
- Yellow is only active as the long-hold reboot gesture; a short Yellow press has no effect

#### Confirmation Sequence

Upon valid mode selection:
1. Rotating LED animation stops immediately
2. All LEDs flash in sequence (confirmation animation) as visual confirmation
3. Control transfers to the selected mode

After confirmation, the selected mode assumes full ownership of LED and audio behavior.

---

### 3.2 Universal Mode Exit & Reboot

#### Trigger Gesture

A global reboot gesture is available at all times, regardless of the current mode or internal state:

> **Yellow button held for ≥ 5 seconds**

This gesture is **always active** — detected in the `MODE_SELECTION` loop and in `loop()` via the shared hw layer (`hw_btn_held_ms(YELLOW) >= 5000`). It is not masked by any mode-specific logic.

#### Action

When the reboot gesture is detected:
- The system performs an immediate reboot (`ESP.restart()`)
- No attempt is made to gracefully transition between modes
- After reboot, the system returns to the boot sequence and enters `MODE_SELECTION`

#### Design Rationale

This mechanism serves as a:
- Simple panic button
- Reliable way to exit any mode
- Guaranteed recovery path from invalid or stuck states

**Design principles:**
- No complex inter-mode transitions
- No shared state between modes
- Every mode change starts from a clean slate

This approach prioritizes robustness, predictability, and ease of use over seamless mode switching.

---

### 3.3 Party Mode — Red Button In-Mode Actions

Within Party Mode, the Red button has a dual role based on hold duration:

| Hold Duration | Action | Effect |
|--------------|--------|--------|
| Short press (< 3 s) | MIDI Resync | `resetForResumeLike()` — resets bar/beat counter, baseline **kept** |
| Long press (≥ 3 s) | Hard Reset | `doManualResync()` → `resetForHardReset()` — clears baseline |

**Notes:**
- Action triggers on button **release** (rising edge), using press-start timestamp to calculate hold duration
- Yellow long-hold (≥5 s) remains the global reboot gesture and is checked independently
- Diagnostic Mode and Game Mode do not use in-mode Red button actions

---

## 4. PWM Fade Strategy

### Problem Statement

Initial linear fade-in from zero duty caused:
- Delayed visual response
- Missed flashes on short presses

This was due to MOSFET non-linearity at low gate voltages (see `hardware-baseline.md` Section 11.1).

### Final Strategy (Validated)

**On press / LED activation:**
- Immediately jump PWM duty to minimum visible value (~70/255)
- After jump, linearly ramp to target brightness

**On release / LED deactivation:**
- Fade out to zero with no minimum clamp

This compensates for MOSFET non-linearity and guarantees instant visual feedback.

### Configuration

The minimum effective duty is defined in firmware configuration:
```cpp
PWM_MIN_EFFECTIVE_DUTY = 70  // ~27% duty
```

---

## 5. DFPlayer Mini Stability (Game Mode)

### Observed Issues

During Game Mode development:
- Occasional lock-ups under rapid interaction
- Missed or overlapping playback commands

### Mitigations Applied (Validated)

| Mitigation | Purpose |
|------------|---------|
| Stop before play | Prevent overlapping audio |
| Inter-command delays | Allow DFPlayer processing time |
| Re-entrancy guards | Prevent concurrent commands |
| Centralized helper | Single point of audio control |

**Result:** DFPlayer behavior is stable during normal interaction.

### Implementation Notes

- Playback function: `dfp.playMp3Folder(trackNumber)`
- Always stop previous playback before starting new audio
- Use `audioFinished` flag to detect playback completion
- Timeout fallbacks remain for safety

---

## 6. Fast Interaction Risk Analysis

### Observations

Rare ESP32 stalls were observed when buttons were pressed extremely rapidly.

### Identified Risks

| Risk | Description |
|------|-------------|
| Re-entrant transitions | State changes interrupting each other |
| Distributed ownership | Multiple code paths controlling PWM/audio |
| No recovery path | System stuck in invalid states |

### Status

These observations motivated architectural recommendations (Section 7) but are not fully resolved in current implementation.

Hardware stress testing (Section 8) validated that the hardware layer is robust; remaining risks are software-architectural.

---

## 7. Shared Hardware Abstraction Layer (hw.h / hw.cpp)

A shared hardware abstraction layer was introduced to eliminate duplicated LED/button code across all three modes and to establish a single, correct debounce implementation.

### LED PWM API

| Function | Description |
|----------|-------------|
| `hw_led_init()` | Initialize LEDC channels 0–3 (one per color) |
| `hw_led_duty(Color, duty)` | Set PWM duty 0–255 |
| `hw_led_all_off()` | Zero all four channels |
| `hw_led_name(Color)` | Human-readable color name |

### Button Debounce API

| Function | Description |
|----------|-------------|
| `hw_btn_init()` | Configure INPUT_PULLUP on all four button pins |
| `hw_btn_update()` | Called **once per loop tick** (in `loop()` before mode dispatch) |
| `hw_btn_edge(Color)` | True for exactly one tick after a confirmed press |
| `hw_btn_pressed(Color)` | True while button is confirmed held |
| `hw_btn_raw(Color)` | Fresh `digitalRead` — for blocking release-wait loops only |
| `hw_btn_any_edge(Color*)` | Returns first edge this tick; writes color to pointer |
| `hw_btn_held_ms(Color)` | Milliseconds since press confirmed (0 if not pressed) |
| `hw_btn_reset_edges()` | Clear all edge flags |

### Ghost-Press Filter

The hw layer requires **3 consecutive matching reads** (≈30 ms at 10 ms loop) **plus a minimum hold time of 50 ms** before a PRESS is confirmed. This filters:

- PWM-switching transients on the MOSFET gate (12.5 kHz bursts much shorter than 50 ms)
- Mechanical bounce on button contacts

Releases are confirmed after 3 consistent reads with no hold-time requirement (no delay on release).

### Color Enum

```cpp
enum Color : uint8_t { BLUE=0, RED=1, GREEN=2, YELLOW=3, COLOR_COUNT=4 };
```

This enum is the shared type used across all modes. Mode-specific aliases (`Wing`, local `Color`) have been removed.

### Architectural Status

| Component | Status |
|-----------|--------|
| Shared button debounce (hw layer) | ✅ Implemented |
| Shared LED PWM (hw layer) | ✅ Implemented |
| Edge-based button handling | ✅ Implemented (via `hw_btn_edge`) |
| Central state machine | ❌ Not implemented (per-mode FSMs remain) |
| Single audio control point | ❌ Not implemented |
| Software watchdog | ❌ Not implemented |

The successful Party Mode POC confirms that a centralized timing service (MIDI Clock) is viable and should be treated as a first-class system component.

---

## 8. Hardware Stress Testing Results

### Test Configuration

- **Duration:** ~1 hour continuous operation
- **Active subsystems:**
  - LED PWM fades on all four channels
  - Button scanning and interaction logic
  - DFPlayer Mini audio playback

### Results

| Subsystem | Result |
|-----------|--------|
| LED behavior | Smooth, stable fades |
| Button behavior | Zero ghost presses |
| Audio behavior | Stable playback, no glitches |

### Failure Observed

- Rare ESP32 lock-up during extremely rapid interaction
- Manual reset restores operation

### Conclusion

**Hardware layer is validated.** Remaining risks are software-architectural.

---

## 9. System Failure Handling

### Failure Classification

The system distinguishes between:
- **Mode-specific errors** (handled within each mode)
- **System-level failures** (require recovery to `MODE_SELECTION`)

### Recovery Mechanisms

| Mechanism | Trigger | Action |
|-----------|---------|--------|
| Universal reboot | Yellow held ≥5 s | Immediate reboot → `MODE_SELECTION` |
| Watchdog timeout | System unresponsive | Auto-reboot → `MODE_SELECTION` |
| Manual reset | Hardware reset button | Full restart |

### Design Principles

- Failure always results in a **visible** state change
- Recovery always returns to a **known-good** state
- No silent failures or undefined states

---

## 10. Boot Sequence

### Power-Up Flow

```
Power On
    ↓
ESP32 Initialization
    ↓
Hardware Setup (pins, peripherals)
    ↓
Enter MODE_SELECTION (Blue/Red/Green rotation, no timeout)
    ↓
Wait for single button press: Blue / Green / Red
  (Yellow held ≥5 s → reboot)
    ↓
Confirmation animation
    ↓
Enter selected mode (Game / Party / Diagnostic)
```

### Boot LED Sequence

**Duration:** ~5-6 seconds

**Visual:**
1. Rainbow wave: LEDs light in sequence (3 cycles)
2. Flash finale: All 4 LEDs flash together 4 times

**Purpose:** Visual confirmation that system is operational.

---

## 11. Configuration Constants

### System-Level Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `PWM_MIN_EFFECTIVE_DUTY` | 70 | Minimum PWM for visible output |
| `PWM_FREQUENCY` | 12500 Hz | PWM carrier frequency |
| `PWM_RESOLUTION` | 8-bit | 0-255 duty range |
| `YELLOW_HOLD_RESET_MS` | 5000 | Yellow hold duration for global reboot |
| `PARTY_RED_LONG_PRESS_MS` | 3000 | Red hold threshold: short=resync, long=hard reset |

### Pin Assignments

See `hardware-baseline.md` for authoritative pin mappings.

---

## 12. Diagnostic Mode Lifecycle

### Overview

Diagnostic Mode runs a 5-phase hardware verification sequence (LED / Buttons / MIDI / I2S / DFPlayer) and then enters a **summary state** until the operator explicitly exits.

### Phase Navigation

The operator can advance or exit at any time without waiting for phases to complete.

#### Skip Current Phase

Any button press skips the current phase and advances to the next:

| Phase | Skip available | Notes |
|-------|----------------|-------|
| A (LED cycling) | ✅ Any button | 200 ms window at phase start where presses are ignored |
| A_CONFIRM | ✅ Any button | Detected via edge (first new press after A completes) |
| B (Buttons) | ❌ Not available | All four buttons are under test; Phase B waits ≥300 ms after LED lights before accepting a press |
| C_PROMPT | ✅ BLUE (primary) | **Edge only** — a BLUE button still held from Phase B cannot confirm instantly |
| C (MIDI listen) | ✅ Any button | **Edge only** — a button held from C_PROMPT cannot skip Phase C with 0 bytes (would falsely FAIL) |
| D (I2S) | ❌ Not available | 3 s blocking measurement; too short to warrant skip |
| E (DFPlayer) | ✅ BLUE | **Edge only** — first new BLUE press after track starts |
| DONE | ✅ Any button | **Edge only** — prevents PWM-switching ghost clicks from triggering spurious restart |

#### Exit to Mode Selection

**At any point during the diagnostic run:**
> **YELLOW held ≥ 5 seconds → `ESP.restart()` → Mode Selection**

This is the universal exit gesture defined in Section 3.2. It is active in all phases including Phase B and Phase D (blocking). It is always announced in the diagnostic startup banner.

#### Design Rationale

- Phase B cannot offer a skip because there is no neutral button — all four are the test subject
- Phase D (I2S) is a 3-second blocking measurement; the complexity of adding a skip outweighs the cost of waiting
- Skip evaluates partial results (e.g. Phase C may have collected some ticks before skip), preserving useful diagnostic information

---

### Phase Results — Three-Tier Logic (C and D)

Phases C (MIDI) and D (I2S) use a three-tier result model that distinguishes hardware connectivity from signal readiness:

#### Phase C — MIDI Clock

| Result | Condition | Meaning |
|--------|-----------|---------|
| PASS | `0xF8` ticks received | Clock running — ready for Party Mode |
| WARN | Bytes received but no `0xF8` | Device connected and talking, clock not started |
| FAIL | Zero bytes received | No MIDI signal — cable disconnected or device off |

**Rationale:** Mixers typically emit Active Sensing bytes (0xFE) every ~300 ms when powered and connected but not playing. "Some bytes, no clock" therefore reliably indicates a wired device that hasn't started playback, distinguishable from a completely dead connection.

#### Phase D — I2S Audio

| Result | Condition | Meaning |
|--------|-----------|---------|
| PASS | `frames ≥ 10000` AND `RMS ≥ 0.050` | I2S clock present, music signal confirmed |
| WARN | `frames ≥ 10000` AND `RMS < 0.050` | Converter connected and clocking, no music playing |
| FAIL | `frames < 10000` | No I2S clock received — converter absent or unpowered |

**Rationale:** When the SPDIF → I2S converter is absent or unpowered, the I2S peripheral receives no clock and `i2s_read` returns 0 bytes (times out). Frame count therefore separates hardware connectivity from content. The RMS threshold of 0.050 is set above the measured idle noise floor (~0.012) and above the Party Mode `BASELINE_MIN_RMS` (0.020) to confirm a genuine music signal.

**Configuration constants** (in `mode_diagnostic.cpp`):
```cpp
D_MIN_FRAMES = 10000   // FAIL below this frame count (no I2S clock)
D_MUSIC_RMS  = 0.050   // PASS above this RMS (music signal present)
```

---

### End-State Behavior (DS_DONE)

After all phases complete and the summary is printed:

1. **Initial visual feedback** (blocking):
   - FAIL: RED wing flashes 6× rapidly
   - WARN: YELLOW wing pulses 4× slowly
   - PASS: All wings light for 2 seconds

2. **Continuous result blink** (non-blocking, indefinite):
   - The result color blinks at ~750 ms on / 750 ms off
   - RED = FAIL, YELLOW = WARN, GREEN = PASS
   - This provides persistent visual feedback for no-monitor use

3. **Exit: any button edge → `ESP.restart()`**
   - A **new** button press (edge, not raw level) reboots the system
   - Raw level detection is explicitly avoided here: the LED blink drives the MOSFET at 12.5 kHz, which can generate ghost LOW readings on button pins; edge detection suppresses these
   - The system returns to `MODE_SELECTION`
   - Yellow held ≥5 s (global reboot gesture) also remains active via `loop()`

### Design Rationale

- Without a monitor, the result must be readable from LEDs alone
- Continuous blinking keeps the result visible even if the operator looks away during the initial flash
- Any-button-to-exit is consistent with the mode selection model (single press = action)
- `ESP.restart()` keeps mode transitions simple — every mode change starts from a clean slate (see Section 3.2)

---

## 13. References

| Document | Content |
|----------|---------|
| `hardware-baseline.md` | Frozen hardware configuration |
| `GAME_MODE_REQUIREMENTS.md` | Game Mode specific requirements |
| `PARTY_MODE_REQUIREMENTS.md` | Party Mode specific requirements |
| `src/mode_diagnostic.cpp` | Diagnostic Mode implementation (5-phase hardware check) |
| `CLAUDE.md` | Developer guide and implementation notes |
