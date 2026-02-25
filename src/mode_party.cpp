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
#include "mode_party.h"

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

// LEDC channels (one per wing)
static constexpr uint8_t LEDC_CH_BLUE   = 0;
static constexpr uint8_t LEDC_CH_RED    = 1;
static constexpr uint8_t LEDC_CH_GREEN  = 2;
static constexpr uint8_t LEDC_CH_YELLOW = 3;

// MOSFET minimum visible duty behavior (empirically ~70/255)
static constexpr uint8_t MIN_VISIBLE_DUTY = 70;

// ---------------- DEBUG CONTROL ----------------
// Set to true for verbose beat-by-beat logging (beats 2,3,4 within bars)
// Bar-level logs (beat 1) and state transitions always print
static constexpr bool DEBUG_BEAT_LOG = false;

// Set to true to log baseline updates and skips at every bar boundary.
// Logs BASE_UPDATE (qualified bar + new baseline values + ratios)
// and BASE_SKIP (rejected bar + skip reason) to track baseline development.
static constexpr bool DEBUG_BASELINE_LOG = true;

// ---------------- VISUAL TUNABLES ----------------
static constexpr uint8_t DEBUG_BRIGHT = 200;

// State brightness caps (0-255 scale, will be normalized)
static constexpr float CAP_STANDARD = 0.70f;
static constexpr float CAP_BREAK    = 0.50f;
static constexpr float CAP_DROP     = 1.00f;
static constexpr float CAND_DIM     = 0.55f;  // Multiplier on STANDARD during CAND

// Base brightness before state cap (max duty request)
static constexpr uint8_t BASE_BRIGHT = 235;

// Global power cap (safety net). If sum of channel duties > cap, scale down.
static constexpr uint16_t GLOBAL_DUTY_CAP = 320;

// Pattern window length
static constexpr uint8_t PATTERN_LEN_BARS = 8;

// Retrigger dip for accented beats (STD-02)
static constexpr float RETRIGGER_DIP = 0.15f;       // Brief dip at beat boundary
static constexpr float ACCENT_BOOST = 1.12f;        // Slight brightness boost on accent

// BREAK fade parameters
static constexpr uint8_t BREAK_FADE_BEATS = 2;      // Crossfade duration in beats

// DROP overlap for smooth half-beat transitions
static constexpr float DROP_OVERLAP_FRAC = 0.10f;

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
static constexpr float BASE_ALPHA_STD      = 0.10f; // post-ready: slow, stable updates
static constexpr float BASE_ALPHA_LEARNING = 0.30f; // pre-ready: fast convergence from false init
static constexpr uint16_t BASELINE_MIN_QUALIFIED_BARS = 16;

static constexpr float BASELINE_MIN_RMS = 0.020f;            // blocks near-silence baseline learning
static constexpr float KICK_PRESENT_KVAR_ABS_MIN = 0.0010f;  // pre-baseInited kick proxy
static constexpr float KICK_PRESENT_KR_MIN = 0.90f;          // for CAND detection (kick gone check)

// Representative bands for baseline updates - prevents drift from outliers
// Bar qualifies for update if: kR in band (mandatory) AND (rR in band OR tR in band)
static constexpr float BASELINE_UPDATE_KR_MIN = 0.90f;
static constexpr float BASELINE_UPDATE_KR_MAX = 1.10f;
static constexpr float BASELINE_UPDATE_RR_MIN = 0.90f;
static constexpr float BASELINE_UPDATE_RR_MAX = 1.10f;
static constexpr float BASELINE_UPDATE_TR_MIN = 0.90f;
static constexpr float BASELINE_UPDATE_TR_MAX = 1.10f;

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

// -------------- kMean 2D DETECTION (v9) --------------
// kMeanR = barKMean / baseKMean  (kick band energy presence, robust to character changes)
static constexpr float CAND_KMEANR_MAX     = 1.05f; // CAND entry blocked if kick band at/above baseline
static constexpr float RECOVERY_KMEANR_MIN = 0.90f; // CAND recovery: AND with kR (both must agree)
static constexpr float BREAK_KMEANR_MAX    = 0.75f; // deep break confirm requires real energy collapse

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

// -------------- TEMPO INTEGRITY GUARD (req 12.3.1) --------------
// Protects visual timing against erratic MIDI clock during BREAK sections.
// Some mixers lose BPM engine reference when kick is absent (BREAK), emitting
// a temporarily incorrect clock that self-corrects at the DROP.
static constexpr float   CLOCK_HOLD_JUMP_BPM     = 8.0f;  // enter hold if jump > this (BPM)
static constexpr float   CLOCK_HOLD_RESUME_BPM   = 4.0f;  // stable = within this of hold ref
static constexpr uint8_t CLOCK_HOLD_RESUME_BEATS = 8;     // consecutive stable beats to release

static bool     clockHoldActive      = false;
static uint32_t bpmHoldIntervalUs    = 0;   // frozen timing reference (us/beat)
static uint8_t  clockHoldStableBeats = 0;   // consecutive beats stable toward release

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

static volatile float lastBarRms = 0.0f, lastBarTr = 0.0f, lastBarKVar = 0.0f, lastBarKMean = 0.0f;
static volatile uint32_t lastBarUs = 0;

// ---- Ratios / context snapshot ----
static volatile float last_rR = 0.0f, last_tR = 0.0f, last_kR = 0.0f;
static volatile float last_bfR = 0.0f, last_bfT = 0.0f, last_bfK = 0.0f;
static volatile bool  last_hasBF = false;
static volatile ContextState last_stateForBar = STANDARD;

// ---------------- Party Mode globals ----------------
static bool baseInited = false;
static float baseRms = 0.0f, baseTr = 0.0f, baseKVar = 0.0f, baseKMean = 0.0f;

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

// Rolling kR / bKVar history for CAND entry context logging (last 4 bars)
static constexpr uint8_t KR_HIST_LEN = 4;
static float kRHist[KR_HIST_LEN]     = {};
static float bKVHist[KR_HIST_LEN]    = {};
static uint8_t kRHistIdx = 0;  // next write slot (wraps modulo KR_HIST_LEN)

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
static const uint8_t WING_CHANNELS[4] = { LEDC_CH_BLUE, LEDC_CH_RED, LEDC_CH_GREEN, LEDC_CH_YELLOW };

