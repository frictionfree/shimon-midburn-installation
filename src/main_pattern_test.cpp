// Pattern tester — standalone 120 BPM visual pattern test.
// Only compiled when PATTERN_TEST is defined (env:pattern_test).
// Set PATTERN_TEST to a PatternID integer in platformio.ini.
//
// Pattern ID map:
//   S1=0  S2=1  S3=2  (STANDARD patterns)
//   B1=3  B2=4  B3=5  (BREAK patterns)
//   D1=6  D2=7  D3=8  (DROP patterns)
//
// Usage:  pio run -e pattern_test -t upload && pio device monitor -e pattern_test

#ifdef PATTERN_TEST

#include <Arduino.h>
#include "hw.h"
#include "party_patterns.h"

// 120 BPM: 500 ms/beat, 250 ms/half-beat
static constexpr uint32_t BEAT_US = 500000UL;
static constexpr uint32_t HALF_US = 250000UL;

static uint32_t lastBeatUs  = 0;
static bool     halfFired   = false;
static uint8_t  bar         = 1;
static uint8_t  beat        = 1;

// Derive the appropriate ContextState for the selected pattern family
// so brightness caps and visual mode routing work correctly.
// Add a case here when adding a new STD or BRK pattern; DROP is the default.
static ContextState ctxForPattern(PatternID p) {
  switch (p) {
    case PAT_STD_01: case PAT_STD_02: case PAT_STD_03:
      return STANDARD;
    case PAT_BRK_01: case PAT_BRK_02: case PAT_BRK_03:
      return BREAK_CONFIRMED;
    default:
      return DROP;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  PatternID pat = (PatternID)PATTERN_TEST;
  Serial.printf("\n=== Pattern Tester: %s at 120 BPM ===\n", pp_patternName(pat));
  hw_led_init();
  hw_btn_init();
  pp_setPattern(pat);
  pp_setContext(ctxForPattern(pat), BEAT_US);
  lastBeatUs = micros();
}

void loop() {
  const uint32_t now = micros();
  const uint32_t dt  = now - lastBeatUs;

  if (!halfFired && dt >= HALF_US) {
    halfFired = true;
    pp_onHalfBeat();
  }

  if (dt >= BEAT_US) {
    lastBeatUs += BEAT_US;
    halfFired = false;

    Serial.printf("bar=%u beat=%u\n", bar, beat);
    pp_onBeat(bar, beat);

    beat++;
    if (beat > 4) { beat = 1; bar++; }
    if (bar  > 8) bar = 1;
  }

  pp_render();
}

#endif // PATTERN_TEST
