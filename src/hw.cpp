#include "hw.h"

const uint8_t HW_LED_PIN[4] = {LED_BLUE,  LED_RED,  LED_GREEN,  LED_YELLOW};
const uint8_t HW_BTN_PIN[4] = {BTN_BLUE,  BTN_RED,  BTN_GREEN,  BTN_YELLOW};
const uint8_t HW_LEDC_CH[4] = {0, 1, 2, 3};

struct BtnState {
  bool     raw;           // last raw digitalRead value
  uint8_t  consistCount;  // consecutive reads matching candidate
  bool     candidate;     // current candidate value (true = pressed)
  uint32_t candidateMs;   // millis() when candidate was last set
  bool     pressed;       // confirmed debounced + ghost-filtered state
  bool     edge;          // true for exactly one tick after press confirmation
  uint32_t pressedMs;     // millis() when press was confirmed
};

static BtnState btn[4] = {};
static bool s_fastInput = false;

void hw_led_init() {
  for (int i = 0; i < 4; i++) {
    ledcSetup(HW_LEDC_CH[i], HW_PWM_FREQ, HW_PWM_BITS);
    ledcAttachPin(HW_LED_PIN[i], HW_LEDC_CH[i]);
    ledcWrite(HW_LEDC_CH[i], 0);
  }
}

void hw_btn_init() {
  for (int i = 0; i < 4; i++) {
    pinMode(HW_BTN_PIN[i], INPUT_PULLUP);
    btn[i] = {};
  }
}

void hw_led_duty(Color c, uint8_t duty) {
  ledcWrite(HW_LEDC_CH[c], duty);
}

void hw_led_all_off() {
  for (int i = 0; i < 4; i++) ledcWrite(HW_LEDC_CH[i], 0);
}

void hw_led_all_set(const uint8_t duties[4]) {
  uint16_t sum = (uint16_t)duties[0] + duties[1] + duties[2] + duties[3];
  if (sum > HW_GLOBAL_DUTY_CAP && sum > 0) {
    float scale = (float)HW_GLOBAL_DUTY_CAP / (float)sum;
    for (int i = 0; i < 4; i++)
      ledcWrite(HW_LEDC_CH[i], (uint8_t)(duties[i] * scale + 0.5f));
  } else {
    for (int i = 0; i < 4; i++)
      ledcWrite(HW_LEDC_CH[i], duties[i]);
  }
}

const char* hw_led_name(Color c) {
  switch (c) {
    case BLUE:   return "BLUE";
    case RED:    return "RED";
    case GREEN:  return "GREEN";
    case YELLOW: return "YELLOW";
    default:     return "?";
  }
}

void hw_btn_set_fast(bool fast) { s_fastInput = fast; }

void hw_btn_update() {
  uint32_t now = (uint32_t)millis();
  uint32_t ghostMs = s_fastInput ? HW_BTN_GHOST_MS_FAST : HW_BTN_GHOST_MS_STANDARD;
  for (int i = 0; i < 4; i++) {
    // Clear edge from previous tick
    btn[i].edge = false;

    // Read raw state (LOW = pressed with INPUT_PULLUP)
    bool raw = (digitalRead(HW_BTN_PIN[i]) == LOW);
    btn[i].raw = raw;

    // Update consistency counter
    if (raw == btn[i].candidate) {
      // Matches candidate: increment consistency (capped)
      if (btn[i].consistCount < HW_BTN_CONSISTENT) btn[i].consistCount++;
    } else {
      // Different from candidate: start new candidate
      btn[i].candidate    = raw;
      btn[i].consistCount = 1;
      btn[i].candidateMs  = now;
    }

    // Act when consistency is established
    if (btn[i].consistCount >= HW_BTN_CONSISTENT) {
      if (btn[i].candidate) {
        // Candidate = pressed: confirm after ghost filter time
        if (!btn[i].pressed && (now - btn[i].candidateMs >= ghostMs)) {
          btn[i].pressed   = true;
          btn[i].edge      = true;
          btn[i].pressedMs = now;
        }
      } else {
        // Candidate = released: release immediately (no ghost delay on release)
        btn[i].pressed   = false;
        btn[i].pressedMs = 0;
      }
    }
  }
}

bool hw_btn_raw(Color c) {
  // Fresh digitalRead for use in blocking release-wait loops
  return digitalRead(HW_BTN_PIN[c]) == LOW;
}

bool hw_btn_pressed(Color c) {
  return btn[c].pressed;
}

bool hw_btn_edge(Color c) {
  return btn[c].edge;
}

bool hw_btn_any_edge(Color* out) {
  for (int i = 0; i < 4; i++) {
    if (btn[i].edge) {
      if (out) *out = (Color)i;
      return true;
    }
  }
  return false;
}

uint32_t hw_btn_held_ms(Color c) {
  if (!btn[c].pressed) return 0;
  return (uint32_t)millis() - btn[c].pressedMs;
}

void hw_btn_reset_edges() {
  for (int i = 0; i < 4; i++) btn[i].edge = false;
}
