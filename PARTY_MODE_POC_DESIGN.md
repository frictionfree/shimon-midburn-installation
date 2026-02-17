# Party Mode Proof of Concept - Design Document

> **⚠️ DEPRECATED**
>
> This document describes the **failed initial POC approach** (FFT-based beat detection without MIDI).
>
> **Superseded by:** `PARTY_MODE_REQUIREMENTS.md` which documents the working architecture using:
> - MIDI Clock as timing authority
> - I2S audio for context detection (BREAK/DROP)
> - Correct 24-bit right-justified I2S decoding
>
> This file is retained for historical reference only.

---

**Status:** ~~Active Development~~ **DEPRECATED**
**Purpose:** ~~Validate feasibility of real-time beat detection~~ Historical reference only
**Created:** 2026-01-27

---

## 1. POC Objective

### 1.1 Critical Question

**Can we achieve reliable, visually appealing beat-synchronized lighting with ESP32 hardware and existing libraries?**

This POC exists to answer this question with confidence before investing in full Party Mode implementation.

### 1.2 Success Criteria

**Minimum Viable Success:**
- [ ] Beat detection accuracy >80% on simple kick drum loops
- [ ] Visual latency <100ms (beat to LED response feels synchronized)
- [ ] System runs stably for >5 minutes without crash or desync
- [ ] Code is understandable and maintainable

**Stretch Goals:**
- [ ] Works with real electronic music tracks (techno/house/trance)
- [ ] Adapts to tempo changes within ±10 BPM
- [ ] Handles breaks/buildups without false triggers
- [ ] Beat detection accuracy >85% on varied tracks

**Failure Criteria (Stop Development):**
- Beat detection <70% accurate even after tuning
- Latency >200ms (visibly out of sync)
- Requires constant retuning for different tracks
- CPU overload causes LED flicker or audio buffer issues

---

## 2. Hardware Configuration

### 2.1 Audio Input

**Development Setup:**
- **Source:** Computer running Ableton Live
- **Interface:** Focusrite Scarlett 16i16 coax SPDIF output → SPDIF-to-I2S converter
- **Converter Model:** [TBD - document specific model used]

**I2S Pin Assignments** (from `hardware-baseline.md` Section 13.1):
```cpp
#define I2S_BCLK  26  // Bit clock
#define I2S_LRCK  25  // Left/Right clock (word select)
#define I2S_DATA  22  // Data input
```

**Audio Format:**
- Sample rate: 44.1 kHz (CD quality, supported by both Focusrite Scarlett 16i16 and Pioneer DJM-900NXS2)
- Word width: 32-bit (hardware validated)
- Channels: Stereo (use mono or left channel only for beat detection)

**Format Selection Rationale:**
- 44.1 kHz is optimal for beat detection (60-120 Hz kick drums)
- Lower CPU load than 48 kHz while maintaining excellent frequency resolution
- Standard audio format, widely supported
- Nyquist theorem: can accurately detect frequencies up to 22.05 kHz (far beyond our bass range needs)

### 2.2 LED Output

**Using existing hardware pins** (from `shimon.h`):
```cpp
#define LED_BLUE   23  // GPIO23
#define LED_RED    19  // GPIO19
#define LED_GREEN  18  // GPIO18
#define LED_YELLOW  5  // GPIO5
```

**Power Constraint:** 100W PSU (~8.3A max)
- For POC: Single LED at a time (no power issues)
- Later: Test 2-LED patterns to validate power budget

**PWM Configuration:**
- Frequency: 12-12.5 kHz
- Resolution: 8-bit (0-255)
- Minimum effective duty: 70/255 (MOSFET conduction threshold)

### 2.3 Reference Documents

- **Hardware baseline:** `hardware-baseline.md`
- **Pin definitions:** `include/shimon.h`
- **I2S validation:** `hardware-baseline.md` Section 13.1

---

## 3. Software Architecture

### 3.1 Design Principles

**Keep It Simple:**
- Single-file sketch for easy experimentation
- Minimal dependencies (only essential libraries)
- Clear, commented code
- Easy to modify parameters for tuning

**No Game Mode Integration (Yet):**
- Standalone program, runs independently
- No FSM, no DFPlayer, no button inputs (initially)
- Just: Audio In → Beat Detection → LED Out

### 3.2 Core Components

