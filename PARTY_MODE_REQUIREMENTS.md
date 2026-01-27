# Party Mode Requirements

**Status:** DRAFT - Requirements Gathering Phase
**Target:** Shimon Installation - Audio-Reactive Lighting System
**Created:** 2026-01-27

---

## 1. Overview

Party Mode transforms the Shimon installation from an interactive game into a music-reactive light show synchronized to DJ-mixed electronic music. The system will detect beats, track tempo changes, and adapt lighting patterns to match the energy and structure of techno/house/trance music.

---

## 2. Hardware Configuration

### 2.1 Audio Input Chain

**Production Setup:**
- **Source:** Pioneer JDM900 NX2 mixer
- **Interface:** Mixer coax SPDIF output → SPDIF-to-I2S converter → ESP32
- **I2S Pin Assignments** (from hardware baseline):
  - BCLK → GPIO 26
  - LRCK → GPIO 25
  - DATA → GPIO 22
- **Audio Format:** 48 kHz sample rate, 32-bit word width (validated)

**Development/Testing Setup:**
- **Source:** Computer (Ableton Live)
- **Interface:** Focusrite Scarlett 16i16 SPDIF output → SPDIF-to-I2S converter → ESP32
- Same I2S configuration as production

**Status:** Hardware connection validated, basic beat detection tested with Ableton kick pattern

### 2.2 LED Output

- **Reuses existing hardware:**
  - 4× 12V COB LED strips (Blue, Red, Green, Yellow)
  - MOSFET switching via GPIO 23, 19, 18, 5
  - Button LEDs (hardwired to mirror LED strips automatically)
- **Power Constraint:** 100W PSU / ~8.3A max
  - Maximum ~2 wings at full brightness simultaneously
  - All patterns must respect global brightness cap

---

## 3. Operating Mode Architecture

### 3.1 Boot Sequence

**On power-up or reset:**
1. ESP32 initialization
2. Hardware setup (pins, audio, peripherals)
3. Boot LED sequence (existing: rainbow wave + 4 LED flashes)
4. **Enter MODE_SELECTION state** (new)

### 3.2 Mode Selection State

**Visual Feedback:**
- Slow ambient breathing pattern on all LEDs (indicates waiting for input)
- Pattern continues indefinitely until user makes selection

**User Input:**
- **Blue + Red buttons pressed simultaneously** → Enter **Game Mode**
- **Green + Yellow buttons pressed simultaneously** → Enter **Party Mode**
- **No timeout** - system waits indefinitely for user choice

**Confirmation Sequence:**
After button press:
1. Stop breathing pattern
2. Quick clockwise LED circle (visual confirmation)
3. Enter selected mode
4. *(Future enhancement: mode-specific audio confirmation)*

### 3.3 Mode Transition - Universal Reboot

**Trigger:** Red + Yellow buttons pressed simultaneously (works in any mode/state)

**Action:**
- Immediate system reboot
- Return to boot sequence → MODE_SELECTION state
- User must select mode again

**Purpose:**
- Simple "panic button" / mode switcher
- No complex state transitions between modes
- Clean slate on every mode change

---

## 4. Game Mode (Existing - Reference Only)

Game Mode functionality remains unchanged from current implementation:
- Interactive Simon Says gameplay
- 4 difficulty levels
- DFPlayer audio feedback
- All existing FSM states and logic

**Note:** No modifications needed to Game Mode logic, except:
- Entry point now from MODE_SELECTION state (not directly from boot)
- Red+Yellow combination triggers reboot to MODE_SELECTION

---

## 5. Party Mode Functional Requirements

### 5.1 Party Mode Overview

Party Mode suspends all game logic and runs music-reactive lighting patterns synchronized to incoming audio from the DJ mixer.

### 5.2 Audio Analysis Requirements

#### 5.2.1 Beat Detection
- **Primary goal:** Detect kick drum beats in real-time
- **Target genres:** Techno, House, Trance
- **Expected BPM range:** TBD (typical: 120-150 BPM?)
- **Latency target:** TBD (acceptable delay between beat and light response?)
- **Detection method:** TBD
  - Onset detection (time-domain energy analysis)?
  - FFT + low-frequency peak detection?
  - Hybrid approach?
  - Evaluate existing ESP32 libraries vs. custom implementation

