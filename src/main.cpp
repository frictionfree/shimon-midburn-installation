#include <Arduino.h>
#include "mode_game.h"

// ============================================================
//  Shimon – Top-level orchestrator (Phase 1: Game Mode only)
//
//  Architecture: Hub-and-spoke mode selection.
//  Each mode exposes init() / tick() / stop().
//  Phase 1: no mode selection UI – boots directly into Game Mode.
//  Phase 2: add mode_party.h + mode_diagnostic.h + MODE_SELECTION hub.
// ============================================================

void setup() {
  game_init();
}

void loop() {
  game_tick();
}
