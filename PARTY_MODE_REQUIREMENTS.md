# Shimon – Party Mode Requirements

**Status:** Validated (Audio/Timing/State Machine), In Development (Visual System)
**Scope:** Party Mode specific requirements

This document defines requirements specific to Party Mode operation. System-level requirements (mode selection, boot sequence, failure handling) are defined in `SYSTEM_REQUIREMENTS.md`. Hardware constraints are defined in `hardware-baseline.md`.

---

## 1. Purpose of Party Mode

Party Mode introduces a second operating mode for Shimon, alongside Game Mode, with the goal of using the installation in live music and party environments.

In Party Mode, Shimon's LED infrastructure presents **predefined, beat-synchronous visual patterns** that respond to the structure and energy of the music being played by a DJ.

### Core Philosophy

The focus is **musical relevance**, not free-form audio reactivity:
- Visual patterns align with meaningful musical moments (breaks, drops, phrase transitions)
- Patterns feel intentional and synchronized rather than arbitrary or overly sensitive
- No per-track profiles or real-time adjustments required

### Design Bias: Robustness Over Tuning

The system is intentionally biased toward detecting only **clear and musically obvious events**, even at the cost of missing marginal cases.

| Priority | Approach |
|----------|----------|
| **False positives** | Unacceptable (especially incorrect DROP detection) |
| **Missed detections** | Acceptable within reason |
| **Uncertainty** | Safe fallback to STANDARD state |

### Safe Fallback

When musical context cannot be determined with sufficient confidence:
- System reverts to `STANDARD` state
- Neutral, beat-synchronous visuals continue
- Visual confidence never exceeds musical confidence

---

## 2. Signal Architecture

### Two Signal Domains

Party Mode relies on two distinct signal sources:

| Domain | Source | Role |
|--------|--------|------|
| **Musical Time** | MIDI Clock | Beat/bar/phrase timing (authoritative) |
| **Musical Meaning** | I2S Audio | Context detection (BREAK/DROP) |

### Role Separation (Architectural Invariant)

This separation is **frozen** and must be maintained:

**MIDI Clock Responsibilities:**
- Beat counting (24 PPQN)
- Bar counting (4 beats per bar)
- Phrase counting
- Deterministic timing reference for visuals and state transitions

**I2S Audio Responsibilities:**
- Detect BREAK and DROP conditions
- Provide evidence of structural changes
- Validate musical context

**Key Rule:** MIDI defines **when** events are evaluated. I2S provides **what** is happening musically.

### Timing Model

- Bar-level aggregates finalized at bar start
- State transitions (STANDARD, CAND, BREAK) occur at bar boundaries
- DROP may be declared mid-bar via Return-Impact detection
- Visual transitions may align to next beat or occur immediately (DROP)

---

## 3. MIDI Clock Handling

### Validated Semantics

| Parameter | Value |
|-----------|-------|
| Beat | 24 MIDI ticks |
| Bar | 4 beats |
| Detection | Explicit bar-start detection |
| Finalization | Bar-level metrics finalized at bar start |

### Real-World Behavior

Some mixers emit continuous clock without reliable START/STOP messages.

**Policy:**
- Continuous clock presence is treated as timing authority
- START/STOP messages are advisory only
- Clock presence detection and timeout handling required

### Hardware Reference

See `hardware-baseline.md` Section 10 for MIDI input configuration (GPIO 34, 31250 bps, optocoupler circuit).

---

### 3.1 Tempo Integrity Guard (CLOCK_HOLD)

**Observed Field Behavior:**
Certain mixers (e.g., DJM-900NXS2) may emit a temporarily erratic MIDI clock tempo when their BPM engine loses grid reference during a BREAK section (missing kick / FX channel reassignment / Pro Link source transition). Jumps of 30+ BPM have been observed while the musical tempo remained constant. The error self-corrects once the kick returns at the DROP.

**Policy by State:**

