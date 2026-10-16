/*
  Stress Test Mode - hardware endurance and button activity monitoring
  - Rotates LEDs continuously (BLUE→RED→GREEN→YELLOW, 500ms each)
  - Plays a random DFPlayer track every 15 s
  - Logs initial and final button states
  - Logs ALL raw button activity at resolution with uptime timestamp and duration:
      GHOST = raw LOW resolved without debounce confirmation (EMI/coupling spike)
      PRESS = raw LOW that reached debounce threshold (human tap or slow ghost)
    Duration distinguishes real taps (>80ms) from slow ghosts (50-80ms range).
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

static constexpr uint32_t STRESS_LED_CYCLE_MS        = 500;
static constexpr uint32_t STRESS_AUDIO_INTERVAL      = 15000;
static constexpr uint32_t STRESS_HOLD_EXIT_MS         = 3000;
static constexpr uint8_t  STRESS_AUDIO_MAX_TRACK      = 5;
static constexpr uint8_t  STRESS_AUDIO_VOL            = 20;
static constexpr uint32_t STRESS_STUCK_FIRST_MS       = 500;   // log if raw LOW persists beyond this
static constexpr uint32_t STRESS_STUCK_INTERVAL_MS    = 1000;  // repeat log every this many ms

enum StressState : uint8_t { SS_RUNNING, SS_DONE };
static StressState stressState;

static uint32_t      ghostCount[4];
static uint32_t      legitCount[4];
static uint32_t      audioCount;
static unsigned long startMs;
static uint8_t       ledIdx;
static unsigned long ledTimer;
static unsigned long audioTimer;

// Per-channel raw tracking
static bool          rawActive[4];
static bool          becameDebounced[4];
static unsigned long rawStartMs[4];
static unsigned long lastStuckLogMs[4];

#ifndef USE_WOKWI
static HardwareSerial      stressDfpSer(2);
static DFRobotDFPlayerMini stressDfp;
static bool                dfpOk;
#endif

static const Color COLS[4] = {BLUE, RED, GREEN, YELLOW};

// Format a specific millis() value as uptime string (e.g. "1h05m23s")
static const char* stressUptimeAt(unsigned long ms) {
  static char buf[16];
  unsigned long s = ms / 1000;
  snprintf(buf, sizeof(buf), "%luh%02lum%02lus", s / 3600, (s % 3600) / 60, s % 60);
  return buf;
}

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
  Serial.println("  Button activity:");
  Serial.println("    Channel  Ghost  Press");
  for (int i = 0; i < 4; i++) {
    Serial.printf("    %-7s  %-5lu  %lu\n",
                  hw_led_name(COLS[i]),
                  (unsigned long)ghostCount[i],
                  (unsigned long)legitCount[i]);
  }
  Serial.println("  (PRESS dur <80ms may be a slow ghost that slipped debounce)");
  Serial.println("=========================================");
}

void stress_init() {
  Serial.println("\n=== STRESS TEST MODE ===");
  Serial.println("  LED rotation + DFPlayer audio. Hold any button 3s to stop.");
  Serial.println("  All button events logged with uptime and duration.");

  memset(ghostCount,      0, sizeof(ghostCount));
  memset(legitCount,      0, sizeof(legitCount));
  memset(rawActive,       0, sizeof(rawActive));
  memset(becameDebounced, 0, sizeof(becameDebounced));
  memset(lastStuckLogMs,  0, sizeof(lastStuckLogMs));
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

  startMs     = millis();
  ledIdx      = 0;
  ledTimer    = millis();
  audioTimer  = millis();
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
      hw_btn_reset_edges();  // prevent immediate restart from the hold edge
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

  // Button activity: log ALL events at resolution with uptime-at-start + duration.
  // GHOST = sub-debounce spike.  PRESS = debounce-confirmed (human or slow ghost).
  // Duration context: real human taps are typically >80ms; 50-80ms is a suspicious zone.
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
          legitCount[i]++;
          Serial.printf("[STRESS] @%s  PRESS  %s  dur=%lums  press#%lu\n",
                        stressUptimeAt(rawStartMs[i]), hw_led_name(COLS[i]),
                        dur, (unsigned long)legitCount[i]);
        } else {
          ghostCount[i]++;
          Serial.printf("[STRESS] @%s  GHOST  %s  dur=%lums  ghost#%lu\n",
                        stressUptimeAt(rawStartMs[i]), hw_led_name(COLS[i]),
                        dur, (unsigned long)ghostCount[i]);
        }
        rawActive[i]       = false;
        becameDebounced[i] = false;
        lastStuckLogMs[i]  = 0;
      } else {
        // Periodic log if button stays LOW — catches stuck buttons before hold-exit fires
        unsigned long elapsed = now - rawStartMs[i];
        if (elapsed >= STRESS_STUCK_FIRST_MS) {
          if (lastStuckLogMs[i] == 0 || now - lastStuckLogMs[i] >= STRESS_STUCK_INTERVAL_MS) {
            lastStuckLogMs[i] = now;
            Serial.printf("[STRESS] @%s  %s  still LOW  %lums\n",
                          stressUptimeAt(rawStartMs[i]), hw_led_name(COLS[i]), elapsed);
          }
        }
      }
    }

    hw_btn_edge(COLS[i]);  // consume edge to prevent accumulation in SS_DONE
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
