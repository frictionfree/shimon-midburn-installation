#include <Arduino.h>
#include "shimon.h"
#include "hw.h"
#include "mode_game.h"
#include "mode_stress.h"
#include "mode_diagnostic.h"

// Shimon - Top-level orchestrator (Event: Game + Stress Test + Diagnostic)
// Mode Selection (blocking, no timeout):
//   BLUE  single press  -> Game Mode
//   GREEN single press  -> Stress Test Mode
//   RED   single press  -> Diagnostic Mode
//   YELLOW hold >=5 s   -> Global Reset

enum TopMode : uint8_t { GAME_MODE = 0, STRESS_MODE = 1, DIAG_MODE = 2 };
static TopMode activeMode = GAME_MODE;

static void selConfirmAnimation() {
  for (int lap = 0; lap < 2; lap++) {
    for (int i = 0; i < 4; i++) {
      hw_led_duty((Color)i, 180); delay(120); hw_led_duty((Color)i, 0);
    }
  }
  uint8_t f[4] = {180, 180, 180, 180};
  hw_led_all_set(f);
  delay(300);
  hw_led_all_off();
}

static TopMode runModeSelection() {
  Serial.println("\n=== MODE SELECTION ===");
  Serial.println("  BLUE   -> Game Mode");
  Serial.println("  RED    -> Diagnostic Mode");
  Serial.println("  YELLOW -> Stress Test Mode  (hold 5s = Global Reset)");
  static const Color SEL_COLORS[] = {BLUE, RED, YELLOW};
  uint8_t rotSlot = 0;
  hw_led_duty(SEL_COLORS[0], 180);
  unsigned long ledTimer = millis();
  while (true) {
    unsigned long now = millis();
    hw_btn_update();

    if (now - ledTimer >= 1000UL) {
      hw_led_duty(SEL_COLORS[rotSlot], 0);
      rotSlot = (rotSlot + 1) % 3;  // cycles BLUE → RED → YELLOW
      hw_led_duty(SEL_COLORS[rotSlot], 180);
      ledTimer = now;
    }

    if (hw_btn_held_ms(YELLOW) >= 5000UL) {
      hw_led_all_off();
      Serial.println("[SYS] Yellow 5s: global reset.");
      delay(200); ESP.restart();
    }

    Color pressed;
    if (hw_btn_any_edge(&pressed) && pressed != GREEN) {
      hw_led_all_off();
      TopMode sel;
      const char* nm;
      switch (pressed) {
        case BLUE:   sel = GAME_MODE;   nm = "Game Mode";        break;
        case RED:    sel = DIAG_MODE;   nm = "Diagnostic Mode";  break;
        default:     sel = STRESS_MODE; nm = "Stress Test Mode"; break;
      }
      Serial.printf("[MODE] %s selected.\n", nm);
      selConfirmAnimation();
      return sel;
    }

    delay(10);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Shimon v" SHIMON_VERSION " ===");
  hw_led_init();
  hw_btn_init();
  Serial.println("[BOOT] Hardware init done. Entering mode selection.");
  activeMode = runModeSelection();
  switch (activeMode) {
    case STRESS_MODE: stress_init(); break;
    case DIAG_MODE:   diag_init();  break;
    default:         game_init();  break;
  }
}

void loop() {
  hw_btn_update();  // Single canonical button update; all modes read from hw layer

  if (hw_btn_held_ms(YELLOW) >= 5000UL) {
    Serial.println("[SYS] Yellow 5s: global reset.");
    switch (activeMode) {
      case STRESS_MODE: stress_stop(); break;
      case DIAG_MODE:   diag_stop();  break;
      default:         game_stop();  break;
    }
    delay(200); ESP.restart();
  }

  switch (activeMode) {
    case STRESS_MODE: stress_tick(); break;
    case DIAG_MODE:   diag_tick();  break;
    default:         game_tick();  break;
  }
}
