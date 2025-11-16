# Mission: Add Difficulty Levels

## Overview
Extend the Shimon game from a single difficulty mode to **four distinct difficulty levels**, each offering unique gameplay mechanics and challenges.

---

## Difficulty Levels Specification

### 🔵 BLUE - "Novice Level"
**Target audience:** Beginners, children, first-time players

**Gameplay:**
- **Confuser mode**: OFF (voice and LED colors always match)
- **Starting sequence**: 1 color
- **Progression**: Cumulative - extends by 1 color each turn
- **Speed progression**: Enabled (accelerates as per current implementation)
- **Audio instructions**: `AUDIO_INSTRUCTIONS_BLUE` (0012.mp3)

**Behavior:**
- LED shows color → Voice says matching color
- Player follows visible LED sequence
- Most forgiving difficulty

---

### 🔴 RED - "Intermediate Level"
**Target audience:** Players comfortable with basic gameplay

**Gameplay:**
- **Confuser mode**: ON (voice calls random misleading colors)
- **LED sequence**: Shows the CORRECT sequence player must follow
- **Voice**: Random colors (intended to confuse)
- **Starting sequence**: 3 colors
- **Progression**: Cumulative - extends by 1 color each turn
- **Speed progression**: Enabled
- **Audio instructions**: `AUDIO_INSTRUCTIONS_RED` (0013.mp3)

**Behavior:**
- LED shows BLUE → Voice might say "Yellow" (ignore voice, follow LED)
- Tests player's ability to focus on visual cues while ignoring audio distractions

---

### 🟢 GREEN - "Advanced Level"
**Target audience:** Experienced players seeking a challenge

**Gameplay:**
- **Confuser mode**: OFF (voice and LED match)
- **Starting sequence**: 3 colors
- **Progression**: **COMPLETELY NEW sequence each turn** (not cumulative!)
- **Speed progression**: DISABLED (difficulty comes from memory challenge)
- **Audio instructions**: `AUDIO_INSTRUCTIONS_GREEN` (0014.mp3)

**Behavior:**
- Turn 1: Remember [Blue, Red, Green] → Repeat
- Turn 2: Remember NEW [Yellow, Blue, Yellow, Red] → Repeat (completely different from turn 1)
- Turn 3: Remember NEW [Green, Green, Blue, Yellow, Red] → Repeat
- Players cannot rely on previous patterns - full memory reset each turn

---

### 🟡 YELLOW - "Pro Level"
**Target audience:** Expert players, ultimate challenge

**Gameplay:**
- **Confuser mode**: N/A (special mixed-mode)
- **Starting sequence**: 1 color
- **Progression**: Cumulative - extends by 1 color each turn
- **Presentation pattern**: **Strictly alternating LED/audio**
  - Even steps (0, 2, 4, ...): LED lights up (SILENT - no audio)
  - Odd steps (1, 3, 5, ...): Audio plays (NO LED - dark)
- **Audio accuracy**: Correct color (when audio plays, it says the right color)
- **Speed progression**: Enabled
- **Audio instructions**: `AUDIO_INSTRUCTIONS_YELLOW` (0015.mp3)

**Example sequence [Blue, Red, Green, Yellow]:**
```
Presentation:
  Step 0: BLUE LED lights (silent)
  Step 1: Audio says "Red" (no LED)
  Step 2: GREEN LED lights (silent)
  Step 3: Audio says "Yellow" (no LED)

Player must respond: Blue, Red, Green, Yellow
```

**Behavior:**
- Players must remember BOTH visual and auditory cues
- Tests multi-sensory memory and integration

---

## User Flow / State Machine Changes

### Current Flow:
```
IDLE → INSTRUCTIONS → AWAIT_START → GAME
```

### New Flow:
```
IDLE
  ↓
INSTRUCTIONS (updated 0011.mp3: "Press a colored button to choose difficulty")
  ↓
DIFFICULTY_SELECTION (new state)
  ↓ (button press detected)
  ↓
DIFFICULTY_INSTRUCTIONS (new state - plays color-specific instructions)
  ↓
AWAIT_START
  ↓
GAME (with difficulty-specific logic)
```

---

## New FSM States

### `DIFFICULTY_SELECTION`
- **Entry**: After main instructions audio completes
- **Visual**: All 4 button LEDs pulsing/glowing to indicate choice available
- **Input**: Wait for any button press (Blue/Red/Green/Yellow)
- **Audio**: Silent (instructions already played)
- **Timeout**: None (wait indefinitely)
- **Exit**: Button pressed → store selected difficulty → transition to `DIFFICULTY_INSTRUCTIONS`

