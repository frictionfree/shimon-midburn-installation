# Multiple Audio Feedbacks Feature

**Feature Status**: Planning Phase - Documentation Only  
**Branch**: `multiple-audio-feedbacks`  
**Target**: Midburn 2025 Shimon Installation  

---

## Overview

This document outlines the design and implementation plan for adding multiple audio feedback variations to the Shimon Butterfly Simon Says game. The goal is to enhance player engagement by providing varied audio responses instead of repeating the same sounds throughout gameplay.

## Current State

The game currently has multiple invite audio files (0001-0005.mp3) which work well for variety. However, the following frequently-used messages have only single audio versions:

- **"My Turn"**: `/mp3/0007.mp3` - Single version, plays before every sequence display
- **"Your Turn"**: `/mp3/0008.mp3` - Single version, plays before every input phase  
- **Positive Feedback**: `/mp3/0011.mp3` - Single version, plays after every correct sequence

These three messages are heard repeatedly during gameplay, creating monotony for players.

## Mission: Multiple Audio Feedbacks

### Feature Goals
1. **Extend "My Turn" messages** - Create 5 different versions of the sequence announcement
2. **Extend "Your Turn" messages** - Create 5 different versions of the input prompt
3. **Extend Positive Feedback** - Create 5 different versions of correct sequence confirmation
4. **Random selection** - Play different version each time to reduce repetition
5. **Maintain timing** - Keep existing game flow and state machine timing unchanged

### Specific Requirements

#### Target Messages for Extension
1. **"My Turn" (currently 0007.mp3)**
   - 5 variations of sequence display announcement
   - Examples: "My turn!", "Watch carefully!", "Here's the pattern!", "Follow this sequence!", "Pay attention!"

2. **"Your Turn" (currently 0008.mp3)**  
   - 5 variations of input prompt
   - Examples: "Your turn!", "Now you try!", "Repeat the pattern!", "Show me the sequence!", "Your move!"

3. **Positive Feedback (currently 0011.mp3)**
   - 5 variations of correct sequence confirmation
   - Examples: "Great job!", "Perfect!", "Excellent!", "Well done!", "Correct!"

#### Implementation Requirements
- **Random selection**: Choose randomly from 5 versions each time
- **No immediate repetition**: Avoid playing the same version twice in a row (if possible)
- **Backward compatibility**: Maintain existing audio file numbering for other sounds
- **Consistent timing**: All variations should have similar duration (~1-2 seconds)

---

## Technical Implementation Plan

### Audio File Structure Changes

#### Proposed Structure: Direct MP3 Files (Not Folders)
```
mp3/
├── 0021.mp3          # "My Turn" variation 1
├── 0022.mp3          # "My Turn" variation 2  
├── 0023.mp3          # "My Turn" variation 3
├── 0024.mp3          # "My Turn" variation 4
├── 0025.mp3          # "My Turn" variation 5
├── 0031.mp3          # "Your Turn" variation 1
├── 0032.mp3          # "Your Turn" variation 2
├── 0033.mp3          # "Your Turn" variation 3
├── 0034.mp3          # "Your Turn" variation 4
├── 0035.mp3          # "Your Turn" variation 5
├── 0041.mp3          # "Positive Feedback" variation 1
├── 0042.mp3          # "Positive Feedback" variation 2
├── 0043.mp3          # "Positive Feedback" variation 3
├── 0044.mp3          # "Positive Feedback" variation 4
└── 0045.mp3          # "Positive Feedback" variation 5
```

#### Existing Files Remain Unchanged
```
mp3/
├── 0001-0005.mp3     # Invite messages (already multiple)
├── 0006.mp3          # Instructions  
├── 0007.mp3          # "My Turn" (original - can be kept as fallback)
├── 0008.mp3          # "Your Turn" (original - can be kept as fallback)
├── 0009.mp3          # Wrong button press
├── 0010.mp3          # Game over
├── 0011.mp3          # Positive feedback (original - can be kept as fallback)
├── 0012.mp3          # Timeout notification
01/                    # Color names (unchanged)
└── 02/               # Score announcements (unchanged)
```

### Code Changes Required

#### Configuration Updates (`shimon.h`)
```cpp
// Multiple audio variations configuration
constexpr uint8_t MYTURN_BASE = 21;            // "My Turn" base file number (0021-0025)
constexpr uint8_t MYTURN_COUNT = 5;            // Number of "My Turn" variations

constexpr uint8_t YOURTURN_BASE = 31;          // "Your Turn" base file number (0031-0035)  
constexpr uint8_t YOURTURN_COUNT = 5;          // Number of "Your Turn" variations

constexpr uint8_t POSITIVE_BASE = 41;          // Positive feedback base file number (0041-0045)
constexpr uint8_t POSITIVE_COUNT = 5;          // Number of positive feedback variations

// Anti-repetition tracking (optional)
constexpr bool ENABLE_ANTI_REPETITION = true;  // Avoid immediate repetition
```

#### Audio Function Updates (`main.cpp`)

**Replace existing single file calls:**
- `playAudio(AUDIO_MY_TURN)` → `playMyTurnVariation()`
- `playAudio(AUDIO_YOUR_TURN)` → `playYourTurnVariation()`
- `playAudio(AUDIO_CORRECT)` → `playPositiveFeedbackVariation()`

**New Functions to Implement:**
```cpp
void playMyTurnVariation();        // Randomly select from 0021-0025.mp3
void playYourTurnVariation();      // Randomly select from 0031-0035.mp3  
void playPositiveFeedbackVariation(); // Randomly select from 0041-0045.mp3
```

