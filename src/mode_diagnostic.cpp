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

#ifndef USE_WOKWI
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#endif

static constexpr uint8_t DIAG_AUDIO_VOL = 20;

#ifndef USE_WOKWI
static HardwareSerial      diagDfpSer(1);
static DFRobotDFPlayerMini diagDfp;
static bool                diagDfpOk;
#endif

static bool wasPressed[4];

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
  if (diagDfpOk) diagDfp.playMp3Folder(track);
#endif
  Serial.printf("[DIAG] AUDIO: %s (track %u)\n", hw_led_name(c), (unsigned)track);
}

void diag_init() {
  Serial.println("\n=== DIAGNOSTIC MODE ===");
  Serial.println("  Press any button: LED on + color audio plays.");
  Serial.println("  Hold YELLOW 3s: return to mode selection.");

  memset(wasPressed, 0, sizeof(wasPressed));
  hw_led_all_off();
  hw_btn_set_fast(true);

  for (int b = 0; b < 2; b++) {
    hw_led_duty(BLUE, 200); delay(200); hw_led_duty(BLUE, 0); delay(150);
  }

#ifndef USE_WOKWI
  diagDfpOk = false;
  diagDfpSer.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (diagDfp.begin(diagDfpSer, true, true)) {
    diagDfpOk = true;
    diagDfp.volume(DIAG_AUDIO_VOL);
    Serial.println("  DFPlayer OK.");
  } else {
    Serial.println("  DFPlayer FAILED — LED/button test still functional.");
  }
#else
  Serial.println("  [SIM] DFPlayer skipped.");
#endif

  Serial.println("[DIAG] Ready.");
}

void diag_tick() {
  for (int i = 0; i < 4; i++) {
    Color c       = (Color)i;
    bool  pressed = hw_btn_pressed(c);

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
#ifndef USE_WOKWI
  if (diagDfpOk) { diagDfp.stop(); diagDfpOk = false; }
  diagDfpSer.end();
#endif
  Serial.println("[DIAG] Mode stopped.");
}