### `DIFFICULTY_INSTRUCTIONS`
- **Entry**: After difficulty selected
- **Visual**: Selected button LED stays ON, others OFF
- **Audio**: Play difficulty-specific instructions:
  - Blue pressed → play `AUDIO_INSTRUCTIONS_BLUE` (0012.mp3)
  - Red pressed → play `AUDIO_INSTRUCTIONS_RED` (0013.mp3)
  - Green pressed → play `AUDIO_INSTRUCTIONS_GREEN` (0014.mp3)
  - Yellow pressed → play `AUDIO_INSTRUCTIONS_YELLOW` (0015.mp3)
- **Duration**: Wait for audio completion (with timeout fallback)
- **Exit**: Audio complete → transition to `AWAIT_START`

---

## Code Changes Required

### 1. **Add New FSM States** (main.cpp)
```cpp
enum GameFSM {
  IDLE,
  INSTRUCTIONS,
  DIFFICULTY_SELECTION,        // NEW
  DIFFICULTY_INSTRUCTIONS,     // NEW
  AWAIT_START,
  // ... rest unchanged
};
```

### 2. **Add Difficulty Enum** (main.cpp or shimon.h)
```cpp
enum DifficultyLevel {
  NOVICE = 0,       // Blue button
  INTERMEDIATE = 1, // Red button
  ADVANCED = 2,     // Green button
  PRO = 3           // Yellow button
};
```

### 3. **Add Difficulty Settings Structure**
```cpp
struct DifficultySettings {
  uint8_t startingSequenceLength;
  bool confuserEnabled;
  bool speedProgressionEnabled;
  bool regenerateSequenceEachTurn;  // For Green level
  bool alternatingLedAudio;         // For Yellow level
};

const DifficultySettings difficultyConfigs[4] = {
  {1, false, true, false, false},  // NOVICE (Blue)
  {3, true, true, false, false},   // INTERMEDIATE (Red)
  {3, false, false, true, false},  // ADVANCED (Green)
  {1, false, true, false, true}    // PRO (Yellow)
};
```

### 4. **Add Global Difficulty Tracker**
```cpp
DifficultyLevel selectedDifficulty = NOVICE;  // Default
```

### 5. **Update Game Initialization** (in `SEQ_DISPLAY_INIT`)
Apply difficulty settings:
```cpp
case SEQ_DISPLAY_INIT: {
  auto settings = difficultyConfigs[selectedDifficulty];

  // Set starting length
  game.level = settings.startingSequenceLength;

  // Generate initial sequence
  if (settings.regenerateSequenceEachTurn) {
    // Green level - generate fresh sequence
    generateNewSequence(game.level);
  } else {
    // Blue/Red/Yellow - start fresh and extend incrementally
    generateNewSequence(game.level);
  }

  // Apply confuser setting
  ENABLE_AUDIO_CONFUSER = settings.confuserEnabled;

  // ... rest of initialization
}
```

### 6. **Modify Sequence Display Logic** (SEQ_DISPLAY)
For Yellow level, alternate LED/audio:
```cpp
case SEQ_DISPLAY: {
  if (!ledOn) {
    Color ledColor = (Color)game.seq[currentStep];

    if (difficultyConfigs[selectedDifficulty].alternatingLedAudio) {
      // Yellow level - alternating presentation
      if (currentStep % 2 == 0) {
        // Even step: LED only (no audio)
        setLed(ledColor, true);
        // Skip audio playback
      } else {
        // Odd step: Audio only (no LED)
        audio.playColorName(ledColor);  // Correct color
        // Skip LED activation
      }
    } else {
      // Blue/Red/Green - normal presentation
      setLed(ledColor, true);
      Color voiceColor = generateConfuserColor(ledColor);
      audio.playColorName(voiceColor);
    }
    // ... rest
  }
}
```

### 7. **Modify Level Progression** (after CORRECT_FEEDBACK)
```cpp
case CORRECT_FEEDBACK: {
  if (isAudioComplete(...)) {
    auto settings = difficultyConfigs[selectedDifficulty];

    if (settings.regenerateSequenceEachTurn) {
      // Green level - generate completely new sequence
      game.level++;  // Increase length
      generateNewSequence(game.level);  // Fresh random sequence
    } else {
      // Blue/Red/Yellow - extend existing sequence
      extendSequence();
    }

    // Apply speed progression only if enabled
    if (settings.speedProgressionEnabled && game.level % 3 == 0) {
      game.cueOnMs = max(CUE_ON_MS_MIN, (unsigned long)(game.cueOnMs * SPEED_STEP));
      // ...
    }

    gameState = SEQ_DISPLAY_INIT;
  }
}
```