```
┌─────────────────────────────────────────────────────────┐
│                    POC Architecture                      │
├─────────────────────────────────────────────────────────┤
│                                                           │
│  I2S Audio Input                                         │
│    ↓                                                      │
│  Buffer Management (circular/double buffer)              │
│    ↓                                                      │
│  FFT Processing (ArduinoFFT)                             │
│    ↓                                                      │
│  Beat Detection (low-freq peak detection)                │
│    ↓                                                      │
│  Tempo Tracking (inter-beat interval averaging)          │
│    ↓                                                      │
│  LED Pattern Generator                                   │
│    ↓                                                      │
│  PWM Output (analogWrite)                                │
│                                                           │
└─────────────────────────────────────────────────────────┘
```

### 3.3 Processing Strategy

**Single-Core Approach (Phase 1):**
- Simpler to implement and debug
- All processing in main loop
- Acceptable for POC validation

**Dual-Core Approach (Phase 2 - if needed):**
- Core 0: I2S audio capture (continuous)
- Core 1: FFT processing + LED control
- FreeRTOS queues for data passing
- Only if single-core performance is insufficient

---

## 4. Algorithm Design

### 4.1 Beat Detection Approach

**Method:** FFT-based low-frequency peak detection

**Rationale:**
- Techno/house/trance has strong, consistent kick drums (60-120 Hz)
- FFT isolates frequency bands effectively
- ArduinoFFT library is mature and ESP32-optimized
- Multiple working examples prove this approach

**Algorithm Steps:**
1. Capture audio samples via I2S (continuous)
2. Fill FFT buffer (e.g., 2048 samples)
3. Perform FFT → frequency domain
4. Sum magnitude in low-frequency bins (bass/kick range)
5. Detect local maximum (onset = beat)
6. Track beat timestamps for tempo calculation
7. Trigger LED pattern on beat

### 4.2 FFT Configuration

**Initial Parameters** (tune based on testing):

```cpp
// FFT Settings
#define FFT_SIZE 2048           // Sample buffer size
#define SAMPLE_RATE 44100       // Hz (44.1 kHz - CD quality, optimal for beat detection)
#define FFT_SPEED FFT_FORWARD   // Forward transform

// Beat Detection
#define KICK_FREQ_MIN 60        // Hz - kick drum lower bound
#define KICK_FREQ_MAX 120       // Hz - kick drum upper bound
#define BEAT_THRESHOLD 1.5      // Multiplier above average (tune this!)
#define MIN_BEAT_INTERVAL 300   // ms - minimum time between beats (~200 BPM max)

// Frequency bin calculation
// bin_frequency = (bin_index * SAMPLE_RATE) / FFT_SIZE
// For 44.1kHz @ 2048 samples: each bin = 21.53 Hz
// 60 Hz  = bin 2.8 ≈ bin 3
// 120 Hz = bin 5.6 ≈ bin 6
// Processing time per FFT frame: ~46.4 ms (2048 samples / 44100 Hz)
```

**Why 44.1 kHz @ 2048 samples?**
- **Sample rate:** 44.1 kHz is CD quality, supported by both audio sources (Scarlett, DJM-900NXS2)
- **Frequency resolution:** 21.53 Hz per bin (excellent for kick detection in 60-120 Hz range)
- **Processing time:** FFT completes in <5ms on ESP32 (low latency)
- **Frame duration:** 46.4 ms per FFT window (acceptable latency for beat detection)
- **Memory:** ~16 KB for buffers (well within ESP32 constraints)
- **Efficiency:** Lower CPU load than 48 kHz while maintaining excellent bass resolution
- **Proven approach:** Similar configurations used in ESP32-music-beat-sync project

### 4.3 Beat Detection Logic (Pseudocode)

