/*
  Shimon – Party Mode (I2S) + Ableton MIDI Clock (bar.beat logs) – Debug v8.1 + VISUALS v1
  + Failure/MusicStop classifier (Clock-only loss / Audio-only loss / Simultaneous stop)

  v8.1 focus: eliminate "short burst" false DROP positives.

  DROP Detection = Return-Impact model (BREAK-only)
  - DROP is detected ONLY in BREAK_CONFIRMED via:
      Phase A: detect "Return Start" when kick returns vs BREAK floor (window-level)
      Phase B: track peaks for a short window and classify:
        DROP = high-impact return (kick strong + lift vs break floor)
        otherwise: no DROP (return expires)

  Sanity / anti-burst mechanism (SINGLE mechanism)
  - REMOVED bar-level sanity check (RESTORE_RR_MIN) entirely.
  - Added post-DROP kick verification (window-level, 75ms):
      After DROP entry, require kick to stay present often enough within a short budget.
      If verification fails -> cancel DROP back to BREAK (not STD).

  Other policy behavior preserved:
  - Baseline learns ONLY in STANDARD and only on qualified bars; frozen in CAND/BREAK/DROP.
  - STANDARD -> CAND: kick absence mandatory (bar-level) + window persistence protection.
  - CAND -> BREAK: after CAND_MIN_BARS, 2 consecutive deep bars.
  - CAND -> STANDARD recovery: bar-level 1 bar meeting thresholds.
  - BREAK -> STANDARD recovery: bar-level 1 bar meeting thresholds,
      but blocked while returnActive==true to avoid stealing the return moment.
  - BREAK floor updates only while in BREAK, and is FROZEN while returnActive==true.
  - DROP duration: fixed DROP_BARS from onset, exit at bar boundary.
*/


#include <Arduino.h>
#include "driver/i2s.h"

// ---------- VISUAL TYPES MUST BE ABOVE ANY FUNCTIONS ----------
enum Wing : uint8_t { W_BLUE=0, W_RED=1, W_GREEN=2, W_YELLOW=3 };
enum VisualMode : uint8_t { VIS_STD=0, VIS_BREAK=1, VIS_DROP=2, VIS_DEBUG=3 };

// ---------- FAILURE TYPES (MUST BE ABOVE ANY FUNCTIONS) ----------
enum SystemMode : uint8_t { SYS_OK=0, SYS_FAIL=1 };
enum FailReason : uint8_t { FAIL_NONE=0, FAIL_CLOCK_LOST=1, FAIL_AUDIO_LOST=2 };

static bool clockLostLatched = false;
static bool audioLostLatched = false;
static uint32_t clockLostAtUs = 0;
static uint32_t audioLostAtUs = 0;
static bool prevBothPresent = false;

// ---------------- STATE (place FIRST to avoid Arduino prototype issues) ----------------
enum ContextState : uint8_t {
  STANDARD = 0,
  BREAK_CANDIDATE = 1,
  BREAK_CONFIRMED = 2,
  DROP = 3
};

static const char* ctxName(ContextState s) {
  switch (s) {
    case STANDARD: return "STD";
    case BREAK_CANDIDATE: return "CAND";
    case BREAK_CONFIRMED: return "BREAK";
    case DROP: return "DROP";
    default: return "?";
  }
}

// ---------------- BASELINE-LOCKED LED / BUTTON PINS ----------------
// PWM pins (locked)
static constexpr int PIN_LED_BLUE   = 23;
static constexpr int PIN_LED_RED    = 19;
static constexpr int PIN_LED_GREEN  = 18;
static constexpr int PIN_LED_YELLOW = 5;

// Button input pins (locked)
static constexpr int PIN_BTN_BLUE   = 21;
static constexpr int PIN_BTN_RED    = 13;
static constexpr int PIN_BTN_GREEN  = 14;
static constexpr int PIN_BTN_YELLOW = 27;

// PWM envelope (locked/validated)
static constexpr int PWM_FREQ_HZ = 12500;
static constexpr int PWM_RES_BITS = 8;

// MOSFET minimum visible duty behavior (empirically ~70/255)
static constexpr uint8_t MIN_VISIBLE_DUTY = 70;

// ---------------- VISUAL TUNABLES ----------------
static constexpr uint8_t STD_BRIGHT   = 170;
static constexpr uint8_t BREAK_BRIGHT = 150;
static constexpr uint8_t DROP_BRIGHT  = 235;
static constexpr uint8_t DEBUG_BRIGHT = 200;

static constexpr float CAND_STD_DIM = 0.55f;

// Global cap (safety net). If sum of channel duties > cap, scale down proportionally.
static constexpr uint16_t GLOBAL_DUTY_CAP = 320;

// DROP overlap: last 10% of half-beat crossfades to next
static constexpr float DROP_OVERLAP_FRAC = 0.10f;

// BREAK crossfade length: 2 beats
static constexpr uint8_t BREAK_FADE_BEATS = 2;

// STANDARD direction flip every 8 bars
static constexpr uint8_t STD_DIR_FLIP_BARS = 8;

// ---------------- I2S ----------------
static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
static constexpr int PIN_I2S_BCLK = 26;
static constexpr int PIN_I2S_LRCK = 25;
static constexpr int PIN_I2S_DATA = 22;

static constexpr int SAMPLE_RATE = 48000;
static constexpr int I2S_READ_FRAMES = 256;

// -------------- MONITOR WINDOW (policy) --------------
static constexpr uint32_t MONITOR_WIN_MS = 75;
static constexpr uint32_t WIN_SAMPLES = (SAMPLE_RATE * MONITOR_WIN_MS) / 1000;

// -------------- FILTERS --------------
static constexpr float LP_ENV_ALPHA = 0.010f;
static constexpr float HP_ALPHA = 0.995f;

// -------------- UTILS ----------------
static inline float safeDiv(float a, float b) { return a / (b + 1e-9f); }
static inline float clamp01(float x) { return (x < 0.0f) ? 0.0f : (x > 1.0f ? 1.0f : x); }

// -------------- WELFORD --------------
struct Welford {
  double mean = 0.0, m2 = 0.0;
  uint32_t n = 0;
  void reset() { mean = 0.0; m2 = 0.0; n = 0; }
  void update(double x) {
    n++;
    double d = x - mean;
    mean += d / (double)n;
    double d2 = x - mean;
    m2 += d * d2;
  }
  float var() const { return (n < 2) ? 0.0f : (float)(m2 / (double)(n - 1)); }
};

// -------------- BASELINE (policy) -------------
static constexpr float BASE_ALPHA_STD  = 0.10f; // used only on qualified STD bars
static constexpr uint16_t BASELINE_MIN_QUALIFIED_BARS = 16;

static constexpr float BASELINE_MIN_RMS = 0.020f;            // blocks near-silence baseline learning
static constexpr float KICK_PRESENT_KVAR_ABS_MIN = 0.0010f;  // pre-baseInited kick proxy
static constexpr float KICK_PRESENT_KR_MIN = 0.90f;          // after baseInited (ratio)