#### Random Selection Logic
```cpp
// Global variables for anti-repetition (optional)
uint8_t lastMyTurn = 0;
uint8_t lastYourTurn = 0;
uint8_t lastPositive = 0;

// Example implementation
void playMyTurnVariation() {
    uint8_t selection;
    do {
        selection = random(0, MYTURN_COUNT);  // 0-4
    } while (ENABLE_ANTI_REPETITION && selection == lastMyTurn && MYTURN_COUNT > 1);
    
    lastMyTurn = selection;
    uint8_t fileNumber = MYTURN_BASE + selection;  // 21-25
    playAudio(fileNumber);  // Play 0021.mp3 to 0025.mp3
}

void playYourTurnVariation() {
    uint8_t selection;
    do {
        selection = random(0, YOURTURN_COUNT);  // 0-4
    } while (ENABLE_ANTI_REPETITION && selection == lastYourTurn && YOURTURN_COUNT > 1);
    
    lastYourTurn = selection;
    uint8_t fileNumber = YOURTURN_BASE + selection;  // 31-35
    playAudio(fileNumber);  // Play 0031.mp3 to 0035.mp3
}

void playPositiveFeedbackVariation() {
    uint8_t selection;
    do {
        selection = random(0, POSITIVE_COUNT);  // 0-4
    } while (ENABLE_ANTI_REPETITION && selection == lastPositive && POSITIVE_COUNT > 1);
    
    lastPositive = selection;
    uint8_t fileNumber = POSITIVE_BASE + selection;  // 41-45
    playAudio(fileNumber);  // Play 0041.mp3 to 0045.mp3
}
```

---

## Implementation Considerations

### Advantages
- **Targeted improvement** - Focuses on the 3 most repetitive messages
- **Simple implementation** - Uses existing DFPlayer folder structure
- **Maintains compatibility** - No changes to existing audio files or game flow
- **Easy testing** - Can test each message type independently

### Technical Considerations
- **Storage impact** - Adds 15 audio files (3 types × 5 variations each)
- **File numbering** - Uses mp3 files 0021-0025, 0031-0035, 0041-0045 (sequential ranges)
- **Timing consistency** - All variations must have similar duration (~1-2 seconds)
- **Anti-repetition logic** - Simple tracking to avoid immediate repeats
- **Simpler implementation** - Direct file numbering (no folder structure needed)

### Migration Strategy
- **Phase 1**: Create 15 new mp3 files with specified numbering
- **Phase 2**: Update configuration constants in `shimon.h`
- **Phase 3**: Implement new playback functions in `main.cpp`
- **Phase 4**: Replace function calls in state machine
- **Phase 5**: Test with both simulation and hardware

---

## Questions for Clarification

1. **Audio Content**: Do you want to provide specific text for the 15 audio files, or should I proceed with the examples shown above?

2. **Voice Style**: Should all variations maintain the same voice/tone as existing files, or can they vary in enthusiasm/energy?

3. **Anti-Repetition**: Do you want to implement simple "don't repeat the last one" logic, or pure random selection?

4. **Fallback Strategy**: If a folder or file is missing, should it fall back to the original single file (0007.mp3, 0008.mp3, 0011.mp3)?

5. **Testing Priority**: Should we implement simulation support first, or focus on hardware testing?

---

## Implementation Plan

1. **Mission Approval** - Confirm the 3-message, 5-variation approach
2. **Audio Content Creation** - Record or source 15 new audio files
3. **SD Card Structure** - Create folders 07/, 08/, 09/ with files 001-005.mp3
4. **Code Implementation**:
   - Update `shimon.h` with new constants
   - Add variation selection functions to `main.cpp`  
   - Replace single audio calls in state machine
5. **Testing & Validation**:
   - Simulation testing with debug output
   - Hardware testing with DFPlayer Mini
   - Verify timing and anti-repetition logic

---

**Status**: ✅ IMPLEMENTED - Multiple audio feedback variations are now active

## Implementation Summary

✅ **Configuration Added**: New constants in `shimon.h` for 3 message types  
✅ **Selection Functions**: Anti-repetition logic with fallback implemented  
✅ **Audio Integration**: Both simulation and hardware versions updated  
✅ **State Machine**: All function calls replaced in game logic  
✅ **Build Verification**: Both `sim` and `hardware` environments compile successfully

## Audio Files Required

The following 15 new audio files need to be created and added to the SD card:

### "My Turn" Variations (0021-0025.mp3)
- `0021.mp3` - "My turn!"
- `0022.mp3` - "Watch carefully!" 
- `0023.mp3` - "Here's the pattern!"
- `0024.mp3` - "Follow this sequence!"
- `0025.mp3` - "Pay attention!"

### "Your Turn" Variations (0031-0035.mp3)  
- `0031.mp3` - "Your turn!"
- `0032.mp3` - "Now you try!"
- `0033.mp3` - "Repeat the pattern!"
- `0034.mp3` - "Show me the sequence!"
- `0035.mp3` - "Your move!"

### Positive Feedback Variations (0041-0045.mp3)
- `0041.mp3` - "Great job!"
- `0042.mp3` - "Perfect!"
- `0043.mp3` - "Excellent!"
- `0044.mp3` - "Well done!"
- `0045.mp3` - "Correct!"

## Debug Output

The system now logs detailed selection information:
```
[AUDIO] Selected My Turn variation 3 (file 0023.mp3), avoiding last: 1
[AUDIO] My Turn variation from /mp3/0023.mp3 (DFPlayer.play(23))
```

## Ready for Testing

- **Simulation**: `pio run -e sim` - Shows variation selection in Serial output
- **Hardware**: `pio run -e hardware` - Plays actual audio files with DFPlayer Mini