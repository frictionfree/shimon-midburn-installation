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

This gesture is **always active** — detected in the `MODE_SELECTION` loop and in `loop()` via raw `digitalRead` + `millis()` timer. It is not masked by any mode-specific logic.

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

## 7. Recommended Architectural Direction

To fully harden the system, the following architecture is recommended:

| Component | Purpose |
|-----------|---------|
| Central state machine | Single source of truth for system state |
| Event-based button handling | Decouple input from action |
| Single LED owner | One code path controls PWM targets |
| Single audio control point | Centralized DFPlayer management |
| Explicit recovery state | Defined path back to known-good state |
| Software watchdog | Detect and recover from stalls |

**Status:** Identified but not yet fully implemented.

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
| `PWM_FREQUENCY` | 12000 Hz | PWM carrier frequency |
| `PWM_RESOLUTION` | 8-bit | 0-255 duty range |
| `YELLOW_HOLD_RESET_MS` | 5000 | Yellow hold duration for global reboot |
| `PARTY_RED_LONG_PRESS_MS` | 3000 | Red hold threshold: short=resync, long=hard reset |

### Pin Assignments

See `hardware-baseline.md` for authoritative pin mappings.

---

## 12. Diagnostic Mode Lifecycle

### Overview

Diagnostic Mode runs a 5-phase hardware verification sequence (LED / Buttons / MIDI / I2S / DFPlayer) and then enters a **summary state** until the operator explicitly exits.

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

3. **Exit: any button press → `ESP.restart()`**
   - Pressing any of the four buttons reboots the system
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
