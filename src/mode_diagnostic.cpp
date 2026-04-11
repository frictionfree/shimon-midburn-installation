/*
  Diagnostic Mode - 5-phase hardware verification
  A:   LED PWM   - cycle each wing at 3 brightness levels, operator confirms
  B:   Buttons   - light each wing, wait for corresponding button press
  C+D: MIDI+I2S  - concurrent 8 s listen; bar-level BPM/RMS/TR/kVar output;
                   visual result: GREEN=full stack OK, YELLOW=I2S weak,
                   RED=no I2S; slow RED blink + 30 s wait if no MIDI
  E:   DFPlayer  - play track 1, press BLUE within 8 s to confirm heard
*/

#include <Arduino.h>
#include <math.h>
#include "driver/i2s.h"
#include "shimon.h"
#include "hw.h"
#include "mode_diagnostic.h"

#ifndef USE_WOKWI
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#endif

// ---- Timing constants ----
static constexpr uint32_t PHASE_A_LEVEL_MS       = 800;
static constexpr uint32_t PHASE_A_TIMEOUT_MS      = 15000;
static constexpr uint32_t PHASE_B_BTN_TOUT        = 5000;
// Order: GREEN→RED→BLUE→YELLOW avoids conflicts with the BLUE press used to
// enter Phase B and the BLUE press used to confirm Phase C_PROMPT afterward.
static const Color phB_ORDER[4] = { GREEN, RED, BLUE, YELLOW };
static constexpr uint32_t PHASE_C_PROMPT_MS       = 30000;
static constexpr uint32_t PHASE_CD_LISTEN_MS      = 8000;
static constexpr uint32_t PHASE_CD_NOMIDI_TOUT_MS = 30000;
static constexpr uint32_t DIAG_DONE_AUTO_MS       = 20000;
static constexpr uint32_t PHASE_E_TIMEOUT_MS      = 8000;

// ---- MIDI ----
static constexpr uint8_t MIDI_CLOCK_BYTE    = 0xF8;
static constexpr uint8_t MIDI_TICKS_PER_BAR = 96;   // 24 ticks/beat × 4 beats/bar

// ---- I2S ----
static constexpr i2s_port_t DIAG_I2S_PORT  = I2S_NUM_0;
static constexpr int        CD_READ_FRAMES = 128;    // samples per non-blocking read
static constexpr uint32_t   D_MIN_FRAMES   = 10000;  // below = no I2S clock (converter absent)
static constexpr float      D_MUSIC_RMS    = 0.050f; // above = music signal present

// ---- Signal processing (mirrors party mode) ----
static constexpr float LP_ENV_ALPHA = 0.010f;  // low-pass envelope coefficient
static constexpr float HP_COEFF     = 0.95f;   // high-pass coefficient (removes DC/sub)

// ---- Phase E ----
static constexpr uint8_t PHASE_E_TRACK = 1;
static constexpr uint8_t PHASE_E_VOL   = 20;

// ---- Hardware objects ----
#ifndef USE_WOKWI
static HardwareSerial      DiagMidi(1);
static HardwareSerial      DiagDfpSer(1);
static DFRobotDFPlayerMini diagDfp;
#endif

// ---- Welford online variance (same algorithm as party mode) ----
struct DiagWelford {
  double mean = 0.0, m2 = 0.0;
  uint32_t n = 0;
  void reset() { mean = 0.0; m2 = 0.0; n = 0; }
  void update(double x) {
    n++;
    double d = x - mean;
    mean += d / (double)n;
    m2   += d * (x - mean);
  }
  float var() const { return (n < 2) ? 0.0f : (float)(m2 / (double)(n - 1)); }
};

// ---- Result / State enums ----
enum DiagResult : uint8_t { DR_NONE=0, DR_PASS, DR_WARN, DR_FAIL };
enum DiagState  : uint8_t {
  DS_PHASE_A, DS_PHASE_A_CONFIRM, DS_PHASE_B,
  DS_PHASE_C_PROMPT, DS_PHASE_CD, DS_PHASE_CD_RESULT, DS_PHASE_E, DS_DONE
};