#### 5.2.2 Tempo Tracking
- **Requirement:** Adapt to gradual BPM changes in real-time
- **Use cases:**
  - DJ beatmatching during transitions
  - Gradual tempo builds/drops
- **Tracking method:** TBD
  - Running average of inter-beat intervals?
  - Phase-locked loop?
  - Exponential moving average?
- **Update rate:** TBD (how often to recalculate tempo?)

#### 5.2.3 Musical Structure Detection
- **Requirement:** Detect breaks, buildups, and drops to trigger pattern changes
- **Detection method:** TBD
  - Energy level tracking (RMS/peak analysis)?
  - Beat density analysis (sparse = break, dense = drop)?
  - Simple threshold-based classification?
- **States to detect:**
  - **Normal/Rhythm:** Steady beat sections with consistent energy
  - **Break/Breakdown:** Reduced energy, sparse or no beats
  - **Buildup:** Increasing energy leading to drop
  - **Drop:** High energy, strong beats

### 5.3 Lighting Pattern Requirements

#### 5.3.1 Pattern Categories

Patterns should vary based on detected musical structure:

**Normal/Rhythm Patterns:**
- Beat-synchronized patterns during main sections
- Examples: single-wing pulse, alternating pulse, chase sequences

**Break/Breakdown Patterns:**
- Calmer, ambient effects during breakdown sections
- Examples: slow breathing, gentle fade cycles

**Buildup Patterns:**
- Intensifying effects that build tension
- Examples: accelerating chase, brightening pulse, expanding pattern

**Drop Patterns:**
- Explosive, high-energy patterns at drops
- Examples: all-wing flash, rapid strobe burst, energetic chase

#### 5.3.2 Pattern Design Constraints

- **Beat Synchronization:** Patterns trigger/update on detected beats
- **Power Budget Compliance:** Maximum ~2 wings at full brightness (100W PSU constraint)
- **MOSFET Constraint:** Fast pulses must jump to minimum effective duty (~70/255) for instant visibility
- **Smooth Transitions:** Pattern changes should feel musical, not jarring
- **Variety:** Multiple patterns per category to prevent repetition
- **Reuse Existing Code:** Leverage existing `setLed()`, PWM functions, and visual patterns where possible

#### 5.3.3 Specific Pattern Ideas (Initial Brainstorm - TBD)

**Normal/Rhythm:**
- Beat pulse: Single wing flashes on each beat, rotating through colors
- Alternating pulse: Two wings alternate on beats
- Quarter-note chase: Clockwise rotation every 4 beats
- Dual intensity: Two wings pulse together, intensity varies with energy

**Break:**
- Ambient breathe: All wings slowly fade in/out
- Gentle twinkle: Random sparse wing activations
- Slow rotation: Single dim wing slowly rotates

**Buildup:**
- Accelerating chase: Rotation speed increases
- Brightening pulse: Same wing pulses with increasing brightness
- Expanding activation: More wings activate as buildup progresses

**Drop:**
- All-wing flash burst: All wings flash rapidly (short duration, power-limited)
- Explosive chase: Very fast rotation
- Strobe effect: Rapid on/off cycling (power-safe pattern)

### 5.4 User Interaction During Party Mode

**Button Inputs:**
- **Red + Yellow combination:** Triggers reboot to MODE_SELECTION
- **All other buttons:** Ignored (no game logic active)

**Visual Feedback:**
- LED patterns driven entirely by music analysis
- Button LEDs mirror LED strips (hardware-wired, automatic)

**Audio System:**
- DFPlayer not used during Party Mode (no voice prompts needed)
- Only mixer audio input active

### 5.5 Fallback Behavior (Silence / No Signal)

**Scenario:** No beats detected or audio signal lost

