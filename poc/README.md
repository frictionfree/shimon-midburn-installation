# Party Mode Proof of Concept

This directory contains the Party Mode POC code demonstrating the working audio-visual system.

## Status: v8.1 - Working

The POC successfully detects musical context (STANDARD/BREAK/DROP states) synchronized to MIDI Clock timing.

## Architecture

The POC uses a **dual-source approach**:
- **MIDI Clock** (24 PPQN) as timing authority for beat/bar alignment
- **I2S Audio** for musical context detection (energy, transients, kick variance)

### Key Components

1. **MIDI Timing Service** (GPIO34 via optocoupler)
   - 24 PPQN clock messages (0xF8)
   - Beat detection every 24 ticks
   - Bar boundaries every 4 beats

2. **I2S Audio Analysis** (BCLK=26, LRCK=25, DATA=22)
   - 24-bit right-justified samples in 32-bit words
   - Correct decode: `int32_t sample = raw >> 8; float x = sample / 8388608.0f;`
   - 75ms monitoring windows for real-time metrics
   - Bar-level metrics for state transitions

3. **Musical State Machine**
   - STANDARD: Normal playback with kick present
   - BREAK_CANDIDATE: Kick absence detected, evaluating
   - BREAK_CONFIRMED: In musical break section
   - DROP: High-energy return moment (8 bars)

4. **Visual System** (Basic patterns per state)
   - VIS_STD: Rotating single wing on each beat
   - VIS_BREAK: Random crossfade between wings (2-beat duration)
   - VIS_DROP: Fast half-beat rotation with crossfades

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

**MIDI Input (Optocoupler):**
- MIDI DIN Pin 5 (current source) → 220Ω → 6N138 Pin 2
- MIDI DIN Pin 4 (current sink) → 6N138 Pin 3
- 6N138 Pin 6 → GPIO 34 + 10kΩ pull-up to 3.3V

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
- Development: Ableton Live → Focusrite Scarlett 16i16 (SPDIF @ 48kHz)
- Production: Pioneer DJM-900NXS2 (SPDIF @ 48kHz)
- Both → SPDIF-to-I2S converter → ESP32

### 3. Test Procedure

**Phase 1: MIDI Sync Verification**
- Start Ableton transport at 120 BPM
- Observe serial output showing bar.beat positions
- Verify beat interval is ~500ms (120 BPM)

**Phase 2: Baseline Learning**
- Play kick-heavy music for 16+ bars
- Watch for `BASELINE_READY` event in serial
- State machine now active

**Phase 3: State Transitions**
- Play music with breaks (kick drops out)
- Watch for `STD→CAND→BREAK` transitions
- Play a drop (kick returns with energy)
- Watch for `BREAK→DROP→STD` cycle

---

## Configuration

**Key thresholds in `party_mode_poc.cpp`:**

```cpp
// Baseline
static constexpr uint16_t BASELINE_MIN_QUALIFIED_BARS = 16;

// CAND/BREAK detection
static constexpr float KICK_GONE_KR_MAX = 0.60f;    // kR < 0.60 triggers CAND
static constexpr float DEEP_BREAK_KR_MAX = 0.40f;   // stricter for BREAK confirm

// DROP detection (Return-Impact model)
static constexpr float DROP_BF_KV_MIN  = 2.50f;     // kick resurgence vs break floor
static constexpr float DROP_BF_RMS_MIN = 1.55f;     // energy lift vs break floor

// Visual brightness
static constexpr uint8_t STD_BRIGHT   = 170;
static constexpr uint8_t BREAK_BRIGHT = 150;
static constexpr uint8_t DROP_BRIGHT  = 235;
```

See `../PARTY_MODE_REQUIREMENTS.md` Section 5 for complete threshold documentation.

---

## Serial Output

**Expected output (bar start):**
```
pos=12.1 t_us=12345678 dt_us=500000 ticks=24 wAge_ms=12 wRms=0.0456 wTr=0.00123 wKVar=0.000456 bAge_ms=2 bRms=0.0445 bTr=0.00118 bKVar=0.000448 | ctx=STD rR=1.02 tR=0.98 kR=1.05 base=READY(16/16)
```

**State transition:**
```
STATE STD->CAND pos=45.1 why=CAND_ENTER_KICK_ABSENCE
STATE CAND->BREAK pos=48.1 why=BREAK_CONFIRM_DEEP_2B
EVENT RETURN_START pos=52.3 w_bfK=1.85
EVENT DROP_CONFIRMED_RETURN pos=52.4 pk_bfK=2.65 pk_bfR=1.72 pk_bfT=1.88
STATE BREAK->DROP pos=52.4 why=RETURN_IMPACT_PEAKS
```

---

## Troubleshooting

**No MIDI clock received:**
- Check optocoupler wiring (220Ω resistor, correct DIN pins)
- Verify Ableton is sending MIDI clock to correct output
- Check serial for `[MIDI_START]` message

**Baseline never becomes ready:**
- Ensure music has consistent kick pattern
- Check `BASELINE_MIN_RMS` threshold (0.020)
- Verify I2S audio signal present (wRms > 0)

**False CAND triggers:**
- Increase `KICK_GONE_CONFIRM_WINDOWS` (currently 4 = ~300ms)
- Verify music has strong, consistent kick

**DROP not detected:**
- Lower `DROP_BF_KV_MIN` threshold
- Check break floor is being established (bfK values in logs)
- Ensure kick return is strong enough

**LEDs not lighting:**
- Check LED wiring (GPIO 23, 19, 18, 5)
- Verify MOSFET module is powered
- Check `MIN_VISIBLE_DUTY` (70/255 minimum)

---

## Files

- `party_mode_poc.cpp` - Main POC code (v8.1)
- `README.md` - This file
- `../PARTY_MODE_REQUIREMENTS.md` - Complete Party Mode requirements
- `../hardware-baseline.md` - Hardware configuration

---

## Success Criteria

**POC is successful if:**
- ✅ MIDI Clock provides reliable beat/bar timing
- ✅ Baseline learns within 16-32 bars of music
- ✅ BREAK detection triggers on kick absence
- ✅ DROP detection triggers on high-impact return
- ✅ Visual patterns differentiate states clearly
- ✅ Runs stably for >30 minutes

**Current Status: All criteria met**

---

## Next Steps

1. ✅ POC validated with MIDI + I2S architecture
2. ⬜ Implement Chapter 13 visual pattern system:
   - 9 patterns (3 per state: STD-01/02/03, BRK-01/02/03, DRP-01/02/03)
   - Pattern switching (round-robin, 8-bar windows)
   - Power budgeting model
   - Pattern execution framework
3. ⬜ Integrate into main FSM (MODE_SELECTION, PARTY_MODE)
4. ⬜ Build full pattern library
5. ⬜ Field testing with DJ equipment

---

## Notes

- This POC runs independently from the main game code
- No DFPlayer audio (MIDI + I2S only)
- Basic FSM covers mode selection stub only
- Visual patterns are minimal (1 pattern per state)
- Full pattern system per Chapter 13 is next phase