static DiagState diagState;
static const char* diagStateName() {
  switch (diagState) {
    case DS_PHASE_A:         return "A";
    case DS_PHASE_A_CONFIRM: return "A_CONFIRM";
    case DS_PHASE_B:         return "B";
    case DS_PHASE_C_PROMPT:  return "C_PROMPT";
    case DS_PHASE_CD:        return "CD";
    case DS_PHASE_CD_RESULT: return "CD_RESULT";
    case DS_PHASE_E:         return "E";
    case DS_DONE:            return "DONE";
    default:                 return "?";
  }
}
static const char* drStr(DiagResult r) {
  switch (r) {
    case DR_PASS: return "PASS";
    case DR_WARN: return "WARN";
    case DR_FAIL: return "FAIL";
    default:      return "NONE";
  }
}

// ---- Shared state ----
static DiagResult    resultA, resultB, resultC, resultD, resultE;
static DiagResult    diagOverallResult;
static bool          diagBlinkOn;
static unsigned long diagTimer;
static unsigned long diagDoneStart;

// ---- Phase A ----
static uint8_t phA_wing, phA_level;
static const uint8_t PH_A_DUTIES[3] = {64, 140, 235};

// ---- Phase B ----
static uint8_t phB_wing, phB_failed;

// ---- Phase CD: MIDI ----
static uint32_t phCD_midiTicks;
static uint32_t phCD_totalBytes;
static float    phCD_bpm;
static uint32_t phCD_lastTickUs;
static uint8_t  phCD_ticksInBar;
static uint8_t  phCD_barCount;

// ---- Phase CD: I2S (global accumulators) ----
static double      phCD_sumSq;
static double      phCD_trSum;
static uint32_t    phCD_frames;
static float       phCD_envLP;
static float       phCD_hp_y;
static float       phCD_hp_xPrev;
static DiagWelford phCD_kickW;

// ---- Phase CD: I2S (per-bar accumulators) ----
static double      phCD_barSumSq;
static double      phCD_barTrSum;
static uint32_t    phCD_barFrames;
static DiagWelford phCD_barKickW;

// ---- Phase CD: result blink ----
static Color         phCD_resultColor;
static uint8_t       phCD_beatsShown;
static unsigned long phCD_nextBeatMs;
static unsigned long phCD_beatOffMs;
static bool          phCD_beatLedOn;

// ---- Phase E ----
static bool phE_dfpOk;

// =============================================================================
// PHASE A — LED PWM
// =============================================================================
static void phA_enter() {
  phA_wing = 0; phA_level = 0;
  hw_led_all_off();
  diagTimer = millis();
  Serial.println("[DIAG] Phase A: LED PWM - cycling all wings at 25/55/92%.");
  hw_led_duty(BLUE, PH_A_DUTIES[0]);
  Serial.printf("  BLUE @ %u%%\n", (unsigned)(PH_A_DUTIES[0] * 100 / 255));
}
static bool phA_tick() {
  if (millis() - diagTimer > 200UL) {
    if (hw_btn_any_edge(nullptr)) {
      Serial.println("  Skipped.");
      hw_led_all_off(); return true;
    }
  }
  if (millis() - diagTimer < PHASE_A_LEVEL_MS) return false;
  hw_led_duty((Color)phA_wing, 0);
  phA_level++;
  if (phA_level >= 3) {
    phA_level = 0; phA_wing++;
    if (phA_wing >= 4) { hw_led_all_off(); return true; }
  }
  hw_led_duty((Color)phA_wing, PH_A_DUTIES[phA_level]);
  Serial.printf("  %s @ %u%%\n", hw_led_name((Color)phA_wing),
                (unsigned)(PH_A_DUTIES[phA_level] * 100 / 255));
  diagTimer = millis();
  return false;
}