**Behavior Options (decision TBD):**
- **Option A:** Continue last pattern at last known tempo (ghost beats)
- **Option B:** Fade to slow ambient pattern (gentle breathing)
- **Option C:** Auto-exit to MODE_SELECTION after timeout
- **Option D:** Display error pattern, wait for Red+Yellow reboot

**Recommendation:** Start with Option B (fade to ambient), add timeout option later if needed

---

## 6. Technical Implementation Considerations

### 6.1 FSM State Structure

**New States Required:**
- `MODE_SELECTION` - Boot state waiting for mode choice
- `PARTY_MODE_ACTIVE` - Running music-reactive patterns
- *(Possibly sub-states for different pattern categories)*

**Modified States:**
- `IDLE` - Entry point changes from boot to mode selection
- All game states - Check for Red+Yellow reboot trigger

### 6.2 Audio Processing Architecture

**I2S Audio Input:**
- Continuous sampling at 48 kHz
- Circular buffer for incoming samples (size TBD)
- Real-time processing (beat detection + analysis)

**Processing Strategy (TBD):**
- **Single-core:** All processing on main core (simpler, may limit performance)
- **Dual-core:** Audio analysis on second core via FreeRTOS task (better performance, more complex)
- **Buffer management:** Double-buffering or ring buffer
- **Downsampling:** Process at lower rate to save CPU (e.g., 12 kHz for beat detection)?

### 6.3 Beat Detection Algorithm (TBD - Research Phase)

**Options to evaluate:**
1. **Simple energy-based onset detection:**
   - Track RMS energy over sliding window
   - Detect rapid increases (onset = beat)
   - Pros: Low CPU, simple to implement
   - Cons: Sensitive to noise, may need tuning

2. **FFT-based low-frequency detection:**
   - FFT to extract bass/kick frequencies (20-100 Hz)
   - Peak detection in frequency domain
   - Pros: More accurate for kick drums
   - Cons: Higher CPU cost, requires FFT library

3. **Hybrid approach:**
   - Bandpass filter → energy detection → adaptive threshold
   - Pros: Balance of accuracy and performance
   - Cons: More complex implementation

4. **Existing libraries:**
   - Evaluate ESP32-compatible beat detection libraries
   - Trade-off: convenience vs. control/optimization

**Decision criteria:**
- Accuracy on test tracks (techno/house/trance)
- CPU usage / real-time performance
- Latency (beat detection → light response)
- Tuning effort required

### 6.4 Integration with Existing Codebase

**Code Organization:**
- Create new module: `party_mode.cpp` / `party_mode.h`?
- Audio analysis module: `audio_analysis.cpp` / `audio_analysis.h`?
- Keep mode selection logic in `main.cpp` FSM
- Add Party Mode settings to `shimon.h` (thresholds, pattern timing, etc.)

**Reuse Existing Functions:**
- `setLed()` for LED control
- Existing visual patterns (clockwise rotation, sparkle, etc.) where applicable
- PWM infrastructure (analogWrite, duty scaling)

**Modifications Required:**
- Add Red+Yellow reboot detection to all states
- Modify boot sequence to enter MODE_SELECTION
- Add mode selection button detection logic

### 6.5 Memory & Performance Constraints

**ESP32-WROOM-32E Resources:**
- RAM: Limited (~520 KB total, ~200 KB usable after system overhead)
- CPU: Dual-core Xtensa @ 240 MHz
- Flash: Program storage

**Memory Allocation:**
- Audio buffer: TBD size (balance latency vs. memory usage)
- FFT buffer (if used): Typically 512-2048 samples
- Pattern state: Minimal (current pattern ID, timing variables)

**Performance Targets (TBD):**
- CPU usage: <70% to leave headroom?
- Audio buffer: Never overrun (real-time constraint)
- LED update rate: Match PWM frequency (12-12.5 kHz)

### 6.6 PWM & Hardware Constraints

**From Hardware Baseline:**
- **Minimum effective duty:** ~70/255 for MOSFET conduction
  - **Implication:** Beat-sync pulses must jump to minimum visible brightness instantly
- **PWM frequency:** 12-12.5 kHz @ 8-bit resolution (stable envelope)
- **Power budget:** Max ~8.3A total, ~2 wings at full brightness

