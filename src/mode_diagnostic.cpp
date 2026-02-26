/*
  Diagnostic Mode - 5-phase hardware verification
  A: LED PWM      - cycle each wing at 3 brightness levels, operator confirms
  B: Buttons      - light each wing, wait for corresponding button press
  C: MIDI Clock   - prompt to start music, then listen 10 s; PASS/WARN/FAIL
  D: I2S Audio    - 3 s RMS; PASS=music signal, WARN=connected/no music, FAIL=no I2S clock
  E: DFPlayer     - play track 001, press BLUE within 8 s to confirm heard
*/

#include <Arduino.h>
#include <math.h>
#include "driver/i2s.h"
#include "shimon.h"
#include "mode_diagnostic.h"

#ifndef USE_WOKWI
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#endif

static constexpr uint32_t DIAG_PWM_FREQ   = 12500;
static constexpr uint8_t  DIAG_PWM_BITS   = 8;
static constexpr uint8_t  LEDC_CH[4]      = {0, 1, 2, 3};
static const     uint8_t  DIAG_LED[4]     = {LED_BLUE, LED_RED, LED_GREEN, LED_YELLOW};
static const     uint8_t  DIAG_BTN[4]     = {BTN_BLUE, BTN_RED, BTN_GREEN, BTN_YELLOW};
static const char* const  WING_NAME[4]    = {"BLUE", "RED", "GREEN", "YELLOW"};
static constexpr uint32_t PHASE_A_LEVEL_MS   = 800;
static constexpr uint32_t PHASE_A_TIMEOUT_MS = 15000;
static constexpr uint32_t PHASE_B_BTN_TOUT   = 5000;
static constexpr uint32_t PHASE_B_MIN_VIS_MS = 300;  // min LED-on time before accepting press
static constexpr uint32_t PHASE_C_PROMPT_MS  = 30000;
static constexpr uint32_t PHASE_C_LISTEN_MS  = 10000;
static constexpr uint32_t DIAG_DONE_AUTO_MS  = 20000;
static constexpr uint32_t PHASE_D_MEASURE_MS = 3000;
static constexpr uint32_t PHASE_E_TIMEOUT_MS = 8000;
static constexpr uint32_t MIDI_BAUD       = 31250;
static constexpr uint8_t  MIDI_CLOCK_BYTE = 0xF8;
static constexpr int      MIDI_RX_PIN     = 34;
static constexpr i2s_port_t DIAG_I2S_PORT  = I2S_NUM_0;
static constexpr int        D_I2S_BCLK     = 26;
static constexpr int        D_I2S_LRCK     = 25;
static constexpr int        D_I2S_DATA     = 22;
static constexpr int        D_SAMPLE_RATE  = 48000;
static constexpr int        D_READ_FRAMES  = 256;
static constexpr uint32_t   D_MIN_FRAMES   = 10000; // below this = no I2S clock (converter absent)
static constexpr float      D_MUSIC_RMS    = 0.050f; // above this = music signal present (PASS)
static constexpr uint8_t PHASE_E_TRACK  = 1;
static constexpr uint8_t PHASE_E_VOL    = 20;
#ifndef USE_WOKWI
static HardwareSerial      DiagMidi(1);
static HardwareSerial      DiagDfpSer(1);
static DFRobotDFPlayerMini diagDfp;
#endif
enum DiagResult : uint8_t { DR_NONE=0, DR_PASS, DR_WARN, DR_FAIL };
static const char* drStr(DiagResult r) {
  switch (r) {
    case DR_PASS: return "PASS";
    case DR_WARN: return "WARN";
    case DR_FAIL: return "FAIL";
    default:      return "NONE";
  }
}
enum DiagState : uint8_t {
  DS_PHASE_A, DS_PHASE_A_CONFIRM, DS_PHASE_B,
  DS_PHASE_C_PROMPT, DS_PHASE_C, DS_PHASE_E, DS_DONE
};
static DiagState  diagState;
static DiagResult resultA, resultB, resultC, resultD, resultE;
static DiagResult    diagOverallResult;
static bool          diagBlinkOn;
static unsigned long diagTimer;
static unsigned long diagDoneStart;
static uint8_t phA_wing, phA_level;
static const uint8_t PH_A_DUTIES[3] = {64, 140, 235};
static uint8_t phB_wing, phB_failed;
static uint32_t phC_ticks;
static uint32_t phC_totalBytes;
static float    phC_bpm;
static uint32_t phC_lastUs;
static bool phE_dfpOk;
static void diagPwmInit() {
  for (int i = 0; i < 4; i++) {
    ledcSetup(LEDC_CH[i], DIAG_PWM_FREQ, DIAG_PWM_BITS);
    ledcAttachPin(DIAG_LED[i], LEDC_CH[i]);
    ledcWrite(LEDC_CH[i], 0);
  }
}
static void diagAllOff() { for (int i = 0; i < 4; i++) ledcWrite(LEDC_CH[i], 0); }
static void diagWing(uint8_t w, uint8_t d) { if (w < 4) ledcWrite(LEDC_CH[w], d); }