### 8. **Add Audio Functions** (Audio struct)
```cpp
void playDifficultyInstructions(DifficultyLevel difficulty) {
  uint8_t instructionFiles[] = {
    AUDIO_INSTRUCTIONS_BLUE,    // 12
    AUDIO_INSTRUCTIONS_RED,     // 13
    AUDIO_INSTRUCTIONS_GREEN,   // 14
    AUDIO_INSTRUCTIONS_YELLOW   // 15
  };

  if (!initialized) return;
  uint8_t fileNumber = instructionFiles[difficulty];
  currentPlayingTrack = fileNumber;
  dfPlayer.playMp3Folder(fileNumber);
  Serial.printf("[AUDIO] Difficulty instructions (%s) from /mp3/%04d.mp3\n",
                difficulty == NOVICE ? "Blue/Novice" :
                difficulty == INTERMEDIATE ? "Red/Intermediate" :
                difficulty == ADVANCED ? "Green/Advanced" : "Yellow/Pro",
                fileNumber);
}
```

### 9. **Implement New States**

#### DIFFICULTY_SELECTION State:
```cpp
case DIFFICULTY_SELECTION: {
  // Visual feedback: pulse all 4 button LEDs
  static unsigned long pulseTimer = 0;
  if (now - pulseTimer > 500) {
    static bool pulseState = false;
    pulseState = !pulseState;
    for (int i = 0; i < COLOR_COUNT; i++) {
      setBtnLed((Color)i, pulseState);
    }
    pulseTimer = now;
  }

  // Wait for button press
  if (anyButtonPressed()) {
    selectedDifficulty = (DifficultyLevel)lastButtonPressed;

    // Turn off all button LEDs except selected
    for (int i = 0; i < COLOR_COUNT; i++) {
      setBtnLed((Color)i, i == selectedDifficulty);
    }

    Serial.printf("Difficulty selected: %d (%s)\n", selectedDifficulty,
                  selectedDifficulty == NOVICE ? "Blue/Novice" :
                  selectedDifficulty == INTERMEDIATE ? "Red/Intermediate" :
                  selectedDifficulty == ADVANCED ? "Green/Advanced" : "Yellow/Pro");

    audioFinished = false;
    audio.playDifficultyInstructions(selectedDifficulty);
    stateTimer = now;
    gameState = DIFFICULTY_INSTRUCTIONS;
  }
  break;
}
```

#### DIFFICULTY_INSTRUCTIONS State:
```cpp
case DIFFICULTY_INSTRUCTIONS: {
  // Keep selected button LED on
  setBtnLed((Color)selectedDifficulty, true);

  // Wait for audio to finish
  if (isAudioComplete(stateTimer, INSTRUCTIONS_DURATION_MS)) {
    // Turn off selected button LED
    for (int i = 0; i < COLOR_COUNT; i++) {
      setBtnLed((Color)i, false);
    }

    stateTimer = now;
    gameState = AWAIT_START;
    Serial.println("Press any button to start game...");
  }
  break;
}
```

### 10. **Update INSTRUCTIONS State Transition**
```cpp
case INSTRUCTIONS: {
  if (isAudioComplete(stateTimer, INSTRUCTIONS_DURATION_MS)) {
    // Turn off all LEDs
    for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);

    stateTimer = now;
    gameState = DIFFICULTY_SELECTION;  // NEW: go to difficulty selection
    Serial.println("Select difficulty: Press Blue/Red/Green/Yellow button");
  }
  break;
}
```

### 11. **Add Sequence Generation Functions**
```cpp
void generateNewSequence(uint8_t length) {
  // Generate completely new random sequence
  for (int i = 0; i < length; i++) {
    game.seq[i] = generateNextColor(i > 0 ? game.seq[i-1] : 255);
  }
  Serial.printf("Generated new sequence of length %d\n", length);
}
```

---

## Audio File Requirements

### Updated Files:
- **0011.mp3**: Updated instructions announcing difficulty selection
  - *"Welcome to Shimon! Press a colored button to choose your difficulty..."*

### New Files:
- **0012.mp3**: `AUDIO_INSTRUCTIONS_BLUE` - Novice level instructions
  - *"Blue level - Novice mode. Follow the lights and sounds. Good luck!"*
