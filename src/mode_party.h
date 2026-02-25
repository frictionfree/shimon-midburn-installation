#pragma once
// Party Mode module interface.
// Exposes init / tick / stop for integration under the Mode Selection hub.

void party_init();   // One-time setup: LEDC, MIDI UART, I2S driver, initial state
void party_tick();   // Called every loop() iteration while Party Mode is active
void party_stop();   // Release owned peripherals (I2S DMA, MIDI UART1, LEDs off); reset state