| State | Behavior on Sharp BPM Jump |
|-------|---------------------------|
| **STANDARD / CAND** | Evaluated normally; adopted if stable |
| **BREAK_CONFIRMED** | Jumps > `CLOCK_HOLD_JUMP_BPM` are rejected; system enters **CLOCK_HOLD** |
| **CLOCK_HOLD active** | `lastBeatIntervalUs` frozen; all timing uses held reference |
| **STD recovery** | After `CLOCK_HOLD_RESUME_BEATS` stable beats, hold releases |

**CLOCK_HOLD Mechanics:**
- `bpmHoldIntervalUs` is captured at BREAK_CONFIRMED entry from `lastBeatIntervalUs` (the last stable tempo)
- While in BREAK without an active hold: `bpmHoldIntervalUs` slides with small stable variations (< `CLOCK_HOLD_JUMP_BPM`) so the reference always represents the last stable value immediately before any jump
- While CLOCK_HOLD is active: `lastBeatIntervalUs` is frozen — all timing consumers (`halfBeatUs`, `breakFadeDurUs`, BPM log fields) use the held reference
- Beat and bar counting continues from raw MIDI ticks regardless of hold state; only tempo-derived timing is affected

**Exit Condition:**
After `CLOCK_HOLD_RESUME_BEATS` (8) consecutive beats whose IIR-candidate BPM is within `CLOCK_HOLD_RESUME_BPM` (4 BPM) of the held reference, while in **STANDARD or DROP** state, CLOCK_HOLD releases and timing authority returns to direct MIDI clock following. BREAK and CAND remain fully frozen regardless of clock stability — the hold counter resets on every beat in those states. The stability gate is the real guard; no additional state restriction beyond excluding BREAK/CAND is needed.

**Log Events:**
- `EVENT CLOCK_HOLD_ENTER pos=X.Y holdBpm=Z candBpm=W delta=D` — hold activated, shows delta magnitude
- `EVENT CLOCK_HOLD_RELEASE pos=X.Y resumedBpm=Z` — hold released after stable recovery
- Bar log lines append `CLKHOLD(Z)` (held BPM) while hold is active

---

## 4. Audio Analysis

### Hardware Reference

See `hardware-baseline.md` Section 9 for I2S configuration:
- 48 kHz sample rate
- 24-bit right-justified in 32-bit words
- Critical decode: `int32_t sample = raw >> 8; float x = sample / 8388608.0f;`

### Audio Metrics

| Metric | Description |
|--------|-------------|
| **RMS** | Average signal energy (overall loudness) |
| **TR** | Transient Response - high-pass filtered magnitude (percussive sharpness) |
| **kVar** | Kick Variance Proxy - variance of low-pass envelope (kick impulsiveness) |
| **kMean** | Kick Mean Proxy - mean of low-pass envelope per bar (kick energy presence) |

### Signal Ratios

**Baseline Ratios (vs STANDARD baseline):**
- `rR = barRms / baseRms` → Energy level
- `tR = barTr / baseTr` → Transient density
- `kR = barKVar / baseKVar` → Kick impulsiveness (sensitive to character changes)
- `kMeanR = barKMean / baseKMean` → Kick band energy presence (robust to character changes)

**Diagnostic (log only, not used in decisions):**
- `kCV = barKVar / barKMean` → Kick coefficient of variation (impulsiveness relative to energy)

**Break-Floor Ratios (vs BREAK floor):**
- `bfR = barRms / breakRms`
- `bfT = barTr / breakTr`
- `bfK = barKVar / breakKVar`

**Window Ratios (75ms windows):**
- `w_rR`, `w_tR`, `w_kR` (vs baseline)
- `w_bfR`, `w_bfT`, `w_bfK` (vs break floor)

---

## 5. Musical Context State Machine

### States

