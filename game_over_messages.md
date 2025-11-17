# Personalized Game Over Messages Feature

## Overview
Add difficulty-based personalized game over messages that evaluate player performance and provide contextual feedback before the general game over message.

## Current Behavior
- When game ends (wrong button or timeout), plays game over message (0053.mp3)
- If score > 0, plays score announcement
- Plays post-game invite
- Returns to idle mode

## New Behavior
- When game ends, evaluate player's score against difficulty-specific thresholds
- Play personalized message based on difficulty level and performance
- Short delay (200-400ms)
- Play general game over message (0058.mp3)
- **Longer delay (5-6 seconds)** before post-game invite to let message sink in
- Play post-game invite
- Return to idle mode

**Note:** Score display state is REMOVED - no individual score announcements.

## Audio File Changes

### File Renaming/Reassignment
- **Old:** `0053.mp3` = General game over message
- **New:** `0058.mp3` = General game over message (renamed/moved)

### New Personalized Messages
| File | Difficulty | Condition | Message Content |
|------|-----------|-----------|-----------------|
| `0053.mp3` | Novice | Score ≥ 8 | "Great job! You're ready for Intermediate level. Try the Red button next time!" |
| `0054.mp3` | Intermediate | Score ≥ 8 | "Impressive! Move up to Advanced level - press Green next time!" |
| `0055.mp3` | Advanced | Score ≥ 10 | "Amazing performance! Join us for the finals on Friday afternoon!" |
| `0056.mp3` | Pro | Score ≥ 10 | "Incredible! You're a memory master! See you at the finals Friday afternoon!" |
| `0057.mp3` | All levels | Below threshold | "Good try! Practice makes perfect - play again!" |

### Updated Audio Configuration (`shimon.h`)
```cpp
// Game Over Messages (personalized based on difficulty and score)
constexpr uint8_t AUDIO_GAME_OVER_NOVICE_STRONG = 53;       // 0053.mp3 - Novice, score >= 8
constexpr uint8_t AUDIO_GAME_OVER_INTERMEDIATE_STRONG = 54; // 0054.mp3 - Intermediate, score >= 8
constexpr uint8_t AUDIO_GAME_OVER_ADVANCED_STRONG = 55;     // 0055.mp3 - Advanced, score >= 10
constexpr uint8_t AUDIO_GAME_OVER_PRO_STRONG = 56;          // 0056.mp3 - Pro, score >= 10
constexpr uint8_t AUDIO_GAME_OVER_MEDIOCRE = 57;            // 0057.mp3 - Below threshold for any level
constexpr uint8_t AUDIO_GAME_OVER_GENERAL = 58;             // 0058.mp3 - General game over (plays after personalized)
```

## Score Thresholds

| Difficulty Level | Strong Scorer Threshold | Mediocre Scorer |
|------------------|------------------------|-----------------|
| Novice (Blue)    | Score ≥ 8              | Score < 8       |
| Intermediate (Red) | Score ≥ 8            | Score < 8       |
| Advanced (Green) | Score ≥ 10             | Score < 10      |
| Pro (Yellow)     | Score ≥ 10             | Score < 10      |

## Implementation Plan

### 1. Update Configuration (`include/shimon.h`)
- Remove old `AUDIO_GAME_OVER = 53` constant
- Add new game over message constants (53-58)
- Remove `SCORE_DISPLAY_DURATION_MS` (no longer needed)
- Add `GAME_OVER_MESSAGE_DELAY_MS = 300` for delay between personalized and general message
- **Update `POST_GAME_INVITE_DELAY_MS = 6000`** (increase from 3000ms to 6000ms for longer pause)

### 2. Add Helper Function (`src/main.cpp`)
Create new function to select appropriate game over message:
```cpp
uint8_t getGameOverMessage(DifficultyLevel difficulty, uint8_t score) {
  // Check strong scorer thresholds
  if (difficulty == DIFFICULTY_NOVICE && score >= 8) {
    return AUDIO_GAME_OVER_NOVICE_STRONG;
  }
  if (difficulty == DIFFICULTY_INTERMEDIATE && score >= 8) {
    return AUDIO_GAME_OVER_INTERMEDIATE_STRONG;
  }
  if (difficulty == DIFFICULTY_ADVANCED && score >= 10) {
    return AUDIO_GAME_OVER_ADVANCED_STRONG;
  }
  if (difficulty == DIFFICULTY_PRO && score >= 10) {
    return AUDIO_GAME_OVER_PRO_STRONG;
  }
  // Below threshold - mediocre scorer
  return AUDIO_GAME_OVER_MEDIOCRE;
}
```

### 3. Update Audio Interface (`src/main.cpp`)
- Modify `playGameOver()` methods to accept a message file number parameter
- Add `playGeneralGameOver()` method for the general message (0058.mp3)

### 4. Modify Game States (`src/main.cpp`)

#### Remove State
- **DELETE:** `SCORE_DISPLAY` state (no longer needed)

#### Modify States
**`WRONG_FEEDBACK` state:**
```cpp
case WRONG_FEEDBACK: {
  if (isAudioComplete(stateTimer, FEEDBACK_DURATION_MS)) {
    // Turn off all LEDs
    // Calculate personalized game over message
    uint8_t gameOverMsg = getGameOverMessage(game.difficulty, game.score);
    delay(400);
    audio.playGameOver(gameOverMsg);  // Play personalized message
    stateTimer = now;
    gameState = GAME_OVER;
    Serial.printf("Game over! Difficulty: %d, Score: %d, Message: %d\n",
                  game.difficulty, game.score, gameOverMsg);
  }
  break;
}
```

