#include <Arduino.h>
#include <math.h>
#include "hw.h"
#include "party_patterns.h"

// ============================================================
// VISUAL TUNABLES
// ============================================================
static constexpr float   CAP_STANDARD    = 0.70f;
static constexpr float   CAP_BREAK       = 0.50f;
static constexpr float   CAP_DROP        = 1.00f;
static constexpr float   CAND_DIM        = 0.55f;
static constexpr uint8_t BASE_BRIGHT     = 235;
static constexpr uint8_t PATTERN_LEN_BARS = 8;
static constexpr float   BREAK_FADE_BEATS = 2.0f;
static constexpr float   DROP_OVERLAP_FRAC = 0.10f;

// ============================================================
// INTERNAL TYPES
// ============================================================
enum VisualMode : uint8_t { VIS_STD=0, VIS_BREAK=1, VIS_DROP=2, VIS_DEBUG=3 };

// ============================================================
// CONTEXT (set by pp_setContext each beat)
// ============================================================
static ContextState ppState          = STANDARD;
static uint32_t     ppBeatIntervalUs = 500000;  // default 120 BPM

// ============================================================
// ROTATION ORDERS
// ============================================================
static const Color CW_ORDER[4]  = { BLUE, RED, GREEN, YELLOW };
static const Color CCW_ORDER[4] = { BLUE, YELLOW, GREEN, RED };

// ============================================================
// WING REQUEST SYSTEM
// ============================================================
static float wingRequest[4] = {0,0,0,0};