```cpp
void detectBeat() {
  // 1. Perform FFT
  FFT.windowing(vReal, FFT_SIZE, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(vReal, vImag, FFT_SIZE, FFT_FORWARD);
  FFT.complexToMagnitude(vReal, vImag, FFT_SIZE);

  // 2. Sum magnitude in kick frequency range
  float bassEnergy = 0;
  int binMin = (KICK_FREQ_MIN * FFT_SIZE) / SAMPLE_RATE;
  int binMax = (KICK_FREQ_MAX * FFT_SIZE) / SAMPLE_RATE;

  for (int i = binMin; i <= binMax; i++) {
    bassEnergy += vReal[i];
  }

  // 3. Track running average
  static float avgBassEnergy = 0;
  avgBassEnergy = (avgBassEnergy * 0.95) + (bassEnergy * 0.05);

  // 4. Detect peak (local maximum above threshold)
  static float lastBassEnergy = 0;
  static unsigned long lastBeatTime = 0;
  unsigned long now = millis();

  bool isPeak = (bassEnergy > lastBassEnergy) &&
                (bassEnergy > avgBassEnergy * BEAT_THRESHOLD) &&
                (now - lastBeatTime > MIN_BEAT_INTERVAL);

  if (isPeak) {
    // Beat detected!
    onBeatDetected(now);
    lastBeatTime = now;
  }

  lastBassEnergy = bassEnergy;
}
```

### 4.4 Tempo Tracking (Simple Approach)

```cpp
void onBeatDetected(unsigned long beatTime) {
  static unsigned long lastBeatTime = 0;

  if (lastBeatTime > 0) {
    unsigned long interval = beatTime - lastBeatTime;

    // Calculate BPM
    float instantBPM = 60000.0 / interval;

    // Exponential moving average for smoothing
    static float avgBPM = 120.0;  // Initial guess
    avgBPM = (avgBPM * 0.9) + (instantBPM * 0.1);

    Serial.printf("Beat! Interval: %lu ms, BPM: %.1f\n", interval, avgBPM);
  }

  lastBeatTime = beatTime;

  // Trigger LED
  triggerLEDPattern();
}
```

---

## 5. LED Pattern Design (POC)

### 5.1 Phase 1: Single LED Blink

**Simplest test - validate beat detection only:**
```cpp
void triggerLEDPattern() {
  // Blink Blue LED on each beat
  analogWrite(LED_BLUE, 255);  // Full brightness
  delay(50);                    // Short pulse
  analogWrite(LED_BLUE, 0);     // Off
}
```

### 5.2 Phase 2: Rotating Pattern

**Add visual variety - validate pattern control:**
```cpp
void triggerLEDPattern() {
  static int currentLED = 0;
  const int ledPins[] = {LED_BLUE, LED_RED, LED_GREEN, LED_YELLOW};

  // Turn off all LEDs
  for (int i = 0; i < 4; i++) {
    analogWrite(ledPins[i], 0);
  }

  // Light up current LED (with minimum effective duty)
  analogWrite(ledPins[currentLED], 255);

  // Rotate to next LED
  currentLED = (currentLED + 1) % 4;

  // Quick pulse
  delay(50);
  analogWrite(ledPins[currentLED], 0);
}
```

### 5.3 Phase 3: Intensity Mapping

**React to energy level - validate dynamic control:**
```cpp
void triggerLEDPattern(float energy) {
  // Map energy to brightness (with minimum duty constraint)
  int brightness = map(energy, 0, maxEnergy, 70, 255);
  brightness = constrain(brightness, 70, 255);

  // Pulse with energy-based brightness
  analogWrite(LED_BLUE, brightness);
  delay(50);
  analogWrite(LED_BLUE, 0);
}
```

---

## 6. Libraries and Dependencies

### 6.1 Required Libraries

**ArduinoFFT** (Primary)
- Repository: https://github.com/kosme/arduinoFFT
- Version: Latest (v2.x recommended)
- PlatformIO: `kosme/arduinoFFT@^2.0.0`
- License: GPL-3.0

**ESP32 I2S Driver** (Built-in)
- Part of ESP-IDF / Arduino-ESP32
- No separate installation needed
- Documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html

### 6.2 Optional Libraries (Future)

**FastLED** (if we want advanced effects)
- For complex LED patterns
- Not needed for POC

---

## 7. Development Setup

### 7.1 PlatformIO Environment

**Add new environment to `platformio.ini`:**

```ini
[env:party-poc]
platform = espressif32
board = esp32dev
framework = arduino
upload_port = COM4
monitor_port = COM4
monitor_speed = 115200
lib_deps =
    kosme/arduinoFFT@^2.0.0
build_flags =
    -D POC_MODE
build_src_filter =
    +<../poc/>
    -<*>
```

**Build Commands:**
```bash
# Build POC
pio run -e party-poc

# Upload POC
pio run -e party-poc -t upload

# Monitor output
pio device monitor -e party-poc
```

### 7.2 Project Structure

