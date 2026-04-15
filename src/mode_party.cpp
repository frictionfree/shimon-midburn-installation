// Shimon – Party Mode: I2S audio analysis + MIDI clock beat sync + visual pattern engine


#include <Arduino.h>
#include "driver/i2s.h"
#include "mode_party.h"
#include "hw.h"
#include "party_patterns.h"
#ifndef USE_WOKWI
#include <DFRobotDFPlayerMini.h>
#include "shimon.h"   // DFPLAYER_RX / DFPLAYER_TX
#endif

// ---------- FAILURE TYPES (MUST BE ABOVE ANY FUNCTIONS) ----------
enum SystemMode : uint8_t { SYS_OK=0, SYS_FAIL=1 };
enum FailReason : uint8_t { FAIL_NONE=0, FAIL_CLOCK_LOST=1, FAIL_AUDIO_LOST=2 };

static bool clockLostLatched = false;
static bool audioLostLatched = false;
static uint32_t clockLostAtUs = 0;
static uint32_t audioLostAtUs = 0;
static bool prevBothPresent = false;

// ---------------- STATE ----------------
// ContextState enum is defined in party_patterns.h
static const char* ctxName(ContextState s) { return pp_ctxName(s); }

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
// I2S_PIN_BCLK / I2S_PIN_LRCK / I2S_PIN_DATA / I2S_SAMPLE_RATE defined in shimon.h

static constexpr int I2S_READ_FRAMES = 256;

// -------------- MONITOR WINDOW (policy) --------------
static constexpr uint32_t MONITOR_WIN_MS = 75;
static constexpr uint32_t WIN_SAMPLES = (I2S_SAMPLE_RATE * MONITOR_WIN_MS) / 1000;

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
static constexpr int   CAND_MIN_BARS    = 0;        // minimum bars in CAND before BREAK eval (0 = allow deep eval on entry bar)

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

// -------------- BPM RANGE + SPIKE GUARDS (D1 / D2) --------------
// D1: reject any beat whose smoothed BPM lands outside the expected DJ range.
// D2: reject a single-beat spike larger than BPM_SPIKE_MAX regardless of state.
// Both guards fire before CLOCK_HOLD; lastBeatIntervalUs is left unchanged on reject.
static constexpr float BPM_RANGE_MIN = 80.0f;   // D1: below this = corrupt clock
static constexpr float BPM_RANGE_MAX = 160.0f;  // D1: above this = corrupt clock
static constexpr float BPM_SPIKE_MAX = 20.0f;   // D2: max single-beat delta (BPM)

static bool     clockHoldActive      = false;
static uint32_t bpmHoldIntervalUs    = 0;   // frozen timing reference (us/beat)
static uint8_t  clockHoldStableBeats = 0;   // consecutive beats stable toward release

// Current position snapshot
static volatile uint32_t curBarForEvents = 0;
static volatile uint8_t  curBeatForEvents = 0;


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
static uint32_t noMidiStartMs = 0;  // set at party_init(); used for 60s no-MIDI timeout

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

// ---------------- BPM helper (used by failure-overlay logs) ----------------
static float currentBPM() {
  if (lastBeatIntervalUs == 0) return 0.0f;
  return 60000000.0f / (float)lastBeatIntervalUs;
}

