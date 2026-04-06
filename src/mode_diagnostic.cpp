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
#include "hw.h"
#include "mode_diagnostic.h"

#ifndef USE_WOKWI
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#endif

static constexpr uint32_t PHASE_A_LEVEL_MS   = 800;
static constexpr uint32_t PHASE_A_TIMEOUT_MS = 15000;
static constexpr uint32_t PHASE_B_BTN_TOUT   = 5000;
static constexpr uint32_t PHASE_B_MIN_VIS_MS = 300;  // min LED-on time before accepting press
static constexpr uint32_t PHASE_C_PROMPT_MS  = 30000;
static constexpr uint32_t PHASE_C_LISTEN_MS  = 5000;
static constexpr uint32_t DIAG_DONE_AUTO_MS  = 20000;
static constexpr uint32_t PHASE_D_MEASURE_MS = 3000;
static constexpr uint32_t PHASE_E_TIMEOUT_MS = 8000;
// MIDI_BAUD_RATE / MIDI_PIN_RX / I2S_PIN_BCLK / I2S_PIN_LRCK / I2S_PIN_DATA / I2S_SAMPLE_RATE
// are all defined in shimon.h
static constexpr uint8_t   MIDI_CLOCK_BYTE = 0xF8;
static constexpr i2s_port_t DIAG_I2S_PORT  = I2S_NUM_0;
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
enum DiagState : uint8_t {
  DS_PHASE_A, DS_PHASE_A_CONFIRM, DS_PHASE_B,
  DS_PHASE_C_PROMPT, DS_PHASE_C, DS_PHASE_E, DS_DONE
};
static DiagState  diagState;
static const char* diagStateName() {
  switch (diagState) {
    case DS_PHASE_A:        return "A";
    case DS_PHASE_A_CONFIRM:return "A_CONFIRM";
    case DS_PHASE_B:        return "B";
    case DS_PHASE_C_PROMPT: return "C_PROMPT";
    case DS_PHASE_C:        return "C";
    case DS_PHASE_E:        return "E";
    case DS_DONE:           return "DONE";
    default:                return "?";
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

static void phA_enter() {
  phA_wing = 0; phA_level = 0;
  hw_led_all_off();
  diagTimer = millis();
  Serial.println("[DIAG] Phase A: LED PWM - cycling all wings at 25/55/92%.");
  hw_led_duty(BLUE, PH_A_DUTIES[0]);
  Serial.printf("  BLUE @ %u%%\n", (unsigned)(PH_A_DUTIES[0] * 100 / 255));
}
static bool phA_tick() {
  if (millis() - diagTimer > 200UL) {  // debounce: ignore presses in first 200ms
    if (hw_btn_any_edge(nullptr)) {
      Serial.println("  Skipped.");
      hw_led_duty((Color)phA_wing, 0); hw_led_all_off(); return true;
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
static void phB_enter() {
  phB_wing = 0; phB_failed = 0;
  hw_led_all_off();
  // Wait for any button held from Phase A confirm to release (up to 500 ms)
  unsigned long releaseStart = millis();
  bool anyHeld;
  do {
    anyHeld = false;
    for (int i = 0; i < 4; i++) anyHeld |= hw_btn_raw((Color)i);
  } while (anyHeld && millis() - releaseStart < 500UL);
  delay(50);
  diagTimer = millis();
  Serial.println("[DIAG] Phase B: Buttons - press each lit button.");
  Serial.printf("  Press %s...\n", hw_led_name(BLUE));
  hw_led_duty(BLUE, 200);
}
static bool phB_tick() {
  if (millis() - diagTimer < PHASE_B_MIN_VIS_MS) return false;  // wait for LED to be visible
  bool confirmed = hw_btn_pressed((Color)phB_wing);  // hw layer handles ghost rejection
  bool timeout   = (millis() - diagTimer >= PHASE_B_BTN_TOUT);
  if (!confirmed && !timeout) return false;
  hw_led_duty((Color)phB_wing, 0);
  if (confirmed) {
    Serial.printf("  %s: PASS\n", hw_led_name((Color)phB_wing));
    hw_led_duty((Color)phB_wing, 235); delay(150); hw_led_duty((Color)phB_wing, 0);
  } else {
    Serial.printf("  %s: FAIL (timeout)\n", hw_led_name((Color)phB_wing));
    phB_failed++;
  }
  phB_wing++;
  if (phB_wing >= 4) return true;
  delay(300);
  diagTimer = millis();
  Serial.printf("  Press %s...\n", hw_led_name((Color)phB_wing));
  hw_led_duty((Color)phB_wing, 200);
  return false;
}
static void phCPrompt_enter() {
  hw_led_all_off();
  diagTimer = millis();
  Serial.printf("[DIAG] Start music on mixer. Press BLUE when ready (auto in %lus).\n",
                (unsigned long)(PHASE_C_PROMPT_MS / 1000));
  hw_led_duty(BLUE, 80);  // dim blue: waiting for operator
}
static bool phCPrompt_tick() {
  // Pulse blue gently while waiting
  uint8_t duty = (uint8_t)(40.0f + 40.0f * sinf((float)(millis() % 1500) * 6.2832f / 1500.0f));
  hw_led_duty(BLUE, duty);
  // BUG FIX #1: use edge (not raw) so a BLUE still held from Phase B cannot confirm instantly
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
static void phC_enter() {
  phC_ticks = 0; phC_totalBytes = 0; phC_bpm = 0.0f; phC_lastUs = 0;
  hw_led_all_off();
  diagTimer = millis();
  Serial.printf("[DIAG] Phase C: MIDI Clock - listening %lu s on GPIO%d.\n",
                (unsigned long)(PHASE_C_LISTEN_MS / 1000), MIDI_PIN_RX);
#ifndef USE_WOKWI
  DiagMidi.begin(MIDI_BAUD_RATE, SERIAL_8N1, MIDI_PIN_RX, -1);
#else
  Serial.println("  [SIM] MIDI skipped.");
#endif
}
static bool phC_tick() {
  // BUG FIX #2: use edge so a button still held from C_PROMPT cannot skip Phase C immediately
  if (hw_btn_any_edge(nullptr)) {
#ifndef USE_WOKWI
    DiagMidi.end();
#endif
    Serial.println("  Skipped.");
    return true;
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
  hw_led_all_off();
  Serial.printf("[DIAG] Phase D: I2S Audio - measuring RMS for %lu s.\n",
                (unsigned long)(PHASE_D_MEASURE_MS / 1000));
#ifdef USE_WOKWI
  resultD = DR_WARN;
  Serial.println("  [SIM] I2S skipped - WARN.");
  return;
#endif
  i2s_config_t cfg  = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate          = I2S_SAMPLE_RATE;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count        = 4;
  cfg.dma_buf_len          = D_READ_FRAMES;
  cfg.use_apll             = false;
  i2s_driver_install(DIAG_I2S_PORT, &cfg, 0, nullptr);
  i2s_pin_config_t pins = {};
  pins.bck_io_num    = I2S_PIN_BCLK;
  pins.ws_io_num     = I2S_PIN_LRCK;
  pins.data_out_num  = I2S_PIN_NO_CHANGE;
  pins.data_in_num   = I2S_PIN_DATA;
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
  hw_led_all_off();
  diagTimer = millis();
  Serial.printf("[DIAG] Phase E: DFPlayer - track %u. Press BLUE within %lu s to confirm.\n",
                (unsigned)PHASE_E_TRACK, (unsigned long)(PHASE_E_TIMEOUT_MS / 1000));
#ifndef USE_WOKWI
  DiagDfpSer.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (!diagDfp.begin(DiagDfpSer)) {
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
  hw_led_duty(BLUE, ((millis() / 400) & 1) ? 180 : 0);  // blink BLUE: "press to confirm"
  if (hw_btn_edge(BLUE)) { hw_led_all_off();
#ifndef USE_WOKWI
    if (phE_dfpOk) diagDfp.stop();
#endif
    resultE = DR_PASS; return true; }
  if (millis() - diagTimer >= PHASE_E_TIMEOUT_MS) {
    hw_led_all_off();
    Serial.println("  No BLUE press within timeout — audio not confirmed. Check: speaker connected, SD card inserted, DFPlayer wired correctly.");
    resultE = DR_WARN;
    return true;
  }
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
void diag_init() {
  Serial.println("\n=== DIAGNOSTIC MODE ===");
  Serial.println("  Phases: A=LED  B=Buttons  C=MIDI  D=I2S  E=DFPlayer");
  Serial.println("  Any button skips phases; BLUE confirms checkpoints and exits.");
  Serial.println("  YELLOW hold 5s exits to Mode Selection at any time.");
  // Diag mode splash: BLUE blinks 2× (matches BLUE=select-diag button)
  for (int b = 0; b < 2; b++) { hw_led_duty(BLUE, 200); delay(200); hw_led_duty(BLUE, 0); delay(150); }

  // DFPlayer already sleeping from mode selection — Phase E will wake it.
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
      // Blink BLUE to signal "press BLUE to confirm"
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
      // BUG FIX #3: edge only (prevents PWM ghost clicks); any button exits
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
        if (diagBlinkOn) {
          Color wing = (diagOverallResult == DR_FAIL) ? RED :
                       (diagOverallResult == DR_WARN) ? YELLOW :
                       GREEN;
          hw_led_duty(wing, 200);
        } else {
          hw_led_all_off();
        }
      }
      break;
    }
  }
}
void diag_stop() {
  hw_led_all_off();
#ifndef USE_WOKWI
  DiagMidi.end();
  DiagDfpSer.end();
#endif
  i2s_driver_uninstall(DIAG_I2S_PORT);
  diagState = DS_PHASE_A;
  Serial.println("[DIAG] Mode stopped.");
}
