# Party Mode Proof of Concept

This directory contains the Party Mode POC code demonstrating the working audio-visual system.

## Purpose

Validate the Party Mode architecture using:
- **MIDI Clock** as timing authority (beat/bar alignment)
- **I2S Audio** for musical context detection (STANDARD/BREAK/DROP states)
- **Correct 24-bit right-justified decoding** for reliable audio analysis

## Documentation

- **Requirements:** `../PARTY_MODE_REQUIREMENTS.md` - Complete Party Mode specification
- **Hardware:** `../hardware-baseline.md` - I2S and MIDI configuration (Sections 9-10)
- **System:** `../SYSTEM_REQUIREMENTS.md` - Mode selection, boot sequence

---

## Quick Start

### 1. Build and Upload

```bash
# Build POC
pio run -e party-poc

# Upload to ESP32
pio run -e party-poc -t upload

# Monitor serial output
pio device monitor -e party-poc
```

### 2. Hardware Setup

**Audio Input (I2S):**
- BCLK → GPIO 26
- LRCK → GPIO 25
- DATA → GPIO 22

**LED Output:**
- Blue → GPIO 23
- Red → GPIO 19
- Green → GPIO 18
- Yellow → GPIO 5

**Audio Source:**
- Development: Ableton Live → Focusrite Scarlett 16i16 (44.1 kHz SPDIF)
- Production: Pioneer DJM-900NXS2 (44.1 kHz SPDIF)
- Both → SPDIF-to-I2S converter → ESP32

### 3. Test Procedure

**Phase 1: Simple Kick Loop**
- Create 120 BPM kick drum loop in Ableton
- Observe Blue LED blinking on each beat
- Check serial output for beat timestamps and BPM

**Phase 2: Real Music**
- Play techno/house/trance tracks
- Verify beats detected accurately
- Tune parameters if needed

**Phase 3: Patterns**
- Enable rotating pattern (see code)
- Verify pattern synchronizes with music

---

## Configuration

**Edit these parameters in `party_mode_poc.cpp`:**

```cpp
// FFT Configuration
#define FFT_SIZE 2048
#define SAMPLE_RATE 44100

// Beat Detection
#define KICK_FREQ_MIN 60
#define KICK_FREQ_MAX 120
#define BEAT_THRESHOLD 1.5
#define MIN_BEAT_INTERVAL 300
```

**Tuning Guide:**
- `BEAT_THRESHOLD` too high → misses beats
- `BEAT_THRESHOLD` too low → false positives
- `KICK_FREQ_MAX` too high → hi-hats trigger beats
- `MIN_BEAT_INTERVAL` too high → misses fast beats

See `../PARTY_MODE_POC_DESIGN.md` Section 11 for detailed tuning guide.

---

## Serial Output

**Expected output:**
```
Party Mode POC v1.0
FFT Size: 2048, Sample Rate: 44100 Hz (44.1 kHz)
Frequency Resolution: 21.53 Hz per bin
Kick Range: 60 - 120 Hz
I2S initialized successfully
Starting beat detection...

[1234] Bass Energy: 145.32, Avg: 98.45, Threshold: 147.68
BEAT! Interval: 500 ms, BPM: 120.0
[1734] Bass Energy: 142.18, Avg: 99.12, Threshold: 148.68
BEAT! Interval: 498 ms, BPM: 120.2
...
```

---

## Troubleshooting

**No beats detected:**
- Check I2S wiring (BCLK, LRCK, DATA)
- Verify audio source is outputting 44.1 kHz SPDIF
- Lower `BEAT_THRESHOLD` (try 1.2)
- Check serial output for bass energy values

**Too many false positives:**
- Raise `BEAT_THRESHOLD` (try 1.8)
- Narrow `KICK_FREQ_MAX` to 100 Hz
- Increase `MIN_BEAT_INTERVAL` to 400 ms

**LEDs not lighting:**
- Check LED wiring (GPIO 23, 19, 18, 5)
- Verify MOSFET module is powered
- Check minimum duty threshold (should be 70/255 minimum)

**Desync over time:**
- Check tempo tracking logic
- Verify `MIN_BEAT_INTERVAL` isn't too restrictive
- Monitor BPM calculation in serial output

---

## Files

- `party_mode_poc.cpp` - Main POC code (to be created)
- `README.md` - This file
- `../PARTY_MODE_POC_DESIGN.md` - Complete design documentation
- `../PARTY_MODE_REQUIREMENTS.md` - Full Party Mode requirements

---

## Success Criteria

**POC is successful if:**
- ✅ Beat detection accuracy >80% on simple kick loops
- ✅ Visual latency <100ms (feels synchronized)
- ✅ Runs stably for >5 minutes
- ✅ Works with real electronic music tracks

**If successful → Proceed with full Party Mode implementation**

**If unsuccessful → Document findings, iterate, or explore alternatives**

---

## Next Steps After Success

1. Archive successful POC code (git tag)
2. Document final parameters
3. Refactor code into modular functions
4. Integrate into main FSM (MODE_SELECTION, PARTY_MODE)
5. Build pattern library
6. Follow `PARTY_MODE_REQUIREMENTS.md` implementation phases

---

## Notes

- This POC runs independently from the main game code
- No button inputs, no DFPlayer, no FSM
- Just: Audio In → Beat Detection → LED Out
- Keep it simple for easy experimentation