```
shimon-midburn-installation/
├── poc/
│   ├── party_mode_poc.cpp        # POC main file
│   ├── audio_capture.h/.cpp      # I2S audio functions (optional split)
│   ├── beat_detector.h/.cpp      # Beat detection logic (optional split)
│   └── README.md                 # POC-specific setup notes
├── include/
│   └── shimon.h                  # Pin definitions (shared)
├── platformio.ini                # Add [env:party-poc]
├── PARTY_MODE_POC_DESIGN.md      # This document
└── PARTY_MODE_REQUIREMENTS.md    # Full requirements
```

### 7.3 Test Audio Setup

**Ableton Test Tracks (Progressive Complexity):**

**Phase 1: Simple Kick Loop**
- Metronome or single kick drum
- Fixed tempo (e.g., 120 BPM)
- Purpose: Validate basic beat detection

**Phase 2: Electronic Pattern**
- Simple techno beat with kick + hi-hat
- Fixed tempo (120-140 BPM)
- Purpose: Test with musical context

**Phase 3: Tempo Change**
- Track with gradual tempo shift (±10 BPM)
- Purpose: Validate tempo tracking

**Phase 4: Real Tracks**
- Actual techno/house/trance tracks
- Includes breaks, buildups, drops
- Purpose: Real-world validation

**Audio Routing & Format:**
```
Development:
Ableton Live (44.1 kHz SPDIF output) →
Focusrite Scarlett 16i16 (SPDIF out) →
SPDIF-to-I2S Converter → ESP32 (GPIO 26, 25, 22)

Production:
DJ Mixer (Pioneer DJM-900NXS2, 44.1 kHz SPDIF output) →
SPDIF-to-I2S Converter → ESP32 (GPIO 26, 25, 22)
```

**SPDIF Source Configuration:**
- Set both Focusrite and Pioneer mixer to 44.1 kHz output
- Both devices support this format natively
- Verify converter is configured for 44.1 kHz I2S output

---

## 8. Testing Methodology

### 8.1 Test Protocol

**For each test track:**

1. **Setup:**
   - Upload POC code to ESP32
   - Connect serial monitor (115200 baud)
   - Start audio playback (Ableton)
   - Observe LED behavior

2. **Measurements:**
   - Count detected beats vs. actual beats (1-minute window)
   - Note false positives (beats detected when none played)
   - Note false negatives (missed beats)
   - Observe LED sync visually (does it "feel" synced?)
   - Check serial output for BPM accuracy

3. **Documentation:**
   - Track name / BPM
   - Detection accuracy (% correct)
   - False positive rate
   - Latency (subjective: good/acceptable/poor)
   - Parameter values used (threshold, min interval, etc.)
   - Any tuning changes made

4. **Iteration:**
   - Adjust parameters if needed
   - Re-test to confirm improvement
   - Document what works and what doesn't

### 8.2 Results Template

**Test Log Format:**

```markdown
## Test Session: [Date]

### Test 1: Simple Kick Loop
- **Track:** 120 BPM Kick Metronome
- **Duration:** 60 seconds
- **Actual Beats:** 120
- **Detected Beats:** 115
- **Accuracy:** 95.8%
- **False Positives:** 2
- **Latency:** Good (visually synced)
- **Parameters:**
  - FFT_SIZE: 2048
  - BEAT_THRESHOLD: 1.5
  - MIN_BEAT_INTERVAL: 300ms
- **Notes:** Missed 5 beats during first 10 seconds (warmup?), then stable.

### Test 2: Techno Track
- **Track:** [Track Name] - 128 BPM
- **Duration:** 120 seconds
- ... (continue format)
```

### 8.3 Success Evaluation

**After testing, answer:**
- [ ] Does beat detection meet minimum 80% accuracy?
- [ ] Is latency acceptable (<100ms)?
- [ ] Does it work across multiple test tracks without retuning?
- [ ] Is the system stable (no crashes, no memory leaks)?
- [ ] Does the visual sync "feel" good subjectively?

**Decision Point:**
- **If YES to all:** Proceed with full Party Mode implementation
- **If NO to any:** Document issues, iterate on POC, or reconsider approach

---

## 9. Known Challenges & Mitigation

### 9.1 Challenge: Latency

**Issue:** Processing time (FFT + detection) delays LED response

