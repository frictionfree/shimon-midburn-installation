/*
  Stress Test Mode - automated click simulation and ghost monitoring
  Simulates BLUE->RED->GREEN->YELLOW->repeat at a fixed rate.
  Each click: LED on + color audio for STRESS_CLICK_DURATION_MS, then LED off.
  Monitors all button GPIO lines — any raw LOW is unexpected (no human input).
  Logs runtime status every STRESS_STATUS_INTERVAL_MS.
  Hold YELLOW 3s to exit (handled by main.cpp).
*/

#include <Arduino.h>
#include "shimon.h"
#include "hw.h"
#include "mode_stress.h"

#ifndef USE_WOKWI
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#endif

static constexpr uint32_t STRESS_CLICK_DURATION_MS  = 1000;
static constexpr uint32_t STRESS_CLICK_GAP_MS       = 2000;
static constexpr uint32_t STRESS_STATUS_INTERVAL_MS = 600000;  // 10 min
static constexpr uint8_t  STRESS_AUDIO_VOL          = 20;
static constexpr uint32_t STRESS_STUCK_FIRST_MS     = 500;
static constexpr uint32_t STRESS_STUCK_INTERVAL_MS  = 1000;

static const Color COLS[4] = {BLUE, RED, GREEN, YELLOW};

enum StressState : uint8_t { SS_CLICK_ACTIVE, SS_GAP };
static StressState   stressState;
static uint8_t       clickIdx;
static uint32_t      clickCount;
static uint32_t      ghostCount[4];
static unsigned long stateTimer;
static unsigned long startMs;
static unsigned long lastStatusMs;

static bool          rawActive[4];
static bool          becameDebounced[4];
static unsigned long rawStartMs[4];
static unsigned long lastStuckLogMs[4];

#ifndef USE_WOKWI
static HardwareSerial      stressDfpSer(2);
static DFRobotDFPlayerMini stressDfp;
static bool                dfpOk;
#endif

static const char* stressRuntime() {
  static char buf[16];
  unsigned long s = (millis() - startMs) / 1000;
  snprintf(buf, sizeof(buf), "%luh%02lum%02lus", s / 3600, (s % 3600) / 60, s % 60);
  return buf;
}

static uint8_t colorAudioTrack(Color c) {
  switch (c) {
    case BLUE:   return AUDIO_COLOR_BLUE;
    case RED:    return AUDIO_COLOR_RED;
    case GREEN:  return AUDIO_COLOR_GREEN;
    default:     return AUDIO_COLOR_YELLOW;
  }
}

static void playColorAudio(Color c) {
  uint8_t track = colorAudioTrack(c);
#ifndef USE_WOKWI
  if (dfpOk) stressDfp.playMp3Folder(track);
#endif
}

static void logStatus() {
  Serial.printf("\n[STRESS ===] @%s  clicks=%lu  ghosts: BLUE=%lu RED=%lu GREEN=%lu YELLOW=%lu\n",
                stressRuntime(), (unsigned long)clickCount,
                (unsigned long)ghostCount[0], (unsigned long)ghostCount[1],
                (unsigned long)ghostCount[2], (unsigned long)ghostCount[3]);
}

static void enterClick() {
  Color c = COLS[clickIdx];
  hw_led_duty(c, 200);
  playColorAudio(c);
  Serial.printf("[STRESS] #%lu  %s\n", (unsigned long)(clickCount + 1), hw_led_name(c));
  stateTimer  = millis();
  stressState = SS_CLICK_ACTIVE;
}

static void monitorGhosts() {
  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    bool raw       = hw_btn_raw(COLS[i]);
    bool debounced = hw_btn_pressed(COLS[i]);

    if (!rawActive[i]) {
      if (raw) {
        rawActive[i]       = true;
        becameDebounced[i] = false;
        rawStartMs[i]      = now;
      }
    } else {
      if (debounced) becameDebounced[i] = true;
      if (!raw) {
        unsigned long dur = now - rawStartMs[i];
        if (becameDebounced[i]) {
          Serial.printf("[STRESS] UNEXPECTED PRESS: %s  dur=%lums  @%s\n",
                        hw_led_name(COLS[i]), dur, stressRuntime());
        } else {
          ghostCount[i]++;
          Serial.printf("[STRESS] UNEXPECTED GHOST: %s  dur=%lums  ghost#%lu  @%s\n",
                        hw_led_name(COLS[i]), dur, (unsigned long)ghostCount[i], stressRuntime());
        }
        rawActive[i]       = false;
        becameDebounced[i] = false;
        lastStuckLogMs[i]  = 0;
      } else {
        unsigned long elapsed = now - rawStartMs[i];
        if (elapsed >= STRESS_STUCK_FIRST_MS) {
          if (lastStuckLogMs[i] == 0 || now - lastStuckLogMs[i] >= STRESS_STUCK_INTERVAL_MS) {
            lastStuckLogMs[i] = now;
            Serial.printf("[STRESS] STUCK: %s  %lums  @%s\n",
                          hw_led_name(COLS[i]), elapsed, stressRuntime());
          }
        }
      }
    }
  }
}

void stress_init() {
  Serial.println("\n=== STRESS TEST MODE ===");
  Serial.printf("  Click: %lums on / %lums gap, order: BLUE->RED->GREEN->YELLOW.\n",
                (unsigned long)STRESS_CLICK_DURATION_MS, (unsigned long)STRESS_CLICK_GAP_MS);
  Serial.println("  All button activity logged. Hold YELLOW 3s to exit.");

  clickIdx   = 0;
  clickCount = 0;
  memset(ghostCount,      0, sizeof(ghostCount));
  memset(rawActive,       0, sizeof(rawActive));
  memset(becameDebounced, 0, sizeof(becameDebounced));
  memset(rawStartMs,      0, sizeof(rawStartMs));
  memset(lastStuckLogMs,  0, sizeof(lastStuckLogMs));

  startMs      = millis();
  lastStatusMs = millis();

  hw_led_all_off();

#ifndef USE_WOKWI
  dfpOk = false;
  stressDfpSer.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (stressDfp.begin(stressDfpSer, true, true)) {
    dfpOk = true;
    stressDfp.volume(STRESS_AUDIO_VOL);
    Serial.println("  DFPlayer OK.");
  } else {
    Serial.println("  DFPlayer FAILED — running without audio.");
  }
#else
  Serial.println("  [SIM] DFPlayer skipped.");
#endif

  enterClick();
}

void stress_tick() {
  unsigned long now = millis();

  monitorGhosts();

  if (now - lastStatusMs >= STRESS_STATUS_INTERVAL_MS) {
    lastStatusMs = now;
    logStatus();
  }

  switch (stressState) {
    case SS_CLICK_ACTIVE:
      if (now - stateTimer >= STRESS_CLICK_DURATION_MS) {
        hw_led_duty(COLS[clickIdx], 0);
        clickCount++;
        stateTimer  = now;
        stressState = SS_GAP;
      }
      break;

    case SS_GAP:
      if (now - stateTimer >= STRESS_CLICK_GAP_MS) {
        clickIdx = (clickIdx + 1) % 4;
        enterClick();
      }
      break;
  }
}

void stress_stop() {
  hw_led_all_off();
#ifndef USE_WOKWI
  if (dfpOk) { stressDfp.stop(); dfpOk = false; }
  stressDfpSer.end();
#endif
  logStatus();
  Serial.println("[STRESS] Mode stopped.");
}