// =============================================================================
// PHASE B — Buttons
// =============================================================================
static void phB_enter() {
  phB_wing = 0; phB_failed = 0;
  hw_led_all_off();
  // Wait for all buttons to be released before starting (clears any held
  // navigation press from the previous phase transition).
  unsigned long releaseStart = millis();
  bool anyHeld;
  do {
    anyHeld = false;
    for (int i = 0; i < 4; i++) anyHeld |= hw_btn_raw((Color)i);
  } while (anyHeld && millis() - releaseStart < 500UL);
  delay(50);
  hw_btn_reset_edges();
  hw_btn_set_fast(true); // Quick taps are sufficient for button test
  diagTimer = millis();
  Color first = phB_ORDER[0];
  Serial.println("[DIAG] Phase B: Buttons - press each lit button.");
  Serial.printf("  Press %s...\n", hw_led_name(first));
  hw_led_duty(first, 200);
}
static bool phB_tick() {
  Color wing = phB_ORDER[phB_wing];
  bool confirmed = hw_btn_edge(wing); // quick tap is enough; no MIN_VIS delay needed
  bool timeout   = (millis() - diagTimer >= PHASE_B_BTN_TOUT);
  if (!confirmed && !timeout) return false;
  hw_led_duty(wing, 0);
  if (confirmed) {
    Serial.printf("  %s: PASS\n", hw_led_name(wing));
    hw_led_duty(wing, 235); delay(150); hw_led_duty(wing, 0);
  } else {
    Serial.printf("  %s: FAIL (timeout)\n", hw_led_name(wing));
    phB_failed++;
  }
  phB_wing++;
  if (phB_wing >= 4) return true;
  delay(300);
  diagTimer = millis();
  Color next = phB_ORDER[phB_wing];
  Serial.printf("  Press %s...\n", hw_led_name(next));
  hw_led_duty(next, 200);
  return false;
}

// =============================================================================
// PHASE C_PROMPT — wait for operator to start music
// =============================================================================
static void phCPrompt_enter() {
  hw_led_all_off();
  diagTimer = millis();
  Serial.printf("[DIAG] Start music on mixer. Press BLUE when ready (auto in %lus).\n",
                (unsigned long)(PHASE_C_PROMPT_MS / 1000));
  hw_led_duty(BLUE, 80);
}
static bool phCPrompt_tick() {
  uint8_t duty = (uint8_t)(40.0f + 40.0f * sinf((float)(millis() % 1500) * 6.2832f / 1500.0f));
  hw_led_duty(BLUE, duty);
  // Edge detection: BLUE held from Phase B cannot confirm instantly (bug fix)
  if (hw_btn_edge(BLUE)) {
    hw_led_all_off();
    Serial.println("  BLUE pressed - starting music phases.");
    return true;
  }
  if (millis() - diagTimer >= PHASE_C_PROMPT_MS) {
    hw_led_all_off();
    Serial.println("  Timeout - proceeding without music confirmation.");
    return true;
  }
  return false;
}

// =============================================================================
// PHASE CD — concurrent MIDI + I2S (8 s)
// =============================================================================
static void phCD_processBar() {
  phCD_barCount++;
  float barRms  = (phCD_barFrames > 0)
                  ? sqrtf((float)(phCD_barSumSq / (double)phCD_barFrames)) : 0.0f;
  float barTr   = (phCD_barFrames > 0)
                  ? (float)(phCD_barTrSum / (double)phCD_barFrames) : 0.0f;
  float barKVar = phCD_barKickW.var();
  Serial.printf("  Bar %u | BPM=%.1f | RMS=%.4f | TR=%.5f | kVar=%.6f\n",
                (unsigned)phCD_barCount, phCD_bpm, barRms, barTr, barKVar);
  phCD_barSumSq = 0.0; phCD_barTrSum = 0.0; phCD_barFrames = 0;
  phCD_barKickW.reset();
}

static void phCD_stopDrivers() {
#ifndef USE_WOKWI
  DiagMidi.end();
  i2s_driver_uninstall(DIAG_I2S_PORT);
#endif
}

