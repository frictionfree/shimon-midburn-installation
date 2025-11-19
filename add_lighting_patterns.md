# Add Lighting Patterns - Design Document

## Overview
This document outlines new visually distinct LED lighting patterns for the Shimon game. All patterns are **power-safe** (only one LED on at a time) to prevent power consumption spikes and maintain system stability.

## Current Problem
After fixing simultaneous LED activation, all visual patterns (idle, boot, invite) look too similar - mostly simple sequential chases. We need more variety and visual interest while maintaining power safety.

## Design Principles
1. **Power Safety**: Only one LED strip powered at any time
2. **Visual Distinctiveness**: Each pattern should have unique character
3. **Contextual Appropriateness**: Pattern should match the game state/mood
4. **Performance**: Patterns should not block game logic unnecessarily

---

## Pattern Library

### Category 1: Attention-Grabbing Patterns
**Use for:** Boot, Invite, Game Start - moments requiring player attention

#### 1. Sparkle Burst
**Purpose:** Exciting, unpredictable energy
**Implementation:**
```
- Select random LED from array
- Light for 50-100ms
- Turn off
- Repeat 10-15 times
- No delay between sparkles (continuous rapid effect)
```
**Suggested use:** Boot finale, Game start
**Priority:** HIGH - Most versatile and exciting

#### 2. Accelerating Chase
**Purpose:** Building excitement and anticipation
**Implementation:**
```
- Start with 400ms per LED (RED→BLUE→GREEN→YELLOW)
- Each complete cycle reduces delay by 20%
- Continue for 2-3 complete cycles
- Final cycle at ~50ms per LED (very fast)
```
**Suggested use:** Invite sequence
**Priority:** HIGH - Great energy builder

#### 3. Bouncing Ball
**Purpose:** Playful, engaging motion
**Implementation:**
```
- Forward: RED→BLUE→GREEN→YELLOW (200ms each)
- Reverse: YELLOW→GREEN→BLUE→RED (180ms each, slightly faster)
- Each bounce reduces delay by 10%
- 3-4 complete bounces
```
**Suggested use:** Post-game invite
**Priority:** HIGH - Unique and playful

---

### Category 2: Calm/Ambient Patterns
**Use for:** Idle states, waiting states - creating welcoming atmosphere

#### 4. Gentle Rotation
**Purpose:** Peaceful, meditative flow
**Implementation:**
```
- Smooth rotation: RED→BLUE→GREEN→YELLOW
- Each LED stays on for 600-800ms
- Continuous loop with no pause between colors
```
**Suggested use:** SLOW_CHASE ambient effect
**Priority:** MEDIUM - Current chase is already good

#### 5. Heartbeat Pulse
**Purpose:** Organic, living feel
**Implementation:**
```
- Pattern for each color:
  * LED ON for 100ms
  * LED OFF for 100ms
  * LED ON for 100ms (second beat)
  * LED OFF for 400ms (pause)
- Move to next color and repeat
- Creates "ba-dum...ba-dum..." rhythm
```
**Suggested use:** BREATHING ambient effect
**Priority:** HIGH - Very distinct and organic

#### 6. Gentle Wave
**Purpose:** Natural, flowing motion
**Implementation:**
```
- Varying durations create wave-like rhythm:
  * RED: 200ms
  * BLUE: 300ms
  * GREEN: 250ms
  * YELLOW: 200ms
  * Pause: 150ms before repeating
```
**Suggested use:** Ready-to-start effect
**Priority:** MEDIUM - Subtle improvement

---

### Category 3: Informative Patterns
**Use for:** Instructions, selections - communicating game state

#### 7. Count-Up Pattern
**Purpose:** Show progression/building concept
**Implementation:**
```
Cycle 1: RED (300ms) → all off (200ms)
Cycle 2: RED (150ms) → BLUE (150ms) → all off (200ms)
Cycle 3: RED (100ms) → BLUE (100ms) → GREEN (100ms) → all off (200ms)
Cycle 4: RED (75ms) → BLUE (75ms) → GREEN (75ms) → YELLOW (75ms) → all off (200ms)
Repeat 2-3 times
```
**Suggested use:** Instructions sequence
**Priority:** MEDIUM - Educational feel

#### 8. Ping-Pong
**Purpose:** Show dialogue/choice between options
**Implementation:**
```
- Phase 1: RED (100ms) → off → GREEN (100ms) → off (repeat 3x)
- Pause: 200ms
- Phase 2: BLUE (100ms) → off → YELLOW (100ms) → off (repeat 3x)
- Pause: 200ms
- Repeat entire sequence 2 times
```
**Suggested use:** Difficulty selection
**Priority:** MEDIUM - Clear communication

#### 9. Color Sweep
**Purpose:** Showcase all available colors
**Implementation:**
```
- Forward sweep: RED→BLUE→GREEN→YELLOW (300ms each)
- Pause: 150ms
- Reverse sweep: YELLOW→GREEN→BLUE→RED (300ms each)
- Pause: 150ms
- Repeat 2-3 complete cycles
```
**Suggested use:** Showing all options/colors
**Priority:** LOW - Nice to have

---

### Category 4: Random/Playful Patterns
**Use for:** Adding variety to idle states

#### 10. Random Walk
**Purpose:** Semi-random but organic movement
**Implementation:**
```
- Start with random LED
- Each step: 60% chance pick adjacent LED, 40% chance any LED
- Each LED stays on for 200-400ms (variable)
- 20-30 total steps
- Creates appearance of wandering
```
**Suggested use:** TWINKLE ambient effect
**Priority:** MEDIUM - Better than purely random