| State | Purpose | Visual Behavior |
|-------|---------|-----------------|
| **STANDARD** | Stable full-groove | Groove-oriented, beat-aligned, CAP=0.7 |
| **CAND** | Kick-absent tension | STANDARD dimmed (~55%) |
| **BREAK** | Confirmed breakdown | Low-energy patterns, CAP=0.5 |
| **DROP** | High-impact return | Maximum intensity, CAP=1.0, 8 bars |

### State Lifecycle

```
STANDARD → CAND (kick disappears)
         → BREAK (deep sustained collapse)
         → DROP (groove restored with impact)
         → STANDARD
```

### Baseline Behavior

**Purpose:** Baseline represents stable STANDARD groove reference.

**Readiness Gate:**
- Must accumulate `BASELINE_MIN_QUALIFIED_BARS` (16) qualified bars
- Until ready: forced to STANDARD, all transitions suppressed

**Tracked values:** `baseRms`, `baseTr`, `baseKVar`, `baseKMean` — all updated via EMA (α=0.10) on qualified STANDARD bars.

**Update Policy:**
- Updates only on qualified STANDARD bars
- Frozen during CAND, BREAK, DROP
- **Qualification criteria (after initialized):**
  1. `barRms ≥ BASELINE_MIN_RMS` (absolute floor)
  2. `BASELINE_UPDATE_KR_MIN ≤ kR ≤ BASELINE_UPDATE_KR_MAX` (mandatory)
  3. AND at least one of:
     - `BASELINE_UPDATE_RR_MIN ≤ rR ≤ BASELINE_UPDATE_RR_MAX`
     - `BASELINE_UPDATE_TR_MIN ≤ tR ≤ BASELINE_UPDATE_TR_MAX`
- **Representative bands (0.90 to 1.10):** Prevents baseline drift from outlier bars

**Persistence:**
- Persists across MIDI stop/start, automatic resync
- Resets only on system reset or manual resync

---

### 5.1 STANDARD

**Entry from CAND (Recovery):**
- 1 finalized bar meeting:
  - `kR ≥ 0.82`
  - AND (`rR ≥ 0.75` OR `tR ≥ 0.75`)

**Entry from BREAK (Recovery):**
- 1 finalized bar meeting:
  - `kR ≥ 0.80`
  - AND (`rR ≥ 0.75` OR `tR ≥ 0.75`)
- Suppressed while `returnActive = true`

---

### 5.2 BREAK_CANDIDATE (CAND)

**Entry from STANDARD (2D gate):**
- `kR < KICK_GONE_KR_MAX` (0.60) — kick impulsiveness low
- AND `kMeanR < CAND_KMEANR_MAX` (1.05) — kick band energy not at baseline level
- Plus 4 consecutive low-kick windows (~300ms)

Rationale: prevents CAND when filtered/processed kicks are still clearly present (kMeanR elevated).
Bass-heavy breaks still enter CAND since bass-only kMeanR (~0.65–0.75) is below the 1.05 cap.

**CAND → BREAK (Deep Confirmation):**
- After `CAND_MIN_BARS` (1)
- Two consecutive bars with:
  - `kR < 0.40`
  - AND (`rR < 0.80` OR `tR < 0.55`)
  - AND `kMeanR < 0.70` — kick band energy must genuinely collapse (not just character change)

**CAND → STANDARD (Recovery — AND logic for kick presence):**
- 1 bar with:
  - `kR ≥ 0.82 AND kMeanR ≥ 0.90` — both kick signals must confirm recovery
  - AND `rR ≥ 0.75 AND tR ≥ 0.75`
- Minimum 1 bar in CAND before recovery is evaluated (prevents same-bar entry+recovery)

Symmetry: AND for both entry (absence) and recovery (presence). Prevents oscillation on
sub-bass-heavy tracks where kMeanR stays near baseline while kR remains low.

---

### 5.3 BREAK_CONFIRMED (BREAK)

**Entry:** Only from CAND after deep confirmation.

**Break Floor:**
- Initialized on BREAK entry
- Updates only while in BREAK
- Frozen during DROP and while `returnActive = true`