**Pattern Design Rules:**
- Never ramp from 0 - always start at minimum effective duty
- Limit simultaneous wing activation (power constraint)
- Test all patterns under power budget

---

## 7. Development Plan & Milestones

### 7.1 Phase 1: Mode Selection Implementation
- [ ] Add MODE_SELECTION state to FSM
- [ ] Implement boot sequence → mode selection flow
- [ ] Add Blue+Red and Green+Yellow detection logic
- [ ] Add visual feedback (breathing pattern, confirmation circle)
- [ ] Implement Red+Yellow reboot trigger
- [ ] Test mode selection and transitions

### 7.2 Phase 2: I2S Audio Infrastructure
- [ ] Set up I2S driver and DMA
- [ ] Implement audio buffer management
- [ ] Verify continuous audio sampling (no overruns)
- [ ] Add debug output (audio level monitoring)
- [ ] Test with development setup (Ableton + soundcard)

### 7.3 Phase 3: Beat Detection Development
- [ ] Research and select beat detection algorithm
- [ ] Implement basic beat detection (prototype)
- [ ] Tune detection parameters with test tracks
- [ ] Measure detection accuracy and latency
- [ ] Optimize for performance (CPU usage)

### 7.4 Phase 4: Tempo Tracking
- [ ] Implement inter-beat interval measurement
- [ ] Add tempo averaging/smoothing logic
- [ ] Test with variable-tempo tracks
- [ ] Validate real-time BPM adaptation

### 7.5 Phase 5: Basic Pattern Implementation
- [ ] Design initial pattern set (1-2 per category)
- [ ] Implement beat-sync pattern engine
- [ ] Test patterns with detected beats
- [ ] Validate power budget compliance
- [ ] Tune visual timing and brightness

### 7.6 Phase 6: Musical Structure Detection
- [ ] Implement energy level tracking
- [ ] Add break/buildup/drop detection logic
- [ ] Test with real tracks (manual validation)
- [ ] Tune thresholds and sensitivity

### 7.7 Phase 7: Pattern Library Expansion
- [ ] Design and implement additional patterns
- [ ] Add pattern variation/randomization
- [ ] Implement smooth pattern transitions
- [ ] Test pattern variety and flow

### 7.8 Phase 8: Integration & Testing
- [ ] Full system integration (all phases combined)
- [ ] Test complete mode selection → Party Mode → reboot flow
- [ ] Stability testing (extended runtime)
- [ ] Test with DJ mixer (production hardware)
- [ ] Field testing with real music sets

### 7.9 Phase 9: Optimization & Polish
- [ ] Performance optimization (CPU, memory)
- [ ] Fine-tune detection parameters
- [ ] Refine pattern timing and transitions
- [ ] Add configuration options to shimon.h
- [ ] Final testing and validation

---

## 8. Open Questions & Decisions

### 8.1 Mode Selection & Transitions
- [x] Button combination for Game Mode: **Blue + Red**
- [x] Button combination for Party Mode: **Green + Yellow**
- [x] Visual feedback during selection: **Slow breathing pattern**
- [x] Confirmation after selection: **Quick clockwise LED circle**
- [x] Reboot trigger: **Red + Yellow (any mode)**
- [ ] Future: Add audio confirmation for mode selection?

### 8.2 Beat Detection
- [ ] Which beat detection algorithm/library to use?
- [ ] What BPM range to optimize for? (Typical: 120-150 BPM?)
- [ ] Acceptable detection latency? (<50ms? <100ms?)
- [ ] False positive/negative tolerance?
- [ ] How to handle double-time or half-time beats?

### 8.3 Tempo Tracking
- [ ] Averaging window size for tempo calculation?
- [ ] How aggressively to adapt to tempo changes?
- [ ] Minimum/maximum BPM limits?

### 8.4 Pattern Design
- [ ] How many patterns per category (initial implementation)?
- [ ] Pattern selection strategy (random? sequential? energy-based)?
- [ ] Transition timing (immediate? fade? beat-aligned?)
- [ ] Should buildups/drops have fixed-duration special patterns?