static inline float pp_clamp01(float x) {
  return (x < 0.0f) ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static uint8_t dutyFromLevel(float level01, uint8_t target) {
  level01 = pp_clamp01(level01);
  if (level01 <= 0.0f) return 0;
  if (target <= HW_PWM_MIN_DUTY) return target;
  float d = (float)HW_PWM_MIN_DUTY + level01 * ((float)target - (float)HW_PWM_MIN_DUTY);
  if (d < 0) d = 0;
  if (d > 255) d = 255;
  return (uint8_t)(d + 0.5f);
}

static void clearRequests() {
  for (int i = 0; i < 4; i++) wingRequest[i] = 0.0f;
}

static void setWing(Color w, float brightness) {
  if (w >= COLOR_COUNT) return;
  wingRequest[w] = pp_clamp01(brightness);
}

static void addWing(Color w, float brightness) {
  if (w >= COLOR_COUNT) return;
  wingRequest[w] = pp_clamp01(wingRequest[w] + brightness);
}

static float getStateCap() {
  switch (ppState) {
    case STANDARD:        return CAP_STANDARD;
    case BREAK_CANDIDATE: return CAP_STANDARD * CAND_DIM;
    case BREAK_CONFIRMED: return CAP_BREAK;
    case DROP:            return CAP_DROP;
    default:              return CAP_STANDARD;
  }
}

static void commitRequests() {
  float cap = getStateCap();
  uint8_t duties[4];
  for (int i = 0; i < 4; i++) {
    duties[i] = dutyFromLevel(wingRequest[i] * cap, BASE_BRIGHT);
  }
  hw_led_all_set(duties);
}

static float easeInOut(float t) {
  t = pp_clamp01(t);
  return 0.5f * (1.0f - cosf(t * 3.14159265f));
}

// ============================================================
// PATTERN STATE
// ============================================================
static PatternID activePattern    = PAT_STD_01;
static uint8_t   stdPatternIdx    = 0;
static uint8_t   brkPatternIdx    = 0;
static uint8_t   drpPatternIdx    = 0;
static bool      ppPatternLocked  = false;  // true = pp_setPattern(); suppresses round-robin switching

static const PatternID STD_PATTERNS[3] = { PAT_STD_01, PAT_STD_02, PAT_STD_03 };
static const PatternID BRK_PATTERNS[3] = { PAT_BRK_01, PAT_BRK_02, PAT_BRK_03 };
static const PatternID DRP_PATTERNS[3] = { PAT_DRP_01, PAT_DRP_02, PAT_DRP_03 };

static uint8_t  patWindowBar       = 0;
static uint8_t  patWindowBeat      = 1;
static uint32_t ppBeatIndex        = 0;  // monotonic beat counter

// BREAK crossfade state
static bool     breakFading        = false;
static Color    breakFrom          = BLUE;
static Color    breakTo            = GREEN;
static uint32_t breakFadeStartUs   = 0;
static uint32_t breakFadeDurUs     = 1000000;
static Color    brkLastWing        = BLUE;

// DROP state
static uint8_t  dropStep           = 0;
static uint32_t lastHalfBeatUs     = 0;
static uint32_t halfBeatUs         = 250000;

// STD-02 retrigger state
static bool     std02IsAccent      = false;
static uint32_t std02RetriggerUs   = 0;

// DRP-02 axis state
static bool     drp02Axis13        = true;

// Visual mode
static VisualMode visMode          = VIS_STD;
static ContextState prevStateForPat = STANDARD;

// ============================================================
// PATTERN IMPLEMENTATIONS
// ============================================================

// --- STD-01: Groove Rotation ---
static void patStd01OnBeat(uint8_t bar, uint8_t beat) {
  clearRequests();
  bool forward = (bar <= 4);
  uint8_t totalBeats = ((bar - 1) * 4) + (beat - 1);
  uint8_t pos;
  if (forward) {
    pos = totalBeats % 4;
  } else {
    pos = (4 - (totalBeats % 4)) % 4;
  }
  setWing(CW_ORDER[pos], 1.0f);
  commitRequests();
}

// --- STD-02: Edge Oscillation Walk ---
static void patStd02OnBeat(uint8_t bar, uint8_t beat) {
  clearRequests();
  static const Color EDGE_PATTERN[4][4] = {
    { RED,    BLUE,   RED,    BLUE   },
    { GREEN,  RED,    GREEN,  RED    },
    { YELLOW, GREEN,  YELLOW, GREEN  },
    { BLUE,   YELLOW, BLUE,   YELLOW }
  };
  uint8_t barIdx  = (bar - 1) % 4;
  uint8_t beatIdx = beat - 1;
  setWing(EDGE_PATTERN[barIdx][beatIdx], 1.0f);
  commitRequests();
}

// --- STD-03: Diagonal Pairs ---
static void patStd03OnBeat(uint8_t bar, uint8_t beat) {
  clearRequests();
  bool oddBar = (bar % 2) == 1;
  Color pair[2];
  if (oddBar) { pair[0] = BLUE;  pair[1] = GREEN;  }
  else         { pair[0] = RED;   pair[1] = YELLOW; }
  setWing(((beat % 2) == 1) ? pair[0] : pair[1], 1.0f);
  commitRequests();
}

// --- BRK-01: Slow Drift Relay ---
static void patBrk01OnBeat(uint8_t bar, uint8_t beat) {
  if ((beat == 1) || (beat == 3)) {
    Color next = brkLastWing;
    while (next == brkLastWing) next = (Color)(esp_random() % 4);
    breakFrom = brkLastWing;
    breakTo   = next;
    brkLastWing = next;
    breakFading = true;
    breakFadeStartUs = micros();
    breakFadeDurUs = (uint32_t)(BREAK_FADE_BEATS * (float)ppBeatIntervalUs);
  }
}

// --- BRK-02: Breathing Anchor ---
static void patBrk02OnBeat(uint8_t bar, uint8_t beat) {
  Color w = CW_ORDER[(bar - 1) % 4];
  breakFrom = w;
  breakTo   = w;
  if (beat == 1) {
    breakFading = true;
    breakFadeStartUs = micros();
    breakFadeDurUs = 4 * ppBeatIntervalUs;
  }
}

// --- BRK-03: Dual Flow Weave ---
static void patBrk03OnBeat(uint8_t bar, uint8_t beat) {
  if ((beat == 1) || (beat == 3)) {
    Color next = CW_ORDER[((uint8_t)brkLastWing + 1) % 4];
    breakFrom = brkLastWing;
    breakTo   = next;
    brkLastWing = next;
    breakFading = true;
    breakFadeStartUs = micros();
    breakFadeDurUs = (uint32_t)(BREAK_FADE_BEATS * (float)ppBeatIntervalUs);
  }
}

// --- DRP-01: Impact Chase ---
static void patDrp01OnBeat(uint8_t bar, uint8_t beat) {
  bool forward = (bar <= 4);
  uint8_t totalHalfBeats = ((bar - 1) * 8) + ((beat - 1) * 2);
  if (forward) {
    dropStep = totalHalfBeats % 8;
  } else {
    dropStep = (8 - (totalHalfBeats % 8)) % 8;
  }
  lastHalfBeatUs = micros();
}

static void patDrp01OnHalfBeat() {
  dropStep = (dropStep + 1) % 8;
  lastHalfBeatUs = micros();
}

// --- DRP-02: Alternating Burst Drive ---
static void patDrp02OnBeat(uint8_t bar, uint8_t beat) {
  clearRequests();
  drp02Axis13 = (bar == 1 || bar == 2 || bar == 5 || bar == 6);
  if (drp02Axis13) { setWing(BLUE, 1.0f); setWing(GREEN, 1.0f); }
  else              { setWing(RED,  1.0f); setWing(YELLOW, 1.0f); }
  dropStep = 0;
  lastHalfBeatUs = micros();
  commitRequests();
}

static void patDrp02OnHalfBeat() {
  clearRequests();
  dropStep = (dropStep + 1) % 2;
  if (dropStep == 1) {
    if (!drp02Axis13) { setWing(BLUE, 0.6f); setWing(GREEN, 0.6f); }
    else               { setWing(RED,  0.6f); setWing(YELLOW, 0.6f); }
  } else {
    if (drp02Axis13)   { setWing(BLUE, 1.0f); setWing(GREEN, 1.0f); }
    else               { setWing(RED,  1.0f); setWing(YELLOW, 1.0f); }
  }
  lastHalfBeatUs = micros();
  commitRequests();
}

// --- DRP-03: Expanding Impact Wave ---
// 16-step symmetrical cycle at half-beat rate: 1-2-3-4-4-3-2-1-1-2-3-4-4-3-2-1
static const uint8_t DRP03_WINGS[16] = {1,2,3,4,4,3,2,1,1,2,3,4,4,3,2,1};

static void patDrp03Step() {
  dropStep = (dropStep + 1) % 16;
  lastHalfBeatUs = micros();
  clearRequests();
  uint8_t n = DRP03_WINGS[dropStep];
  for (uint8_t i = 0; i < n; i++) setWing(CW_ORDER[i], 1.0f);
  commitRequests();
}

static void patDrp03OnBeat(uint8_t bar, uint8_t beat) {
  if (bar == 1 && beat == 1) dropStep = 15;
  patDrp03Step();
}

static void patDrp03OnHalfBeat() {
  patDrp03Step();
}

// ============================================================
// PATTERN DISPATCH
// ============================================================
static void patternOnBeat(uint8_t bar, uint8_t beat) {
  switch (activePattern) {
    case PAT_STD_01: patStd01OnBeat(bar, beat); break;
    case PAT_STD_02: patStd02OnBeat(bar, beat); break;
    case PAT_STD_03: patStd03OnBeat(bar, beat); break;
    case PAT_BRK_01: patBrk01OnBeat(bar, beat); break;
    case PAT_BRK_02: patBrk02OnBeat(bar, beat); break;
    case PAT_BRK_03: patBrk03OnBeat(bar, beat); break;
    case PAT_DRP_01: patDrp01OnBeat(bar, beat); break;
    case PAT_DRP_02: patDrp02OnBeat(bar, beat); break;
    case PAT_DRP_03: patDrp03OnBeat(bar, beat); break;
    default: break;
  }
}

static void patternOnHalfBeat() {
  switch (activePattern) {
    case PAT_DRP_01: patDrp01OnHalfBeat(); break;
    case PAT_DRP_02: patDrp02OnHalfBeat(); break;
    case PAT_DRP_03: patDrp03OnHalfBeat(); break;
    default: break;
  }
}

// ============================================================
// VISUAL MODE MANAGEMENT
// ============================================================
static VisualMode modeForState(ContextState s) {
  switch (s) {
    case BREAK_CONFIRMED: return VIS_BREAK;
    case DROP:            return VIS_DROP;
    default:              return VIS_STD;
  }
}

static void onVisualModeEnter(VisualMode m) {
  if (m == VIS_BREAK) { breakFading = false; }
  else if (m == VIS_DROP) { dropStep = 0; lastHalfBeatUs = micros(); }
}

static void refreshVisualMode() {
  VisualMode m = modeForState(ppState);
  if (m != visMode) { visMode = m; onVisualModeEnter(visMode); }
}

// ============================================================
// PUBLIC API IMPLEMENTATION
// ============================================================

const char* pp_patternName(PatternID p) {
  switch (p) {
    case PAT_STD_01: return "S-1";
    case PAT_STD_02: return "S-2";
    case PAT_STD_03: return "S-3";
    case PAT_BRK_01: return "B-1";
    case PAT_BRK_02: return "B-2";
    case PAT_BRK_03: return "B-3";
    case PAT_DRP_01: return "D-1";
    case PAT_DRP_02: return "D-2";
    case PAT_DRP_03: return "D-3";
    default: return "?";
  }
}

const char* pp_ctxName(ContextState s) {
  switch (s) {
    case STANDARD:        return "STD";
    case BREAK_CANDIDATE: return "CAND";
    case BREAK_CONFIRMED: return "BREAK";
    case DROP:            return "DROP";
    default:              return "?";
  }
}

PatternID pp_activePattern() { return activePattern; }

void pp_setContext(ContextState state, uint32_t beatIntervalUs) {
  ppState          = state;
  ppBeatIntervalUs = beatIntervalUs;
}

void pp_setPattern(PatternID p) {
  activePattern    = p;
  ppPatternLocked  = true;
  breakFading      = false;
  dropStep         = 0;
  lastHalfBeatUs   = micros();
  patWindowBar     = 0;
  patWindowBeat    = 1;
}

void pp_selectForState(ContextState s) {
  ppPatternLocked = false;
  switch (s) {
    case STANDARD:
    case BREAK_CANDIDATE:
      activePattern = STD_PATTERNS[stdPatternIdx];
      stdPatternIdx = (stdPatternIdx + 1) % 3;
      break;
    case BREAK_CONFIRMED:
      activePattern = BRK_PATTERNS[brkPatternIdx];
      brkPatternIdx = (brkPatternIdx + 1) % 3;
      break;
    case DROP:
      activePattern = DRP_PATTERNS[drpPatternIdx];
      drpPatternIdx = (drpPatternIdx + 1) % 3;
      break;
    default:
      activePattern = PAT_STD_01;
      break;
  }
  breakFading   = false;
  dropStep      = 0;
  lastHalfBeatUs = micros();
  patWindowBar  = 0;
  patWindowBeat = 1;
}

void pp_onBeat(uint8_t bar, uint8_t beat) {
  ppBeatIndex++;
  bool isBarStart = (beat == 1);

  refreshVisualMode();

  // Pattern selection on state transition
  bool shouldSelect = false;
  if (ppState != prevStateForPat) {
    if (ppState == BREAK_CONFIRMED || ppState == DROP) {
      shouldSelect = true;
    } else if (ppState == STANDARD &&
               (prevStateForPat == BREAK_CONFIRMED || prevStateForPat == DROP)) {
      shouldSelect = true;
    }
    prevStateForPat = ppState;
  }
  if (shouldSelect && !ppPatternLocked) {
    pp_selectForState(ppState);
    patWindowBar = 0;
    Serial.printf("PATTERN_SELECT pat=%s state=%s\n",
                  pp_patternName(activePattern), pp_ctxName(ppState));
  }

  // Advance pattern window
  if (isBarStart) {
    patWindowBar++;
    if (patWindowBar > PATTERN_LEN_BARS) {
      if (!ppPatternLocked) {
        pp_selectForState(ppState);
        Serial.printf("PATTERN_SWITCH pat=%s\n", pp_patternName(activePattern));
      }
      patWindowBar = 1;
    }
    patWindowBeat = 1;
  } else {
    patWindowBeat++;
    if (patWindowBeat > 4) patWindowBeat = 1;
  }

  patternOnBeat(patWindowBar, patWindowBeat);
}

void pp_onHalfBeat() {
  refreshVisualMode();
  if (visMode == VIS_DROP) patternOnHalfBeat();
}

void pp_render() {
  refreshVisualMode();

  const uint32_t nowUs = micros();

  if (visMode == VIS_BREAK) {
    if (!breakFading) return;
    const uint32_t dt = nowUs - breakFadeStartUs;
    float t = (breakFadeDurUs == 0) ? 1.0f : (float)dt / (float)breakFadeDurUs;
    t = pp_clamp01(t);
    float eased = easeInOut(t);
    clearRequests();
    if (activePattern == PAT_BRK_02) {
      float breath = (t < 0.5f) ? easeInOut(t * 2.0f) : easeInOut((1.0f - t) * 2.0f);
      setWing(breakFrom, breath);
    } else {
      setWing(breakFrom, 1.0f - eased);
      setWing(breakTo,   eased);
    }
    commitRequests();
    if (t >= 1.0f) breakFading = false;
  }
  else if (visMode == VIS_DROP) {
    halfBeatUs = ppBeatIntervalUs / 2;
    if (halfBeatUs < 20000) halfBeatUs = 20000;
    uint32_t dt = nowUs - lastHalfBeatUs;
    if (dt > halfBeatUs * 4) dt = halfBeatUs;
    float phase = pp_clamp01((float)dt / (float)halfBeatUs);
    clearRequests();
    if (activePattern == PAT_DRP_01) {
      Color cur = CW_ORDER[dropStep % 4];
      Color nxt = CW_ORDER[(dropStep + 1) % 4];
      float holdEnd = 1.0f - DROP_OVERLAP_FRAC;
      if (phase <= holdEnd) {
        setWing(cur, 1.0f);
      } else {
        float u = pp_clamp01((phase - holdEnd) / DROP_OVERLAP_FRAC);
        setWing(cur, 1.0f - u);
        setWing(nxt, u);
      }
    }
    else if (activePattern == PAT_DRP_02) {
      float pulse    = 0.7f  + 0.3f  * (1.0f - phase);
      float altPulse = 0.4f  + 0.2f  * (1.0f - phase);
      if (dropStep == 0) {
        if (drp02Axis13) { setWing(BLUE, pulse);    setWing(GREEN,  pulse);    }
        else              { setWing(RED,  pulse);    setWing(YELLOW, pulse);    }
      } else {
        if (!drp02Axis13) { setWing(BLUE, altPulse); setWing(GREEN,  altPulse); }
        else               { setWing(RED,  altPulse); setWing(YELLOW, altPulse); }
      }
    }
    else if (activePattern == PAT_DRP_03) {
      float pulse = 0.85f + 0.15f * (1.0f - phase);
      uint8_t n = DRP03_WINGS[dropStep % 16];
      for (uint8_t i = 0; i < n; i++) setWing(CW_ORDER[i], pulse);
    }
    commitRequests();
  }
  // VIS_STD: no continuous render; patterns commit on beat events
}

void pp_reset() {
  activePattern   = PAT_STD_01;
  ppPatternLocked = false;
  stdPatternIdx   = 1;   // next switch gets S-2
  brkPatternIdx   = 0;
  drpPatternIdx   = 0;
  patWindowBar    = 0;
  patWindowBeat   = 1;
  ppBeatIndex     = 0;
  breakFading     = false;
  brkLastWing     = BLUE;
  dropStep        = 0;
  lastHalfBeatUs  = micros();
  std02IsAccent   = false;
  std02RetriggerUs = 0;
  drp02Axis13     = true;
  visMode         = VIS_STD;
  prevStateForPat = STANDARD;
  ppState         = STANDARD;
  hw_led_all_off();
}