**BREAK → STANDARD (Recovery):**
- 1 bar with:
  - `kR ≥ 0.80`
  - AND (`rR ≥ 0.75` OR `tR ≥ 0.75`)
- Suppressed while `returnActive = true`

---

### 5.4 DROP (Return-Impact Model)

**Entry:** Only from BREAK via Return-Impact detection.

**Phase A — Return Start:**
- `w_bfK ≥ 1.60` for 3 consecutive windows (~225ms)
- Sets `returnActive = true`, freezes break floor

**Phase B — Impact Qualification:**
- Within evaluation window (12 windows, ~900ms):
  - `peak_bfK ≥ 2.50` (mandatory)
  - AND (`peak_bfR ≥ 1.55` OR `peak_bfT ≥ 1.60`)

**Short-Burst Protection:**
- If `w_bfK < 1.60` for 4 consecutive windows → cancel return tracking
- If evaluation window expires without qualification → remain in BREAK

**Duration:**
- Fixed at `DROP_BARS` (8 bars)
- Then → STANDARD at bar boundary

---

## 6. Threshold Summary (v8)

### Baseline Learning

| Parameter | Value |
|-----------|-------|
| `BASELINE_MIN_QUALIFIED_BARS` | 16 |
| `BASELINE_MIN_RMS` | 0.020 |
| `KICK_PRESENT_KVAR_ABS_MIN` | 0.0010 |
| `KICK_PRESENT_KR_MIN` | 0.90 |

### Baseline Update Bands (prevents drift)

| Parameter | Value |
|-----------|-------|
| `BASELINE_UPDATE_KR_MIN` | 0.90 |
| `BASELINE_UPDATE_KR_MAX` | 1.10 |
| `BASELINE_UPDATE_RR_MIN` | 0.90 |
| `BASELINE_UPDATE_RR_MAX` | 1.10 |
| `BASELINE_UPDATE_TR_MIN` | 0.90 |
| `BASELINE_UPDATE_TR_MAX` | 1.10 |

### CAND Entry (2D gate)

| Parameter | Value | Role |
|-----------|-------|------|
| `KICK_GONE_KR_MAX` | 0.60 | Kick impulsiveness threshold |
| `CAND_KMEANR_MAX` | 1.05 | Kick band energy cap (blocks if groove clearly on) |
| `KICK_GONE_CONFIRM_WINDOWS` | 4 (~300ms) | Window persistence |
| `CAND_MIN_BARS` | 1 | Minimum bars in CAND before BREAK eval |

### BREAK Confirmation

| Parameter | Value | Role |
|-----------|-------|------|
| `DEEP_BREAK_KR_MAX` | 0.40 | Kick impulsiveness floor |
| `DEEP_BREAK_RMS_MAX` | 0.80 | Energy floor |
| `DEEP_BREAK_TR_MAX` | 0.55 | Transient floor |
| `BREAK_KMEANR_MAX` | 0.75 | Kick band energy must genuinely collapse |

### Recovery

| Parameter | Value | Role |
|-----------|-------|------|
| `RECOVERY_RR_MIN` | 0.75 | Energy recovery floor |
| `RECOVERY_TR_MIN` | 0.75 | Transient recovery floor |
| `RECOVERY_KR_MIN` | 0.80 | BREAK→STD kick impulsiveness recovery |
| `CAND_RECOVERY_KR_MIN` | 0.82 | CAND→STD kick impulsiveness recovery (AND) |
| `RECOVERY_KMEANR_MIN` | 0.90 | CAND→STD kick band energy recovery (AND) |

### Tempo Integrity Guard

| Parameter | Value | Role |
|-----------|-------|------|
| `CLOCK_HOLD_JUMP_BPM` | 8.0 | BPM jump threshold to enter CLOCK_HOLD (in BREAK) |
| `CLOCK_HOLD_RESUME_BPM` | 4.0 | BPM stability tolerance toward release |
| `CLOCK_HOLD_RESUME_BEATS` | 8 (~2 bars) | Consecutive stable beats required to release |

