#include <Arduino.h>
#include <esp_system.h>
#include <math.h>
#include "shimon.h"
#include "hw.h"
#include "mode_game.h"
#include "mode_stress.h"
#include "mode_diagnostic.h"

// Shimon - Top-level orchestrator (Event: Game + Stress Test + Diagnostic)
//
// Boot behaviour:
//   Power-on / crash  → auto-start Game Mode (no operator intervention needed)
//   SW restart with rtcBootIntent==1 → Mode Selection (set by yellow 5s from any mode)
//
// Mode Selection (blocking):
//   BLUE   → Game Mode
//   RED    → Diagnostic Mode
//   YELLOW → Stress Test Mode  (hold 5s → restart loop back to mode selection)

static const char* uptimeStr() {
  static char buf[16];
  unsigned long s = millis() / 1000;
  snprintf(buf, sizeof(buf), "%luh%02lum%02lus", s / 3600, (s % 3600) / 60, s % 60);
  return buf;
}

// ---- Persistent state (survives SW restart; cleared to 0 on power-on) ----
RTC_DATA_ATTR static uint8_t rtcBootIntent;     // 1 = enter mode selection on next boot
RTC_DATA_ATTR static uint8_t rtcPassiveRetries; // counts passive-mode auto-restart attempts

// ---- Mode ----
enum TopMode : uint8_t { GAME_MODE = 0, STRESS_MODE = 1, DIAG_MODE = 2 };
static TopMode activeMode = GAME_MODE;

// ---- System health ----
enum SysHealth : uint8_t { HEALTHY, PASSIVE };
static SysHealth     sysHealth      = HEALTHY;
static unsigned long passiveEnterMs = 0;
static char          passiveReason[48];

// ---- Ghost storm monitor state ----
static bool          gsRawActive[4];
static bool          gsGotDebounced[4];
static unsigned long gsRawStartMs[4];
static uint8_t       gsWindowCount = 0;
static unsigned long gsWindowStart = 0;

// =============================================================================
// PASSIVE MODE
// =============================================================================
static void enterPassiveMode(const char* reason) {
  if (sysHealth == PASSIVE) return;
  sysHealth      = PASSIVE;
  passiveEnterMs = millis();
  snprintf(passiveReason, sizeof(passiveReason), "%s", reason);
  hw_led_all_off();
  Serial.printf("[PASSIVE] Entering at %s: %s\n", uptimeStr(), passiveReason);
  Serial.printf("[PASSIVE] Retry budget: %u/%u (auto-restart in %lu s)\n",
                (unsigned)rtcPassiveRetries, (unsigned)PASSIVE_MAX_RETRIES,
                (unsigned long)(PASSIVE_RETRY_INTERVAL_MS / 1000));
}

static void passiveTick() {
  unsigned long now = millis();

  // Slow RED breathing — signals "alive but unhealthy"
  uint8_t duty = (uint8_t)(20.0f + 25.0f * sinf((float)(now % 3000) * 6.2832f / 3000.0f));
  hw_led_duty(RED, duty);

  // Manual exit: yellow 5s → mode selection
  if (hw_btn_held_ms(YELLOW) >= YELLOW_EXIT_HOLD_MS) {
    hw_led_all_off();
    Serial.println("[PASSIVE] Yellow 3s: manual exit to mode selection.");
    rtcBootIntent = 1;
    delay(200); ESP.restart();
  }

  // Auto-restart (up to PASSIVE_MAX_RETRIES attempts)
  if (rtcPassiveRetries < PASSIVE_MAX_RETRIES) {
    if (now - passiveEnterMs >= PASSIVE_RETRY_INTERVAL_MS) {
      rtcPassiveRetries++;
      hw_led_all_off();
      Serial.printf("[PASSIVE] Auto-restart attempt %u/%u — restarting to game mode.\n",
                    (unsigned)rtcPassiveRetries, (unsigned)PASSIVE_MAX_RETRIES);
      rtcBootIntent = 0;
      delay(200); ESP.restart();
    }
    // Periodic status log (every 60 s)
    static unsigned long lastLog = 0;
    if (now - lastLog >= 60000UL) {
      lastLog = now;
      Serial.printf("[PASSIVE] %s — auto-restart in %lu s (attempt %u/%u)\n",
                    passiveReason,
                    (PASSIVE_RETRY_INTERVAL_MS - (now - passiveEnterMs)) / 1000,
                    (unsigned)(rtcPassiveRetries + 1), (unsigned)PASSIVE_MAX_RETRIES);
    }
  }
  // Max retries reached: stay dark indefinitely until power-cycle or yellow 5s
}