static void phCD_enter() {
  // MIDI
  phCD_midiTicks = 0; phCD_totalBytes = 0; phCD_bpm = 0.0f;
  phCD_lastTickUs = 0; phCD_ticksInBar = 0; phCD_barCount = 0;
  // I2S global
  phCD_sumSq = 0.0; phCD_trSum = 0.0; phCD_frames = 0;
  phCD_envLP = 0.0f; phCD_hp_y = 0.0f; phCD_hp_xPrev = 0.0f;
  phCD_kickW.reset();
  // I2S per-bar
  phCD_barSumSq = 0.0; phCD_barTrSum = 0.0; phCD_barFrames = 0;
  phCD_barKickW.reset();

  hw_led_all_off();
  diagTimer = millis();
  Serial.printf("[DIAG] Phase C+D: MIDI+I2S — listening %lus. BLUE to skip early.\n",
                (unsigned long)(PHASE_CD_LISTEN_MS / 1000));

#ifndef USE_WOKWI
  DiagMidi.begin(MIDI_BAUD_RATE, SERIAL_8N1, MIDI_PIN_RX, -1);

  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate          = I2S_SAMPLE_RATE;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count        = 4;
  cfg.dma_buf_len          = 256;
  cfg.use_apll             = false;
  i2s_driver_install(DIAG_I2S_PORT, &cfg, 0, nullptr);
  i2s_pin_config_t pins = {};
  pins.bck_io_num   = I2S_PIN_BCLK;
  pins.ws_io_num    = I2S_PIN_LRCK;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num  = I2S_PIN_DATA;
  i2s_set_pin(DIAG_I2S_PORT, &pins);
#else
  Serial.println("  [SIM] MIDI+I2S skipped.");
#endif
}

// Returns true when listen phase is done.
// While MIDI is present, flashes the result-color LED on every beat (live feedback).
// Color re-evaluated each beat from running I2S data:
//   GREEN=music signal OK, YELLOW=I2S connected/no music, RED=no I2S frames.
static bool phCD_tick() {
  bool bluePressed = hw_btn_edge(BLUE);
  bool timeout     = (millis() - diagTimer >= PHASE_CD_LISTEN_MS);

  if (bluePressed || timeout) {
    phCD_stopDrivers();
    hw_led_all_off();
    if (phCD_midiTicks == 0) {
      Serial.printf(
        "  Skipped/timeout — no MIDI input on GPIO%d. Check mixer clock output and cable.\n",
        MIDI_PIN_RX);
      resultC = DR_FAIL;
      resultD = DR_NONE;
    }
    return true;
  }

#ifndef USE_WOKWI
  // --- MIDI reads ---
  bool newBeat = false;
  while (DiagMidi.available()) {
    uint8_t b = DiagMidi.read();
    phCD_totalBytes++;
    if (b == MIDI_CLOCK_BYTE) {
      uint32_t us = micros();
      if (phCD_lastTickUs > 0) {
        uint32_t dt = us - phCD_lastTickUs;
        if (dt > 0) phCD_bpm = 60000000.0f / ((float)dt * 24.0f);
      }
      phCD_lastTickUs = us;
      phCD_midiTicks++;
      phCD_ticksInBar++;
      if (phCD_midiTicks % 24 == 0) newBeat = true;          // every beat
      if (phCD_ticksInBar >= MIDI_TICKS_PER_BAR) {
        phCD_processBar();
        phCD_ticksInBar = 0;
      }
    }
  }

  // --- I2S reads (non-blocking) ---
  int32_t buf[CD_READ_FRAMES];
  size_t  bytes = 0;
  i2s_read(DIAG_I2S_PORT, buf, sizeof(buf), &bytes, 0);
  uint32_t frames = bytes / 4;
  for (uint32_t i = 0; i < frames; i++) {
    float x  = (float)buf[i] / 2147483647.0f;
    float hp = HP_COEFF * (phCD_hp_y + x - phCD_hp_xPrev);
    phCD_hp_y     = hp;
    phCD_hp_xPrev = x;
    float ax = fabsf(x);
    phCD_envLP += LP_ENV_ALPHA * (ax - phCD_envLP);
    phCD_sumSq += (double)(x * x);
    phCD_trSum += (double)fabsf(hp);
    phCD_frames++;
    phCD_kickW.update((double)phCD_envLP);
    phCD_barSumSq += (double)(x * x);
    phCD_barTrSum += (double)fabsf(hp);
    phCD_barFrames++;
    phCD_barKickW.update((double)phCD_envLP);
  }

  // --- Live beat flash ---
  if (newBeat) {
    // Re-evaluate I2S color from running data
    if (phCD_frames < 100) {
      phCD_resultColor = RED;
    } else {
      float runRms = sqrtf((float)(phCD_sumSq / (double)phCD_frames));
      phCD_resultColor = (runRms >= D_MUSIC_RMS) ? GREEN : YELLOW;
    }
    float beatMs = (phCD_bpm > 0.0f) ? (60000.0f / phCD_bpm) : 500.0f;
    uint32_t onDur = max(80UL, (unsigned long)(beatMs * 0.25f));
    hw_led_duty(phCD_resultColor, 235);
    phCD_beatLedOn = true;
    phCD_beatOffMs = millis() + onDur;
  }
  if (phCD_beatLedOn && millis() >= phCD_beatOffMs) {
    hw_led_all_off();
    phCD_beatLedOn = false;
  }
#endif
  return false;
}