// -------------- CAND / BREAK / RECOVERY (policy) --------------
static constexpr float KICK_GONE_KR_MAX = 0.60f;    // kR < 0.60 triggers CAND
static constexpr int   CAND_MIN_BARS    = 1;        // minimum bars in CAND before BREAK eval

static constexpr float DEEP_BREAK_TR_MAX  = 0.55f;
static constexpr float DEEP_BREAK_RMS_MAX = 0.80f;
static constexpr float DEEP_BREAK_KR_MAX  = 0.40f;   // stricter kick absence for BREAK confirm

static constexpr uint8_t KICK_GONE_CONFIRM_WINDOWS = 4; // ~300ms at 75ms windows

// NOTE: keep existing tuned recovery values for now
static constexpr float RECOVERY_RR_MIN = 0.75f;
static constexpr float RECOVERY_TR_MIN = 0.75f;
static constexpr float RECOVERY_KR_MIN = 0.80f;

static constexpr float CAND_RECOVERY_KR_MIN = 0.82f;

// -------------- BREAK FLOOR --------------
static constexpr float BREAK_ALPHA = 0.10f;

// -------------- v8 DROP (Return-Impact) --------------
// Return start: kick returns vs BREAK floor
static constexpr float   KICK_RETURN_BF_MIN = 1.60f;      // w_bfK >= this starts return tracking (tune)
static constexpr uint8_t KICK_RETURN_CONFIRM_WINDOWS = 3; // ~225ms at 75ms windows
static constexpr uint8_t KICK_RETURN_CANCEL_WINDOWS  = 4; // ~300ms at 75ms windows
static constexpr uint8_t RETURN_EVAL_WINDOWS          = 12; // 900ms

// DROP qualification using PEAKS within return window (tune)
static constexpr float DROP_BF_KV_MIN  = 2.50f;  // mandatory kick resurgence vs break floor
static constexpr float DROP_BF_RMS_MIN = 1.55f;  // lift vs break floor (energy)
static constexpr float DROP_BF_TR_MIN  = 1.60f;  // lift vs break floor (transients)

// v8.1: POST-DROP KICK VERIFICATION (single sanity mechanism)
static constexpr uint8_t DROP_VERIFY_WINDOWS  = 12; // 900ms
static constexpr uint8_t DROP_VERIFY_MIN_GOOD = 6;  // good windows required within budget
static constexpr float   DROP_VERIFY_BF_K_MIN = 0.70f * KICK_RETURN_BF_MIN; // tolerate inter-kick gaps

static constexpr int DROP_BARS = 8;

// ---------------- MIDI (UART1 on GPIO34) ----------------
HardwareSerial MidiSerial(1);

static bool midiRunning = false;
static uint8_t tickInBeat = 0;          // 0..23
static uint32_t ticksSinceBeat = 0;

static uint32_t barCount = 0;           // 1..N (display)
static uint8_t  beatInBar = 0;          // 1..4 (display)
static uint32_t lastBeatUs = 0;
static uint32_t lastBeatIntervalUs = 500000; // default ~120bpm

// Current position snapshot
static volatile uint32_t curBarForEvents = 0;
static volatile uint8_t  curBeatForEvents = 0;

// ---------------- Beat index (must be above visuals use) ----------------
static uint32_t gBeatIndex = 0;
static inline uint32_t globalBeatIndex() { return gBeatIndex; }

// ---------------- I2S accumulators ----------------
static uint32_t barN = 0, winN = 0;
static double barSumSq = 0.0, barTrSum = 0.0;
static double winSumSq = 0.0, winTrSum = 0.0;
static Welford barKickW, winKickW;

// filter states
static float envLP = 0.0f;
static float hp_y = 0.0f;
static float hp_x_prev = 0.0f;

// ---- Latest I2S snapshots for logging ----
static volatile float lastWinRms = 0.0f, lastWinTr = 0.0f, lastWinKVar = 0.0f;
static volatile uint32_t lastWinUs = 0;

static volatile float lastBarRms = 0.0f, lastBarTr = 0.0f, lastBarKVar = 0.0f;
static volatile uint32_t lastBarUs = 0;

// ---- Ratios / context snapshot ----
static volatile float last_rR = 0.0f, last_tR = 0.0f, last_kR = 0.0f;
static volatile float last_bfR = 0.0f, last_bfT = 0.0f, last_bfK = 0.0f;
static volatile bool  last_hasBF = false;
static volatile ContextState last_stateForBar = STANDARD;

// ---------------- Party Mode globals ----------------
static bool baseInited = false;
static float baseRms = 0.0f, baseTr = 0.0f, baseKVar = 0.0f;

// baseline readiness tracking
static bool baselineReady = false;
static uint16_t baselineQualifiedBars = 0;

static bool breakInited = false;
static float breakRms = 0.0f, breakTr = 0.0f, breakKVar = 0.0f;

static ContextState state = STANDARD;

// CAND tracking
static uint32_t candEnterBar = 0;

// Recovery tracking
static uint8_t breakRecoveryBars = 0;        // policy wants 1
static uint8_t candDeepStreak = 0;           // consecutive deep bars while in CAND
static uint8_t stdKickGoneWinStreak = 0;     // only used while in STD

// -------------- v8 Return-Impact tracking --------------
static bool returnActive = false;
static uint8_t returnWinStreak = 0;
static uint8_t kickLostStreak = 0;
static uint8_t returnBudget = 0;

static float peak_bfR = 0.0f, peak_bfT = 0.0f, peak_bfK = 0.0f;

// -------------- v8.1 Post-DROP verification --------------
static bool dropVerifyActive = false;
static uint8_t dropVerifyBudget = 0;
static uint8_t dropVerifyGood = 0;

// DROP timing
static uint32_t dropOnsetBarStart = 0;
static uint32_t dropEndBar = 0;

// ---------------- FAILURE / MUSIC-STOP tracking ----------------
static SystemMode sysMode = SYS_OK;
static FailReason failReason = FAIL_NONE;

// Presence tracking
static bool seenAnyClock = false;
static uint32_t lastClockUs = 0;

static bool seenAnyAudio = false;
static uint32_t lastAudioUs = 0;

// Thresholds (tune later)
static constexpr uint32_t CLOCK_LOSS_US = 600000;      // 0.6s
static constexpr uint32_t AUDIO_LOSS_US = 1500000;     // 1.5s
static constexpr uint32_t STOP_COINCIDE_US = 1000000;  // 1.0s window
static constexpr float    AUDIO_PRESENT_MIN_RMS = 0.004f;

// ---------------- Accumulator resets ----------------
static void resetBarAcc() {
  barN = 0;
  barSumSq = 0.0;
  barTrSum = 0.0;
  barKickW.reset();
}

static void resetWinAcc() {
  winN = 0;
  winSumSq = 0.0;
  winTrSum = 0.0;
  winKickW.reset();
}

// ---------------- Logging helpers ----------------
static void logTransition(ContextState from, ContextState to, const char* why) {
  if (from == to) return;
  Serial.printf("STATE %s->%s pos=%lu.%u why=%s\n",
                ctxName(from), ctxName(to),
                (unsigned long)curBarForEvents, (unsigned)curBeatForEvents, why);
}

