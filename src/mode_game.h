#pragma once
// Game Mode module interface.
// Exposes init / tick / stop for integration under the Mode Selection hub.
// In Phase 1 the thin main.cpp calls these directly (no mode selection yet).

void game_init();   // One-time setup: hardware, audio, boot sequence, game state
void game_tick();   // Called every loop() iteration while Game Mode is active
void game_stop();   // Release owned peripherals (audio off, LEDs off); called before mode switch
