#include <Arduino.h>
#include "shimon.h"      // shared pin constants
#include "mode_game.h"
#include "mode_party.h"

// ============================================================
//  Shimon – Top-level orchestrator (Phase 2: Game + Party)
//
//  Hub-and-spoke mode architecture:
//    Boot → Mode Selection (3-second window) → active mode runs indefinitely
//
//  Button mapping during selection:
//    BLUE  (or no press / timeout) → Game Mode
//    GREEN                         → Party Mode
//    YELLOW                        → Diagnostic Mode (Phase 3, not yet available)
//
//  Phase 3: add mode_diagnostic.h + Diagnostic Mode
//  Phase 4: add mid-session mode switching (hold-combo returns to selection)
// ============================================================

enum TopMode : uint8_t { GAME_MODE = 0, PARTY_MODE = 1 };
static TopMode activeMode = GAME_MODE;

// ---- Boot-time Mode Selection (3-second window) ----
static TopMode runModeSelection() {
  const uint8_t LEDP[4] = { LED_BLUE, LED_RED, LED_GREEN, LED_YELLOW };
  const uint8_t BTNP[4] = { BTN_BLUE, BTN_RED, BTN_GREEN, BTN_YELLOW };

  // Configure pins for selection UI (plain GPIO, no LEDC yet)
  for (uint8_t p : LEDP) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  for (uint8_t p : BTNP)   pinMode(p, INPUT_PULLUP);

  Serial.println("\n=== MODE SELECTION (3 seconds) ===");
  Serial.println("  BLUE  button -> Game Mode  (default)");
  Serial.println("  GREEN button -> Party Mode");
  Serial.println("  YELLOW       -> Diagnostic (Phase 3, not yet available)");
  Serial.println("  No press     -> Game Mode");

  const unsigned long WINDOW_MS = 3000UL;
  unsigned long start    = millis();
  unsigned long ledTimer = start;
  uint8_t ledIdx = 0;

  while (millis() - start < WINDOW_MS) {
    // Rotating single-LED chase to show selection is live
    unsigned long now = millis();
    if (now - ledTimer > 200UL) {
      for (uint8_t p : LEDP) digitalWrite(p, LOW);
      digitalWrite(LEDP[ledIdx & 3], HIGH);
      ledIdx++;
      ledTimer = now;
    }

    // Edge-detect each button (INPUT_PULLUP → LOW when pressed)
    for (int i = 0; i < 4; i++) {
      if (digitalRead(BTNP[i]) == LOW) {
        for (uint8_t p : LEDP) digitalWrite(p, LOW);
        switch (i) {
          case 2:  // GREEN → Party Mode
            Serial.println("[MODE] Party Mode selected.");
            return PARTY_MODE;
          case 3:  // YELLOW → Diagnostic (not yet available)
            Serial.println("[MODE] Diagnostic not yet available – defaulting to Game Mode.");
            return GAME_MODE;
          default: // BLUE (0) or RED (1) → Game Mode
            Serial.println("[MODE] Game Mode selected.");
            return GAME_MODE;
        }
      }
    }
  }

  for (uint8_t p : LEDP) digitalWrite(p, LOW);
  Serial.println("[MODE] Timeout – defaulting to Game Mode.");
  return GAME_MODE;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Shimon Integrated Firmware (Phase 2: Game + Party) ===");

  activeMode = runModeSelection();

  if (activeMode == PARTY_MODE) {
    party_init();
  } else {
    game_init();
  }
}

void loop() {
  if (activeMode == PARTY_MODE) {
    party_tick();
  } else {
    game_tick();
  }
}