**Mitigation:**
- Use 2048 sample FFT (not larger) - faster processing
- Consider dual-core processing if single-core too slow
- Optimize buffer management (avoid copying)
- Target <50ms total processing time

### 9.2 Challenge: False Positives

**Issue:** Non-kick sounds trigger beat detection (hi-hats, snares)

**Mitigation:**
- Narrow frequency range (focus on 60-100 Hz kick fundamentals)
- Use MIN_BEAT_INTERVAL to prevent rapid false triggers
- Adjust BEAT_THRESHOLD higher if needed
- Consider energy derivative (rate of change) not just peak

### 9.3 Challenge: Tempo Changes

**Issue:** Sudden tempo change causes desync

**Mitigation:**
- Use exponential moving average (smooths gradual changes)
- Detect tempo change (large interval deviation) and reset tracking
- Keep EMA weight adjustable (faster tracking = less smooth)

### 9.4 Challenge: Breaks/Silence

**Issue:** No beats during breakdown, algorithm state becomes stale

**Mitigation:**
- Detect prolonged silence (no beats for X seconds)
- Pause pattern updates during breaks (hold last state)
- Resume on next detected beat
- Don't let averages drift too far during silence

### 9.5 Challenge: Memory Constraints

**Issue:** FFT buffers + audio buffers use significant RAM

**Mitigation:**
- Use in-place FFT (no separate output buffer)
- Single or double buffer (not larger ring buffer)
- Monitor free heap during development
- Optimize buffer sizes if needed

---

## 10. Debug & Monitoring

### 10.1 Serial Output

**Essential debug information:**
```cpp
// Startup info
Serial.printf("POC Version: 1.0\n");
Serial.printf("FFT Size: %d, Sample Rate: %d Hz (44.1 kHz)\n", FFT_SIZE, SAMPLE_RATE);
Serial.printf("Frequency Resolution: %.2f Hz per bin\n", (float)SAMPLE_RATE / FFT_SIZE);
Serial.printf("Kick Range: %d - %d Hz\n", KICK_FREQ_MIN, KICK_FREQ_MAX);

// Periodic monitoring
Serial.printf("[%lu] Bass Energy: %.2f, Avg: %.2f, Threshold: %.2f\n",
              millis(), bassEnergy, avgEnergy, avgEnergy * BEAT_THRESHOLD);

// Beat detection
Serial.printf("BEAT! Interval: %lu ms, BPM: %.1f\n", interval, bpm);

// Errors
Serial.printf("ERROR: I2S read failed\n");
```

### 10.2 Visual Indicators

**Beyond LED patterns:**
- Flash built-in LED on beat detection (redundant indicator)
- Different color for threshold-exceeded vs. beat-confirmed
- Brightness proportional to bass energy (debug mode)

### 10.3 Performance Monitoring

```cpp
// Track processing time
unsigned long startTime = micros();
performFFT();
unsigned long fftTime = micros() - startTime;

// Log periodically
if (millis() - lastLogTime > 5000) {
  Serial.printf("FFT Time: %lu us, Free Heap: %d bytes\n",
                fftTime, ESP.getFreeHeap());
  lastLogTime = millis();
}
```

---

## 11. Parameter Tuning Guide

### 11.1 Primary Tuning Parameters

| Parameter | Initial Value | Effect | Tuning Direction |
|-----------|---------------|--------|------------------|
| `BEAT_THRESHOLD` | 1.5 | Beat sensitivity | ↑ = fewer false positives, more misses<br>↓ = more detections, more false positives |
| `MIN_BEAT_INTERVAL` | 300 ms | Rate limiter | ↑ = prevents double-hits, may miss fast beats<br>↓ = catches fast beats, may double-trigger |
| `KICK_FREQ_MIN` | 60 Hz | Low-end cutoff | ↑ = ignore sub-bass rumble<br>↓ = catch deeper kicks |
| `KICK_FREQ_MAX` | 120 Hz | High-end cutoff | ↑ = include more bass harmonics<br>↓ = focus on fundamental only |
| `FFT_SIZE` | 2048 | Frequency resolution vs. speed | ↑ = better resolution, slower<br>↓ = faster, less resolution |

### 11.2 Tuning Process

**Step 1:** Start with simple kick loop
- Verify beats detected (should be ~100% accurate)
- If many false positives: increase `BEAT_THRESHOLD`
- If missing beats: decrease `BEAT_THRESHOLD`

