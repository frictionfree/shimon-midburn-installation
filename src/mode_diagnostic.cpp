/*
  Diagnostic Mode - interactive button/LED/audio tester
  Press any button: correlated LED on, color audio plays, serial log.
  Release: LED off, serial log.
  Hold YELLOW 3s: exit to mode selection (handled by main.cpp).
*/

#include <Arduino.h>
#include "shimon.h"
#include "hw.h"
#include "mode_diagnostic.h"

#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>

static constexpr uint8_t DIAG_AUDIO_VOL = 20;

static HardwareSerial      diagDfpSer(1);
static DFRobotDFPlayerMini diagDfp;
static bool                diagDfpOk;

static bool          wasPressed[4];
static bool          rawActive[4];
static bool          rawDebounced[4];
static unsigned long rawStartMs[4];

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
  if (diagDfpOk) diagDfp.playMp3Folder(track);
  Serial.printf("[DIAG] AUDIO: %s (track %u)\n", hw_led_name(c), (unsigned)track);
}

void diag_init() {
  Serial.println("\n=== DIAGNOSTIC MODE ===");
  Serial.println("  Press any button: LED on + color audio plays.");
  Serial.println("  Hold YELLOW 3s: return to mode selection.");

  memset(wasPressed,    0, sizeof(wasPressed));
  memset(rawActive,     0, sizeof(rawActive));
  memset(rawDebounced,  0, sizeof(rawDebounced));
  hw_led_all_off();
  hw_btn_set_fast(true);

  for (int b = 0; b < 2; b++) {
    hw_led_duty(BLUE, 200); delay(200); hw_led_duty(BLUE, 0); delay(150);
  }

  diagDfpOk = false;
  diagDfpSer.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (diagDfp.begin(diagDfpSer, true, true)) {
    diagDfpOk = true;
    diagDfp.volume(DIAG_AUDIO_VOL);
    Serial.println("  DFPlayer OK.");
  } else {
    Serial.println("  DFPlayer FAILED — LED/button test still functional.");
  }

  Serial.println("[DIAG] Ready.");
}

void diag_tick() {
  unsigned long now = millis();

  for (int i = 0; i < 4; i++) {
    Color c        = (Color)i;
    bool  raw      = hw_btn_raw(c);
    bool  pressed  = hw_btn_pressed(c);

    // Ghost monitoring: raw LOW that resolves without debounce confirmation
    if (!rawActive[i]) {
      if (raw) {
        rawActive[i]    = true;
        rawDebounced[i] = false;
        rawStartMs[i]   = now;
      }
    } else {
      if (pressed) rawDebounced[i] = true;
      if (!raw) {
        if (!rawDebounced[i]) {
          Serial.printf("[DIAG] GHOST: %s  dur=%lums\n",
                        hw_led_name(c), now - rawStartMs[i]);
        }
        rawActive[i]    = false;
        rawDebounced[i] = false;
      }
    }

    // Press / release logging
    if (hw_btn_edge(c)) {
      hw_led_duty(c, 200);
      Serial.printf("[DIAG] PRESS: %s\n", hw_led_name(c));
      playColorAudio(c);
      wasPressed[i] = true;
    }

    if (wasPressed[i] && !pressed) {
      hw_led_duty(c, 0);
      Serial.printf("[DIAG] RELEASE: %s\n", hw_led_name(c));
      wasPressed[i] = false;
    }
  }
}

void diag_stop() {
  hw_led_all_off();
  hw_btn_set_fast(false);
  if (diagDfpOk) { diagDfp.stop(); diagDfpOk = false; }
  diagDfpSer.end();
  Serial.println("[DIAG] Mode stopped.");
}