// Physical rotation order (clockwise from top-left): W1=BLUE, W2=RED, W3=GREEN, W4=YELLOW
static const Wing CW_ORDER[4]  = { W_BLUE, W_RED, W_GREEN, W_YELLOW };
static const Wing CCW_ORDER[4] = { W_BLUE, W_YELLOW, W_GREEN, W_RED };

static VisualMode visMode = VIS_STD;

// Per-wing current output duty
static uint8_t outDuty[4] = {0,0,0,0};

// Per-wing brightness request (0.0-1.0, before caps)
static float wingRequest[4] = {0,0,0,0};

// Optional debug trigger
static bool debugForced = false;

// ---------------- PATTERN FRAMEWORK ----------------

// Pattern IDs
enum PatternID : uint8_t {
  PAT_STD_01 = 0,   // Groove Rotation
  PAT_STD_02 = 1,   // Edge Oscillation Walk
  PAT_STD_03 = 2,   // Diagonal Pairs
  PAT_BRK_01 = 3,   // Slow Drift Relay
  PAT_BRK_02 = 4,   // Breathing Anchor
  PAT_BRK_03 = 5,   // Dual Flow Weave
  PAT_DRP_01 = 6,   // Impact Chase
  PAT_DRP_02 = 7,   // Alternating Burst Drive
  PAT_DRP_03 = 8,   // Expanding Impact Wave
  PAT_COUNT  = 9
};

// Pattern catalogs per state
static const PatternID STD_PATTERNS[3] = { PAT_STD_01, PAT_STD_02, PAT_STD_03 };
static const PatternID BRK_PATTERNS[3] = { PAT_BRK_01, PAT_BRK_02, PAT_BRK_03 };
static const PatternID DRP_PATTERNS[3] = { PAT_DRP_01, PAT_DRP_02, PAT_DRP_03 };