// Evaluate I2S result and set phCD_resultColor. Called only when MIDI present.
static void phCD_evaluateAndLog() {
  float rms  = (phCD_frames > 0)
               ? sqrtf((float)(phCD_sumSq / (double)phCD_frames)) : 0.0f;
  float tr   = (phCD_frames > 0)
               ? (float)(phCD_trSum / (double)phCD_frames) : 0.0f;
  float kvar = phCD_kickW.var();

  Serial.printf("  MIDI: PASS (%lu ticks, BPM=%.1f)\n",
                (unsigned long)phCD_midiTicks, phCD_bpm);
  resultC = DR_PASS;

  if (phCD_frames < D_MIN_FRAMES) {
    resultD = DR_FAIL;
    phCD_resultColor = RED;
    Serial.printf("  I2S: FAIL  frames=%lu — no clock, converter absent?\n",
                  (unsigned long)phCD_frames);
  } else if (rms < D_MUSIC_RMS) {
    resultD = DR_WARN;
    phCD_resultColor = YELLOW;
    Serial.printf("  I2S: WARN  frames=%lu RMS=%.4f TR=%.5f kVar=%.6f — connected, no music\n",
                  (unsigned long)phCD_frames, rms, tr, kvar);
  } else {
    resultD = DR_PASS;
    phCD_resultColor = GREEN;
    Serial.printf("  I2S: PASS  frames=%lu RMS=%.4f TR=%.5f kVar=%.6f\n",
                  (unsigned long)phCD_frames, rms, tr, kvar);
  }
}

// MIDI present: blink at result color, press BLUE to continue.
// No MIDI:      slow RED blink, press BLUE or wait 30s to continue.
static void phCD_result_enter() {
  hw_led_all_off();
  diagTimer = millis();
  if (resultC == DR_PASS) {
    Serial.printf("  C+D done — %s result color. Press BLUE to continue to Phase E.\n",
                  phCD_resultColor == GREEN ? "GREEN" : phCD_resultColor == YELLOW ? "YELLOW" : "RED");
  } else {
    Serial.printf("  No MIDI — slow RED blink. Press BLUE or wait %lus to continue to Phase E.\n",
                  (unsigned long)(PHASE_CD_NOMIDI_TOUT_MS / 1000));
  }
}

static bool phCD_result_tick() {
  if (resultC == DR_PASS) {
    hw_led_duty(phCD_resultColor, ((millis() / 400) & 1) ? 235 : 0);
    if (hw_btn_edge(BLUE)) { hw_led_all_off(); return true; }
    return false;
  }
  // No MIDI: slow red blink with auto-timeout
  hw_led_duty(RED, ((millis() / 500) & 1) ? 200 : 0);
  if (hw_btn_edge(BLUE) || millis() - diagTimer >= PHASE_CD_NOMIDI_TOUT_MS) {
    hw_led_all_off();
    return true;
  }
  return false;
}

