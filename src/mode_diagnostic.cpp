/*
  Diagnostic Mode - 3-phase hardware verification (game hardware only)
  A: LED PWM   - cycle each wing at 3 brightness levels, operator confirms
  B: Buttons   - light each wing, wait for corresponding button press
  E: DFPlayer  - play track 1, press BLUE within 8 s to confirm heard
*/

#include <Arduino.h>
#include "shimon.h"
#include "hw.h"
#include "mode_diagnostic.h"

#ifndef USE_WOKWI
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#endif

// ---- Timing constants ----
static constexpr uint32_t PHASE_A_LEVEL_MS   = 800;
static constexpr uint32_t PHASE_A_TIMEOUT_MS = 15000;
static constexpr uint32_t PHASE_B_BTN_TOUT   = 5000;
// Order: GREEN→RED→BLUE→YELLOW avoids conflicts with the BLUE press used to
// enter Phase B and the BLUE press used to confirm Phase A_CONFIRM afterward.
static const Color phB_ORDER[4] = { GREEN, RED, BLUE, YELLOW };
static constexpr uint32_t DIAG_DONE_AUTO_MS  = 20000;
static constexpr uint32_t PHASE_E_TIMEOUT_MS = 8000;
static constexpr uint8_t  PHASE_E_TRACK      = 1;
static constexpr uint8_t  PHASE_E_VOL        = 20;

// ---- Hardware objects ----
#ifndef USE_WOKWI
static HardwareSerial      DiagDfpSer(1);
static DFRobotDFPlayerMini diagDfp;
#endif

// ---- Result / State enums ----
enum DiagResult : uint8_t { DR_NONE=0, DR_PASS, DR_WARN, DR_FAIL };
enum DiagState  : uint8_t { DS_PHASE_A, DS_PHASE_A_CONFIRM, DS_PHASE_B, DS_PHASE_E, DS_DONE };

static DiagState diagState;
static const char* drStr(DiagResult r) {
  switch (r) {
    case DR_PASS: return "PASS";
    case DR_WARN: return "WARN";
    case DR_FAIL: return "FAIL";
    default:      return "NONE";
  }
}

// ---- Shared state ----
static DiagResult    resultA, resultB, resultE;
static DiagResult    diagOverallResult;
static bool          diagBlinkOn;
static unsigned long diagTimer;
static unsigned long diagDoneStart;

// ---- Phase A ----
static uint8_t phA_wing, phA_level;
static const uint8_t PH_A_DUTIES[3] = {64, 140, 235};

// ---- Phase B ----
static uint8_t phB_wing, phB_failed;

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
  hw_btn_set_fast(true);
  diagTimer = millis();
  Color first = phB_ORDER[0];
  Serial.println("[DIAG] Phase B: Buttons - press each lit button.");
  Serial.printf("  Press %s...\n", hw_led_name(first));
  hw_led_duty(first, 200);
}
static bool phB_tick() {
  Color wing = phB_ORDER[phB_wing];
  bool confirmed = hw_btn_edge(wing);
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
    Serial.println("  DFPlayer init FAILED. Check: power, wiring, SD card.");
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
      resultE = DR_PASS;
    } else {
      Serial.println("  Skipped.");
    }
    return true;
  }
  if (millis() - diagTimer >= PHASE_E_TIMEOUT_MS) {
    hw_led_all_off();
    Serial.println("  No BLUE press — audio not confirmed. Check: speaker, SD card, DFPlayer wiring.");
    resultE = DR_WARN;
    return true;
  }
  return false;
}

// =============================================================================
// SUMMARY
// =============================================================================
static void printSummary() {
  bool anyFail = (resultA==DR_FAIL || resultB==DR_FAIL || resultE==DR_FAIL);
  bool anyWarn = (resultA==DR_WARN || resultB==DR_WARN || resultE==DR_WARN);
  Serial.println("\n========== DIAGNOSTIC SUMMARY ==========");
  Serial.printf("  A (LED PWM):  %s\n", drStr(resultA));
  Serial.printf("  B (Buttons):  %s\n", drStr(resultB));
  Serial.printf("  E (DFPlayer): %s\n", resultE == DR_NONE ? "SKIP" : drStr(resultE));
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
  Serial.println("  Phases: A=LED  B=Buttons  E=DFPlayer");
  Serial.println("  Any button skips phase; YELLOW hold 5s exits to Mode Selection.");
  for (int b = 0; b < 2; b++) { hw_led_duty(BLUE, 200); delay(200); hw_led_duty(BLUE, 0); delay(150); }

  resultA = resultB = resultE = DR_NONE;
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
        hw_btn_set_fast(false);
        resultB = (phB_failed == 0) ? DR_PASS : (phB_failed < 4 ? DR_WARN : DR_FAIL);
        Serial.printf("  Phase B: %s  (%u/4 ok)\n", drStr(resultB), 4 - phB_failed);
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
  if (phE_dfpOk) { diagDfp.stop(); phE_dfpOk = false; }
  DiagDfpSer.end();
#endif
  diagState = DS_PHASE_A;
  Serial.println("[DIAG] Mode stopped.");
}
