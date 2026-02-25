#include <Arduino.h>
#include "shimon.h"
#include "mode_game.h"
#include "mode_party.h"
#include "mode_diagnostic.h"

// Shimon - Top-level orchestrator  (Phase 3: Game + Party + Diagnostic)
// Mode Selection (blocking, no timeout):
//   BLUE  single press  -> Game Mode
//   GREEN single press  -> Party Mode
//   RED   single press  -> Diagnostic Mode
//   YELLOW hold >=5 s   -> Global Reset

enum TopMode : uint8_t { GAME_MODE = 0, PARTY_MODE = 1, DIAG_MODE = 2 };
static TopMode activeMode = GAME_MODE;

static constexpr uint8_t SEL_CH[4]  = {0, 1, 2, 3};
static const     uint8_t SEL_LED[4] = {LED_BLUE, LED_RED, LED_GREEN, LED_YELLOW};
static const     uint8_t SEL_BTN[4] = {BTN_BLUE, BTN_RED, BTN_GREEN, BTN_YELLOW};

static void selPwmInit() {
  for (int i = 0; i < 4; i++) {
    ledcSetup(SEL_CH[i], 12500, 8);
    ledcAttachPin(SEL_LED[i], SEL_CH[i]);
    ledcWrite(SEL_CH[i], 0);
  }
}
static void selAllOff() { for (int i = 0; i < 4; i++) ledcWrite(SEL_CH[i], 0); }

static void selConfirmAnimation() {
  for (int lap = 0; lap < 2; lap++) {
    for (int i = 0; i < 4; i++) {
      ledcWrite(SEL_CH[i], 180); delay(120); ledcWrite(SEL_CH[i], 0);
    }
  }
  for (int i = 0; i < 4; i++) ledcWrite(SEL_CH[i], 180);
  delay(300);
  selAllOff();
}

static TopMode runModeSelection() {
  selPwmInit();
  for (int i = 0; i < 4; i++) pinMode(SEL_BTN[i], INPUT_PULLUP);
  Serial.println("\n=== MODE SELECTION ===");
  Serial.println("  BLUE  -> Game Mode");
  Serial.println("  GREEN -> Party Mode");
  Serial.println("  RED   -> Diagnostic Mode");
  Serial.println("  YELLOW hold 5s -> Global Reset");
  uint8_t rotSlot = 0;
  ledcWrite(SEL_CH[0], 180);
  unsigned long ledTimer = millis();
  unsigned long yHoldStart = 0;
  bool yHeld = false;
  bool btnPrev[3] = {false, false, false};
  while (true) {
    unsigned long now = millis();
    if (now - ledTimer >= 1000UL) {
      ledcWrite(SEL_CH[rotSlot], 0);
      rotSlot = (rotSlot + 1) % 3;
      ledcWrite(SEL_CH[rotSlot], 180);
      ledTimer = now;
    }
    bool yNow = (digitalRead(SEL_BTN[3]) == LOW);
    if (yNow && !yHeld) { yHoldStart = now; yHeld = true; }
    else if (!yNow) { yHeld = false; }
    if (yHeld && now - yHoldStart >= 5000UL) {
      selAllOff();
      Serial.println("[SYS] Yellow 5s: global reset.");
      delay(200); ESP.restart();
    }
    for (int i = 0; i < 3; i++) {
      bool btnNow = (digitalRead(SEL_BTN[i]) == LOW);
      if (btnNow && !btnPrev[i]) {
        selAllOff();
        TopMode sel;
        const char* nm;
        switch (i) {
          case 0: sel = GAME_MODE;   nm = "Game Mode";       break;
          case 1: sel = DIAG_MODE;   nm = "Diagnostic Mode"; break;
          default: sel = PARTY_MODE; nm = "Party Mode";      break;
        }
        Serial.printf("[MODE] %s selected.\n", nm);
        selConfirmAnimation();
        return sel;
      }
      btnPrev[i] = btnNow;
    }
    delay(10);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Shimon Integrated Firmware (Phase 3: Game + Party + Diagnostic) ===");
  activeMode = runModeSelection();
  switch (activeMode) {
    case PARTY_MODE: party_init(); break;
    case DIAG_MODE:  diag_init();  break;
    default:         game_init();  break;
  }
}

void loop() {
  {
    static unsigned long yHoldStart = 0;
    static bool yHeld = false;
    bool yNow = (digitalRead(BTN_YELLOW) == LOW);
    if (yNow && !yHeld) { yHoldStart = millis(); yHeld = true; }
    else if (!yNow) { yHeld = false; }
    if (yHeld && millis() - yHoldStart >= 5000UL) {
      Serial.println("[SYS] Yellow 5s: global reset.");
      delay(200); ESP.restart();
    }
  }
  switch (activeMode) {
    case PARTY_MODE: party_tick(); break;
    case DIAG_MODE:  diag_tick();  break;
    default:         game_tick();  break;
  }
}