static void logEvent(const char* e) {
  Serial.printf("EVENT %s pos=%lu.%u\n",
                e, (unsigned long)curBarForEvents, (unsigned)curBeatForEvents);
}

// ---------------- VISUALS ----------------

// IMPORTANT: pin map must match Wing enum ordering above
static const int WING_PINS[4] = { PIN_LED_BLUE, PIN_LED_RED, PIN_LED_GREEN, PIN_LED_YELLOW };

// Physical rotation orders
static const Wing STD_ORDER[4]  = { W_BLUE, W_RED, W_GREEN, W_YELLOW };      // CCW

static VisualMode visMode = VIS_STD;

// Per-wing current output duty
static uint8_t outDuty[4] = {0,0,0,0};

// --- STANDARD state ---
static int8_t stdDir = +1;
static uint8_t stdPos = 0;
static uint8_t stdBarsSinceFlip = 0;

// --- BREAK crossfade state ---
static bool breakFading = false;
static Wing breakFrom = W_BLUE;
static Wing breakTo = W_GREEN;
static uint32_t breakFadeStartUs = 0;
static uint32_t breakFadeDurUs = 1000000;

// --- DROP sequencing ---
static uint8_t dropStep = 0;
static const Wing DROP_STEPS[8] = {
  W_BLUE, W_GREEN, W_RED, W_YELLOW,
  W_BLUE, W_GREEN, W_RED, W_YELLOW
};

static uint32_t lastHalfBeatUs = 0;
static uint32_t halfBeatUs = 250000;

// Optional debug trigger
static bool debugForced = false;

static inline void writeWingDuty(Wing w, uint8_t duty) {
  ledcWrite(WING_PINS[w], duty);
  outDuty[w] = duty;
}

static uint8_t dutyFromLevel(float level01, uint8_t target) {
  level01 = clamp01(level01);
  if (level01 <= 0.0f) return 0;
  if (target <= MIN_VISIBLE_DUTY) return target;
  float d = (float)MIN_VISIBLE_DUTY + level01 * ((float)target - (float)MIN_VISIBLE_DUTY);
  if (d < 0) d = 0;
  if (d > 255) d = 255;
  return (uint8_t)(d + 0.5f);
}

static void applyGlobalCap(uint8_t req[4]) {
  uint16_t sum = (uint16_t)req[0] + req[1] + req[2] + req[3];
  if (sum <= GLOBAL_DUTY_CAP || sum == 0) return;

  float scale = (float)GLOBAL_DUTY_CAP / (float)sum;
  for (int i = 0; i < 4; i++) {
    if (req[i] == 0) continue;
    float v = (float)req[i] * scale;
    req[i] = (uint8_t)(v + 0.5f);
    if (req[i] > 0 && req[i] < MIN_VISIBLE_DUTY) req[i] = MIN_VISIBLE_DUTY;
  }
}

static void allOff() {
  for (int i = 0; i < 4; i++) writeWingDuty((Wing)i, 0);
}

static void onVisualModeEnter(VisualMode m) {
  if (m == VIS_STD) {
    stdBarsSinceFlip = 0;
  } else if (m == VIS_BREAK) {
    breakFading = false;
  } else if (m == VIS_DROP) {
    dropStep = 0;
    lastHalfBeatUs = micros();
  }
}

static VisualMode modeForState() {
  if (debugForced) return VIS_DEBUG;
  switch (state) {
    case STANDARD: return VIS_STD;
    case BREAK_CANDIDATE: return VIS_STD;
    case BREAK_CONFIRMED: return VIS_BREAK;
    case DROP: return VIS_DROP;
    default: return VIS_STD;
  }
}

static void refreshVisualMode() {
  VisualMode m = modeForState();
  if (m != visMode) {
    visMode = m;
    onVisualModeEnter(visMode);
  }
}

static void visualsOnBeat(bool isBarStart) {
  refreshVisualMode();

  if (visMode == VIS_STD) {
    stdPos = (uint8_t)((stdPos + (stdDir > 0 ? 1 : 3)) % 4);
    Wing onWing = STD_ORDER[stdPos];

    uint8_t req[4] = {0,0,0,0};

    uint8_t bright = STD_BRIGHT;
    if (state == BREAK_CANDIDATE) {
      bright = (uint8_t)(STD_BRIGHT * CAND_STD_DIM + 0.5f);
    }
    req[onWing] = bright;

    applyGlobalCap(req);
    for (int i = 0; i < 4; i++) writeWingDuty((Wing)i, req[i]);

    if (isBarStart) {
      stdBarsSinceFlip++;
      if (stdBarsSinceFlip >= STD_DIR_FLIP_BARS) {
        stdBarsSinceFlip = 0;
        stdDir = (int8_t)-stdDir;
        stdPos = (uint8_t)((stdPos + (stdDir > 0 ? 1 : 3)) % 4);
      }
    }
  }
  else if (visMode == VIS_BREAK) {
    if ((barCount == 0) || ((uint32_t)(millis()) == 0)) return; // harmless guard
    if ((globalBeatIndex() % 2) == 0) {
      Wing current = breakFading ? breakTo : breakFrom;
      Wing next = current;
      while (next == current) next = (Wing)(esp_random() % 4);

      breakFrom = current;
      breakTo = next;
      breakFading = true;
      breakFadeStartUs = micros();
      breakFadeDurUs = (uint32_t)BREAK_FADE_BEATS * lastBeatIntervalUs;
    }
  }
  else if (visMode == VIS_DEBUG) {
    uint8_t req[4] = {0,0,0,0};
    bool on = ((globalBeatIndex() % 2) == 0);
    req[W_RED] = on ? DEBUG_BRIGHT : 0;
    applyGlobalCap(req);
    for (int i = 0; i < 4; i++) writeWingDuty((Wing)i, req[i]);
  }
}

static void visualsOnHalfBeat() {
  refreshVisualMode();
  if (visMode != VIS_DROP) return;

  dropStep = (uint8_t)((dropStep + 1) % 8);
  lastHalfBeatUs = micros();
}