// Short pattern names for logging
static const char* patternName(PatternID p) {
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

// Calculate current BPM from beat interval
static float currentBPM() {
  if (lastBeatIntervalUs == 0) return 0.0f;
  return 60000000.0f / (float)lastBeatIntervalUs;
}

// Round-robin indices (persist while powered)
static uint8_t stdPatternIdx = 0;
static uint8_t brkPatternIdx = 0;
static uint8_t drpPatternIdx = 0;

// Current active pattern
static PatternID activePattern = PAT_STD_01;

// Pattern execution context
static uint8_t patWindowBar = 0;      // 1-8 within 8-bar window
static uint8_t patWindowBeat = 1;     // 1-4 within bar
static uint32_t patWindowStartBeat = 0;  // Global beat at window start

// BREAK crossfade state
static bool breakFading = false;
static Wing breakFrom = W_BLUE;
static Wing breakTo = W_GREEN;
static uint32_t breakFadeStartUs = 0;
static uint32_t breakFadeDurUs = 1000000;
static Wing brkLastWing = W_BLUE;     // For no-repeat rule

// DROP timing state
static uint8_t dropStep = 0;
static uint32_t lastHalfBeatUs = 0;
static uint32_t halfBeatUs = 250000;

// STD-02 retrigger state
static bool std02IsAccent = false;
static uint32_t std02RetriggerUs = 0;

static inline void writeWingDuty(Wing w, uint8_t duty) {
  ledcWrite(WING_CHANNELS[w], duty);
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

// Clear all wing requests
static void clearRequests() {
  for (int i = 0; i < 4; i++) wingRequest[i] = 0.0f;
}

// Set a wing's brightness request (0.0-1.0)
static void setWing(Wing w, float brightness) {
  if (w >= 4) {
    Serial.printf("WING_OOB setWing w=%u pos=%lu.%u\n",
                  (unsigned)w, (unsigned long)curBarForEvents, (unsigned)curBeatForEvents);
    return;
  }
  wingRequest[w] = clamp01(brightness);
}

// Add to a wing's brightness (for overlaps)
static void addWing(Wing w, float brightness) {
  if (w >= 4) {
    Serial.printf("WING_OOB addWing w=%u pos=%lu.%u\n",
                  (unsigned)w, (unsigned long)curBarForEvents, (unsigned)curBeatForEvents);
    return;
  }
  wingRequest[w] = clamp01(wingRequest[w] + brightness);
}

// Get state brightness cap
static float getStateCap() {
  switch (state) {
    case STANDARD: return CAP_STANDARD;
    case BREAK_CANDIDATE: return CAP_STANDARD * CAND_DIM;
    case BREAK_CONFIRMED: return CAP_BREAK;
    case DROP: return CAP_DROP;
    default: return CAP_STANDARD;
  }
}

// Apply state cap and write to LEDs
static void commitRequests() {
  float cap = getStateCap();
  uint8_t duties[4];

  for (int i = 0; i < 4; i++) {
    float level = wingRequest[i] * cap;
    duties[i] = dutyFromLevel(level, BASE_BRIGHT);
  }

  applyGlobalCap(duties);

  for (int i = 0; i < 4; i++) {
    writeWingDuty((Wing)i, duties[i]);
  }
}

// Cosine ease for smooth fades (0.0-1.0 input, 0.0-1.0 output)
static float easeInOut(float t) {
  t = clamp01(t);
  return 0.5f * (1.0f - cosf(t * 3.14159265f));
}

// ---------------- PATTERN IMPLEMENTATIONS ----------------

// --- STD-01: Groove Rotation ---
// CCW bars 1-4, CW bars 5-8, one wing per beat
static void patStd01OnBeat(uint8_t bar, uint8_t beat) {
  clearRequests();

  // Determine direction based on phase
  bool forward = (bar <= 4);

  // Calculate position in rotation (0-3)
  // Total beats from window start
  uint8_t totalBeats = ((bar - 1) * 4) + (beat - 1);
  uint8_t pos;

  if (forward) {
    pos = totalBeats % 4;  // CCW: 0,1,2,3
  } else {
    pos = (4 - (totalBeats % 4)) % 4;  // CW: 0,3,2,1
  }

  setWing(CW_ORDER[pos], 1.0f);
  commitRequests();
}

// --- STD-02: Edge Oscillation Walk ---
// Adjacent-wing oscillation that walks clockwise around the frame.
// Bar 1 (Top Edge):    W2 → W1 → W2 → W1
// Bar 2 (Right Edge):  W3 → W2 → W3 → W2
// Bar 3 (Bottom Edge): W4 → W3 → W4 → W3
// Bar 4 (Left Edge):   W1 → W4 → W1 → W4
// Bars 5-8: repeat Bars 1-4
static void patStd02OnBeat(uint8_t bar, uint8_t beat) {
  clearRequests();

  // Edge patterns: [beat1, beat2, beat3, beat4] for each bar
  // Oscillates between two adjacent wings per bar
  static const Wing EDGE_PATTERN[4][4] = {
    { W_RED, W_BLUE, W_RED, W_BLUE },       // Bar 1: Top edge (W2↔W1)
    { W_GREEN, W_RED, W_GREEN, W_RED },     // Bar 2: Right edge (W3↔W2)
    { W_YELLOW, W_GREEN, W_YELLOW, W_GREEN }, // Bar 3: Bottom edge (W4↔W3)
    { W_BLUE, W_YELLOW, W_BLUE, W_YELLOW }  // Bar 4: Left edge (W1↔W4)
  };

  // Bars 5-8 repeat bars 1-4
  uint8_t barIdx = ((bar - 1) % 4);
  uint8_t beatIdx = beat - 1;  // 0-3

  Wing w = EDGE_PATTERN[barIdx][beatIdx];
  setWing(w, 1.0f);

  commitRequests();
}

// --- STD-03: Diagonal Pairs ---
// Odd bars: W1↔W3, Even bars: W2↔W4, one wing per beat
static void patStd03OnBeat(uint8_t bar, uint8_t beat) {
  clearRequests();

  bool oddBar = (bar % 2) == 1;
  Wing pair[2];

  if (oddBar) {
    pair[0] = W_BLUE;   // W1
    pair[1] = W_GREEN;  // W3
  } else {
    pair[0] = W_RED;    // W2
    pair[1] = W_YELLOW; // W4
  }

  // Alternate within pair: beat 1,3 = pair[0]; beat 2,4 = pair[1]
  Wing w = ((beat % 2) == 1) ? pair[0] : pair[1];

  setWing(w, 1.0f);
  commitRequests();
}

// --- BRK-01: Slow Drift Relay ---
// 2-beat crossfades, pseudo-random sequence (no immediate repeat)
static void patBrk01OnBeat(uint8_t bar, uint8_t beat) {
  // Trigger new crossfade every 2 beats
  if ((beat == 1) || (beat == 3)) {
    Wing next = brkLastWing;
    // Pick random different wing
    while (next == brkLastWing) {
      next = (Wing)(esp_random() % 4);
    }

    breakFrom = brkLastWing;
    breakTo = next;
    brkLastWing = next;
    breakFading = true;
    breakFadeStartUs = micros();
    breakFadeDurUs = (uint32_t)BREAK_FADE_BEATS * lastBeatIntervalUs;
  }
  // Actual rendering happens in visualsRender() for smooth fades
}

// --- BRK-02: Breathing Anchor ---
// Single wing holds 1 bar, fade in first 2 beats, fade out last 2 beats
static void patBrk02OnBeat(uint8_t bar, uint8_t beat) {
  // Select wing for this bar (cycle through)
  Wing w = CW_ORDER[(bar - 1) % 4];

  // Store for render
  breakFrom = w;
  breakTo = w;  // Same wing, we'll handle fade in render

  if (beat == 1) {
    breakFading = true;
    breakFadeStartUs = micros();
    // Full bar duration
    breakFadeDurUs = 4 * lastBeatIntervalUs;
  }
}

// --- BRK-03: Dual Flow Weave ---
// Two wings active, one fading out, one fading in, transition every 2 beats
static void patBrk03OnBeat(uint8_t bar, uint8_t beat) {
  if ((beat == 1) || (beat == 3)) {
    // Advance to next wing
    Wing next = brkLastWing;
    next = CW_ORDER[((uint8_t)next + 1) % 4];  // Sequential, not random

    breakFrom = brkLastWing;
    breakTo = next;
    brkLastWing = next;
    breakFading = true;
    breakFadeStartUs = micros();
    breakFadeDurUs = (uint32_t)BREAK_FADE_BEATS * lastBeatIntervalUs;
  }
}

// --- DRP-01: Impact Chase ---
// Half-beat rotation, reverse at bar 5
static void patDrp01OnBeat(uint8_t bar, uint8_t beat) {
  // Reset step on beat for alignment
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
// Dual-axis 2-bar switching: W1+W3 bars 1-2,5-6; W2+W4 bars 3-4,7-8
// Half-beat pulses alternate between axes for energy
static bool drp02Axis13 = true;  // Track current axis for half-beat render

static void patDrp02OnBeat(uint8_t bar, uint8_t beat) {
  clearRequests();

  // Determine which axis based on bar
  drp02Axis13 = (bar == 1 || bar == 2 || bar == 5 || bar == 6);

  // Strong hit on beat
  if (drp02Axis13) {
    setWing(W_BLUE, 1.0f);
    setWing(W_GREEN, 1.0f);
  } else {
    setWing(W_RED, 1.0f);
    setWing(W_YELLOW, 1.0f);
  }

  dropStep = 0;  // Reset for half-beat tracking
  lastHalfBeatUs = micros();
  commitRequests();
}

static void patDrp02OnHalfBeat() {
  clearRequests();

  // Alternate: half-beat shows opposite axis briefly
  dropStep = (dropStep + 1) % 2;

  if (dropStep == 1) {
    // Off-beat: flash opposite axis
    if (!drp02Axis13) {
      setWing(W_BLUE, 0.6f);
      setWing(W_GREEN, 0.6f);
    } else {
      setWing(W_RED, 0.6f);
      setWing(W_YELLOW, 0.6f);
    }
  } else {
    // Main beat: primary axis
    if (drp02Axis13) {
      setWing(W_BLUE, 1.0f);
      setWing(W_GREEN, 1.0f);
    } else {
      setWing(W_RED, 1.0f);
      setWing(W_YELLOW, 1.0f);
    }
  }

  lastHalfBeatUs = micros();
  commitRequests();
}

// --- DRP-03: Expanding Impact Wave ---
// Bars 1-4: 1→2→3→4 wings; Bars 5-8: 4→3→2→1 wings
static void patDrp03OnBeat(uint8_t bar, uint8_t beat) {
  clearRequests();

  uint8_t numWings;
  if (bar <= 4) {
    numWings = bar;  // 1,2,3,4 wings
  } else {
    numWings = 9 - bar;  // 4,3,2,1 wings
  }

  // Light the first N wings in CW order, with beat-based emphasis
  for (uint8_t i = 0; i < numWings; i++) {
    float brightness = 1.0f;
    // Emphasize beat 1
    if (beat == 1) brightness = 1.0f;
    else brightness = 0.85f;

    setWing(CW_ORDER[i], brightness);
  }

  lastHalfBeatUs = micros();
  commitRequests();
}

static void patDrp03OnHalfBeat() {
  // Half-beat shimmer during multi-wing phases
  lastHalfBeatUs = micros();
}

// ---------------- PATTERN DISPATCH ----------------

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

// Select next pattern for state (round-robin)
static void selectPatternForState(ContextState s) {
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

  // Reset pattern window
  patWindowBar = 0;
  patWindowBeat = 1;
  patWindowStartBeat = gBeatIndex;

  // Reset pattern-specific state
  breakFading = false;
  dropStep = 0;
  lastHalfBeatUs = micros();
}

// ---------------- VISUAL MODE MANAGEMENT ----------------

static void onVisualModeEnter(VisualMode m) {
  if (m == VIS_STD) {
    // Select STANDARD pattern
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

// Track previous state for pattern switching
static ContextState prevStateForPattern = STANDARD;

static void visualsOnBeat(bool isBarStart) {
  VisualMode prevMode = visMode;
  refreshVisualMode();

  // Check for state change -> select new pattern
  // CAND continues current STD pattern (just dimmed), no pattern change
  bool shouldSelectNewPattern = false;
  if (state != prevStateForPattern) {
    if (state == BREAK_CONFIRMED || state == DROP) {
      // Entering BREAK or DROP: select new pattern for that state
      shouldSelectNewPattern = true;
    } else if (state == STANDARD && (prevStateForPattern == BREAK_CONFIRMED || prevStateForPattern == DROP)) {
      // Returning to STD from BREAK or DROP: select new STD pattern
      shouldSelectNewPattern = true;
    }
    // STD->CAND or CAND->STD: no pattern change, continue current pattern
    prevStateForPattern = state;
  }
  if (shouldSelectNewPattern) {
    selectPatternForState(state);
    patWindowBar = 0;  // Reset window for new pattern
    Serial.printf("PATTERN_SELECT pat=%s state=%s bpm=%.1f\n",
                  patternName(activePattern), ctxName(state), currentBPM());
  }

  // Update pattern window position
  // patWindowBar starts at 0, increments at bar start BEFORE use
  if (isBarStart) {
    patWindowBar++;
    // Check if 8-bar window completed
    if (patWindowBar > PATTERN_LEN_BARS) {
      selectPatternForState(state);
      patWindowBar = 1;  // Override internal reset: start at bar 1 of new pattern
      Serial.printf("PATTERN_SWITCH pat=%s bpm=%.1f\n", patternName(activePattern), currentBPM());
    }
    patWindowBeat = 1;
  } else {
    patWindowBeat++;
    if (patWindowBeat > 4) patWindowBeat = 1;  // Safety
  }

  // Debug mode override
  if (visMode == VIS_DEBUG) {
    clearRequests();
    bool on = ((globalBeatIndex() % 2) == 0);
    setWing(W_RED, on ? 1.0f : 0.0f);
    commitRequests();
    return;
  }

  // Dispatch to active pattern
  patternOnBeat(patWindowBar, patWindowBeat);
}

static void visualsOnHalfBeat() {
  refreshVisualMode();

  // Only DROP patterns use half-beats
  if (visMode == VIS_DROP) {
    patternOnHalfBeat();
  }
}

static void visualsRender() {
  // No audio signal ever seen: clock running but no music yet
  if (seenAnyClock && !seenAnyAudio) {
    static uint32_t lastNoAudioMs = 0;
    const uint32_t ms = millis();
    if ((uint32_t)(ms - lastNoAudioMs) >= 4000) {
      lastNoAudioMs = ms;
      Serial.printf("NO_AUDIO_SIGNAL bpm=%.1f\n", currentBPM());
    }
  }

  // FAILURE overlay: time-based blink, independent of MIDI/I2S
  if (sysMode == SYS_FAIL) {
    const uint32_t ms = millis();

    static uint32_t lastFailHbMs = 0;
    if ((uint32_t)(ms - lastFailHbMs) >= 4000) {
      lastFailHbMs = ms;
      const uint32_t nowUs = micros();
      const uint32_t clockAgeMs = seenAnyClock ? (uint32_t)((nowUs - lastClockUs) / 1000) : 999999;
      if (failReason == FAIL_AUDIO_LOST) {
        Serial.printf("NO_AUDIO bpm=%.1f clockAge_ms=%lu\n", currentBPM(), (unsigned long)clockAgeMs);
      } else {
        const uint32_t audioAgeMs = seenAnyAudio ? (uint32_t)((nowUs - lastAudioUs) / 1000) : 999999;
        Serial.printf("FAIL reason=%u clockAge_ms=%lu audioAge_ms=%lu ctx=%s\n",
          (unsigned)failReason, (unsigned long)clockAgeMs, (unsigned long)audioAgeMs, ctxName(state));
      }
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

  // BREAK patterns: smooth crossfade rendering
  if (visMode == VIS_BREAK) {
    if (!breakFading) return;

    const uint32_t dt = nowUs - breakFadeStartUs;
    float t = (breakFadeDurUs == 0) ? 1.0f : (float)dt / (float)breakFadeDurUs;
    t = clamp01(t);

    // Apply easing for smooth transitions
    float eased = easeInOut(t);

    clearRequests();

    // BRK-02 (Breathing Anchor) uses single wing with breath envelope
    if (activePattern == PAT_BRK_02) {
      // Fade in for first half, fade out for second half
      float breath;
      if (t < 0.5f) {
        breath = easeInOut(t * 2.0f);  // 0→1 over first half
      } else {
        breath = easeInOut((1.0f - t) * 2.0f);  // 1→0 over second half
      }
      setWing(breakFrom, breath);
    } else {
      // BRK-01 and BRK-03: crossfade between two wings
      setWing(breakFrom, 1.0f - eased);
      setWing(breakTo, eased);
    }

    commitRequests();

    if (t >= 1.0f) {
      breakFading = false;
    }
  }
  // DROP patterns: half-beat rotation with overlapping transitions
  else if (visMode == VIS_DROP) {
    halfBeatUs = lastBeatIntervalUs / 2;
    if (halfBeatUs < 20000) halfBeatUs = 20000;

    uint32_t dt = nowUs - lastHalfBeatUs;
    if (dt > (halfBeatUs * 4)) dt = halfBeatUs;

    float phase = (float)dt / (float)halfBeatUs;
    phase = clamp01(phase);

    clearRequests();

    // Pattern-specific DROP rendering
    if (activePattern == PAT_DRP_01) {
      // Impact Chase: half-beat rotation through all wings
      Wing cur = CW_ORDER[dropStep % 4];
      Wing nxt = CW_ORDER[(dropStep + 1) % 4];

      float holdEnd = 1.0f - DROP_OVERLAP_FRAC;

      if (phase <= holdEnd) {
        setWing(cur, 1.0f);
      } else {
        float u = (phase - holdEnd) / DROP_OVERLAP_FRAC;
        u = clamp01(u);
        setWing(cur, 1.0f - u);
        setWing(nxt, u);
      }
    }
    else if (activePattern == PAT_DRP_02) {
      // Alternating Burst Drive: smooth pulse decay between half-beats
      float pulse = 0.7f + 0.3f * (1.0f - phase);  // Decay from 1.0 to 0.7

      // Apply decay to current state (onBeat/onHalfBeat sets base)
      if (dropStep == 0) {
        // Primary axis
        if (drp02Axis13) {
          setWing(W_BLUE, pulse);
          setWing(W_GREEN, pulse);
        } else {
          setWing(W_RED, pulse);
          setWing(W_YELLOW, pulse);
        }
      } else {
        // Alternate axis (lower intensity)
        float altPulse = 0.4f + 0.2f * (1.0f - phase);
        if (!drp02Axis13) {
          setWing(W_BLUE, altPulse);
          setWing(W_GREEN, altPulse);
        } else {
          setWing(W_RED, altPulse);
          setWing(W_YELLOW, altPulse);
        }
      }
    }
    else if (activePattern == PAT_DRP_03) {
      // Expanding Impact Wave: shimmer on multi-wing lighting
      uint8_t numWings;
      if (patWindowBar <= 4) {
        numWings = patWindowBar;
      } else {
        numWings = 9 - patWindowBar;
      }

      float pulse = 0.85f + 0.15f * (1.0f - phase);  // Subtle shimmer
      for (uint8_t i = 0; i < numWings; i++) {
        setWing(CW_ORDER[i], pulse);
      }
    }

    commitRequests();
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

static void baselineInit(float rms, float tr, float kVar, float kMean) {
  baseRms = rms; baseTr = tr; baseKVar = kVar; baseKMean = kMean;
  baseInited = true;
  Serial.printf("BASE_INIT rms=%.4f tr=%.6f kVar=%.8f kMean=%.6f pos=%lu.%u\n",
                rms, tr, kVar, kMean, (unsigned long)curBarForEvents, (unsigned)curBeatForEvents);
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

static bool baselineEligibleBar(float rms, float kVar, float rR, float tR, float kR) {
  if (rms < BASELINE_MIN_RMS) return false;

  // Before baseline initialized: just check kick presence
  if (!baseInited) return (kVar >= KICK_PRESENT_KVAR_ABS_MIN);

  // After initialized but before ready: kick presence only - no band cap, allows convergence in any direction
  // Upper cap removed: false-low inits (quiet intro bar) would otherwise deadlock, blocking all groove bars
  if (!baselineReady) return (kVar >= KICK_PRESENT_KVAR_ABS_MIN);

  // After ready: use representative bands to protect stable baseline from drift
  // kR must be in band (mandatory)
  if (kR < BASELINE_UPDATE_KR_MIN || kR > BASELINE_UPDATE_KR_MAX) return false;

  // At least one of rR or tR must also be in band
  bool rRinBand = (rR >= BASELINE_UPDATE_RR_MIN && rR <= BASELINE_UPDATE_RR_MAX);
  bool tRinBand = (tR >= BASELINE_UPDATE_TR_MIN && tR <= BASELINE_UPDATE_TR_MAX);

  return (rRinBand || tRinBand);
}

static void baselineMaybeInitAndUpdate(float rms, float tr, float kVar, float kMean, float rR, float tR, float kR) {
  if (state != STANDARD) return; // freeze outside STD

  if (!baselineEligibleBar(rms, kVar, rR, tR, kR)) {
    if (DEBUG_BASELINE_LOG) {
      const char* why;
      if (rms < BASELINE_MIN_RMS)                                             why = "SILENCE";
      else if (kVar < KICK_PRESENT_KVAR_ABS_MIN)                             why = "NO_KICK";
      else if (baselineReady && (kR < BASELINE_UPDATE_KR_MIN ||
                                 kR > BASELINE_UPDATE_KR_MAX))                why = "KR_OOB";
      else                                                                     why = "ENERGY_OOB";
      Serial.printf("BASE_SKIP pos=%lu.%u why=%s rms=%.4f kVar=%.6f kR=%.2f rR=%.2f tR=%.2f\n",
                    (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                    why, rms, kVar, kR, rR, tR);
    }
    return;
  }

  if (!baseInited) {
    baselineInit(rms, tr, kVar, kMean); // logs BASE_INIT
    baselineQualifiedBars = 1;
    baselineReady = (baselineQualifiedBars >= BASELINE_MIN_QUALIFIED_BARS);
    if (baselineReady) logEvent("BASELINE_READY");
    return;
  }

  const float prevBlKVar  = baseKVar;
  const float prevBlKMean = baseKMean;
  if (DEBUG_BASELINE_LOG && prevBlKVar > 0.5f) {
    Serial.printf("BASE_ANOMALY pos=%lu.%u prevBlKVar=%.8f baseInited=%d baselineReady=%d qual=%u\n",
                  (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                  prevBlKVar, (int)baseInited, (int)baselineReady,
                  (unsigned)baselineQualifiedBars);
  }
  const float a = baselineReady ? BASE_ALPHA_STD : BASE_ALPHA_LEARNING;
  baseRms   = (1.0f - a) * baseRms   + a * rms;
  baseTr    = (1.0f - a) * baseTr    + a * tr;
  baseKVar  = (1.0f - a) * baseKVar  + a * kVar;
  baseKMean = (1.0f - a) * baseKMean + a * kMean;

  if (!baselineReady) {
    baselineQualifiedBars++;
    if (baselineQualifiedBars >= BASELINE_MIN_QUALIFIED_BARS) {
      baselineReady = true;
      logEvent("BASELINE_READY");
    }
  }

  if (DEBUG_BASELINE_LOG) {
    Serial.printf("BASE_UPDATE pos=%lu.%u kR=%.2f rR=%.2f tR=%.2f kVar=%.8f->%.8f kMean=%.6f->%.6f qual=%u/%u\n",
                  (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                  kR, rR, tR, prevBlKVar, baseKVar, prevBlKMean, baseKMean,
                  (unsigned)baselineQualifiedBars, (unsigned)BASELINE_MIN_QUALIFIED_BARS);
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
static void onBarFinalized(uint32_t finalizedBarNumber, float rms, float tr, float kVar, float kMean) {
  const float rR    = baseInited ? safeDiv(rms,   baseRms)   : 0.0f;
  const float tR    = baseInited ? safeDiv(tr,    baseTr)    : 0.0f;
  const float kR    = baseInited ? safeDiv(kVar,  baseKVar)  : 0.0f;

  baselineMaybeInitAndUpdate(rms, tr, kVar, kMean, rR, tR, kR);

  // kMeanR computed after baseline update so baseKMean is current
  const float kMeanR = (baseInited && baseKMean > 0.0f) ? safeDiv(kMean, baseKMean) : 0.0f;

  // Push bar kR and post-update bKVar into rolling history (used at CAND entry)
  if (baseInited) {
    kRHist[kRHistIdx % KR_HIST_LEN] = kR;
    bKVHist[kRHistIdx % KR_HIST_LEN] = baseKVar;
    kRHistIdx++;
  }

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
  // 2D gate: kick impulsiveness low (kR) AND kick band energy not at baseline level (kMeanR)
  if (state == STANDARD) {
    if ((kR < KICK_GONE_KR_MAX) && (kMeanR < CAND_KMEANR_MAX) && (stdKickGoneWinStreak >= KICK_GONE_CONFIRM_WINDOWS)) {
      state = BREAK_CANDIDATE;
      candEnterBar = finalizedBarNumber;
      breakRecoveryBars = 0;
      candDeepStreak = 0;
      breakReset();
      clearReturnTracking();
      clearDropVerify();
      logTransition(prev, state, "CAND_ENTER_KICK_ABSENCE");
      // Dump last 4 bars of kR and bKVar to show drift trajectory leading into CAND
      Serial.printf("CAND_CONTEXT wStr=%u kMeanR=%.2f last%u_kR=", (unsigned)stdKickGoneWinStreak, kMeanR, (unsigned)KR_HIST_LEN);
      for (uint8_t i = 0; i < KR_HIST_LEN; i++) {
        uint8_t slot = (kRHistIdx - KR_HIST_LEN + i) % KR_HIST_LEN;
        Serial.printf("%.2f%s", kRHist[slot], (i < KR_HIST_LEN - 1) ? "," : "");
      }
      Serial.printf(" last%u_bKV=", (unsigned)KR_HIST_LEN);
      for (uint8_t i = 0; i < KR_HIST_LEN; i++) {
        uint8_t slot = (kRHistIdx - KR_HIST_LEN + i) % KR_HIST_LEN;
        Serial.printf("%.6f%s", bKVHist[slot], (i < KR_HIST_LEN - 1) ? "," : "");
      }
      Serial.printf("\n");
      prev = state;
    }
  }

  // ----- CAND -> BREAK OR recover to STD -----
  if (state == BREAK_CANDIDATE) {
    const bool canEvalDeep = (finalizedBarNumber >= (candEnterBar + (uint32_t)CAND_MIN_BARS));
    const bool deep = (kR < DEEP_BREAK_KR_MAX) &&
                      ((rR < DEEP_BREAK_RMS_MAX) || (tR < DEEP_BREAK_TR_MAX)) &&
                      (kMeanR < BREAK_KMEANR_MAX); // kick band energy must genuinely collapse

    if (canEvalDeep && deep) candDeepStreak++;
    else                     candDeepStreak = 0;

    if (candDeepStreak >= 2) {
      state = BREAK_CONFIRMED;
      candDeepStreak = 0;
      breakReset();           // break floor will init on first BREAK bar update below
      clearReturnTracking();
      clearDropVerify();
      // Capture stable pre-BREAK tempo as reference for CLOCK_HOLD guard
      bpmHoldIntervalUs    = lastBeatIntervalUs;
      clockHoldActive      = false;
      clockHoldStableBeats = 0;
      logTransition(prev, state, "BREAK_CONFIRM_DEEP_2B");
      prev = state;
    } else {
      // AND logic: both kick impulsiveness AND kick band energy must agree on recovery.
      // Symmetric with entry (which requires both kR and kMeanR to signal absence).
      // Prevents oscillation on sub-bass-heavy tracks where kMeanR stays near baseline
      // while kR remains low — OR logic would falsely recover on kMeanR alone.
      // Minimum 1 bar in CAND before recovery evaluated (prevents same-bar entry+recovery).
      const bool canEvalRecover = (finalizedBarNumber > candEnterBar);
      const bool kickPresent = (kR >= CAND_RECOVERY_KR_MIN) && (kMeanR >= RECOVERY_KMEANR_MIN);
      const bool okRecover = canEvalRecover && (rR >= RECOVERY_RR_MIN) && (tR >= RECOVERY_TR_MIN) && kickPresent;
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
  float rms = 0.0f, tr = 0.0f, kVar = 0.0f, kMean = 0.0f;
  if (barN > 0) {
    rms = sqrtf((float)(barSumSq / (double)barN));
    tr  = (float)(barTrSum / (double)barN);
    kVar = barKickW.var();
    kMean = (float)barKickW.mean;
  }

  lastBarRms = rms;
  lastBarTr  = tr;
  lastBarKVar = kVar;
  lastBarKMean = kMean;
  lastBarUs = stampUs;

  resetBarAcc();
  onBarFinalized(finalizedBarNumber, rms, tr, kVar, kMean);
}

// ---------------- Beat log ----------------
static void logBeatLine(uint32_t nowUs, bool isBarStart) {
  const uint32_t dtUs = (lastBeatUs == 0) ? 0 : (nowUs - lastBeatUs);
  if (dtUs > 0) {
    const uint32_t candidateUs = (uint32_t)(0.85f * (float)lastBeatIntervalUs + 0.15f * (float)dtUs);

    if (clockHoldActive) {
      // Hold active: count toward release in STANDARD or DROP once clock re-stabilises.
      // BREAK/CAND remain fully frozen. The stability gate (within CLOCK_HOLD_RESUME_BPM)
      // is the real guard — no need to restrict by state beyond excluding BREAK/CAND.
      if ((state == STANDARD || state == DROP) && bpmHoldIntervalUs > 0) {
        const float holdBpm = 60000000.0f / (float)bpmHoldIntervalUs;
        const float candBpm = 60000000.0f / (float)candidateUs;
        if (fabsf(candBpm - holdBpm) <= CLOCK_HOLD_RESUME_BPM) {
          clockHoldStableBeats++;
          if (clockHoldStableBeats >= CLOCK_HOLD_RESUME_BEATS) {
            clockHoldActive      = false;
            clockHoldStableBeats = 0;
            lastBeatIntervalUs   = candidateUs;
            Serial.printf("EVENT CLOCK_HOLD_RELEASE pos=%lu.%u resumedBpm=%.1f\n",
                          (unsigned long)curBarForEvents, (unsigned)curBeatForEvents, candBpm);
          }
          // else: still counting; lastBeatIntervalUs stays frozen
        } else {
          clockHoldStableBeats = 0; // erratic even in STD; stay frozen
        }
      } else {
        // BREAK / CAND / DROP: fully frozen, reset stable-beat counter
        clockHoldStableBeats = 0;
      }
      // lastBeatIntervalUs is NOT updated while hold is active

    } else if (state == BREAK_CONFIRMED && bpmHoldIntervalUs > 0) {
      // In BREAK, no hold yet: use raw tick BPM for immediate jump detection.
      // Comparing against the IIR candidate would hide a sudden jump — the IIR
      // absorbs only 15% per beat, so a 33 BPM raw jump appears as ~5 BPM per
      // filtered step, never crossing the threshold while the reference slides.
      // Using the raw tick catches the erratic beat on its very first occurrence.
      const float holdBpm  = 60000000.0f / (float)bpmHoldIntervalUs;
      const float rawBpm   = 60000000.0f / (float)dtUs;  // unsmoothed raw tick
      const float bpmDelta = fabsf(rawBpm - holdBpm);
      if (bpmDelta > CLOCK_HOLD_JUMP_BPM) {
        clockHoldActive      = true;
        clockHoldStableBeats = 0;
        Serial.printf("EVENT CLOCK_HOLD_ENTER pos=%lu.%u holdBpm=%.1f rawBpm=%.1f delta=%.1f\n",
                      (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                      holdBpm, rawBpm, bpmDelta);
        // Don't update lastBeatIntervalUs — reject the erratic tick
      } else {
        // Stable tick in BREAK: accept IIR update for smooth visual timing.
        // bpmHoldIntervalUs intentionally NOT updated — fixed at BREAK entry
        // so the reference never drifts toward a gradually-converging erratic value.
        lastBeatIntervalUs = candidateUs;
      }

    } else {
      // STANDARD / CAND / no hold reference: normal IIR update
      lastBeatIntervalUs = candidateUs;
    }
  }
  lastBeatUs = nowUs;

  // Suppress bar logs when there is no audio signal
  if (!seenAnyAudio || sysMode == SYS_FAIL) {
    ticksSinceBeat = 0;
    return;
  }

  // Skip mid-bar beats (2,3,4) unless DEBUG_BEAT_LOG enabled
  if (!isBarStart && !DEBUG_BEAT_LOG) {
    ticksSinceBeat = 0;
    return;
  }

  // Compact bar-level format (when DEBUG_BEAT_LOG is off)
  if (isBarStart && !DEBUG_BEAT_LOG) {
    Serial.printf("bar=%lu state=%s rR=%.2f tR=%.2f kR=%.2f",
                  (unsigned long)barCount, ctxName(last_stateForBar),
                  last_rR, last_tR, last_kR);

    if (baseInited) {
      Serial.printf(" wStr=%u", (unsigned)stdKickGoneWinStreak);
    }

    if (baseInited) {
      const float _kMeanR = (baseKMean > 0.0f) ? safeDiv(lastBarKMean, baseKMean) : 0.0f;
      const float _kCV    = (lastBarKMean > 0.0f) ? safeDiv(lastBarKVar, lastBarKMean) : 0.0f;
      Serial.printf(" kVar=%.6f kMean=%.6f blKV=%.6f kMeanR=%.2f kCV=%.4f", lastBarKVar, lastBarKMean, baseKVar, _kMeanR, _kCV);
    }

    if (last_hasBF) {
      Serial.printf(" bfK=%.2f", last_bfK);
    }

    if (!baselineReady) {
      Serial.printf(" qual=%u/%u", (unsigned)baselineQualifiedBars, (unsigned)BASELINE_MIN_QUALIFIED_BARS);
    }

    if (state == BREAK_CONFIRMED && returnActive) {
      Serial.printf(" RET(pkK=%.2f)", peak_bfK);
    }

    if (state == DROP && dropVerifyActive) {
      Serial.printf(" VFY(%u/%u)", (unsigned)dropVerifyGood, (unsigned)DROP_VERIFY_MIN_GOOD);
    }

    if (clockHoldActive) {
      Serial.printf(" CLKHOLD(%.1f)", 60000000.0f / (float)bpmHoldIntervalUs);
    }

    Serial.print("\n");
    ticksSinceBeat = 0;
    return;
  }

  // Verbose format (DEBUG_BEAT_LOG enabled)
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
    Serial.printf(" | state=%s rR=%.2f tR=%.2f kR=%.2f",
                  ctxName(last_stateForBar), last_rR, last_tR, last_kR);

    if (baseInited) {
      Serial.printf(" wStr=%u", (unsigned)stdKickGoneWinStreak);
    }

    if (last_hasBF) {
      Serial.printf(" bfR=%.2f bfT=%.2f bfK=%.2f", last_bfR, last_bfT, last_bfK);
    }

    if (!baselineReady) {
      Serial.printf(" qual=%s(%u/%u)",
                    (baseInited ? "LEARN" : "OFF"),
                    (unsigned)baselineQualifiedBars,
                    (unsigned)BASELINE_MIN_QUALIFIED_BARS);
    }

    if (state == BREAK_CONFIRMED && returnActive) {
      Serial.printf(" ret=ON(budg=%u pkK=%.2f pkR=%.2f pkT=%.2f)",
                    (unsigned)returnBudget, peak_bfK, peak_bfR, peak_bfT);
    }

    if (state == DROP && dropVerifyActive) {
      Serial.printf(" vfy=ON(budg=%u good=%u/%u)",
                    (unsigned)dropVerifyBudget, (unsigned)dropVerifyGood, (unsigned)DROP_VERIFY_WINDOWS);
    }

    if (clockHoldActive) {
      Serial.printf(" CLKHOLD(hold=%.1f)", 60000000.0f / (float)bpmHoldIntervalUs);
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

  clockHoldActive      = false;
  bpmHoldIntervalUs    = 0;
  clockHoldStableBeats = 0;

  curBarForEvents = 1;
  curBeatForEvents = 0;

  stdKickGoneWinStreak = 0;

  resetBarAcc();
  resetWinAcc();
  envLP = 0.0f;
  hp_y = 0.0f;
  hp_x_prev = 0.0f;

  baseInited = false;
  baseRms = baseTr = baseKVar = baseKMean = 0.0f;
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
  activePattern = PAT_STD_01;
  stdPatternIdx = 1;  // Next switch will get S-2
  brkPatternIdx = 0;
  drpPatternIdx = 0;
  patWindowBar = 0;
  patWindowBeat = 1;
  patWindowStartBeat = 0;
  prevStateForPattern = STANDARD;
  breakFading = false;
  brkLastWing = W_BLUE;
  dropStep = 0;
  lastHalfBeatUs = micros();
  std02IsAccent = false;
  std02RetriggerUs = 0;
  allOff();

  gBeatIndex = 0;
}

static void resetForResumeLike() {
  tickInBeat = 0;
  ticksSinceBeat = 0;
  barCount = 1;
  beatInBar = 0;
  lastBeatUs = 0;

  clockHoldActive      = false;
  bpmHoldIntervalUs    = 0;
  clockHoldStableBeats = 0;

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
  activePattern = PAT_STD_01;
  stdPatternIdx = 1;  // Next switch will get S-2
  brkPatternIdx = 0;
  drpPatternIdx = 0;
  patWindowBar = 0;
  patWindowBeat = 1;
  patWindowStartBeat = 0;
  prevStateForPattern = STANDARD;
  breakFading = false;
  brkLastWing = W_BLUE;
  dropStep = 0;
  lastHalfBeatUs = micros();
  std02IsAccent = false;
  std02RetriggerUs = 0;
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
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
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
  // Setup LEDC channels (older ESP32 Arduino API)
  ledcSetup(LEDC_CH_BLUE,   PWM_FREQ_HZ, PWM_RES_BITS);
  ledcSetup(LEDC_CH_RED,    PWM_FREQ_HZ, PWM_RES_BITS);
  ledcSetup(LEDC_CH_GREEN,  PWM_FREQ_HZ, PWM_RES_BITS);
  ledcSetup(LEDC_CH_YELLOW, PWM_FREQ_HZ, PWM_RES_BITS);

  // Attach pins to channels
  ledcAttachPin(PIN_LED_BLUE,   LEDC_CH_BLUE);
  ledcAttachPin(PIN_LED_RED,    LEDC_CH_RED);
  ledcAttachPin(PIN_LED_GREEN,  LEDC_CH_GREEN);
  ledcAttachPin(PIN_LED_YELLOW, LEDC_CH_YELLOW);

  allOff();
}

// ---------------- MODE INTERFACE ----------------
void party_init() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\nShimon – Party Mode + MIDI Clock (Debug v8.1) + Visuals v1 + Failure/MusicStop\n");
  Serial.printf("MEM baseKVar=0x%08X wingReq[0]=0x%08X [1]=0x%08X [2]=0x%08X [3]=0x%08X\n",
                (uint32_t)&baseKVar,
                (uint32_t)&wingRequest[0], (uint32_t)&wingRequest[1],
                (uint32_t)&wingRequest[2], (uint32_t)&wingRequest[3]);

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

void party_tick() {
  processMidi();
  processAudio();
  processFailureWatchdog();
  processButtons();
  visualsRender();
  delay(1);
}

void party_stop() {
  allOff();                          // zero all LED duties immediately
  resetForHardReset();               // reset FSM, baselines, accumulators, visuals
  i2s_driver_uninstall(I2S_PORT);   // free DMA buffers
  MidiSerial.end();                  // release UART1 so Game Mode can use it for DFPlayer

  // Reset failure-tracking state not covered by resetForHardReset()
  midiRunning      = false;
  sysMode          = SYS_OK;
  failReason       = FAIL_NONE;
  seenAnyClock     = false;
  seenAnyAudio     = false;
  clockLostLatched = false;
  audioLostLatched = false;

  Serial.println("[PARTY] Mode stopped.");
}