### Return-Impact DROP

| Parameter | Value |
|-----------|-------|
| `MONITOR_WIN_MS` | 75 |
| `KICK_RETURN_BF_MIN` | 1.60 |
| `KICK_RETURN_CONFIRM_WINDOWS` | 3 (~225ms) |
| `KICK_RETURN_CANCEL_WINDOWS` | 4 (~300ms) |
| `RETURN_EVAL_WINDOWS` | 12 (~900ms) |
| `DROP_BF_KV_MIN` | 2.50 |
| `DROP_BF_RMS_MIN` | 1.55 |
| `DROP_BF_TR_MIN` | 1.60 |
| `DROP_BARS` | 8 |

---

## 7. Signal Presence & Failure Handling

| Condition | Classification | Action |
|-----------|----------------|--------|
| Clock lost, audio continues | FAILURE | Suspend state machine, visual fail indicator |
| Audio lost, clock continues | FAILURE | Suspend state machine, visual fail indicator |
| Both cease within ~1s | MUSIC_STOP | Safe STANDARD, preserve baseline |

**MUSIC_STOP** is not a failure - it represents coordinated stop of timing and audio.

---

## 8. Visual System

### 8.1 Purpose

The visual layer produces music-synchronized visual behavior suitable for live DJ environments.

**Design goals:**
- React to musical structure, not raw audio
- Align visuals with beat- and phrase-level context
- Clearly communicate BREAK and DROP states
- Operate safely within validated power envelope

**Key principle:** The visual system **consumes** musical state, not audio directly.

### 8.2 State-to-Visual Mapping

| State | Visual Character | Brightness Cap |
|-------|------------------|----------------|
| **STANDARD** | Groove-oriented, beat-aligned, moderate | 0.7 |
| **CAND** | STANDARD dimmed | 0.7 × DIM_FACTOR |
| **BREAK** | Reduced motion, low-energy | 0.5 |
| **DROP** | High-impact, maximum intensity | 1.0 |

### 8.3 Transition Alignment

- STANDARD ↔ BREAK transitions: Next bar boundary (Beat 1)
- DROP entry: May begin mid-bar (preempts scheduled transitions)
- DROP exit: Bar boundary after 8 bars

---

## 9. Pattern Model

### 9.1 Pattern Definition

A pattern is a predefined, beat-synchronized visual behavior with:
- Active LED strips
- Motion/animation logic
- Internal beat/bar structure
- Expected peak power scenario
- Allowed musical state
- Brightness request (normalized 0..1)

### 9.2 Execution Window

| Parameter | Value |
|-----------|-------|
| `PATTERN_LEN_BARS` | 8 |
| Window start | Bar 1 Beat 1 |
| Window end | Beat 4 of Bar 8 |
| Reset | On state entry |

### 9.3 Internal Phases

Patterns may define internal phases:
- Phase boundaries align to bar boundaries
- Phase durations sum to 8 bars
- Example: Bars 1-4 Phase A, Bars 5-8 Phase B

### 9.4 Temporal Quantization

**Allowed step resolutions:**
- 0.5 beat (DROP only)
- 1 beat
- 2 beats

No free-running time-based animation permitted.

---

## 10. Pattern Switching

### 10.1 Switch Rules

**STANDARD and BREAK:**
- Patterns run for full 8-bar window
- If state remains active, Pattern Switch at Beat 1 of next bar
- Switch mode: **HARD CUT** (no crossfade)

**DROP:**
- One pattern selected on entry (round-robin)
- No switch during DROP window
- May begin mid-bar

### 10.2 State Entry Behavior

| Transition | Timing | Pattern Reset |
|------------|--------|---------------|
| STANDARD ↔ BREAK | Next bar boundary | Yes, to Bar 1.1 |
| → DROP | Immediate | Yes, bar count from entry |