static void visualsRender() {
  // FAILURE overlay: time-based blink, independent of MIDI/I2S
  if (sysMode == SYS_FAIL) {
    const uint32_t ms = millis();

    static uint32_t lastFailHbMs = 0;
    if ((uint32_t)(ms - lastFailHbMs) >= 1000) {
      lastFailHbMs = ms;
      const uint32_t nowUs = micros();
      const uint32_t clockAgeMs = seenAnyClock ? (uint32_t)((nowUs - lastClockUs) / 1000) : 999999;
      const uint32_t audioAgeMs = seenAnyAudio ? (uint32_t)((nowUs - lastAudioUs) / 1000) : 999999;
      Serial.printf("FAIL_HEARTBEAT reason=%u pos=%lu.%u clockAge_ms=%lu audioAge_ms=%lu ctx=%s\n",
        (unsigned)failReason,
        (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
        (unsigned long)clockAgeMs, (unsigned long)audioAgeMs,
        ctxName(state));
    }

    const bool on = (ms % 1000) < 150;
    uint8_t req[4] = {0,0,0,0};
    req[W_RED] = on ? DEBUG_BRIGHT : 0;
    applyGlobalCap(req);
    for (int i = 0; i < 4; i++) writeWingDuty((Wing)i, req[i]);
    return;
  }

  refreshVisualMode();

  const uint32_t nowUs = micros();

  if (visMode == VIS_BREAK) {
    if (!breakFading) return;

    const uint32_t dt = nowUs - breakFadeStartUs;
    float t = (breakFadeDurUs == 0) ? 1.0f : (float)dt / (float)breakFadeDurUs;
    t = clamp01(t);

    uint8_t req[4] = {0,0,0,0};

    uint8_t fromDuty = dutyFromLevel(1.0f - t, BREAK_BRIGHT);
    uint8_t toDuty   = dutyFromLevel(t, BREAK_BRIGHT);
    if (t >= 1.0f) fromDuty = 0;

    req[breakFrom] = fromDuty;
    req[breakTo]   = toDuty;

    applyGlobalCap(req);
    for (int i = 0; i < 4; i++) writeWingDuty((Wing)i, req[i]);

    if (t >= 1.0f) {
      breakFading = false;
      for (uint8_t i = 0; i < 4; i++) {
        if (STD_ORDER[i] == breakTo) { stdPos = i; break; }
      }
    }
  }
  else if (visMode == VIS_DROP) {
    halfBeatUs = lastBeatIntervalUs / 2;
    if (halfBeatUs < 20000) halfBeatUs = 20000;

    uint32_t dt = nowUs - lastHalfBeatUs;
    if (dt > (halfBeatUs * 4)) dt = halfBeatUs;

    float phase = (float)dt / (float)halfBeatUs;
    phase = clamp01(phase);

    Wing cur = DROP_STEPS[dropStep];
    Wing nxt = DROP_STEPS[(dropStep + 1) % 8];

    uint8_t req[4] = {0,0,0,0};

    float holdEnd = 1.0f - DROP_OVERLAP_FRAC;

    if (phase <= holdEnd) {
      req[cur] = DROP_BRIGHT;
    } else {
      float u = (phase - holdEnd) / DROP_OVERLAP_FRAC;
      u = clamp01(u);

      uint8_t curDuty = dutyFromLevel(1.0f - u, DROP_BRIGHT);
      uint8_t nxtDuty = dutyFromLevel(u, DROP_BRIGHT);
      if (u >= 1.0f) curDuty = 0;

      req[cur] = curDuty;
      req[nxt] = nxtDuty;
    }

    applyGlobalCap(req);
    for (int i = 0; i < 4; i++) writeWingDuty((Wing)i, req[i]);
  }
}

// ---------------- Forward declarations (reset split) ----------------
static void resetForHardReset();   // clears baseline
static void resetForResumeLike();  // keeps baseline

// ---------------- Party Mode helpers ----------------
static void autoResyncIfRecovered(bool clockPresent, bool audioPresent) {
  const bool bothPresent = clockPresent && audioPresent;
  if (!prevBothPresent && bothPresent) {
    logEvent("AUTO_RESYNC_BOTH_PRESENT");
    sysMode = SYS_OK;
    failReason = FAIL_NONE;

    resetForResumeLike(); // keep baseline
  }
  prevBothPresent = bothPresent;
}

static void baselineInit(float rms, float tr, float kVar) {
  baseRms = rms; baseTr = tr; baseKVar = kVar;
  baseInited = true;
  Serial.printf("BASE_INIT rms=%.4f tr=%.6f kVar=%.8f pos=%lu.%u\n",
                rms, tr, kVar, (unsigned long)curBarForEvents, (unsigned)curBeatForEvents);
}

static void breakReset() {
  breakInited = false;
  breakRms = breakTr = breakKVar = 0.0f;
}

static void clearReturnTracking() {
  returnActive = false;
  returnWinStreak = 0;
  kickLostStreak = 0;
  returnBudget = 0;
  peak_bfR = peak_bfT = peak_bfK = 0.0f;
}

static void clearDropVerify() {
  dropVerifyActive = false;
  dropVerifyBudget = 0;
  dropVerifyGood = 0;
}

static void breakUpdate(float rms, float tr, float kVar) {
  if (state != BREAK_CONFIRMED) return;
  if (returnActive) return; // freeze break floor during return evaluation

  if (!breakInited) {
    breakRms = rms; breakTr = tr; breakKVar = kVar;
    breakInited = true;
    return;
  }
  breakRms  = (1.0f - BREAK_ALPHA) * breakRms  + BREAK_ALPHA * rms;
  breakTr   = (1.0f - BREAK_ALPHA) * breakTr   + BREAK_ALPHA * tr;
  breakKVar = (1.0f - BREAK_ALPHA) * breakKVar + BREAK_ALPHA * kVar;
}

static bool baselineEligibleBar(float rms, float kVar, float kR) {
  if (rms < BASELINE_MIN_RMS) return false;
  if (!baseInited) return (kVar >= KICK_PRESENT_KVAR_ABS_MIN);
  return (kR >= KICK_PRESENT_KR_MIN);
}

static void baselineMaybeInitAndUpdate(float rms, float tr, float kVar, float kR) {
  if (state != STANDARD) return; // freeze outside STD
  if (!baselineEligibleBar(rms, kVar, kR)) return;

  if (!baseInited) {
    baselineInit(rms, tr, kVar);
    baselineQualifiedBars = 1;
    baselineReady = (baselineQualifiedBars >= BASELINE_MIN_QUALIFIED_BARS);
    if (baselineReady) logEvent("BASELINE_READY");
    return;
  }

  const float a = BASE_ALPHA_STD;
  baseRms  = (1.0f - a) * baseRms  + a * rms;
  baseTr   = (1.0f - a) * baseTr   + a * tr;
  baseKVar = (1.0f - a) * baseKVar + a * kVar;

  if (!baselineReady) {
    baselineQualifiedBars++;
    if (baselineQualifiedBars >= BASELINE_MIN_QUALIFIED_BARS) {
      baselineReady = true;
      logEvent("BASELINE_READY");
    }
  }
}

static void enterDrop(const char* whyEventName, const char* whyTransition) {
  ContextState prev = state;

  dropOnsetBarStart = curBarForEvents;
  dropEndBar = dropOnsetBarStart + (uint32_t)DROP_BARS;

  state = DROP;

  // Clear return trackers
  clearReturnTracking();
  breakRecoveryBars = 0;

  // Start post-drop verification (single sanity mechanism)
  dropVerifyActive = true;
  dropVerifyBudget = DROP_VERIFY_WINDOWS;
  dropVerifyGood = 0;
  Serial.printf("EVENT DROP_VERIFY_START pos=%lu.%u win=%u need=%u bfKmin=%.2f\n",
                (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                (unsigned)DROP_VERIFY_WINDOWS, (unsigned)DROP_VERIFY_MIN_GOOD, DROP_VERIFY_BF_K_MIN);

  logEvent(whyEventName);
  logTransition(prev, state, whyTransition);
}

static void cancelDropBackToBreak(const char* why) {
  if (state != DROP) return;
  if (!breakInited) { // safety: if no floor, don't stay in BREAK
    ContextState p = state;
    state = STANDARD;
    clearReturnTracking();
    clearDropVerify();
    dropOnsetBarStart = 0;
    dropEndBar = 0;
    logTransition(p, state, "DROP_CANCEL_NO_BREAKFLOOR");
    return;
  }

  ContextState p = state;
  state = BREAK_CONFIRMED;

  // keep break floor as-is; allow updates again (returnActive already false)
  clearReturnTracking();
  clearDropVerify();

  // keep DROP timer cleared
  dropOnsetBarStart = 0;
  dropEndBar = 0;

  Serial.printf("EVENT DROP_VERIFY_CANCEL pos=%lu.%u why=%s good=%u/%u\n",
                (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                why, (unsigned)dropVerifyGood, (unsigned)DROP_VERIFY_WINDOWS);

  logTransition(p, state, "DROP_CANCEL_TO_BREAK");
}

// ---------------- v8 RETURN-IMPACT monitor (75ms windows) ----------------
static void onMonitorWindow(float winRms, float winTr, float winKVar) {
  if (!baselineReady) return;
  if (!baseInited) return;
  if (sysMode == SYS_FAIL) return;

  // --- STD-only kick-absence persistence (prevents bar-average false CAND) ---
  if (state == STANDARD) {
    const float w_kR = safeDiv(winKVar, baseKVar);
    if (w_kR < KICK_GONE_KR_MAX) stdKickGoneWinStreak++;
    else                        stdKickGoneWinStreak = 0;
  } else {
    stdKickGoneWinStreak = 0;
  }

  // We need break floor for any bfK-based logic (RETURN + VERIFY)
  if (!breakInited) {
    clearReturnTracking();
    clearDropVerify();
    return;
  }

  // Break-floor window ratios
  const float w_bfR = safeDiv(winRms,  breakRms);
  const float w_bfT = safeDiv(winTr,   breakTr);
  const float w_bfK = safeDiv(winKVar, breakKVar);

  // ---------------- POST-DROP verification (single sanity mechanism) ----------------
  if (state == DROP && dropVerifyActive) {
    if (w_bfK >= DROP_VERIFY_BF_K_MIN) dropVerifyGood++;

    if (dropVerifyGood >= DROP_VERIFY_MIN_GOOD) {
      dropVerifyActive = false;
      Serial.printf("EVENT DROP_VERIFY_PASS pos=%lu.%u good=%u/%u\n",
                    (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                    (unsigned)dropVerifyGood, (unsigned)DROP_VERIFY_WINDOWS);
      return; // done verifying
    }

    if (dropVerifyBudget > 0) dropVerifyBudget--;
    if (dropVerifyBudget == 0) {
      cancelDropBackToBreak("KICK_NOT_SUSTAINED");
      return;
    }

    // While verifying, do NOT run Return-Impact (we're already in DROP)
    return;
  }

  // v8 DROP monitor active ONLY in BREAK_CONFIRMED when break floor exists
  if (state != BREAK_CONFIRMED) {
    clearReturnTracking();
    return;
  }

  // ---- Phase A: detect Return Start (kick back) ----
  if (!returnActive) {
    if (w_bfK >= KICK_RETURN_BF_MIN) returnWinStreak++;
    else                            returnWinStreak = 0;

    if (returnWinStreak >= KICK_RETURN_CONFIRM_WINDOWS) {
      returnActive = true;
      returnBudget = RETURN_EVAL_WINDOWS;
      kickLostStreak = 0;
      peak_bfR = peak_bfT = peak_bfK = 0.0f;
      returnWinStreak = 0;

      Serial.printf("EVENT RETURN_START pos=%lu.%u w_bfK=%.2f\n",
                    (unsigned long)curBarForEvents, (unsigned)curBeatForEvents, w_bfK);
    }
    return;
  }

  // ---- returnActive: cancellation on sustained kick loss ----
  if (w_bfK < KICK_RETURN_BF_MIN) kickLostStreak++;
  else                            kickLostStreak = 0;

  if (kickLostStreak >= KICK_RETURN_CANCEL_WINDOWS) {
    Serial.printf("EVENT RETURN_CANCEL pos=%lu.%u\n",
                  (unsigned long)curBarForEvents, (unsigned)curBeatForEvents);
    clearReturnTracking();
    return;
  }

  // ---- Phase B: track peaks + decide DROP ----
  if (w_bfR > peak_bfR) peak_bfR = w_bfR;
  if (w_bfT > peak_bfT) peak_bfT = w_bfT;
  if (w_bfK > peak_bfK) peak_bfK = w_bfK;

  const bool okKick = (peak_bfK >= DROP_BF_KV_MIN);
  const bool okLift = (peak_bfR >= DROP_BF_RMS_MIN) || (peak_bfT >= DROP_BF_TR_MIN);

  if (okKick && okLift) {
    Serial.printf("EVENT DROP_CONFIRMED_RETURN pos=%lu.%u pk_bfK=%.2f pk_bfR=%.2f pk_bfT=%.2f\n",
                  (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                  peak_bfK, peak_bfR, peak_bfT);
    enterDrop("DROP_CONFIRMED", "RETURN_IMPACT_PEAKS");
    return;
  }

  if (returnBudget > 0) returnBudget--;
  if (returnBudget == 0) {
    Serial.printf("EVENT RETURN_EXPIRE_NO_DROP pos=%lu.%u pk_bfK=%.2f pk_bfR=%.2f pk_bfT=%.2f\n",
                  (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                  peak_bfK, peak_bfR, peak_bfT);
    clearReturnTracking();
    return;
  }
}

// ---------------- Bar finalization (policy transitions) ----------------
static void onBarFinalized(uint32_t finalizedBarNumber, float rms, float tr, float kVar) {
  const float rR = baseInited ? safeDiv(rms,  baseRms)  : 0.0f;
  const float tR = baseInited ? safeDiv(tr,   baseTr)   : 0.0f;
  const float kR = baseInited ? safeDiv(kVar, baseKVar) : 0.0f;

  baselineMaybeInitAndUpdate(rms, tr, kVar, kR);

  if (!baselineReady) {
    state = STANDARD;
    candEnterBar = 0;
    breakRecoveryBars = 0;
    candDeepStreak = 0;
    breakReset();
    clearReturnTracking();
    clearDropVerify();

    last_rR = rR; last_tR = tR; last_kR = kR;
    last_bfR = last_bfT = last_bfK = 0.0f;
    last_hasBF = false;
    last_stateForBar = state;
    return;
  }

  ContextState prev = state;
  if (state != BREAK_CANDIDATE) candDeepStreak = 0;

  // ----- BREAK -> STANDARD recovery (bar-level) -----
  // Priority: if returnActive is true, do not allow recovery to steal the moment
  if (state == BREAK_CONFIRMED && !returnActive) {
    const bool ok = (kR >= RECOVERY_KR_MIN) &&
                    ((rR >= RECOVERY_RR_MIN) || (tR >= RECOVERY_TR_MIN));
    breakRecoveryBars = ok ? 1 : 0;
    if (breakRecoveryBars >= 1) {
      state = STANDARD;
      candEnterBar = 0;
      breakRecoveryBars = 0;
      candDeepStreak = 0;
      breakReset();
      clearReturnTracking();
      clearDropVerify();
      logTransition(prev, state, "BREAK_RECOVER_BAR");
      prev = state;
    }
  }

  // ----- STANDARD -> CAND -----
  if (state == STANDARD) {
    if ((kR < KICK_GONE_KR_MAX) && (stdKickGoneWinStreak >= KICK_GONE_CONFIRM_WINDOWS)) {
      state = BREAK_CANDIDATE;
      candEnterBar = finalizedBarNumber;
      breakRecoveryBars = 0;
      candDeepStreak = 0;
      breakReset();
      clearReturnTracking();
      clearDropVerify();
      logTransition(prev, state, "CAND_ENTER_KICK_ABSENCE");
      prev = state;
    }
  }

  // ----- CAND -> BREAK OR recover to STD -----
  if (state == BREAK_CANDIDATE) {
    const bool canEvalDeep = (finalizedBarNumber >= (candEnterBar + (uint32_t)CAND_MIN_BARS));
    const bool deep = (kR < DEEP_BREAK_KR_MAX) &&
                      ((rR < DEEP_BREAK_RMS_MAX) || (tR < DEEP_BREAK_TR_MAX));

    if (canEvalDeep && deep) candDeepStreak++;
    else                     candDeepStreak = 0;

    if (candDeepStreak >= 2) {
      state = BREAK_CONFIRMED;
      candDeepStreak = 0;
      breakReset();           // break floor will init on first BREAK bar update below
      clearReturnTracking();
      clearDropVerify();
      logTransition(prev, state, "BREAK_CONFIRM_DEEP_2B");
      prev = state;
    } else {
      const bool okRecover = (rR >= RECOVERY_RR_MIN) && (tR >= RECOVERY_TR_MIN) && (kR >= CAND_RECOVERY_KR_MIN);
      if (okRecover) {
        state = STANDARD;
        candEnterBar = 0;
        candDeepStreak = 0;
        breakRecoveryBars = 0;
        breakReset();
        clearReturnTracking();
        clearDropVerify();
        logTransition(prev, state, "CAND_RECOVER_BAR");
        prev = state;
      }
    }
  }

  // ----- Update BREAK floor (only in BREAK, frozen during returnActive) -----
  breakUpdate(rms, tr, kVar);

  // ----- bf ratios for logging -----
  float bfR = 0, bfT = 0, bfK = 0;
  bool hasBF = false;
  if (breakInited && (state == BREAK_CONFIRMED || state == DROP)) {
    bfR = safeDiv(rms,  breakRms);
    bfT = safeDiv(tr,   breakTr);
    bfK = safeDiv(kVar, breakKVar);
    hasBF = true;
  }

  last_rR = rR; last_tR = tR; last_kR = kR;
  last_bfR = bfR; last_bfT = bfT; last_bfK = bfK;
  last_hasBF = hasBF;
  last_stateForBar = state;
}

// ---------------- Bar finalize (MIDI-synchronous) ----------------
static void finalizeBarNow(uint32_t stampUs, uint32_t finalizedBarNumber) {
  float rms = 0.0f, tr = 0.0f, kVar = 0.0f;
  if (barN > 0) {
    rms = sqrtf((float)(barSumSq / (double)barN));
    tr  = (float)(barTrSum / (double)barN);
    kVar = barKickW.var();
  }

  lastBarRms = rms;
  lastBarTr  = tr;
  lastBarKVar = kVar;
  lastBarUs = stampUs;

  resetBarAcc();
  onBarFinalized(finalizedBarNumber, rms, tr, kVar);
}

// ---------------- Beat log ----------------
static void logBeatLine(uint32_t nowUs, bool isBarStart) {
  const uint32_t dtUs  = (lastBeatUs == 0) ? 0 : (nowUs - lastBeatUs);
  if (dtUs > 0) {
    lastBeatIntervalUs = (uint32_t)(0.85f * (float)lastBeatIntervalUs + 0.15f * (float)dtUs);
  }
  lastBeatUs = nowUs;

  const uint32_t wAgeMs = (lastWinUs == 0) ? 999999 : (uint32_t)((nowUs - lastWinUs) / 1000);
  const uint32_t bAgeMs = (lastBarUs == 0) ? 999999 : (uint32_t)((nowUs - lastBarUs) / 1000);

  char pos[16];
  snprintf(pos, sizeof(pos), "%lu.%u", (unsigned long)barCount, (unsigned)beatInBar);

  Serial.printf(
    "pos=%s t_us=%lu dt_us=%lu ticks=%lu "
    "wAge_ms=%lu wRms=%.4f wTr=%.5f wKVar=%.6f "
    "bAge_ms=%lu bRms=%.4f bTr=%.5f bKVar=%.6f",
    pos,
    (unsigned long)nowUs,
    (unsigned long)dtUs,
    (unsigned long)ticksSinceBeat,
    (unsigned long)wAgeMs, lastWinRms, lastWinTr, lastWinKVar,
    (unsigned long)bAgeMs, lastBarRms, lastBarTr, lastBarKVar
  );

  if (isBarStart) {
    Serial.printf(" | ctx=%s rR=%.2f tR=%.2f kR=%.2f",
                  ctxName(last_stateForBar), last_rR, last_tR, last_kR);

    if (last_hasBF) {
      Serial.printf(" bfR=%.2f bfT=%.2f bfK=%.2f", last_bfR, last_bfT, last_bfK);
    }

    Serial.printf(" base=%s(%u/%u)",
                  (baselineReady ? "READY" : (baseInited ? "LEARN" : "OFF")),
                  (unsigned)baselineQualifiedBars,
                  (unsigned)BASELINE_MIN_QUALIFIED_BARS);

    if (state == BREAK_CONFIRMED && returnActive) {
      Serial.printf(" ret=ON(budg=%u pkK=%.2f pkR=%.2f pkT=%.2f)",
                    (unsigned)returnBudget, peak_bfK, peak_bfR, peak_bfT);
    }

    if (state == DROP && dropVerifyActive) {
      Serial.printf(" vfy=ON(budg=%u good=%u/%u)",
                    (unsigned)dropVerifyBudget, (unsigned)dropVerifyGood, (unsigned)DROP_VERIFY_WINDOWS);
    }
  }

  Serial.print("\n");
  ticksSinceBeat = 0;
}

// ---------------- MIDI processing ----------------
static void resetForHardReset() {
  tickInBeat = 0;
  ticksSinceBeat = 0;
  barCount = 1;
  beatInBar = 0;
  lastBeatUs = 0;

  curBarForEvents = 1;
  curBeatForEvents = 0;

  stdKickGoneWinStreak = 0;

  resetBarAcc();
  resetWinAcc();
  envLP = 0.0f;
  hp_y = 0.0f;
  hp_x_prev = 0.0f;

  baseInited = false;
  baseRms = baseTr = baseKVar = 0.0f;
  baselineReady = false;
  baselineQualifiedBars = 0;

  breakReset();
  clearReturnTracking();
  clearDropVerify();

  state = STANDARD;
  candEnterBar = 0;
  breakRecoveryBars = 0;
  candDeepStreak = 0;

  lastWinUs = 0;
  lastBarUs = 0;
  lastWinRms = lastWinTr = lastWinKVar = 0.0f;
  lastBarRms = lastBarTr = lastBarKVar = 0.0f;
  last_rR = last_tR = last_kR = 0.0f;
  last_bfR = last_bfT = last_bfK = 0.0f;
  last_hasBF = false;
  last_stateForBar = STANDARD;

  dropOnsetBarStart = 0;
  dropEndBar = 0;

  debugForced = false;
  visMode = VIS_STD;
  stdDir = +1;
  stdPos = 0;
  stdBarsSinceFlip = 0;
  breakFading = false;
  dropStep = 0;
  lastHalfBeatUs = micros();
  allOff();

  gBeatIndex = 0;
}

static void resetForResumeLike() {
  tickInBeat = 0;
  ticksSinceBeat = 0;
  barCount = 1;
  beatInBar = 0;
  lastBeatUs = 0;

  stdKickGoneWinStreak = 0;

  curBarForEvents = 1;
  curBeatForEvents = 0;

  resetBarAcc();
  resetWinAcc();
  envLP = 0.0f;
  hp_y = 0.0f;
  hp_x_prev = 0.0f;

  breakReset();
  clearReturnTracking();
  clearDropVerify();

  state = STANDARD;
  candEnterBar = 0;
  breakRecoveryBars = 0;
  candDeepStreak = 0;

  lastWinUs = 0;
  lastBarUs = 0;
  lastWinRms = lastWinTr = lastWinKVar = 0.0f;
  lastBarRms = lastBarTr = lastBarKVar = 0.0f;
  last_rR = last_tR = last_kR = 0.0f;
  last_bfR = last_bfT = last_bfK = 0.0f;
  last_hasBF = false;
  last_stateForBar = STANDARD;

  dropOnsetBarStart = 0;
  dropEndBar = 0;

  debugForced = false;
  visMode = VIS_STD;
  stdDir = +1;
  stdPos = 0;
  stdBarsSinceFlip = 0;
  breakFading = false;
  dropStep = 0;
  lastHalfBeatUs = micros();
  allOff();

  gBeatIndex = 0;
}

static void doManualResync() {
  Serial.printf("EVENT MANUAL_RESYNC pos=%lu.%u\n",
                (unsigned long)curBarForEvents, (unsigned)curBeatForEvents);
  resetForHardReset();
  Serial.printf("===SYNC_LOG_START===\n[MANUAL_RESYNC] t_us=%lu\n", (unsigned long)micros());
}

static void enterFailure(FailReason r) {
  if (sysMode == SYS_FAIL) return;

  sysMode = SYS_FAIL;
  failReason = r;

  state = STANDARD;
  breakReset();
  clearReturnTracking();
  clearDropVerify();
  candEnterBar = 0;
  breakRecoveryBars = 0;
  candDeepStreak = 0;

  if (r == FAIL_CLOCK_LOST) logEvent("FAIL_CLOCK_LOST");
  else if (r == FAIL_AUDIO_LOST) logEvent("FAIL_AUDIO_LOST");
  else logEvent("FAIL");
}

static void enterMusicStop() {
  logEvent("MUSIC_STOP");

  sysMode = SYS_OK;
  failReason = FAIL_NONE;

  midiRunning = false;

  resetForResumeLike();

  seenAnyClock = false;
  seenAnyAudio = false;
  lastClockUs = 0;
  lastAudioUs = 0;
}

static void clearStopLatches() {
  clockLostLatched = false;
  audioLostLatched = false;
  clockLostAtUs = 0;
  audioLostAtUs = 0;
}

static void processFailureWatchdog() {
  const uint32_t nowUs = micros();

  const uint32_t clockAgeMs = seenAnyClock ? (uint32_t)((nowUs - lastClockUs) / 1000) : 999999;
  const uint32_t audioAgeMs = seenAnyAudio ? (uint32_t)((nowUs - lastAudioUs) / 1000) : 999999;

  const bool clockLostNow = seenAnyClock && ((uint32_t)(nowUs - lastClockUs) > CLOCK_LOSS_US);
  const bool audioLostNow = seenAnyAudio && ((uint32_t)(nowUs - lastAudioUs) > AUDIO_LOSS_US);

  const bool clockPresent = seenAnyClock && ((uint32_t)(nowUs - lastClockUs) <= CLOCK_LOSS_US);
  const bool audioPresent = seenAnyAudio && ((uint32_t)(nowUs - lastAudioUs) <= AUDIO_LOSS_US);

  const bool bothPresent = clockPresent && audioPresent;
  if (!prevBothPresent && bothPresent) {
    logEvent("AUTO_RESYNC_BOTH_PRESENT");
    sysMode = SYS_OK;
    failReason = FAIL_NONE;
    resetForResumeLike();
  }
  prevBothPresent = bothPresent;

  if (clockLostNow && !clockLostLatched) {
    clockLostLatched = true;
    clockLostAtUs = nowUs;
    Serial.printf("EVENT CLOCK_LOST_LATCH pos=%lu.%u age_ms=%lu ctx=%s\n",
      (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
      (unsigned long)clockAgeMs, ctxName(state));
  }

  if (audioLostNow && !audioLostLatched) {
    audioLostLatched = true;
    audioLostAtUs = nowUs;
    Serial.printf("EVENT AUDIO_LOST_LATCH pos=%lu.%u age_ms=%lu ctx=%s\n",
      (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
      (unsigned long)audioAgeMs, ctxName(state));
  }

  if (!clockLostLatched && !audioLostLatched) return;

  if (clockLostLatched && audioLostLatched) {
    const uint32_t diff = (clockLostAtUs > audioLostAtUs) ? (clockLostAtUs - audioLostAtUs)
                                                          : (audioLostAtUs - clockLostAtUs);
    if (diff <= STOP_COINCIDE_US) {
      enterMusicStop();
      clearStopLatches();
      return;
    }

    if (clockLostAtUs < audioLostAtUs) enterFailure(FAIL_CLOCK_LOST);
    else                               enterFailure(FAIL_AUDIO_LOST);
    clearStopLatches();
    return;
  }

  const uint32_t latchedAt = clockLostLatched ? clockLostAtUs : audioLostAtUs;
  if ((uint32_t)(nowUs - latchedAt) < STOP_COINCIDE_US) {
    return;
  }

  if (clockLostLatched) enterFailure(FAIL_CLOCK_LOST);
  else                  enterFailure(FAIL_AUDIO_LOST);

  clearStopLatches();
}

static void onMidiBeat() {
  const uint32_t nowUs = micros();

  bool isBarStart = false;
  if (beatInBar == 0) { beatInBar = 1; isBarStart = true; }
  else if (beatInBar < 4) beatInBar++;
  else { beatInBar = 1; barCount++; isBarStart = true; }

  curBarForEvents = barCount;
  curBeatForEvents = beatInBar;

  gBeatIndex++;

  // DROP timeout exit at bar boundary (fixed DROP_BARS)
  if (isBarStart && state == DROP && dropEndBar > 0 && barCount >= dropEndBar) {
    ContextState prev = state;
    state = STANDARD;
    breakReset();
    clearReturnTracking();
    clearDropVerify();
    candEnterBar = 0;
    breakRecoveryBars = 0;
    candDeepStreak = 0;
    dropOnsetBarStart = 0;
    dropEndBar = 0;
    logTransition(prev, state, "DROP_TIMEOUT_FROM_ONSET");
  }

  // finalize previous bar at start of current bar (barCount >= 2)
  if (isBarStart && barCount >= 2) {
    finalizeBarNow(nowUs, barCount - 1);
  }

  visualsOnBeat(isBarStart);
  logBeatLine(nowUs, isBarStart);

  ticksSinceBeat = 0;
}

static void onMidiHalfBeat() { visualsOnHalfBeat(); }

static void processMidi() {
  while (MidiSerial.available() > 0) {
    const uint8_t b = (uint8_t)MidiSerial.read();

    if (b == 0xFA) { Serial.printf("[MIDI_START] t_us=%lu\n", (unsigned long)micros()); }
    else if (b == 0xFB) { Serial.printf("[MIDI_CONTINUE] t_us=%lu\n", (unsigned long)micros()); }
    else if (b == 0xFC) { Serial.printf("[MIDI_STOP] t_us=%lu\n", (unsigned long)micros()); }
    else if (b == 0xF8) { // CLOCK
      const uint32_t nowUs = micros();
      lastClockUs = nowUs;
      seenAnyClock = true;

      if (!midiRunning) {
        midiRunning = true;
        if (barCount == 0) { barCount = 1; beatInBar = 0; }
      }

      if (tickInBeat == 0) { onMidiBeat(); onMidiHalfBeat(); }
      if (tickInBeat == 12) { onMidiHalfBeat(); }

      tickInBeat = (uint8_t)((tickInBeat + 1) % 24);
      ticksSinceBeat++;
    }
  }
}

// ---------------- I2S INIT ----------------
static void i2sInit() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_SLAVE | I2S_MODE_RX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = PIN_I2S_BCLK;
  pins.ws_io_num = PIN_I2S_LRCK;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = PIN_I2S_DATA;

  ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &cfg, 0, nullptr));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
  ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_PORT));
}

// ---------------- AUDIO PROCESS ----------------
static void processAudio() {
  static int32_t buf[I2S_READ_FRAMES * 2];
  size_t bytesRead = 0;

  if (i2s_read(I2S_PORT, buf, sizeof(buf), &bytesRead, 5 / portTICK_PERIOD_MS) != ESP_OK || bytesRead == 0)
    return;

  const int frames = (bytesRead / 4) / 2;

  for (int i = 0; i < frames; i++) {
    const int32_t vL = buf[i * 2 + 0] >> 8;
    const int32_t vR = buf[i * 2 + 1] >> 8;
    const float x = 0.5f * ((float)vL + (float)vR) * (1.0f / 8388608.0f);

    const float ax = fabsf(x);

    const float hp = HP_ALPHA * (hp_y + x - hp_x_prev);
    hp_x_prev = x;
    hp_y = hp;
    const float trAx = fabsf(hp);

    envLP += LP_ENV_ALPHA * (ax - envLP);

    barSumSq += (double)(x * x);
    barTrSum += (double)trAx;
    barKickW.update((double)envLP);
    barN++;

    winSumSq += (double)(x * x);
    winTrSum += (double)trAx;
    winKickW.update((double)envLP);
    winN++;

    if (winN >= WIN_SAMPLES) {
      const float winRms  = sqrtf((float)(winSumSq / (double)winN));
      const float winTr   = (float)(winTrSum / (double)winN);
      const float winKVar = winKickW.var();

      lastWinRms = winRms;
      lastWinTr  = winTr;
      lastWinKVar = winKVar;
      lastWinUs = micros();

      if (winRms >= AUDIO_PRESENT_MIN_RMS) {
        lastAudioUs = lastWinUs;
        seenAnyAudio = true;
      }

      onMonitorWindow(winRms, winTr, winKVar);

      resetWinAcc();
    }
  }
}

// ---------------- BUTTONS (RED universal reset) ----------------
static bool lastRedBtn = true;

static void processButtons() {
  bool redNow = (digitalRead(PIN_BTN_RED) != LOW); // true=not pressed
  if (lastRedBtn == true && redNow == false) {
    Serial.printf("BTN RED_PRESS pos=%lu.%u sys=%s action=%s\n",
      (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
      (sysMode == SYS_FAIL ? "FAIL" : "OK"),
      "HARD_RESET(BASELINE_CLEARED)");

    if (sysMode == SYS_FAIL) {
      sysMode = SYS_OK;
      failReason = FAIL_NONE;
      logEvent("FAIL_EXIT_BY_RESET");
      clearStopLatches();
    }

    seenAnyClock = false;
    seenAnyAudio = false;
    lastClockUs = 0;
    lastAudioUs = 0;

    doManualResync();
  }
  lastRedBtn = redNow;
}

// ---------------- LED PWM INIT ----------------
static void pwmInit() {
  ledcAttach(PIN_LED_BLUE,   PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttach(PIN_LED_GREEN,  PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttach(PIN_LED_RED,    PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttach(PIN_LED_YELLOW, PWM_FREQ_HZ, PWM_RES_BITS);
  allOff();
}

// ---------------- ARDUINO ----------------
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\nShimon – Party Mode + MIDI Clock (Debug v8.1) + Visuals v1 + Failure/MusicStop\n");

  pinMode(PIN_BTN_BLUE,   INPUT_PULLUP);
  pinMode(PIN_BTN_RED,    INPUT_PULLUP);
  pinMode(PIN_BTN_GREEN,  INPUT_PULLUP);
  pinMode(PIN_BTN_YELLOW, INPUT_PULLUP);
  lastRedBtn = (digitalRead(PIN_BTN_RED) != LOW);

  pwmInit();

  MidiSerial.begin(31250, SERIAL_8N1, 34, -1);

  i2sInit();
  resetBarAcc();
  resetWinAcc();

  curBarForEvents = 0;
  curBeatForEvents = 0;

  Serial.println("Ready. Baseline learns only on qualified STD bars; transitions only after BASELINE_READY.");
  Serial.println("Policy v8.1: DROP=Return-Impact; sanity=post-DROP kick verification (window-level); cancel returns to BREAK.");
}

void loop() {
  processMidi();
  processAudio();
  processFailureWatchdog();
  processButtons();
  visualsRender();
  delay(1);
}