// =============================================================================
// PHASE E — DFPlayer audio confirmation
// =============================================================================
static void phE_enter() {
  phE_dfpOk = false;
  hw_led_all_off();
  diagTimer = millis();
  Serial.printf("[DIAG] Phase E: DFPlayer - track %u. Press BLUE within %lus to confirm.\n",
                (unsigned)PHASE_E_TRACK, (unsigned long)(PHASE_E_TIMEOUT_MS / 1000));
#ifndef USE_WOKWI
  DiagDfpSer.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (!diagDfp.begin(DiagDfpSer, true, true)) {
    Serial.println("  DFPlayer init FAILED. Possible causes: chip not powered, wiring fault, SD card missing or corrupt.");
    resultE = DR_FAIL;
  } else {
    phE_dfpOk = true;
    diagDfp.volume(PHASE_E_VOL);
    diagDfp.playMp3Folder(PHASE_E_TRACK);
    Serial.println("  Playing. Press BLUE to confirm audio heard.");
  }
#else
  Serial.println("  [SIM] DFPlayer skipped - PASS.");
  resultE = DR_PASS;
#endif
}
static bool phE_tick() {
  if (resultE != DR_NONE) return true;
  hw_led_duty(BLUE, ((millis() / 400) & 1) ? 180 : 0);
  Color pressed;
  if (hw_btn_any_edge(&pressed)) {
    hw_led_all_off();
    if (pressed == BLUE) {
      resultE = DR_PASS;  // confirmed heard
    } else {
      Serial.println("  Skipped.");
      // resultE stays DR_NONE — not penalised in summary
    }
    return true;
  }
  if (millis() - diagTimer >= PHASE_E_TIMEOUT_MS) {
    hw_led_all_off();
    Serial.println("  No BLUE press within timeout — audio not confirmed. Check: speaker connected, SD card inserted, DFPlayer wired correctly.");
    resultE = DR_WARN;
    return true;
  }
  return false;
}

// =============================================================================
// SUMMARY
// =============================================================================
static void printSummary() {
  bool anyFail = (resultA==DR_FAIL||resultB==DR_FAIL||resultC==DR_FAIL||
                  resultD==DR_FAIL||resultE==DR_FAIL);
  bool anyWarn = (resultA==DR_WARN||resultB==DR_WARN||resultC==DR_WARN||
                  resultD==DR_WARN||resultE==DR_WARN);
  Serial.println("\n========== DIAGNOSTIC SUMMARY ==========");
  Serial.printf("  A (LED PWM):    %s\n", drStr(resultA));
  Serial.printf("  B (Buttons):    %s\n", drStr(resultB));
  Serial.printf("  C (MIDI Clock): %s\n", drStr(resultC));
  Serial.printf("  D (I2S Audio):  %s\n", drStr(resultD));
  Serial.printf("  E (DFPlayer):   %s\n", resultE == DR_NONE ? "SKIP" : drStr(resultE));
  Serial.println("-----------------------------------------");
  diagOverallResult = anyFail ? DR_FAIL : (anyWarn ? DR_WARN : DR_PASS);
  Serial.println(anyFail ? "  OVERALL: FAIL" : (anyWarn ? "  OVERALL: WARN" : "  OVERALL: PASS"));
  Serial.println("=========================================");
  Serial.printf("  Press any button to return, or wait %lus for auto-return.\n",
                (unsigned long)(DIAG_DONE_AUTO_MS / 1000));
  if (anyFail) {
    for (int i = 0; i < 6; i++) { hw_led_duty(RED, 235); delay(120); hw_led_duty(RED, 0); delay(120); }
  } else if (anyWarn) {
    for (int i = 0; i < 4; i++) { hw_led_duty(YELLOW, 200); delay(350); hw_led_duty(YELLOW, 0); delay(350); }
  } else {
    uint8_t p[4] = {200, 200, 200, 200};
    hw_led_all_set(p);
    delay(2000); hw_led_all_off();
  }
  diagTimer = millis();
  diagBlinkOn = false;
}

