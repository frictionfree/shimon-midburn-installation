#pragma once
// Diagnostic Mode module interface.
// Interactive button/LED/audio tester: press any button to light its LED and play color audio.

void diag_init();   // One-time setup: init DFPlayer, blink entry confirmation
void diag_tick();   // Called every loop() iteration — handles press/release per button
void diag_stop();   // Release peripherals, restore debounce mode