// =============================================================================
// GHOST STORM MONITOR  (called from loop() when activeMode == GAME_MODE)
// =============================================================================
static void ghostMonitorTick() {
  unsigned long now = millis();

  // Slide the window
  if (now - gsWindowStart > GHOST_STORM_WINDOW_MS) {
    gsWindowStart = now;
    gsWindowCount = 0;
  }

  for (int i = 0; i < 4; i++) {
    Color c        = (Color)i;
    bool  raw      = hw_btn_raw(c);
    bool  debounced = hw_btn_pressed(c);

    if (!gsRawActive[i]) {
      if (raw) {
        gsRawActive[i]    = true;
        gsGotDebounced[i] = false;
        gsRawStartMs[i]   = now;
      }
    } else {
      if (debounced) gsGotDebounced[i] = true;
      if (!raw) {
        if (!gsGotDebounced[i]) {
          // Raw LOW resolved without debounce confirmation → ghost
          gsWindowCount++;
          Serial.printf("[GHOST] %s dur=%lums window=%u/%u\n",
                        hw_led_name(c), now - gsRawStartMs[i],
                        (unsigned)gsWindowCount, (unsigned)GHOST_STORM_COUNT);
          if (gsWindowCount >= GHOST_STORM_COUNT) {
            game_stop();
            enterPassiveMode("Ghost storm");
            return;
          }
        }
        gsRawActive[i]    = false;
        gsGotDebounced[i] = false;
      }
    }
  }
}

