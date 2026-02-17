# Shimon – System Requirements

**Status:** Validated
**Scope:** Shared system behaviors for Game Mode and Party Mode

This document defines system-level requirements that apply consistently across all operating modes. Mode-specific requirements are documented separately in `GAME_MODE_REQUIREMENTS.md` and `PARTY_MODE_REQUIREMENTS.md`.

Hardware interfaces and electrical constraints are defined in the frozen `hardware-baseline.md`.

---

## 1. Document Hierarchy

```
hardware-baseline.md        ← Frozen hardware truth
        ↓
SYSTEM_REQUIREMENTS.md      ← This document (shared behaviors)
        ↓
    ┌───┴───┐
    ↓       ↓
GAME_MODE   PARTY_MODE      ← Mode-specific requirements
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
- All LED wings display a slow ambient breathing pattern
- The pattern indicates the system is idle and awaiting user input
- The pattern continues indefinitely (no timeout)

No audio feedback is produced in this state.

#### User Input

Mode selection is performed using **simultaneous button presses**:

| Buttons | Action |
|---------|--------|
| **Blue + Red** | Enter Game Mode |
| **Green + Yellow** | Enter Party Mode |

**Notes:**
- Both buttons must be detected as pressed within the same interaction window
- Accidental single-button presses have no effect
- There is no timeout; the system waits indefinitely for a valid selection

#### Confirmation Sequence

Upon valid mode selection:
1. Ambient breathing pattern stops immediately
2. A quick clockwise LED circle animation plays as visual confirmation
3. Control transfers to the selected mode
4. *(Future enhancement: mode-specific audio confirmation cue)*

After confirmation, the selected mode assumes full ownership of LED and audio behavior.

---

### 3.2 Universal Mode Exit & Reboot

#### Trigger Gesture

A global reboot gesture is available at all times, regardless of the current mode or internal state:

> **Red + Yellow buttons pressed simultaneously**

This gesture is **always active** and is not masked by any mode-specific logic.

#### Action

When the reboot gesture is detected:
- The system performs an immediate reboot
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
| Universal reboot | Red + Yellow buttons | Immediate reboot → `MODE_SELECTION` |
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
Boot LED Sequence (rainbow wave + 4 flashes)
    ↓
Enter MODE_SELECTION
    ↓
Wait for user input (Blue+Red or Green+Yellow)
    ↓
Confirmation animation
    ↓
Enter selected mode
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
| `MODE_SELECT_DEBOUNCE_MS` | 50 | Button combination detection window |

### Pin Assignments

See `hardware-baseline.md` for authoritative pin mappings.

---

## 12. References

| Document | Content |
|----------|---------|
| `hardware-baseline.md` | Frozen hardware configuration |
| `GAME_MODE_REQUIREMENTS.md` | Game Mode specific requirements |
| `PARTY_MODE_REQUIREMENTS.md` | Party Mode specific requirements |
| `CLAUDE.md` | Developer guide and implementation notes |