- **0013.mp3**: `AUDIO_INSTRUCTIONS_RED` - Intermediate level instructions
  - *"Red level - Intermediate. Ignore the voice, follow the lights!"*
- **0014.mp3**: `AUDIO_INSTRUCTIONS_GREEN` - Advanced level instructions
  - *"Green level - Advanced. Each round is a new sequence. Remember carefully!"*
- **0015.mp3**: `AUDIO_INSTRUCTIONS_YELLOW` - Pro level instructions
  - *"Yellow level - Pro mode. Remember both lights and sounds. Ultimate challenge!"*

---

## Testing Plan

### Test Scenarios:

**1. Blue (Novice) Level:**
- ✅ Starts with 1 color
- ✅ Voice and LED match
- ✅ Extends by 1 each turn
- ✅ Speed increases
- ✅ Button feedback plays on correct press

**2. Red (Intermediate) Level:**
- ✅ Starts with 3 colors
- ✅ Voice is random/different from LED
- ✅ Following LED sequence = correct
- ✅ Following voice = wrong
- ✅ Speed increases

**3. Green (Advanced) Level:**
- ✅ Starts with 3 colors
- ✅ Each turn is completely new sequence
- ✅ Speed does NOT increase
- ✅ Sequence length grows by 1 each turn
- ✅ Voice and LED match

**4. Yellow (Pro) Level:**
- ✅ Starts with 1 color
- ✅ Even steps: LED only (silent)
- ✅ Odd steps: Audio only (dark)
- ✅ Audio says correct color
- ✅ Player must remember both
- ✅ Speed increases

**5. State Flow:**
- ✅ IDLE → INSTRUCTIONS → DIFFICULTY_SELECTION
- ✅ Button press in DIFFICULTY_SELECTION → DIFFICULTY_INSTRUCTIONS
- ✅ DIFFICULTY_INSTRUCTIONS → AWAIT_START → GAME
- ✅ Selected difficulty persists through game
- ✅ Game over → returns to IDLE (difficulty reset on next game)

---

## Edge Cases & Considerations

1. **Difficulty Persistence:**
   - Should difficulty reset after game over, or remember last selection?
   - **Recommendation**: Reset to DIFFICULTY_SELECTION each new game

2. **Yellow Level Edge Case:**
   - Sequence length 1: Only LED shows (even step 0)
   - Sequence length 2: LED (step 0), Audio (step 1)
   - Works correctly with alternating pattern

3. **Green Level Memory:**
   - Max sequence length still 64 (MAX_SEQUENCE_LENGTH)
   - May reach very long sequences since it doesn't rely on incremental memory

4. **Speed Limits:**
   - Green level: No speed changes (always default)
   - Blue/Red/Yellow: Use existing MIN values

5. **Confuser Compatibility:**
   - Only Red level uses confuser
   - Yellow level has its own "confusion" via alternating presentation

---

## Implementation Checklist

- [ ] Add `DifficultyLevel` enum
- [ ] Add `DifficultySettings` struct and configs array
- [ ] Add `DIFFICULTY_SELECTION` and `DIFFICULTY_INSTRUCTIONS` states
- [ ] Add `selectedDifficulty` global variable
- [ ] Implement `playDifficultyInstructions()` audio function
- [ ] Update `INSTRUCTIONS` state to transition to `DIFFICULTY_SELECTION`
- [ ] Implement `DIFFICULTY_SELECTION` state logic (button LEDs pulsing)
- [ ] Implement `DIFFICULTY_INSTRUCTIONS` state logic
- [ ] Add `generateNewSequence()` function for Green level
- [ ] Modify `SEQ_DISPLAY_INIT` to apply difficulty settings
- [ ] Modify `SEQ_DISPLAY` to handle Yellow level alternating pattern
- [ ] Modify `CORRECT_FEEDBACK` to handle Green level regeneration
- [ ] Modify speed progression to check difficulty settings
- [ ] Update CLAUDE.md documentation
- [ ] Test all 4 difficulty levels
- [ ] Create/update audio files (0011-0015.mp3)

---

## Potential Future Enhancements

- **Difficulty mixing**: Combine mechanics (e.g., Red + Yellow = confuser + alternating)
- **Score multipliers**: Higher difficulty = more points
- **Difficulty badges**: Track highest level completed
- **Custom difficulty**: Let users configure individual parameters
- **Tournament mode**: Fixed seed for competitive play

---

**Document Status:** Planning phase - awaiting approval before implementation