// PatternID enum is defined in party_patterns.h


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
    req[RED] = on ? 200 : 0;
    hw_led_all_set(req);
    return;
  }

  pp_render();
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
    const float candBpm = 60000000.0f / (float)candidateUs;

    // D1: range guard — reject smoothed BPM outside [BPM_RANGE_MIN, BPM_RANGE_MAX]
    bool rejectTick = false;
    if (candBpm < BPM_RANGE_MIN || candBpm > BPM_RANGE_MAX) {
      Serial.printf("EVENT BPM_RANGE_REJECT pos=%lu.%u bpm=%.1f (range [%.0f,%.0f])\n",
                    (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                    candBpm, BPM_RANGE_MIN, BPM_RANGE_MAX);
      rejectTick = true;
    }
    // D2: spike guard — reject single-beat jump > BPM_SPIKE_MAX
    else if (lastBeatIntervalUs > 0) {
      const float curBpm = 60000000.0f / (float)lastBeatIntervalUs;
      const float delta  = fabsf(candBpm - curBpm);
      if (delta > BPM_SPIKE_MAX) {
        Serial.printf("EVENT BPM_SPIKE_REJECT pos=%lu.%u candBpm=%.1f curBpm=%.1f delta=%.1f\n",
                      (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                      candBpm, curBpm, delta);
        rejectTick = true;
      }
    }

    if (!rejectTick) {
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
    } // if (!rejectTick)
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
    Serial.printf("bar=%lu state=%s bpm=%.1f pat=%s rR=%.2f tR=%.2f kR=%.2f",
                  (unsigned long)barCount,
                  ctxName(last_stateForBar),
                  60000000.0f / (float)lastBeatIntervalUs,
                  pp_patternName(pp_activePattern()),
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

  pp_reset();
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

  pp_reset();
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
  // Already in FAIL — nothing more to classify. Clear stale latches so they
  // don't cause a spurious re-trigger on recovery, then bail out.
  if (sysMode == SYS_FAIL) { clearStopLatches(); return; }

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

  // In FAIL state: suppress all audio analysis and visual output driven by beats.
  if (sysMode == SYS_FAIL) { ticksSinceBeat = 0; return; }

  // finalize previous bar at start of current bar (barCount >= 2)
  if (isBarStart && barCount >= 2) {
    finalizeBarNow(nowUs, barCount - 1);
  }

  pp_setContext(state, lastBeatIntervalUs);
  pp_onBeat(barCount, beatInBar);
  logBeatLine(nowUs, isBarStart);

  ticksSinceBeat = 0;
}

static void onMidiHalfBeat() { if (sysMode != SYS_FAIL) pp_onHalfBeat(); }

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

      if (tickInBeat == 0)  { onMidiBeat(); }
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
  cfg.sample_rate = I2S_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = I2S_PIN_BCLK;
  pins.ws_io_num = I2S_PIN_LRCK;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = I2S_PIN_DATA;

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
// Red button: short press = MIDI/bar resync (keep baseline); long press >= 3 s = hard reset
static constexpr uint32_t RED_LONG_PRESS_MS  = 3000;
static constexpr uint32_t RED_SHORT_MIN_MS   = 20;   // min hold-time to reject PWM ghost clicks

static void processButtons() {
  static uint32_t redHoldAtRelease = 0;
  static bool     redWasPressed    = false;
  bool redIsPressed = hw_btn_pressed(RED);

  if (redIsPressed) redHoldAtRelease = hw_btn_held_ms(RED);

  if (redWasPressed && !redIsPressed) {
    // Release edge - act based on captured hold duration
    // Clear failure state on any Red action
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

    if (redHoldAtRelease < RED_SHORT_MIN_MS) {
      // Sub-threshold: ghost click from PWM noise — ignore silently
      // (hw layer's 50ms ghost filter makes this redundant, kept for documentation)
      Serial.printf("BTN RED_GHOST holdMs=%lu ignored\n", (unsigned long)redHoldAtRelease);
    } else if (redHoldAtRelease >= RED_LONG_PRESS_MS) {
      // Long press: hard reset (clears baseline)
      Serial.printf("BTN RED_LONG pos=%lu.%u holdMs=%lu action=HARD_RESET(BASELINE_CLEARED)\n",
                    (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                    (unsigned long)redHoldAtRelease);
      doManualResync();
    } else {
      // Short press: MIDI/bar resync (keeps baseline)
      Serial.printf("BTN RED_SHORT pos=%lu.%u holdMs=%lu action=MIDI_RESYNC\n",
                    (unsigned long)curBarForEvents, (unsigned)curBeatForEvents,
                    (unsigned long)redHoldAtRelease);
      Serial.printf("EVENT MANUAL_RESYNC pos=%lu.%u\n",
                    (unsigned long)curBarForEvents, (unsigned)curBeatForEvents);
      resetForResumeLike();
    }
  }

  redWasPressed = redIsPressed;
}

// ---------------- MODE INTERFACE ----------------
void party_init() {
  Serial.println("[PARTY] Entered Party Mode.");

  // Party mode splash: 2× all-wing pulse
  uint8_t on[4] = {180, 180, 180, 180};
  uint8_t off4[4] = {0, 0, 0, 0};
  for (int f = 0; f < 2; f++) { hw_led_all_set(on); delay(150); hw_led_all_set(off4); delay(100); }

#ifndef USE_WOKWI
  // DFPlayer is left running during party mode (~45mA).
  // Sleep mode was removed — reliable wake-from-sleep via UART requires
  // hardware investigation (MOSFET on VCC) before re-enabling.
#endif
  MidiSerial.begin(MIDI_BAUD_RATE, SERIAL_8N1, MIDI_PIN_RX, -1);

  i2sInit();
  resetBarAcc();
  resetWinAcc();

  curBarForEvents = 0;
  curBeatForEvents = 0;
  noMidiStartMs = millis();

  Serial.printf("[PARTY] MIDI: listening on pin %d at %d bps\n", MIDI_PIN_RX, MIDI_BAUD_RATE);
  Serial.println("[PARTY] I2S: initialized.");
  Serial.printf("[PARTY] Visual patterns: %d loaded.\n", PAT_COUNT);
  Serial.println("[PARTY] Waiting for MIDI clock...");
}

void party_tick() {
  processMidi(); // always runs — detects first MIDI tick and sets seenAnyClock

  if (!seenAnyClock) {
    // Waiting for MIDI clock: flash GREEN slowly (500ms on/off)
    const uint32_t ms = millis();
    hw_led_duty(GREEN, ((ms / 500) & 1) ? 180 : 0);

    // No MIDI for 60s → return to mode selection
    if (ms - noMidiStartMs >= 60000UL) {
      Serial.println("[PARTY] No MIDI detected for 60s — returning to mode selection.");
      party_stop();
      delay(200);
      ESP.restart();
    }
    return;
  }

  processAudio();
  processFailureWatchdog();
  processButtons();
  visualsRender();
  delay(1);
}

void party_stop() {
  hw_led_all_off();                          // zero all LED duties immediately
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