static void phA_enter() {
  phA_wing = 0; phA_level = 0;
  diagAllOff();
  diagTimer = millis();
  Serial.println("[DIAG] Phase A: LED PWM - cycling all wings at 25/55/92%.");
  diagWing(0, PH_A_DUTIES[0]);
  Serial.printf("  BLUE @ %u%%\n", (unsigned)(PH_A_DUTIES[0] * 100 / 255));
}
static bool phA_tick() {
  if (millis() - diagTimer > 200UL) {  // debounce: ignore presses in first 200ms
    for (int i = 0; i < 4; i++) {
      if (digitalRead(DIAG_BTN[i]) == LOW) {
        Serial.println("  Skipped.");
        diagWing(phA_wing, 0); diagAllOff(); return true;
      }
    }
  }
  if (millis() - diagTimer < PHASE_A_LEVEL_MS) return false;
  diagWing(phA_wing, 0);
  phA_level++;
  if (phA_level >= 3) {
    phA_level = 0; phA_wing++;
    if (phA_wing >= 4) { diagAllOff(); return true; }
  }
  diagWing(phA_wing, PH_A_DUTIES[phA_level]);
  Serial.printf("  %s @ %u%%\n", WING_NAME[phA_wing],
                (unsigned)(PH_A_DUTIES[phA_level] * 100 / 255));
  diagTimer = millis();
  return false;
}
static void phB_enter() {
  phB_wing = 0; phB_failed = 0;
  diagAllOff();
  // Wait for any button held from Phase A confirm to release (up to 500 ms)
  unsigned long releaseStart = millis();
  bool anyHeld;
  do {
    anyHeld = false;
    for (int i = 0; i < 4; i++) anyHeld |= (digitalRead(DIAG_BTN[i]) == LOW);
  } while (anyHeld && millis() - releaseStart < 500UL);
  delay(50);
  diagTimer = millis();
  Serial.println("[DIAG] Phase B: Buttons - press each lit button.");
  Serial.printf("  Press %s...\n", WING_NAME[0]);
  diagWing(0, 200);
}
static bool phB_tick() {
  if (millis() - diagTimer < PHASE_B_MIN_VIS_MS) return false;  // wait for LED to be visible
  bool pressed = (digitalRead(DIAG_BTN[phB_wing]) == LOW);
  bool timeout = (millis() - diagTimer >= PHASE_B_BTN_TOUT);
  if (!pressed && !timeout) return false;
  diagWing(phB_wing, 0);
  if (pressed) {
    Serial.printf("  %s: PASS\n", WING_NAME[phB_wing]);
    diagWing(phB_wing, 235); delay(150); diagWing(phB_wing, 0);
  } else {
    Serial.printf("  %s: FAIL (timeout)\n", WING_NAME[phB_wing]);
    phB_failed++;
  }
  phB_wing++;
  if (phB_wing >= 4) return true;
  delay(300);
  diagTimer = millis();
  Serial.printf("  Press %s...\n", WING_NAME[phB_wing]);
  diagWing(phB_wing, 200);
  return false;
}
static void phCPrompt_enter() {
  diagAllOff();
  diagTimer = millis();
  Serial.printf("[DIAG] Start music on mixer. Press BLUE when ready (auto in %lus).\n",
                (unsigned long)(PHASE_C_PROMPT_MS / 1000));
  diagWing(0, 80);  // dim blue: waiting for operator
}
static bool phCPrompt_tick() {
  // Pulse blue gently while waiting
  uint8_t duty = (uint8_t)(40.0f + 40.0f * sinf((float)(millis() % 1500) * 6.2832f / 1500.0f));
  diagWing(0, duty);
  if (digitalRead(DIAG_BTN[0]) == LOW) {
    diagAllOff();
    Serial.println("  BLUE pressed - starting music phases.");
    return true;
  }
  if (millis() - diagTimer >= PHASE_C_PROMPT_MS) {
    diagAllOff();
    Serial.println("  Timeout - proceeding without music confirmation.");
    return true;
  }
  return false;
}
static void phC_enter() {
  phC_ticks = 0; phC_totalBytes = 0; phC_bpm = 0.0f; phC_lastUs = 0;
  diagAllOff();
  diagTimer = millis();
  Serial.printf("[DIAG] Phase C: MIDI Clock - listening %lu s on GPIO%d.\n",
                (unsigned long)(PHASE_C_LISTEN_MS / 1000), MIDI_RX_PIN);
#ifndef USE_WOKWI
  DiagMidi.begin(MIDI_BAUD, SERIAL_8N1, MIDI_RX_PIN, -1);
#else
  Serial.println("  [SIM] MIDI skipped.");
#endif
}
static bool phC_tick() {
  for (int i = 0; i < 4; i++) {
    if (digitalRead(DIAG_BTN[i]) == LOW) {
#ifndef USE_WOKWI
      DiagMidi.end();
#endif
      Serial.println("  Skipped.");
      return true;
    }
  }
  if (millis() - diagTimer >= PHASE_C_LISTEN_MS) {
#ifndef USE_WOKWI
    DiagMidi.end();
#endif
    return true;
  }
#ifndef USE_WOKWI
  while (DiagMidi.available()) {
    uint8_t b = DiagMidi.read();
    phC_totalBytes++;
    if (b == MIDI_CLOCK_BYTE) {
      uint32_t us = micros();
      if (phC_lastUs > 0) {
        uint32_t dt = us - phC_lastUs;
        if (dt > 0) phC_bpm = 60000000.0f / ((float)dt * 24.0f);
      }
      phC_lastUs = us; phC_ticks++;
    }
  }
#endif
  return false;
}
static void phD_run() {
  diagAllOff();
  Serial.printf("[DIAG] Phase D: I2S Audio - measuring RMS for %lu s.\n",
                (unsigned long)(PHASE_D_MEASURE_MS / 1000));
#ifdef USE_WOKWI
  resultD = DR_WARN;
  Serial.println("  [SIM] I2S skipped - WARN.");
  return;
#endif
  i2s_config_t cfg  = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate          = D_SAMPLE_RATE;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count        = 4;
  cfg.dma_buf_len          = D_READ_FRAMES;
  cfg.use_apll             = false;
  i2s_driver_install(DIAG_I2S_PORT, &cfg, 0, nullptr);
  i2s_pin_config_t pins = {};
  pins.bck_io_num    = D_I2S_BCLK;
  pins.ws_io_num     = D_I2S_LRCK;
  pins.data_out_num  = I2S_PIN_NO_CHANGE;
  pins.data_in_num   = D_I2S_DATA;
  i2s_set_pin(DIAG_I2S_PORT, &pins);
  double sumSq = 0.0; uint32_t n = 0;
  int32_t buf[D_READ_FRAMES];
  uint32_t start = millis();
  while (millis() - start < PHASE_D_MEASURE_MS) {
    size_t bytes = 0;
    i2s_read(DIAG_I2S_PORT, buf, sizeof(buf), &bytes, pdMS_TO_TICKS(200));
    uint32_t frames = bytes / 4;
    for (uint32_t i = 0; i < frames; i++) {
      float s = (float)buf[i] / 2147483647.0f;
      sumSq += (double)(s * s); n++;
    }
  }
  i2s_driver_uninstall(DIAG_I2S_PORT);
  float rms = (n > 0) ? sqrtf((float)(sumSq / (double)n)) : 0.0f;
  if (n < D_MIN_FRAMES) {
    resultD = DR_FAIL;
    Serial.printf("  frames=%lu  RMS=%.4f  Result: FAIL (no I2S clock - converter absent?)\n",
                  (unsigned long)n, rms);
  } else if (rms < D_MUSIC_RMS) {
    resultD = DR_WARN;
    Serial.printf("  frames=%lu  RMS=%.4f  Result: WARN (connected, no music signal)\n",
                  (unsigned long)n, rms);
  } else {
    resultD = DR_PASS;
    Serial.printf("  frames=%lu  RMS=%.4f  Result: PASS\n", (unsigned long)n, rms);
  }
}
static void phE_enter() {
  phE_dfpOk = false;
  diagAllOff();
  diagTimer = millis();
  Serial.printf("[DIAG] Phase E: DFPlayer - track %u. Press BLUE within %lu s to confirm.\n",
                (unsigned)PHASE_E_TRACK, (unsigned long)(PHASE_E_TIMEOUT_MS / 1000));
#ifndef USE_WOKWI
  DiagDfpSer.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (!diagDfp.begin(DiagDfpSer)) {
    Serial.println("  DFPlayer init FAILED.");
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
  if (digitalRead(DIAG_BTN[0]) == LOW) { resultE = DR_PASS; return true; }
  if (millis() - diagTimer >= PHASE_E_TIMEOUT_MS) { resultE = DR_WARN; return true; }
  return false;
}
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
  Serial.printf("  E (DFPlayer):   %s\n", drStr(resultE));
  Serial.println("-----------------------------------------");
  diagOverallResult = anyFail ? DR_FAIL : (anyWarn ? DR_WARN : DR_PASS);
  Serial.println(anyFail ? "  OVERALL: FAIL" : (anyWarn ? "  OVERALL: WARN" : "  OVERALL: PASS"));
  Serial.println("=========================================");
  Serial.printf("  Press any button to return, or wait %lus for auto-return.\n",
                (unsigned long)(DIAG_DONE_AUTO_MS / 1000));
  if (anyFail) {
    for (int i = 0; i < 6; i++) { diagWing(1,235); delay(120); diagWing(1,0); delay(120); }
  } else if (anyWarn) {
    for (int i = 0; i < 4; i++) { diagWing(3,200); delay(350); diagWing(3,0); delay(350); }
  } else {
    for (int i = 0; i < 4; i++) diagWing(i,200);
    delay(2000); diagAllOff();
  }
  diagTimer = millis();
  diagBlinkOn = false;
}
void diag_init() {
  Serial.println("\n=== DIAGNOSTIC MODE ===");
  Serial.println("  Phases: A=LED  B=Buttons  C=MIDI  D=I2S  E=DFPlayer");
  Serial.println("  Any button skips current phase (except B: buttons under test).");
  Serial.println("  YELLOW hold 5s exits to Mode Selection at any time.");
  diagPwmInit();
  resultA = resultB = resultC = resultD = resultE = DR_NONE;
  diagState = DS_PHASE_A;
  phA_enter();
}
void diag_tick() {
  switch (diagState) {
    case DS_PHASE_A:
      if (phA_tick()) {
        for (int i = 0; i < 4; i++) diagWing(i, 40);
        Serial.printf("  All wings cycled. Press any button to confirm (auto in %lus).\n",
                      (unsigned long)(PHASE_A_TIMEOUT_MS / 1000));
        diagTimer = millis();
        diagState = DS_PHASE_A_CONFIRM;
      }
      break;
    case DS_PHASE_A_CONFIRM: {
      bool any = false;
      for (int i = 0; i < 4; i++) any |= (digitalRead(DIAG_BTN[i]) == LOW);
      if (any || millis() - diagTimer >= PHASE_A_TIMEOUT_MS) {
        diagAllOff();
        resultA = DR_PASS;
        Serial.printf("  Phase A: %s\n", drStr(resultA));
        diagState = DS_PHASE_B;
        phB_enter();
      }
      break;
    }
    case DS_PHASE_B:
      if (phB_tick()) {
        resultB = (phB_failed == 0) ? DR_PASS : (phB_failed < 4 ? DR_WARN : DR_FAIL);
        Serial.printf("  Phase B: %s  (%u/4 ok)\n", drStr(resultB), 4 - phB_failed);
        diagState = DS_PHASE_C_PROMPT;
        phCPrompt_enter();
      }
      break;
    case DS_PHASE_C_PROMPT:
      if (phCPrompt_tick()) {
        diagState = DS_PHASE_C;
        phC_enter();
      }
      break;
    case DS_PHASE_C:
      if (phC_tick()) {
        if (phC_ticks > 0) {
          resultC = DR_PASS;
          Serial.printf("  Phase C: PASS  (%lu ticks, BPM=%.1f)\n",
                        (unsigned long)phC_ticks, phC_bpm);
        } else if (phC_totalBytes > 0) {
          resultC = DR_WARN;
          Serial.printf("  Phase C: WARN  (device present, %lu bytes, no clock)\n",
                        (unsigned long)phC_totalBytes);
        } else {
          resultC = DR_FAIL;
          Serial.println("  Phase C: FAIL  (no signal - cable disconnected?)");
        }
        phD_run();
        diagState = DS_PHASE_E;
        phE_enter();
      }
      break;
    case DS_PHASE_E:
      if (phE_tick()) {
        Serial.printf("  Phase E: %s\n", drStr(resultE));
#ifndef USE_WOKWI
        if (phE_dfpOk) diagDfp.stop();
        DiagDfpSer.end();
#endif
        printSummary();
        diagDoneStart = millis();
        diagState = DS_DONE;
      }
      break;
    case DS_DONE: {
      for (int i = 0; i < 4; i++) {
        if (digitalRead(DIAG_BTN[i]) == LOW) {
          diagAllOff();
          Serial.println("[DIAG] Returning to Mode Selection...");
          delay(200); ESP.restart();
        }
      }
      if (millis() - diagDoneStart >= DIAG_DONE_AUTO_MS) {
        diagAllOff();
        Serial.println("[DIAG] Auto-returning to Mode Selection...");
        delay(200); ESP.restart();
      }
      if (millis() - diagTimer >= 750UL) {
        diagTimer = millis();
        diagBlinkOn = !diagBlinkOn;
        if (diagBlinkOn) {
          uint8_t wing = (diagOverallResult == DR_FAIL) ? 1 :  // RED
                         (diagOverallResult == DR_WARN) ? 3 :  // YELLOW
                         2;                                     // GREEN
          diagWing(wing, 200);
        } else {
          diagAllOff();
        }
      }
      break;
    }
  }
}
void diag_stop() {
  diagAllOff();
#ifndef USE_WOKWI
  DiagMidi.end();
  DiagDfpSer.end();
#endif
  i2s_driver_uninstall(DIAG_I2S_PORT);
  diagState = DS_PHASE_A;
  Serial.println("[DIAG] Mode stopped.");
}
