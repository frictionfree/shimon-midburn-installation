#pragma once
// Diagnostic Mode module interface.
// Runs a 5-phase hardware verification sequence (LED / Buttons / MIDI / I2S / DFPlayer).

void diag_init();   // One-time setup: init LEDC, reset state, begin Phase A
void diag_tick();   // Advances diagnostic state machine; call every loop() iteration
void diag_stop();   // Release any open peripherals; reset state