#### 11. Skip Pattern
**Purpose:** Mathematical/rhythmic interest
**Implementation:**
```
- Pass 1: Light every other LED
  * RED (200ms) → off → GREEN (200ms) → off
- Pass 2: Fill the gaps
  * BLUE (200ms) → off → YELLOW (200ms) → off
- Pause: 300ms
- Repeat 3 times
```
**Suggested use:** PULSE_WAVE ambient effect
**Priority:** MEDIUM - Distinct rhythm

#### 12. Knock-Knock Pattern
**Purpose:** Personality/communication feel
**Implementation:**
```
For each color in sequence:
- Short pulse (100ms) → off (150ms)
- Short pulse (100ms) → off (300ms)
- Long pulse (400ms) → off (200ms)
Creates "knock knock...answer" rhythm
```
**Suggested use:** Waiting for input states
**Priority:** LOW - Adds personality

---

## Suggested Implementation Mapping

### Current Usage → New Pattern

```
BOOT SEQUENCE:
├─ Rainbow wave (keep as-is) ✓
└─ Sequential flash → Replace with: "Sparkle Burst"

INVITE SEQUENCE:
├─ Sequential flash → Replace with: "Accelerating Chase"
├─ Spinning pattern (keep as-is) ✓
└─ Final flash → Replace with: "Sparkle Burst"

INSTRUCTIONS SEQUENCE:
└─ Current sequential → Replace with: "Count-Up Pattern" or "Ping-Pong"

READY-TO-START:
└─ Rotating pulse → Replace with: "Heartbeat Pulse" or "Gentle Wave"

GAME START:
└─ Sequential bursts → Replace with: "Sparkle Burst"

IDLE AMBIENT EFFECTS (4 modes):
├─ BREATHING → Replace with: "Heartbeat Pulse"
├─ SLOW_CHASE → Keep or use: "Gentle Rotation"
├─ TWINKLE → Replace with: "Random Walk"
└─ PULSE_WAVE → Replace with: "Skip Pattern"

POST-GAME INVITE:
└─ (Uses regular invite) → Could use: "Bouncing Ball"

DIFFICULTY SELECTION:
└─ All flash → Could add: "Ping-Pong" between difficulty choices
```

---

## Implementation Priority

### Phase 1: High Impact (Implement First)
1. **Sparkle Burst** - Boot finale, game start, invite finale
2. **Heartbeat Pulse** - BREATHING ambient (most noticeable improvement)
3. **Accelerating Chase** - Invite sequence (builds excitement)
4. **Bouncing Ball** - Post-game invite (unique character)

### Phase 2: Refinements
5. **Random Walk** - TWINKLE ambient (more organic)
6. **Ping-Pong** - Instructions or difficulty selection
7. **Count-Up Pattern** - Alternative for instructions

### Phase 3: Nice to Have
8. **Gentle Wave** - Ready-to-start alternative
9. **Skip Pattern** - PULSE_WAVE ambient
10. **Color Sweep** - Showcase mode
11. **Knock-Knock** - Personality touches
12. **Gentle Rotation** - Alternative ambient

---

## Technical Implementation Notes

### Code Structure
```cpp
// Add new pattern functions alongside existing ones
void sparkleBurstSequence() {
  // Random LED pattern
}

void acceleratingChaseSequence() {
  // Building speed pattern
}

void heartbeatPulseEffect(unsigned long now) {
  // Ambient heartbeat pattern
}

// etc.
```

### Configuration
Add new timing constants to `shimon.h`:
```cpp
// Pattern-specific timings
constexpr unsigned long SPARKLE_BURST_DELAY_MS = 75;
constexpr unsigned long HEARTBEAT_SHORT_PULSE_MS = 100;
constexpr unsigned long HEARTBEAT_PAUSE_MS = 400;
constexpr unsigned long ACCELERATE_START_MS = 400;
constexpr unsigned long ACCELERATE_MIN_MS = 50;
// etc.
```

### Power Safety Verification
All patterns MUST follow these rules:
1. Only call `setLed(color, true)` for ONE color at a time
2. Always call `setLed(color, false)` before lighting a different LED
3. For rapid sequences, turn off immediately after brief delay
4. Never use loops that light multiple LEDs simultaneously

---

## Testing Checklist

For each implemented pattern:
- [ ] Verify only one LED is ever on at any time
- [ ] Test visual appeal on actual hardware
- [ ] Verify timing feels appropriate for context
- [ ] Check pattern is visually distinct from others
- [ ] Ensure pattern doesn't block game logic excessively
- [ ] Confirm power consumption stays within limits

---

## Future Enhancements

### Possible Additions:
- **Speed variations**: Some patterns could have "slow" and "fast" versions
- **Reverse modes**: Some patterns could reverse direction
- **Intensity levels**: Vary brightness (if hardware supports PWM)
- **Pattern chaining**: Combine patterns for special moments
- **User preferences**: Allow configuration of which patterns to use

### Pattern Combinations:
- Boot: Rainbow wave → Sparkle Burst
- Invite: Accelerating Chase → Sparkle Burst
- Victory: Bouncing Ball → Sparkle Burst
- High score: Special rapid Sparkle Burst (more sparkles)

---

## Notes for Implementation

1. **Start with Phase 1 patterns** - These provide maximum visual impact
2. **Test each pattern individually** before integrating
3. **Use consistent naming convention**: `patternNameSequence()` for blocking patterns, `patternNameEffect()` for ambient
4. **Document timing values** in code comments for easy tuning
5. **Keep old patterns commented out** initially for easy rollback if needed
6. **Get user feedback** on pattern appeal before final commit

---

## Success Criteria

✓ Each game state has visually distinct LED pattern
✓ All patterns maintain power safety (one LED at a time)
✓ Patterns feel appropriate for their context
✓ Visual variety makes game more engaging
✓ No performance degradation from pattern complexity
