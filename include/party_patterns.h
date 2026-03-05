#pragma once
#include <stdint.h>
#include "hw.h"

// ---- Context state (audio analysis result, drives pattern selection) ----
enum ContextState : uint8_t {
  STANDARD       = 0,
  BREAK_CANDIDATE = 1,
  BREAK_CONFIRMED = 2,
  DROP           = 3
};

// ---- Pattern IDs ----
enum PatternID : uint8_t {
  PAT_STD_01 = 0,   // Groove Rotation
  PAT_STD_02 = 1,   // Edge Oscillation Walk
  PAT_STD_03 = 2,   // Diagonal Pairs
  PAT_BRK_01 = 3,   // Slow Drift Relay
  PAT_BRK_02 = 4,   // Breathing Anchor
  PAT_BRK_03 = 5,   // Dual Flow Weave
  PAT_DRP_01 = 6,   // Impact Chase
  PAT_DRP_02 = 7,   // Alternating Burst Drive
  PAT_DRP_03 = 8,   // Expanding Impact Wave
  PAT_STD_04 = 9,   // Corner Chase
  PAT_STD_05 = 10,  // Symmetrical Flutter
  PAT_STD_06 = 11,  // Pulsing Cross
  PAT_COUNT  = 12
};

// ---- Info ----
const char* pp_patternName(PatternID p);
const char* pp_ctxName(ContextState s);
PatternID   pp_activePattern();

// ---- Context update (call before pp_onBeat each beat) ----
// state        : current audio analysis context
// beatIntervalUs : microseconds per beat (used for BRK fade durations)
void pp_setContext(ContextState state, uint32_t beatIntervalUs);

// ---- Pattern selection ----
// Fixed pattern, ignores round-robin (used by pattern tester)
void pp_setPattern(PatternID p);
// Round-robin selection for state (used by party mode on state transitions)
void pp_selectForState(ContextState s);

// ---- Beat events ----
// Call pp_setContext() first, then pp_onBeat() each beat.
// bar  : 1..N (monotonic bar counter from party mode, or 1-8 cyclic from tester)
// beat : 1..4
void pp_onBeat(uint8_t bar, uint8_t beat);
void pp_onHalfBeat();

// ---- Render (call every loop tick) ----
// Renders BREAK crossfades and DROP half-beat shimmers continuously.
// STD patterns are rendered on beat events; render is a no-op for VIS_STD.
void pp_render();

// ---- Reset ----
// Clears all visual state (pattern indices, crossfade, drop step, etc.)
// and turns off all LEDs. Call on hard reset or mode entry.
void pp_reset();