### 8.5 Musical Structure Detection
- [ ] Energy thresholds for break/buildup/drop classification?
- [ ] Minimum duration to confirm structure change (avoid false triggers)?
- [ ] Should beat density be tracked separately from energy?

### 8.6 Fallback & Error Handling
- [x] No signal behavior: **Start with Option B (fade to ambient)**
- [ ] Timeout duration before fallback (if implemented)?
- [ ] Should system indicate "no signal" state visually?

### 8.7 Performance & Architecture
- [ ] Single-core or dual-core audio processing?
- [ ] Buffer sizes (latency vs. stability trade-off)?
- [ ] Target CPU usage threshold?
- [ ] Should audio processing yield periodically to avoid blocking?

### 8.8 Testing & Validation
- [ ] What constitutes "accurate" beat detection? (% threshold?)
- [ ] How to measure latency in production (without dev tools)?
- [ ] Minimum runtime duration for stability validation?

---

## 9. Success Criteria

### 9.1 Functional Requirements
- [ ] Mode selection works reliably (no false button detections)
- [ ] Red+Yellow reboot trigger works from any mode/state
- [ ] Beat detection accuracy: TBD (e.g., >90% of beats detected?)
- [ ] Tempo tracking adapts smoothly to ±5% BPM changes
- [ ] Patterns synchronize visibly with music (subjective evaluation)
- [ ] Pattern transitions feel musical and appropriate to song structure

### 9.2 Performance Requirements
- [ ] System runs continuously for >2 hours without crash or desync
- [ ] CPU usage stays below TBD threshold
- [ ] No audio buffer overruns or underruns
- [ ] LED PWM remains stable (no flicker or artifacts)

### 9.3 User Experience
- [ ] Mode selection is intuitive and responsive
- [ ] Party Mode lighting is engaging and musical (field testing validation)
- [ ] Power budget constraint not noticeable (patterns feel "full")
- [ ] Fallback behavior (silence) is graceful, not confusing

---

## 10. Future Enhancements (Out of Scope for Initial Implementation)

- Audio confirmation messages for mode selection (DFPlayer voice prompts)
- Advanced frequency analysis (bass/mids/highs controlling different wings)
- User-adjustable sensitivity during Party Mode (button inputs)
- Multiple pattern "themes" selectable at mode selection
- Persistent mode memory (remember last mode after power cycle)
- MIDI control integration for external pattern triggering
- Recording/playback of lighting sequences
- Multi-wing coordination patterns (complex choreography)
- Brightness/energy level display (wings as VU meters)

---

## 11. References

- **Hardware Baseline:** `hardware-baseline.md` - Frozen as-built hardware documentation
- **Game Mode Documentation:** `CLAUDE.md` - Existing game implementation
- **I2S Hardware Validation:** Section 13.1 of `hardware-baseline.md`
- **Power Constraints:** Section 10.3 of `hardware-baseline.md`
- **PWM Constraints:** Section 10.1 of `hardware-baseline.md`
- **DJ Mixer Specs:** Pioneer DJM-900NXS2 (coax SPDIF output)
- **SPDIF-to-I2S Converter:** (Model/specs TBD - add when documented)

---

## 12. Document History

| Date       | Version | Changes                                      | Author       |
|------------|---------|----------------------------------------------|--------------|
| 2026-01-27 | 0.1     | Initial draft - requirements gathering complete | Claude + User |

---

## Next Steps

1. **Review this requirements document:**
   - Confirm all requirements are captured correctly
   - Make decisions on open questions (Section 8)
   - Prioritize features if needed

2. **Research phase:**
   - Evaluate ESP32 beat detection libraries/algorithms
   - Prototype basic I2S audio capture
   - Test beat detection accuracy with sample tracks

3. **Create implementation plan:**
   - Break down Phase 1 (Mode Selection) into detailed tasks
   - Set up development environment for audio testing
   - Prepare test tracks and validation methodology

4. **Begin development:**
   - Start with Phase 1 (Mode Selection) as foundation
   - Iterate on I2S + beat detection in parallel
   - Build up pattern library incrementally