// =============================================================================
// MODE SELECTION  (blocking — only reached via intentional SW restart)
// =============================================================================
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
  Serial.println("  YELLOW -> Stress Test Mode  (hold 5s = restart to game mode)");
  static const Color SEL_COLORS[] = {BLUE, RED, YELLOW};
  uint8_t rotSlot = 0;
  hw_led_duty(SEL_COLORS[0], 180);
  unsigned long ledTimer = millis();
  while (true) {
    unsigned long now = millis();
    hw_btn_update();
    if (now - ledTimer >= 1000UL) {
      hw_led_duty(SEL_COLORS[rotSlot], 0);
      rotSlot = (rotSlot + 1) % 3;
      hw_led_duty(SEL_COLORS[rotSlot], 180);
      ledTimer = now;
    }
    // Yellow 5s in mode selection → restart directly to game mode
    if (hw_btn_held_ms(YELLOW) >= YELLOW_EXIT_HOLD_MS) {
      hw_led_all_off();
      Serial.println("[SYS] Yellow 3s in mode selection: restarting to game mode.");
      rtcBootIntent = 0;
      delay(200); ESP.restart();
    }
    Color pressed;
    if (hw_btn_any_edge(&pressed) && pressed != GREEN) {
      hw_led_all_off();
      TopMode sel;
      const char* nm;
      switch (pressed) {
        case BLUE:  sel = GAME_MODE;   nm = "Game Mode";        break;
        case RED:   sel = DIAG_MODE;   nm = "Diagnostic Mode";  break;
        default:    sel = STRESS_MODE; nm = "Stress Test Mode"; break;
      }
      Serial.printf("[MODE] %s selected.\n", nm);
      selConfirmAnimation();
      return sel;
    }
    delay(10);
  }
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Shimon v" SHIMON_VERSION " ===");
  hw_led_init();
  hw_btn_init();

  esp_reset_reason_t resetReason = esp_reset_reason();
  // Clear retry counter and intent on cold start or brownout
  if (resetReason == ESP_RST_POWERON || resetReason == ESP_RST_BROWNOUT) {
    rtcPassiveRetries = 0;
    rtcBootIntent     = 0;
    Serial.println("[BOOT] Cold start — passive retry counter cleared.");
  }
  Serial.printf("[BOOT] Reset reason=%d  passiveRetries=%u  bootIntent=%u\n",
                (int)resetReason, (unsigned)rtcPassiveRetries, (unsigned)rtcBootIntent);

  // --- Stuck button check (before mode selection) ---
  static char stuckBuf[40];
  for (int i = 0; i < 4; i++) {
    Color c = (Color)i;
    unsigned long t = millis();
    while (hw_btn_raw(c) && millis() - t < STUCK_BUTTON_MS) delay(10);
    if (hw_btn_raw(c)) {
      snprintf(stuckBuf, sizeof(stuckBuf), "Stuck button: %s", hw_led_name(c));
      enterPassiveMode(stuckBuf);
      return;  // loop() will run passiveTick() with activeMode at default GAME_MODE
    }
  }

  // --- Mode decision ---
  if (resetReason == ESP_RST_SW && rtcBootIntent == 1) {
    rtcBootIntent = 0;
    Serial.println("[BOOT] Mode selection restart.");
    activeMode = runModeSelection();
  } else {
    activeMode = GAME_MODE;
    Serial.println("[BOOT] Auto-start: Game Mode.");
  }

  switch (activeMode) {
    case STRESS_MODE: stress_init(); break;
    case DIAG_MODE:   diag_init();   break;
    default:          game_init();   break;
  }

  // --- Post-init health check (game mode: DFPlayer) ---
  if (activeMode == GAME_MODE && !game_dfpOk()) {
    game_stop();
    enterPassiveMode("DFPlayer init failed");
    return;
  }

  // --- Init ghost monitor ---
  memset(gsRawActive,    0, sizeof(gsRawActive));
  memset(gsGotDebounced, 0, sizeof(gsGotDebounced));
  gsWindowCount = 0;
  gsWindowStart = millis();
  Serial.println("[BOOT] Ready.");
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
  hw_btn_update();

  // Passive mode: suspend all other logic
  if (sysHealth == PASSIVE) {
    passiveTick();
    return;
  }

  // Yellow 5s from any active mode → restart to mode selection
  if (hw_btn_held_ms(YELLOW) >= YELLOW_EXIT_HOLD_MS) {
    Serial.println("[SYS] Yellow 3s: restarting to mode selection.");
    switch (activeMode) {
      case STRESS_MODE: stress_stop(); break;
      case DIAG_MODE:   diag_stop();   break;
      default:          game_stop();   break;
    }
    rtcBootIntent = 1;
    delay(200); ESP.restart();
  }

  // DFPlayer consecutive timeout check (game mode only)
  if (activeMode == GAME_MODE && game_audioFailCount() >= AUDIO_FAIL_THRESHOLD) {
    game_stop();
    enterPassiveMode("DFPlayer unresponsive");
    return;
  }

  // Periodic uptime heartbeat (every 5 min)
  static unsigned long lastUptimeLog = 0;
  if (millis() - lastUptimeLog >= 300000UL) {
    lastUptimeLog = millis();
    const char* modeStr = (activeMode == STRESS_MODE) ? "stress"
                        : (activeMode == DIAG_MODE)   ? "diag"
                        :                               "game";
    Serial.printf("[SYS] Uptime: %s  mode=%s\n", uptimeStr(), modeStr);
  }

  switch (activeMode) {
    case STRESS_MODE: stress_tick(); break;
    case DIAG_MODE:   diag_tick();   break;
    default:          game_tick();   break;
  }
}