### 10.3 No Pattern Persistence

On every state entry:
- Pattern window restarts from Bar 1
- Internal pattern state resets
- No history preserved across states

### 10.4 Selection Policy (Round-Robin)

Each state maintains independent:
- Pattern catalog
- Index pointer (persists while powered)

On each switch:
1. Select pattern at current index
2. Increment index (wrap at end)

On boot: All indices initialize to first catalog entry.

---

## 11. Power Budgeting

### 11.1 Hardware Constants

| Parameter | Value |
|-----------|-------|
| `PSU_V` | 12V |
| `PSU_W` | 100W |
| `PSU_MAX_A` | ~8.33A |
| `HEADROOM_FACTOR` | 0.8 |
| `I_BUDGET_A` | ~6.66A |
| `STRIP_W_PER_M` | 11.52 W/m |
| `I_PER_M` | ~0.96 A/m |
| `I_WING_FULL` (2m) | ~1.92A |

### 11.2 Budgeting Model

At any visual update:
```
I_EST_A = Σ ( I_WING_FULL × duty )
```

**Rule:** `I_EST_A ≤ I_BUDGET_A`

### 11.3 Pattern Power Declaration

Each pattern declares peak overlap scenario.

```
patternPowerScale = min(1.0, I_BUDGET_A / I_PATTERN_PEAK_A)
effectiveDuty = requestedDuty × patternPowerScale × CAP_STATE
```

### 11.4 Runtime Clamp (Safety Backstop)

If `I_EST_A > I_BUDGET_A`: proportionally scale all duties.

Guarantees: No PSU overload, no brownouts.

---

## 12. Creative Philosophy

### 12.1 BREAK — Smooth / Sparse / Organic

| Aspect | Behavior |
|--------|----------|
| **Goal** | Represent structural collapse and tension |
| **Motion** | Slow, fluid, less directional |
| **Timing** | 1-beat and 2-beat transitions |
| **Overlap** | Heavy, 2-wing overlaps common |
| **Rules** | No immediate same-wing repeat, emphasize decay |

### 12.2 STANDARD — Beat / Direction / Groove

| Aspect | Behavior |
|--------|----------|
| **Goal** | Stable long-running groove engine |
| **Motion** | Predictable, structured, recognizable in 1-2 bars |
| **Timing** | Strictly 1-beat transitions |
| **Overlap** | Minimal |
| **Rules** | Directional consistency, may reverse mid-window |

### 12.3 DROP — Intense / Sharp / Impactful

| Aspect | Behavior |
|--------|----------|
| **Goal** | Clear, unmistakable structural return |
| **Motion** | High energy, fast, punchy |
| **Timing** | Half-beats allowed, higher density |
| **Overlap** | Allowed, may approach power limits |
| **Rules** | Must feel distinct from STANDARD |

---

## 13. Pattern Catalog (PoC Phase 1)

### 13.1 STANDARD Patterns

All STANDARD patterns: 8-bar window, 1-beat resolution, deterministic, minimal overlap.

#### STD-01 — Groove Rotation

**Identity:** Classic directional motion with mid-window reversal.

| Phase | Bars | Motion |
|-------|------|--------|
| A | 1-4 | Counterclockwise: W1 → W2 → W3 → W4 |
| B | 5-8 | Clockwise: W4 → W3 → W2 → W1 |

#### STD-02 — Alternating Axis

**Identity:** Structured symmetry using opposite wings.

| Bars | Motion |
|------|--------|
| Odd (1,3,5,7) | W1 → W3 → W1 → W3 |
| Even (2,4,6,8) | W2 → W4 → W2 → W4 |

#### STD-03 — Directional Sweep Ladder

**Identity:** Structured spatial travel with mirrored second half.

| Phase | Bars | Motion |
|-------|------|--------|
| A | 1-4 | Progressive sweep forward |
| B | 5-8 | Mirrored sweep back |

---