**Step 2:** Add musical context (techno beat)
- If hi-hats trigger beats: narrow `KICK_FREQ_MAX` to <100 Hz
- If still issues: increase `BEAT_THRESHOLD`

**Step 3:** Test tempo changes
- If slow to adapt: decrease EMA weight (faster tracking)
- If too jittery: increase EMA weight (smoother)

**Step 4:** Test real tracks
- If breaks cause issues: add silence detection
- If buildups trigger too much: check energy derivative

---

## 12. Next Steps After POC

### 12.1 If POC Succeeds

**Immediate:**
1. Document final parameters in this file
2. Archive successful test results
3. Create git tag: `poc-success-v1.0`

**Integration Planning:**
1. Refactor POC code into modular functions
2. Design integration with main FSM (MODE_SELECTION, PARTY_MODE states)
3. Plan pattern library expansion
4. Design configuration system (move params to shimon.h)

**Full Implementation:**
- Follow `PARTY_MODE_REQUIREMENTS.md` Phase 1-9
- Integrate beat detection into main codebase
- Implement mode selection logic
- Build pattern library

### 12.2 If POC Requires Iteration

**Troubleshooting:**
- Review serial logs for patterns
- Try different FFT sizes (1024, 4096)
- Test different frequency ranges
- Consider time-domain detection (if FFT insufficient)
- Research alternative algorithms

**Decision Point:**
- If iteration shows promise: continue refining
- If fundamental issues: document findings, explore alternatives
- If hardware limitation: consider external audio processor

### 12.3 If POC Fails

**Document Lessons:**
- What didn't work and why
- What parameters were tried
- Hardware limitations discovered
- Alternative approaches to consider

**Alternative Paths:**
- External beat detection hardware (expensive)
- Pre-analyzed music (BPM tracks with beat markers)
- Simplified mode (manual BPM input + click track)
- Different technology (dedicated DSP chip)

---

## 13. Reference Materials

### 13.1 Code Examples
- [ESP32-music-beat-sync](https://github.com/blaz-r/ESP32-music-beat-sync) - Direct reference project
- [audio-reactive-led-strip](https://github.com/zhujisheng/audio-reactive-led-strip) - Pattern ideas
- [ArduinoFFT Examples](https://github.com/kosme/arduinoFFT/tree/master/examples) - FFT usage

### 13.2 Theory & Background
- [FFT on the ESP32](http://www.robinscheibler.org/2017/12/12/esp32-fft.html) - Performance analysis
- [Beat Detection Algorithms](https://www.clear.rice.edu/elec301/Projects01/beat_sync/beatalgo.html) - Theory
- [Audio Signal Processing Basics](https://en.wikipedia.org/wiki/Audio_signal_processing) - General reference

### 13.3 Project Documents
- `PARTY_MODE_REQUIREMENTS.md` - Full feature requirements
- `hardware-baseline.md` - As-built hardware documentation
- `CLAUDE.md` - Existing game mode documentation
- `include/shimon.h` - Pin definitions and configuration

---

## 14. Document Maintenance

**Update this document when:**
- Parameters are tuned and finalized
- Test results are significant
- Successful approach is validated
- Alternative approaches are explored
- Integration decisions are made

**Version History:**

| Date       | Version | Changes                           | Author       |
|------------|---------|-----------------------------------|--------------|
| 2026-01-27 | 1.0     | Initial POC design document       | Claude + User |
| 2026-01-27 | 1.1     | Updated to use 44.1 kHz sample rate, added SPDIF format documentation | Claude + User |

---

## Appendix A: Quick Start Checklist

**Before starting POC development:**
- [ ] Review this design document completely
- [ ] Verify hardware connections (I2S pins, LED pins)
- [ ] Install ArduinoFFT library
- [ ] Set up PlatformIO environment
- [ ] Prepare test audio tracks in Ableton
- [ ] Set up serial monitor for debugging

**During development:**
- [ ] Start with Phase 1 (single LED blink)
- [ ] Use simple kick loop for initial testing
- [ ] Document all parameter changes
- [ ] Log test results using template in Section 8.2
- [ ] Iterate based on findings

**After successful POC:**
- [ ] Archive final code and parameters
- [ ] Document test results summary
- [ ] Create integration plan
- [ ] Update main requirements document
- [ ] Proceed to full implementation

---

**Ready to begin POC development!**