**`TIMEOUT_FEEDBACK` state:**
```cpp
case TIMEOUT_FEEDBACK: {
  if (isAudioComplete(stateTimer, FEEDBACK_DURATION_MS)) {
    // Turn off all LEDs
    // Calculate personalized game over message
    uint8_t gameOverMsg = getGameOverMessage(game.difficulty, game.score);
    delay(400);
    audio.playGameOver(gameOverMsg);  // Play personalized message
    stateTimer = now;
    gameState = GAME_OVER;
    Serial.printf("Game over! Difficulty: %d, Score: %d, Message: %d\n",
                  game.difficulty, game.score, gameOverMsg);
  }
  break;
}
```

**`GAME_OVER` state:**
```cpp
case GAME_OVER: {
  if (isAudioComplete(stateTimer, GAME_OVER_DURATION_MS)) {
    // Personalized message finished, play general game over
    delay(300);
    audio.playGeneralGameOver();  // Play 0058.mp3
    stateTimer = now;
    gameState = GENERAL_GAME_OVER;  // NEW STATE
    Serial.println("Playing general game over message...");
  }
  break;
}
```

#### Add New State
**`GENERAL_GAME_OVER` state:**
```cpp
case GENERAL_GAME_OVER: {
  if (isAudioComplete(stateTimer, GAME_OVER_DURATION_MS)) {
    // General game over finished, wait longer before post-game invite
    Serial.printf("Game over! Final score: %d\n", game.score);
    Serial.println("Waiting 6000 ms before post-game invite...");
    delay(POST_GAME_INVITE_DELAY_MS);  // 6 second pause

    gameState = POST_GAME_INVITE;
    audio.playRandomInvite();
    playPostGameInviteSequence();
    stateTimer = now;
  }
  break;
}
```

### 5. Update Game State Enum (`src/main.cpp`)
Add new state after `GAME_OVER`:
```cpp
enum GameState {
  // ... existing states ...
  GAME_OVER,
  GENERAL_GAME_OVER,  // NEW: For general game over message (0058.mp3)
  POST_GAME_INVITE,
  // ... rest of states ...
};
```

### 6. Update CLAUDE.md Documentation
- Update audio file structure section with new game over messages (0053-0058)
- Document the removed SCORE_DISPLAY state
- Add new GENERAL_GAME_OVER state to state machine documentation
- Update game flow diagrams with new 6-second delay before invite

## Testing Checklist

### Novice Level Tests
- [ ] Score ≥ 8: Verify 0053.mp3 plays, then 0058.mp3, then **6 second pause**, then invite
- [ ] Score < 8: Verify 0057.mp3 plays, then 0058.mp3, then **6 second pause**, then invite

### Intermediate Level Tests
- [ ] Score ≥ 8: Verify 0054.mp3 plays, then 0058.mp3, then **6 second pause**, then invite
- [ ] Score < 8: Verify 0057.mp3 plays, then 0058.mp3, then **6 second pause**, then invite

### Advanced Level Tests
- [ ] Score ≥ 10: Verify 0055.mp3 plays, then 0058.mp3, then **6 second pause**, then invite
- [ ] Score < 10: Verify 0057.mp3 plays, then 0058.mp3, then **6 second pause**, then invite

### Pro Level Tests
- [ ] Score ≥ 10: Verify 0056.mp3 plays, then 0058.mp3, then **6 second pause**, then invite
- [ ] Score < 10: Verify 0057.mp3 plays, then 0058.mp3, then **6 second pause**, then invite

### Edge Cases
- [ ] Score = 0 (immediate failure): Verify 0057.mp3 → 0058.mp3 → 6s pause → invite
- [ ] Timeout game over: Same message flow as wrong button
- [ ] Audio finish detection works for both messages
- [ ] Delays between messages feel natural (not too long/short)
- [ ] **6-second pause feels appropriate and not awkward**

## Timing Summary

| Transition | Delay | Purpose |
|------------|-------|---------|
| Personalized message → General game over | 300ms | DFPlayer stabilization |
| General game over → Post-game invite | **6000ms** | Let message sink in, create closure |
| Post-game invite → Idle mode | 0ms | Immediate transition |

## File Changes Summary

### Modified Files
- `include/shimon.h` - Update audio constants, remove score display timeout, increase POST_GAME_INVITE_DELAY_MS to 6000ms
- `src/main.cpp` - Add helper function, modify states, add GENERAL_GAME_OVER state
- `CLAUDE.md` - Update documentation

### Audio Files Required
- `0053.mp3` - Novice strong scorer (NEW)
- `0054.mp3` - Intermediate strong scorer (NEW)
- `0055.mp3` - Advanced strong scorer (NEW)
- `0056.mp3` - Pro strong scorer (NEW)
- `0057.mp3` - Mediocre scorer (NEW)
- `0058.mp3` - General game over (RENAMED from 0053.mp3)

## Implementation Notes

1. **Audio finish detection**: Use existing `isAudioComplete()` system with DFPlayer notifications
2. **Short delays (300-400ms)**: Between audio messages to allow DFPlayer to stabilize
3. **Long delay (6000ms)**: After general game over to create closure and let message resonate
4. **Score tracking**: Score is already tracked in `game.score`, just need to access it
5. **Difficulty tracking**: Already stored in `game.difficulty` (DifficultyLevel enum)
6. **Backward compatibility**: Old saves/configs not affected, just audio file changes needed

## Migration Notes

⚠️ **Important**: The existing `0053.mp3` file must be renamed/moved to `0058.mp3` before implementing this feature, otherwise the general game over message will be lost.