// =============================================================================
// MODE INTERFACE
// =============================================================================
void diag_init() {
  Serial.println("\n=== DIAGNOSTIC MODE ===");
  Serial.println("  Phases: A=LED  B=Buttons  C+D=MIDI+I2S  E=DFPlayer");
  Serial.println("  BLUE skips/confirms; YELLOW hold 5s exits to Mode Selection.");
  for (int b = 0; b < 2; b++) { hw_led_duty(BLUE, 200); delay(200); hw_led_duty(BLUE, 0); delay(150); }

  resultA = resultB = resultC = resultD = resultE = DR_NONE;
  diagState = DS_PHASE_A;
  phA_enter();
}

void diag_tick() {
  switch (diagState) {

    case DS_PHASE_A:
      if (phA_tick()) {
        hw_led_all_off();
        Serial.printf("  All wings cycled. Press BLUE to confirm (auto in %lus).\n",
                      (unsigned long)(PHASE_A_TIMEOUT_MS / 1000));
        diagTimer = millis();
        diagState = DS_PHASE_A_CONFIRM;
      }
      break;

    case DS_PHASE_A_CONFIRM:
      hw_led_duty(BLUE, ((millis() / 400) & 1) ? 180 : 0);
      if (hw_btn_edge(BLUE) || millis() - diagTimer >= PHASE_A_TIMEOUT_MS) {
        hw_led_all_off();
        resultA = DR_PASS;
        Serial.printf("  Phase A: %s\n", drStr(resultA));
        diagState = DS_PHASE_B;
        phB_enter();
      }
      break;

    case DS_PHASE_B:
      if (phB_tick()) {
        hw_btn_set_fast(false); // Restore standard debounce after button test
        resultB = (phB_failed == 0) ? DR_PASS : (phB_failed < 4 ? DR_WARN : DR_FAIL);
        Serial.printf("  Phase B: %s  (%u/4 ok)\n", drStr(resultB), 4 - phB_failed);
        diagState = DS_PHASE_C_PROMPT;
        phCPrompt_enter();
      }
      break;

    case DS_PHASE_C_PROMPT:
      if (phCPrompt_tick()) {
        diagState = DS_PHASE_CD;
        phCD_enter();
      }
      break;

    case DS_PHASE_CD:
      if (phCD_tick()) {
        if (phCD_midiTicks > 0) phCD_evaluateAndLog();
        diagState = DS_PHASE_CD_RESULT;
        phCD_result_enter();
      }
      break;

    case DS_PHASE_CD_RESULT:
      if (phCD_result_tick()) {
        diagState = DS_PHASE_E;
        phE_enter();
      }
      break;

    case DS_PHASE_E:
      if (phE_tick()) {
        Serial.printf("  Phase E: %s\n", drStr(resultE));
#ifndef USE_WOKWI
        if (phE_dfpOk) { diagDfp.stop(); phE_dfpOk = false; }
        DiagDfpSer.end();
#endif
        printSummary();
        diagDoneStart = millis();
        diagState = DS_DONE;
      }
      break;

    case DS_DONE: {
      // Edge detection prevents PWM ghost clicks from restarting
      if (hw_btn_any_edge(nullptr)) {
        hw_led_all_off();
        Serial.println("[DIAG] Returning to Mode Selection...");
        delay(200); ESP.restart();
      }
      if (millis() - diagDoneStart >= DIAG_DONE_AUTO_MS) {
        hw_led_all_off();
        Serial.println("[DIAG] Auto-returning to Mode Selection...");
        delay(200); ESP.restart();
      }
      if (millis() - diagTimer >= 750UL) {
        diagTimer = millis();
        diagBlinkOn = !diagBlinkOn;
        Color wing = (diagOverallResult == DR_FAIL) ? RED :
                     (diagOverallResult == DR_WARN) ? YELLOW : GREEN;
        hw_led_duty(wing, diagBlinkOn ? 200 : 0);
      }
      break;
    }
  }
}

void diag_stop() {
  hw_led_all_off();
#ifndef USE_WOKWI
  DiagMidi.end();
  if (phE_dfpOk) diagDfp.stop();
  DiagDfpSer.end();
#endif
  i2s_driver_uninstall(DIAG_I2S_PORT);
  diagState = DS_PHASE_A;
  Serial.println("[DIAG] Mode stopped.");
}
