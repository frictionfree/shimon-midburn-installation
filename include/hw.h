#pragma once
#include <Arduino.h>
#include "shimon.h"

// Shared Hardware Abstraction Layer
// Single source of truth for LED PWM and button debounce across all modes.

enum Color : uint8_t { BLUE=0, RED=1, GREEN=2, YELLOW=3, COLOR_COUNT=4 };

extern const uint8_t HW_LED_PIN[4];   // {LED_BLUE, LED_RED, LED_GREEN, LED_YELLOW}
extern const uint8_t HW_BTN_PIN[4];   // {BTN_BLUE, BTN_RED, BTN_GREEN, BTN_YELLOW}
extern const uint8_t HW_LEDC_CH[4];   // {0, 1, 2, 3}

static constexpr uint32_t HW_PWM_FREQ     = 12500;
static constexpr uint8_t  HW_PWM_BITS     = 8;
static constexpr uint8_t  HW_PWM_MIN_DUTY = PWM_MIN_EFFECTIVE_DUTY; // 70

static constexpr uint8_t  HW_BTN_CONSISTENT = 3;   // consecutive matching reads required
static constexpr uint32_t HW_BTN_GHOST_MS   = 50;  // minimum hold time for valid press

void hw_led_init();                       // ledcSetup + ledcAttachPin channels 0-3
void hw_btn_init();                       // INPUT_PULLUP all 4 button pins

void        hw_led_duty(Color c, uint8_t duty);          // set PWM duty (0-255)
void        hw_led_all_off();                             // all duties to 0
void        hw_led_all_set(const uint8_t duties[4]);      // write all 4 channels with global cap
const char* hw_led_name(Color c);                        // "BLUE"/"RED"/"GREEN"/"YELLOW"

// Call hw_btn_update() ONCE per loop tick before any query
void     hw_btn_update();
bool     hw_btn_raw(Color c);        // instantaneous digitalRead — only for release-wait loops
bool     hw_btn_pressed(Color c);    // debounced: held >= HW_BTN_GHOST_MS
bool     hw_btn_edge(Color c);       // true on first tick after confirmed press
bool     hw_btn_any_edge(Color* out);// any button edge; writes color to *out (may be nullptr)
uint32_t hw_btn_held_ms(Color c);    // ms since confirmed press (0 if not pressed)
void     hw_btn_reset_edges();       // clear all edge flags