### 13.2 BREAK Patterns

All BREAK patterns: 8-bar window, 1-2 beat transitions, smooth fades, visible overlap.

#### BRK-01 — Slow Drift Relay

**Identity:** Calm, flowing wing-to-wing relay with soft overlap.

- Each transition: 2 beats
- Wing A fades out, Wing B fades in (overlap maintained)
- Pseudo-random sequence (no immediate same-wing repeat)

#### BRK-02 — Breathing Anchor

**Identity:** Single-wing breathing pulses with slow cross transitions.

- Each wing holds 1 full bar
- Fade in over first 2 beats
- Fade out over last 2 beats

#### BRK-03 — Dual Flow Weave

**Identity:** Soft dual-wing weave with continuous overlap.

- Two wings partially active at any time
- One fading out, one fading in
- Transition every 2 beats

---

### 13.3 DROP Patterns

All DROP patterns: 8-bar window, immediate start, half-beat allowed, fast transitions.

#### DRP-01 — Impact Chase

**Identity:** High-speed rotational drive.

| Phase | Bars | Motion |
|-------|------|--------|
| A | 1-4 | Half-beat rotational sweep forward |
| B | 5-8 | Reverse half-beat sweep |

Strong Beat-1 emphasis each bar.

#### DRP-02 — Alternating Burst Drive

**Identity:** Aggressive axis-based burst with 2-bar switching.

| Bars | Primary Axis |
|------|--------------|
| 1-2 | W1 + W3 |
| 3-4 | W2 + W4 |
| 5-6 | W1 + W3 |
| 7-8 | W2 + W4 |

Within each bar: Strong dual-wing hit on beats 1,3; half-beat pulses on 2,4.

#### DRP-03 — Expanding Impact Wave

**Identity:** Spatial expansion and contraction.

| Phase | Bars | Motion |
|-------|------|--------|
| Expansion | 1-4 | 1 wing → 2 → 3 → 4 wings |
| Contraction | 5-8 | 4 wings → 3 → 2 → 1 wing |

Half-beat shimmer allowed during multi-wing phases.

---

## 14. Visual Constants

### Timing

| Parameter | Value |
|-----------|-------|
| `PATTERN_LEN_BARS` | 8 |
| `BEATS_PER_BAR` | 4 |
| `SWITCH_MODE` | HARD_CUT |

### State Brightness Caps

| Parameter | Value |
|-----------|-------|
| `CAP_STANDARD` | 0.7 |
| `CAP_BREAK` | 0.5 |
| `CAP_DROP` | 1.0 |
| `CAND_DIM_FACTOR` | (tunable) |

---

## 15. Implementation Status

### Validated (POC Complete)

- ✅ SPDIF → I2S → ESP32 audio pipeline
- ✅ 24-bit right-justified decoding
- ✅ Musical context state machine (STANDARD, CAND, BREAK, DROP)
- ✅ MIDI clock integration (USB-MIDI and DIN-MIDI)
- ✅ Basic visual pattern per state (1 each)
- ✅ Power-aware PWM brightness control

### In Development (Current Focus)

- ⏳ Full pattern catalog (9 patterns)
- ⏳ Pattern switching logic (round-robin)
- ⏳ Pattern execution framework (8-bar windows)
- ⏳ Power budgeting model (pattern declarations)

### Open Issues

- Detection confidence during long buildups, sparse breaks
- DROP sensitivity tuning
- Automatic recovery validation
- Manual resync control

### Recently Completed

- ✅ Tempo Integrity Guard (CLOCK_HOLD) — req 3.1 / field-validated vs. DJM-900NXS2

---

## 16. References

| Document | Content |
|----------|---------|
| `hardware-baseline.md` | I2S config (Section 9), MIDI config (Section 10) |
| `SYSTEM_REQUIREMENTS.md` | Mode selection, boot sequence, failure handling |
| `GAME_MODE_REQUIREMENTS.md` | Game Mode specific requirements |
