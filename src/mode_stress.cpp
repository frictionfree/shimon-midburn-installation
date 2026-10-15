/*
  Stress Test Mode - hardware endurance and ghost-click monitoring
  - Rotates LEDs continuously (BLUE→RED→GREEN→YELLOW, 500ms each)
  - Plays a random DFPlayer track every 15 s
  - Logs initial and final button states
  - Detects ghost clicks: raw GPIO LOW that never reaches debounced press
  - Logs legitimate presses (debounce-confirmed) separately
  - Hold any button >= 3 s to stop and print summary
*/

#include <Arduino.h>
#include "shimon.h"
#include "hw.h"
#include "mode_stress.h"

#ifndef USE_WOKWI
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#endif

static constexpr uint32_t STRESS_LED_CYCLE_MS    = 500;
static constexpr uint32_t STRESS_AUDIO_INTERVAL  = 15000;
static constexpr uint32_t STRESS_HOLD_EXIT_MS    = 3000;
static constexpr uint8_t  STRESS_AUDIO_MAX_TRACK = 5;
static constexpr uint8_t  STRESS_AUDIO_VOL       = 20;

enum StressState : uint8_t { SS_RUNNING, SS_DONE };
static StressState stressState;

static uint32_t      ghostCount[4];
static uint32_t      legitCount[4];
static uint32_t      audioCount;
static unsigned long startMs;
static uint8_t       ledIdx;
static unsigned long ledTimer;
static unsigned long audioTimer;

// Ghost detection: track raw GPIO state vs debounced press state per channel
static bool          rawActive[4];
static bool          becameDebounced[4];
static unsigned long rawStartMs[4];

#ifndef USE_WOKWI
static HardwareSerial      stressDfpSer(2);
static DFRobotDFPlayerMini stressDfp;
static bool                dfpOk;
#endif

static const Color COLS[4] = {BLUE, RED, GREEN, YELLOW};

static void logButtonStates(const char* label) {
  Serial.printf("[STRESS] %s: ", label);
  for (int i = 0; i < 4; i++) {
    Serial.printf("%s=%s%s", hw_led_name(COLS[i]),
                  hw_btn_raw(COLS[i]) ? "STUCK" : "open",
                  i < 3 ? "  " : "\n");
  }
}

static void playAudio() {
  uint8_t track = (uint8_t)(random(1, STRESS_AUDIO_MAX_TRACK + 1));
  audioCount++;
#ifndef USE_WOKWI
  if (dfpOk) stressDfp.playMp3Folder(track);
#endif
  Serial.printf("[STRESS] Audio: track %u (play #%lu)\n", track, (unsigned long)audioCount);
}

static void printSummary() {
  unsigned long durMs = millis() - startMs;
  Serial.println("\n========== STRESS TEST SUMMARY ==========");
  Serial.printf("  Duration:    %lu s\n", durMs / 1000);
  Serial.printf("  Audio plays: %lu\n", (unsigned long)audioCount);
  Serial.println("  Button activity (ghost / legit press):");
  for (int i = 0; i < 4; i++) {
    Serial.printf("    %s: %lu ghost  %lu legit\n",
                  hw_led_name(COLS[i]),
                  (unsigned long)ghostCount[i],
                  (unsigned long)legitCount[i]);
  }
  Serial.println("=========================================");
}

void stress_init() {
  Serial.println("\n=== STRESS TEST MODE ===");
  Serial.println("  LED rotation + DFPlayer audio. Hold any button 3s to stop.");

  memset(ghostCount,      0, sizeof(ghostCount));
  memset(legitCount,      0, sizeof(legitCount));
  memset(rawActive,       0, sizeof(rawActive));
  memset(becameDebounced, 0, sizeof(becameDebounced));
  audioCount = 0;

  logButtonStates("INITIAL state");

#ifndef USE_WOKWI
  dfpOk = false;
  stressDfpSer.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (stressDfp.begin(stressDfpSer, true, true)) {
    dfpOk = true;
    stressDfp.volume(STRESS_AUDIO_VOL);
    Serial.println("  DFPlayer OK.");
  } else {
    Serial.println("  DFPlayer FAILED. Audio test disabled.");
  }
#else
  Serial.println("  [SIM] DFPlayer skipped.");
#endif

  startMs    = millis();
  ledIdx     = 0;
  ledTimer   = millis();
  audioTimer = millis();
  stressState = SS_RUNNING;

  hw_led_all_off();
  hw_led_duty(COLS[ledIdx], 200);
  Serial.println("[STRESS] Running...");
}

void stress_tick() {
  unsigned long now = millis();

  if (stressState == SS_DONE) {
    if (hw_btn_any_edge(nullptr)) {
      delay(200); ESP.restart();
    }
    return;
  }

  // Exit: hold any button >= 3s → stop and show summary
  for (int i = 0; i < 4; i++) {
    if (hw_btn_held_ms(COLS[i]) >= STRESS_HOLD_EXIT_MS) {
      hw_led_all_off();
      logButtonStates("FINAL state");
      printSummary();
      stressState = SS_DONE;
      Serial.println("[STRESS] Press any button to restart.");
      return;
    }
  }

  // LED rotation
  if (now - ledTimer >= STRESS_LED_CYCLE_MS) {
    hw_led_duty(COLS[ledIdx], 0);
    ledIdx = (ledIdx + 1) % 4;
    hw_led_duty(COLS[ledIdx], 200);
    ledTimer = now;
  }

  // Periodic audio
  if (now - audioTimer >= STRESS_AUDIO_INTERVAL) {
    playAudio();
    audioTimer = now;
  }

  // Ghost + legitimate press detection
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
        if (!becameDebounced[i]) {
          ghostCount[i]++;
          Serial.printf("[STRESS] Ghost: %s  dur=%lums  total=%lu\n",
                        hw_led_name(COLS[i]),
                        now - rawStartMs[i],
                        (unsigned long)ghostCount[i]);
        }
        rawActive[i]       = false;
        becameDebounced[i] = false;
      }
    }

    if (hw_btn_edge(COLS[i])) {
      legitCount[i]++;
      Serial.printf("[STRESS] Press: %s at %lums  total=%lu\n",
                    hw_led_name(COLS[i]), now, (unsigned long)legitCount[i]);
    }
  }
}

void stress_stop() {
  hw_led_all_off();
#ifndef USE_WOKWI
  if (dfpOk) { stressDfp.stop(); dfpOk = false; }
  stressDfpSer.end();
#endif
  Serial.println("[STRESS] Mode stopped.");
}
