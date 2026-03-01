#include <Arduino.h>
#include "shimon.h"
#include "hw.h"
#include "mode_game.h"

#ifndef USE_WOKWI
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#endif

// --- Difficulty Level System ---
enum DifficultyLevel {
  NOVICE = 0,       // Blue button - Basic mode
  INTERMEDIATE = 1, // Red button - Confuser mode
  ADVANCED = 2,     // Green button - New sequence each turn
  PRO = 3           // Yellow button - Alternating LED/audio
};

struct DifficultySettings {
  uint8_t startingSequenceLength;
  bool confuserEnabled;
  bool regenerateSequenceEachTurn;  // For Green level - generates completely new sequence each turn
  bool alternatingLedAudio;         // For Yellow level - alternates LED and audio presentation
};

const DifficultySettings difficultyConfigs[4] = {
  {1, false, false, false},  // NOVICE (Blue) - starts at 1, extends cumulatively
  {3, true, false, false},   // INTERMEDIATE (Red) - confuser mode, starts at 3
  {3, false, true, false},   // ADVANCED (Green) - new sequence each turn, starts at 3
  {3, false, false, true}    // PRO (Yellow) - alternating LED/audio, starts at 3
};

DifficultyLevel selectedDifficulty = NOVICE;  // Default

// --- Game Data Structure ---
struct GameState {
  uint8_t seq[MAX_SEQUENCE_LENGTH]; // Color sequence storage
  uint8_t level;             // Current level (sequence length)
  uint8_t score;             // Player score
  uint8_t strikes;           // Number of mistakes
  // Timing values removed - now calculated on-the-fly based on level
};

// --- Runtime Configurable Features ---
bool ENABLE_AUDIO_CONFUSER = false; // Default: confuser mode OFF (configured automatically based on difficulty level)

// ---- Audio Playback Tracking ----
// Audio playback tracking (declared here for global access)
bool audioFinished = false;
unsigned long audioStartTime = 0;
int currentPlayingTrack = -1;  // Track which file is currently playing (-1 = none)

// --- Multiple Audio Variations Tracking ---
// Global variables for variation tracking (255 = none played yet)
uint8_t lastMyTurn = 255;      // Track last "My Turn" variation
uint8_t lastYourTurn = 255;    // Track last "Your Turn" variation  
uint8_t lastPositive = 255;    // Track last positive feedback variation

// --- Variation Selection Helper Function ---
uint8_t selectVariationWithFallback(uint8_t base, uint8_t count, uint8_t& lastPlayed, const char* category) {
    if (count == 0) {
        Serial.printf("[AUDIO] No variations available for %s, using base file %d\n", category, base);
        return base;
    }
    
    uint8_t selection;
    do {
        selection = random(0, count);
    } while (ENABLE_ANTI_REPETITION && count > 1 && selection == lastPlayed);
    
    lastPlayed = selection;
    uint8_t fileNumber = base + selection;
    
    Serial.printf("[AUDIO] Selected %s variation %d (file %04d.mp3), avoiding last: %d\n", 
                  category, selection, fileNumber, (lastPlayed == 255) ? -1 : (int)(lastPlayed));
    
    return fileNumber;
}

// ---- Forward Declarations for Visual Pattern Functions ----
void clockwiseRotation(uint8_t cycles, uint16_t delayMs);
void sparkleBurstSequence(uint8_t sparkleCount, uint8_t delayMs);
void diagonalCrossPattern(uint8_t cycles, uint16_t delayMs);
void acceleratingChaseSequence();

// ---- Enhanced Audio System ----
#ifdef USE_WOKWI
// Simulation version - prints to Serial
struct Audio {
  void begin() {
    Serial.println("[AUDIO] DFPlayer initialized (simulation)");
  }
  
  void playInvite() {
    uint8_t inviteNum = random(1, AUDIO_INVITE_COUNT + 1); // Files 0001-000X.mp3
    Serial.printf("[AUDIO] Playing invite %d from /mp3/%04d.mp3\n", inviteNum, inviteNum);
  }
  
  void playInstructions() {
    Serial.printf("[AUDIO] Playing instructions from /mp3/%04d.mp3\n", AUDIO_INSTRUCTIONS);
    audioFinished = false;
  }

  void playDifficultyInstructions(DifficultyLevel difficulty) {
    uint8_t instructionFiles[] = {
      AUDIO_INSTRUCTIONS_BLUE,    // 12
      AUDIO_INSTRUCTIONS_RED,     // 13
      AUDIO_INSTRUCTIONS_GREEN,   // 14
      AUDIO_INSTRUCTIONS_YELLOW   // 15
    };
    const char* difficultyNames[] = {"Blue/Novice", "Red/Intermediate", "Green/Advanced", "Yellow/Pro"};
    Serial.printf("[AUDIO] Difficulty instructions (%s) from /mp3/%04d.mp3\n",
                  difficultyNames[difficulty], instructionFiles[difficulty]);
    audioFinished = false;
  }

  void playMyTurn() {
    uint8_t fileNumber = selectVariationWithFallback(MYTURN_BASE, MYTURN_COUNT, lastMyTurn, "My Turn");
    Serial.printf("[AUDIO] My Turn variation from /mp3/%04d.mp3 (simulation)\n", fileNumber);
    audioFinished = false;
  }
  
  void playYourTurn() {
    uint8_t fileNumber = selectVariationWithFallback(YOURTURN_BASE, YOURTURN_COUNT, lastYourTurn, "Your Turn");
    Serial.printf("[AUDIO] Your Turn variation from /mp3/%04d.mp3 (simulation)\n", fileNumber);
    audioFinished = false;
  }
  
  void playColorName(Color c) {
    uint8_t colorFileNumbers[] = {AUDIO_COLOR_BLUE, AUDIO_COLOR_RED, AUDIO_COLOR_GREEN, AUDIO_COLOR_YELLOW};
    Serial.printf("[AUDIO] Color name: %s from /mp3/%04d.mp3\n",
                  c==BLUE?"Blue":c==RED?"Red":c==GREEN?"Green":"Yellow",
                  colorFileNumbers[c]);
  }

  void playCorrect() {
    uint8_t fileNumber = selectVariationWithFallback(POSITIVE_BASE, POSITIVE_COUNT, lastPositive, "Positive Feedback");
    Serial.printf("[AUDIO] Positive feedback variation from /mp3/%04d.mp3 (simulation)\n", fileNumber);
    audioFinished = false;
  }

  void playWrong() {
    Serial.printf("[AUDIO] Wrong from /mp3/%04d.mp3\n", AUDIO_WRONG);
  }

  void playTimeout() {
    Serial.printf("[AUDIO] Timeout from /mp3/%04d.mp3\n", AUDIO_TIMEOUT);
  }

  void playGameOver(uint8_t messageFile) {
    Serial.printf("[AUDIO] Personalized Game Over from /mp3/%04d.mp3\n", messageFile);
    audioFinished = false;
  }

  void playGeneralGameOver() {
    Serial.printf("[AUDIO] General Game Over from /mp3/%04d.mp3\n", AUDIO_GAME_OVER_GENERAL);
    audioFinished = false;
  }

  void playScore(uint8_t score) {
    Serial.printf("[AUDIO] Score: %d from /02/%03d.mp3\n", score, score);
  }

  void stop() {
    Serial.println("[AUDIO] Stop called (simulation)");
    audioFinished = true;
    currentPlayingTrack = -1;
  }
} audio;

#else
// Real hardware version - uses DFPlayer Mini
HardwareSerial dfPlayerSerial(1); // Use UART1
DFRobotDFPlayerMini dfPlayer;

struct Audio {
  bool initialized = false;
  
  void begin() {
    dfPlayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
    Serial.println("Initializing DFPlayer Mini...");
    
    if (!dfPlayer.begin(dfPlayerSerial)) {
      Serial.println("DFPlayer Mini initialization failed!");
      Serial.println("Check connections and SD card");
      return;
    }
    
    Serial.println("DFPlayer Mini initialized successfully");
    
    // Configure DFPlayer
    dfPlayer.volume(DFPLAYER_VOLUME);  // Set volume from config
    dfPlayer.EQ(DFPLAYER_EQ);         // Set EQ from config
    delay(100); // DFPlayer init delay
    
    initialized = true;
  }
  
  bool isReady() {
    return initialized && dfPlayer.available();
  }
  
  void playInvite() {
    if (!initialized) return;
    uint8_t inviteNum = random(1, AUDIO_INVITE_COUNT + 1); // Files 0001-000X.mp3
    currentPlayingTrack = inviteNum;
    dfPlayer.playMp3Folder(inviteNum);
    Serial.printf("[AUDIO] Playing invite %d from /mp3/%04d.mp3 (DFPlayer.playMp3Folder(%d))\n", inviteNum, inviteNum, inviteNum);
  }

  void playInstructions() {
    if (!initialized) return;
    currentPlayingTrack = AUDIO_INSTRUCTIONS;
    dfPlayer.playMp3Folder(AUDIO_INSTRUCTIONS);
    Serial.printf("[AUDIO] Playing instructions from /mp3/%04d.mp3 (DFPlayer.playMp3Folder(%d))\n", AUDIO_INSTRUCTIONS, AUDIO_INSTRUCTIONS);
    audioFinished = false;
  }

  void playDifficultyInstructions(DifficultyLevel difficulty) {
    if (!initialized) return;
    uint8_t instructionFiles[] = {
      AUDIO_INSTRUCTIONS_BLUE,    // 12
      AUDIO_INSTRUCTIONS_RED,     // 13
      AUDIO_INSTRUCTIONS_GREEN,   // 14
      AUDIO_INSTRUCTIONS_YELLOW   // 15
    };
    const char* difficultyNames[] = {"Blue/Novice", "Red/Intermediate", "Green/Advanced", "Yellow/Pro"};
    uint8_t fileNumber = instructionFiles[difficulty];
    currentPlayingTrack = fileNumber;
    dfPlayer.playMp3Folder(fileNumber);
    Serial.printf("[AUDIO] Difficulty instructions (%s) from /mp3/%04d.mp3 (DFPlayer.playMp3Folder(%d))\n",
                  difficultyNames[difficulty], fileNumber, fileNumber);
    audioFinished = false;
  }

  void playMyTurn() {
    if (!initialized) return;
    uint8_t fileNumber = selectVariationWithFallback(MYTURN_BASE, MYTURN_COUNT, lastMyTurn, "My Turn");
    currentPlayingTrack = fileNumber;
    dfPlayer.playMp3Folder(fileNumber);
    Serial.printf("[AUDIO] My Turn variation from /mp3/%04d.mp3 (DFPlayer.playMp3Folder(%d))\n", fileNumber, fileNumber);
    audioFinished = false;
  }

  void playYourTurn() {
    if (!initialized) return;
    uint8_t fileNumber = selectVariationWithFallback(YOURTURN_BASE, YOURTURN_COUNT, lastYourTurn, "Your Turn");
    currentPlayingTrack = fileNumber;
    dfPlayer.playMp3Folder(fileNumber);
    Serial.printf("[AUDIO] Your Turn variation from /mp3/%04d.mp3 (DFPlayer.playMp3Folder(%d))\n", fileNumber, fileNumber);
    audioFinished = false;
  }

  void playColorName(Color c) {
    if (!initialized) return;
    uint8_t colorFileNumbers[] = {AUDIO_COLOR_BLUE, AUDIO_COLOR_RED, AUDIO_COLOR_GREEN, AUDIO_COLOR_YELLOW};
    uint8_t fileNumber = colorFileNumbers[c];
    currentPlayingTrack = fileNumber;
    dfPlayer.playMp3Folder(fileNumber);
    Serial.printf("[AUDIO] Color name: %s from /mp3/%04d.mp3 (DFPlayer.playMp3Folder(%d))\n",
                  c==BLUE?"Blue":c==RED?"Red":c==GREEN?"Green":"Yellow",
                  fileNumber, fileNumber);
  }

  void playCorrect() {
    if (!initialized) return;
    uint8_t fileNumber = selectVariationWithFallback(POSITIVE_BASE, POSITIVE_COUNT, lastPositive, "Positive Feedback");
    currentPlayingTrack = fileNumber;
    dfPlayer.playMp3Folder(fileNumber);
    Serial.printf("[AUDIO] Positive feedback variation from /mp3/%04d.mp3 (DFPlayer.playMp3Folder(%d))\n", fileNumber, fileNumber);
    audioFinished = false;
  }

  void playWrong() {
    if (!initialized) return;
    currentPlayingTrack = AUDIO_WRONG;
    dfPlayer.playMp3Folder(AUDIO_WRONG);
    Serial.printf("[AUDIO] Wrong from /mp3/%04d.mp3 (DFPlayer.playMp3Folder(%d))\n", AUDIO_WRONG, AUDIO_WRONG);
  }

  void playTimeout() {
    if (!initialized) return;
    currentPlayingTrack = AUDIO_TIMEOUT;
    dfPlayer.playMp3Folder(AUDIO_TIMEOUT);
    Serial.printf("[AUDIO] Timeout from /mp3/%04d.mp3 (DFPlayer.playMp3Folder(%d))\n", AUDIO_TIMEOUT, AUDIO_TIMEOUT);
  }

  void playGameOver(uint8_t messageFile) {
    if (!initialized) return;
    currentPlayingTrack = messageFile;
    audioFinished = false;
    dfPlayer.playMp3Folder(messageFile);
    Serial.printf("[AUDIO] Personalized Game Over from /mp3/%04d.mp3 (DFPlayer.playMp3Folder(%d))\n", messageFile, messageFile);
  }

  void playGeneralGameOver() {
    if (!initialized) return;
    // Stop any currently playing track to ensure clean transition
    dfPlayer.stop();
    delay(100);  // Brief pause after stop
    currentPlayingTrack = AUDIO_GAME_OVER_GENERAL;
    audioFinished = false;
    dfPlayer.playMp3Folder(AUDIO_GAME_OVER_GENERAL);
    Serial.printf("[AUDIO] General Game Over from /mp3/%04d.mp3 (DFPlayer.playMp3Folder(%d))\n", AUDIO_GAME_OVER_GENERAL, AUDIO_GAME_OVER_GENERAL);
  }

  void playScore(uint8_t score) {
    if (!initialized) return;
    if (score <= 100) {
      uint8_t fileNumber = AUDIO_SCORE_BASE + score;  // 70 + score
      currentPlayingTrack = fileNumber;
      dfPlayer.playMp3Folder(fileNumber);
      Serial.printf("[AUDIO] Score: %d from /mp3/%04d.mp3 (DFPlayer.playMp3Folder(%d))\n", score, fileNumber, fileNumber);
    }
  }
  
  // Check DFPlayer status and handle errors
  void handleStatus() {
    if (!initialized) return;

    if (dfPlayer.available()) {
      uint8_t type = dfPlayer.readType();
      int value = dfPlayer.read();

      switch (type) {
        case DFPlayerPlayFinished:
          // Only set finished flag if this matches the track we're expecting
          if (value == currentPlayingTrack || currentPlayingTrack == -1) {
            Serial.printf("Audio finished: track %d\n", value);
            audioFinished = true;
            currentPlayingTrack = -1;  // Clear the expected track
          }
          // Silently ignore stale notifications - DFPlayer sends late completion events
          break;
        case DFPlayerError:
          Serial.printf("DFPlayer error: %d\n", value);
          break;
        case DFPlayerCardInserted:
          Serial.println("SD card inserted");
          break;
        case DFPlayerCardRemoved:
          Serial.println("SD card removed");
          break;
      }
    }
  }

  void stop() {
    if (!initialized) return;
    Serial.println("[AUDIO] Stop called - stopping DFPlayer playback");
    dfPlayer.stop();
    audioFinished = true;
    currentPlayingTrack = -1;
  }
} audio;
#endif

// ---- Game State Machine ----
enum GameFSM {
  IDLE,                 // Waiting for player, periodic invites
  INSTRUCTIONS,         // Playing instructions
  DIFFICULTY_SELECTION, // Waiting for difficulty button press
  DIFFICULTY_INSTRUCTIONS, // Playing difficulty-specific instructions
  AWAIT_START,          // Waiting for start button
  SEQ_DISPLAY_INIT,     // Initialize sequence display
  SEQ_DISPLAY_MYTURN,   // Playing "My Turn" audio
  SEQ_DISPLAY,          // Showing LED sequence with audio
  SEQ_DISPLAY_YOURTURN, // UNUSED - "Your Turn" now plays during SEQ_INPUT for immediate responsiveness
  SEQ_INPUT,            // Waiting for player input (with "Your Turn" audio playing in background)
  CORRECT_FEEDBACK,     // Playing correct sound
  WRONG_FEEDBACK,       // Playing wrong sound
  TIMEOUT_FEEDBACK,     // Playing timeout sound
  GAME_OVER,            // Personalized game over message based on difficulty/score
  GENERAL_GAME_OVER,    // General game over message (plays after personalized)
  POST_GAME_INVITE      // Post-game invite to encourage replay
};

// ---- Ambient Visual Effects ----
enum AmbientEffect {
  BREATHING,            // Gentle breathing effect
  SLOW_CHASE,          // Slow color chase
  TWINKLE,             // Random twinkling
  PULSE_WAVE           // Wave pulse effect
};

GameFSM gameState = IDLE;
GameState game;
uint8_t currentStep = 0;           // Current step in sequence display/input
unsigned long stateTimer = 0;      // Timer for current state
unsigned long lastInvite = 0;      // Last invite time
unsigned long nextInviteDelay = 0; // Next invite delay
bool ledOn = false;                // Current LED state during display
Color lastButtonPressed = RED;     // Track button presses

// Ambient effects state
AmbientEffect currentAmbientEffect = BREATHING;
unsigned long ambientTimer = 0;
unsigned long effectChangeTimer = 0;
uint8_t ambientStep = 0;

// ---- Utility Functions ----
// Helper function to check if audio playback is complete (with timeout fallback)
bool isAudioComplete(unsigned long startTime, unsigned long timeoutMs) {
  unsigned long elapsed = millis() - startTime;

  #ifdef USE_WOKWI
    // Simulation: just use timeout
    return elapsed > timeoutMs;
  #else
    // Real hardware: check audio finished flag OR timeout
    if (audioFinished) {
      Serial.printf("Audio complete via DFPlayer notification (%lu ms)\n", elapsed);
      return true;
    }
    if (elapsed > timeoutMs) {
      Serial.printf("Audio complete via timeout fallback (%lu ms)\n", elapsed);
      return true;
    }
    return false;
  #endif
}

static inline void setLed(Color c, bool on) {
  hw_led_duty(c, on ? 255 : 0);
}

// Helper function to select appropriate game over message based on difficulty and score
uint8_t getGameOverMessage(DifficultyLevel difficulty, uint8_t score) {
  // Check strong scorer thresholds for each difficulty level
  if (difficulty == NOVICE && score >= 8) {
    return AUDIO_GAME_OVER_NOVICE_STRONG;
  }
  if (difficulty == INTERMEDIATE && score >= 8) {
    return AUDIO_GAME_OVER_INTERMEDIATE_STRONG;
  }
  if (difficulty == ADVANCED && score >= 10) {
    return AUDIO_GAME_OVER_ADVANCED_STRONG;
  }
  if (difficulty == PRO && score >= 10) {
    return AUDIO_GAME_OVER_PRO_STRONG;
  }
  // Below threshold - mediocre scorer
  return AUDIO_GAME_OVER_MEDIOCRE;
}

// Helper functions to calculate timing based on level (accelerates every 3 levels)
unsigned long getCurrentCueOnMs(uint8_t level) {
  unsigned long calculated = (unsigned long)(CUE_ON_MS_DEFAULT * pow(SEQUENCE_SPEED_STEP, level / 3));
  return max(CUE_ON_MS_MIN, calculated);
}

unsigned long getCurrentCueGapMs(uint8_t level) {
  unsigned long calculated = (unsigned long)(CUE_GAP_MS_DEFAULT * pow(SEQUENCE_SPEED_STEP, level / 3));
  return max(CUE_GAP_MS_MIN, calculated);
}

unsigned long getCurrentInputTimeout(uint8_t level) {
  unsigned long calculated = (unsigned long)(INPUT_TIMEOUT_MS_DEFAULT * pow(INPUT_SPEED_STEP, level / 3));
  return max(INPUT_TIMEOUT_MS_MIN, calculated);
}

// Forward declarations
void generateNewSequence(uint8_t length);
Color generateNextColor();

void initializeGame() {
  // Apply difficulty settings
  auto settings = difficultyConfigs[selectedDifficulty];

  game.level = settings.startingSequenceLength;
  game.score = 0;
  game.strikes = 0;
  // Timing values now calculated on-the-fly based on level using helper functions

  // Set confuser mode based on difficulty
  ENABLE_AUDIO_CONFUSER = settings.confuserEnabled;

  // Generate initial sequence based on starting length
  generateNewSequence(game.level);

  const char* difficultyNames[] = {"Blue/Novice", "Red/Intermediate", "Green/Advanced", "Yellow/Pro"};
  Serial.printf("Game initialized: Difficulty=%s, Level=%d, Confuser=%s\n",
                difficultyNames[selectedDifficulty], game.level,
                ENABLE_AUDIO_CONFUSER ? "ON" : "OFF");
}

Color generateNextColor() {
  // Avoid too many consecutive same colors
  // Add bounds check to prevent array access issues
  if (game.level > MAX_SAME_COLOR && game.level > 0 && game.level <= MAX_SEQUENCE_LENGTH) {
    uint8_t sameCount = 1;
    Color lastColor = (Color)game.seq[game.level - 1];
    
    // Count consecutive occurrences from the end
    for (int i = game.level - 2; i >= 0 && sameCount < MAX_SAME_COLOR; i--) {
      if (game.seq[i] == lastColor) {
        sameCount++;
      } else {
        break;
      }
    }
    
    // If we have too many of the same, avoid it
    if (sameCount >= MAX_SAME_COLOR) {
      Color newColor;
      do {
        newColor = (Color)random(0, COLOR_COUNT);
      } while (newColor == lastColor);
      return newColor;
    }
  }
  
  return (Color)random(0, COLOR_COUNT);
}

Color generateConfuserColor(Color ledColor) {
  if (!ENABLE_AUDIO_CONFUSER || !CONFUSER_MUST_DIFFER) {
    return ledColor; // No confuser or same color allowed
  }

  // Generate different color for confuser
  Color voiceColor;
  do {
    voiceColor = (Color)random(0, COLOR_COUNT);
  } while (voiceColor == ledColor);

  return voiceColor;
}

void generateNewSequence(uint8_t length) {
  // Generate completely new random sequence (used for Green difficulty)
  // Temporarily set game.level to generate sequence properly
  uint8_t originalLevel = game.level;
  for (int i = 0; i < length && i < MAX_SEQUENCE_LENGTH; i++) {
    game.level = i;
    game.seq[i] = generateNextColor();
  }
  game.level = originalLevel;  // Restore original level
  Serial.printf("Generated new sequence of length %d\n", length);
}

void extendSequence() {
  if (game.level < MAX_SEQUENCE_LENGTH) {
    game.seq[game.level] = generateNextColor();
    game.level++;
    Serial.printf("Sequence extended to level %d\n", game.level);
  }
}

// ---- Button Input Handling ----

bool anyButtonPressed() {
  Color c;
  if (hw_btn_any_edge(&c)) {
    lastButtonPressed = c;
    Serial.printf("BUTTON DETECTED: %s button pressed (pin %d)\n",
                  hw_led_name(c), HW_BTN_PIN[c]);
    return true;
  }
  return false;
}

void scheduleNextInvite() {
  // For testing: first invite sooner, then normal intervals
  static bool firstInvite = true;
  if (firstInvite) {
    nextInviteDelay = FIRST_INVITE_DELAY_SEC * 1000; // First invite delay from config
    firstInvite = false;
    Serial.printf("First invite scheduled in %d seconds\n", FIRST_INVITE_DELAY_SEC);
  } else {
    nextInviteDelay = random(INVITE_INTERVAL_MIN_SEC * 1000, (INVITE_INTERVAL_MAX_SEC + 1) * 1000);
    Serial.printf("Next invite scheduled in %lu seconds\n", nextInviteDelay / 1000);
  }
  lastInvite = millis();
}

// ---- Visual LED Effects ----
void bootSequence() {
  Serial.println("Playing boot LED sequence...");
  
  // Rainbow wave effect
  for (int wave = 0; wave < 3; wave++) {
    for (int i = 0; i < COLOR_COUNT; i++) {
      setLed((Color)i, true);
      delay(BOOT_WAVE_DELAY_MS);
      setLed((Color)i, false);
    }
  }

  // Exciting sparkle burst finale
  sparkleBurstSequence(20, 80);

  Serial.println("Boot sequence complete!");
}

void inviteSequence() {
  Serial.println("Playing invite LED sequence...");

  // Eye-catching sparkle burst to grab attention
  sparkleBurstSequence(15, 75);

  // Fast clockwise rotation accent
  clockwiseRotation(2, 150);

  // Finale sparkle burst
  sparkleBurstSequence(10, 60);
}

void instructionsSequence() {
  Serial.println("Playing instructions LED sequence...");

  // Sequential wave pattern (power-safe - one LED at a time)
  // Creates visual rhythm similar to alternating pairs but sequential
  for (int cycle = 0; cycle < 6; cycle++) {
    // First sequence: RED -> GREEN
    setLed(RED, true);
    delay(INSTRUCTIONS_PATTERN_DELAY_MS / 2);
    setLed(RED, false);
    setLed(GREEN, true);
    delay(INSTRUCTIONS_PATTERN_DELAY_MS / 2);
    setLed(GREEN, false);

    // Second sequence: BLUE -> YELLOW
    setLed(BLUE, true);
    delay(INSTRUCTIONS_PATTERN_DELAY_MS / 2);
    setLed(BLUE, false);
    setLed(YELLOW, true);
    delay(INSTRUCTIONS_PATTERN_DELAY_MS / 2);
    setLed(YELLOW, false);
  }

  // Turn off all (already off but for safety)
  for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);
}

void readyToStartEffect(unsigned long now) {
  // Clockwise rotation at medium speed to indicate "ready to start"
  // Consistent visual cue for player input states
  static unsigned long readyTimer = 0;
  static uint8_t readyStep = 0;
  const Color clockwiseOrder[] = {BLUE, RED, GREEN, YELLOW};
  const uint16_t INPUT_ROTATION_DELAY_MS = 400;  // Medium speed

  if (now - readyTimer > INPUT_ROTATION_DELAY_MS) {
    // Turn off all wings
    for (int i = 0; i < COLOR_COUNT; i++) {
      setLed((Color)i, false);
    }

    // Light up current wing in clockwise sequence
    setLed(clockwiseOrder[readyStep % COLOR_COUNT], true);

    readyStep++;
    readyTimer = now;
  }
}

void gameStartSequence() {
  Serial.println("Game starting visual confirmation!");

  // Exciting sparkle burst to energize game start
  sparkleBurstSequence(15, 70);

  Serial.println("Game start sequence complete - all LEDs should be OFF");
}

// ---- New Visual Pattern Functions ----

void clockwiseRotation(uint8_t cycles, uint16_t delayMs) {
  // Smooth clockwise rotation: BLUE→RED→GREEN→YELLOW (follows butterfly perimeter)
  const Color clockwiseOrder[] = {BLUE, RED, GREEN, YELLOW};

  for (uint8_t cycle = 0; cycle < cycles; cycle++) {
    for (uint8_t i = 0; i < COLOR_COUNT; i++) {
      setLed(clockwiseOrder[i], true);
      delay(delayMs);
      setLed(clockwiseOrder[i], false);
    }
  }
}

void sparkleBurstSequence(uint8_t sparkleCount, uint8_t delayMs) {
  // Random wing sparkles for exciting, unpredictable effect
  for (uint8_t i = 0; i < sparkleCount; i++) {
    Color randomWing = (Color)random(0, COLOR_COUNT);
    setLed(randomWing, true);
    delay(delayMs);
    setLed(randomWing, false);
  }
}

void diagonalCrossPattern(uint8_t cycles, uint16_t delayMs) {
  // Slow alternating diagonal pattern (BLUE↔GREEN, RED↔YELLOW)
  for (uint8_t cycle = 0; cycle < cycles; cycle++) {
    // Top-left to bottom-right diagonal
    setLed(BLUE, true);
    delay(delayMs);
    setLed(BLUE, false);

    setLed(GREEN, true);
    delay(delayMs);
    setLed(GREEN, false);

    // Top-right to bottom-left diagonal
    setLed(RED, true);
    delay(delayMs);
    setLed(RED, false);

    setLed(YELLOW, true);
    delay(delayMs);
    setLed(YELLOW, false);
  }
}

void acceleratingChaseSequence() {
  // Speed builds excitement - starts slow, accelerates to very fast
  const Color clockwiseOrder[] = {BLUE, RED, GREEN, YELLOW};
  uint16_t currentDelay = 400; // Start slow
  const uint16_t minDelay = 50; // Very fast finale
  const float speedStep = 0.8; // 20% faster each cycle

  for (uint8_t cycle = 0; cycle < 3; cycle++) {
    for (uint8_t i = 0; i < COLOR_COUNT; i++) {
      setLed(clockwiseOrder[i], true);
      delay(currentDelay);
      setLed(clockwiseOrder[i], false);
    }
    currentDelay = max(minDelay, (uint16_t)(currentDelay * speedStep));
  }
}

// ---- Ambient Effects for Idle State ----
void updateAmbientEffects(unsigned long now) {
  // Smooth clockwise rotation ambient effect (non-blocking)
  // BLUE→RED→GREEN→YELLOW at 600ms per wing
  const Color clockwiseOrder[] = {BLUE, RED, GREEN, YELLOW};
  const uint16_t IDLE_ROTATION_DELAY_MS = 600;

  if (now - ambientTimer > IDLE_ROTATION_DELAY_MS) {
    // Turn off all wings
    for (int i = 0; i < COLOR_COUNT; i++) {
      setLed((Color)i, false);
    }

    // Light up current wing in clockwise sequence
    setLed(clockwiseOrder[ambientStep % COLOR_COUNT], true);

    ambientStep++;
    ambientTimer = now;
  }
}


void game_init() {
  pinMode(LED_SERVICE, OUTPUT);
  audio.begin();
  randomSeed(esp_random());
  
  
  Serial.println("Shimon Game Ready - Butterfly Simon Says!");
  Serial.printf(">>> Confuser Mode: %s <<<\n", ENABLE_AUDIO_CONFUSER ? "ENABLED" : "DISABLED");

  // Play boot sequence
  Serial.println("Starting boot sequence...");
  bootSequence();
  
  // Initialize game state
  scheduleNextInvite();
  
  // Initialize ambient effects
  unsigned long now = millis();
  ambientTimer = now;
  effectChangeTimer = now;
  
  Serial.println("Press any button to start, or wait for invite...");
}

void game_tick() {
  static unsigned long lastLoopDebug = 0;

  digitalWrite(LED_SERVICE, (millis() >> 9) & 1); // Heartbeat LED

#ifndef USE_WOKWI
  audio.handleStatus(); // Handle DFPlayer status/errors for real hardware
#endif

  unsigned long now = millis();

  // Debug: Print loop counter every 10 seconds
  if (now - lastLoopDebug > 10000) {
    Serial.printf("=== LOOP DEBUG: Running at %lu ms, State: %d ===\n", now, gameState);
    lastLoopDebug = now;
  }
  
  switch (gameState) {
    case IDLE: {
      // Run ambient effects continuously
      updateAmbientEffects(now);
      
      // Debug invite timing
      static unsigned long lastDebug = 0;
      if (now - lastDebug > DEBUG_INTERVAL_MS) {
        unsigned long remaining = nextInviteDelay - (now - lastInvite);
        Serial.printf("IDLE: Invite in %lu seconds (Effect: %d)\n",
                      remaining / 1000, currentAmbientEffect);
        // RAW BUTTON DEBUG: Commented out to reduce clutter
        // Serial.printf("RAW BUTTONS: R=%d B=%d G=%d Y=%d (LOW=pressed, pins %d,%d,%d,%d)\n",
        //               digitalRead(btnPins[RED]), digitalRead(btnPins[BLUE]),
        //               digitalRead(btnPins[GREEN]), digitalRead(btnPins[YELLOW]),
        //               btnPins[RED], btnPins[BLUE], btnPins[GREEN], btnPins[YELLOW]);
        lastDebug = now;
      }
      
      // Check for invite timing
      if (now - lastInvite >= nextInviteDelay) {
        audio.playInvite();
        inviteSequence(); // Visual invite!
        scheduleNextInvite();
        
        // Reset ambient effects after invite
        ambientTimer = now;
      }
      
      // Check for any button press to start
      if (anyButtonPressed()) {
        // All buttons now behave the same - start game with instructions
        // (Confuser mode toggle removed - will be replaced with difficulty selection later)

        // Clear ambient effects
        for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);

        audioFinished = false; // Reset flag before playing
        audio.playInstructions();
        instructionsSequence(); // Visual instructions!
        stateTimer = now;
        gameState = INSTRUCTIONS;
        Serial.println("Instructions playing...");
      }
      break;
    }

    case INSTRUCTIONS: {
      // After 2 seconds, allow button press to select difficulty directly
      const unsigned long INSTRUCTIONS_MIN_DURATION_MS = 2000;
      bool minTimeElapsed = (now - stateTimer) >= INSTRUCTIONS_MIN_DURATION_MS;
      bool buttonPressed = anyButtonPressed();  // Call only once to avoid consuming edge detection
      bool audioComplete = isAudioComplete(stateTimer, MAIN_INSTRUCTIONS_DURATION_MS);

      // Check if user pressed button after minimum time OR audio completed
      if ((minTimeElapsed && buttonPressed) || audioComplete) {
        // Turn off all LEDs
        for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);

        if (minTimeElapsed && buttonPressed) {
          // User pressed button - use it to select difficulty directly
          selectedDifficulty = (DifficultyLevel)lastButtonPressed;
          audio.stop();  // Stop audio playback immediately

          const char* difficultyNames[] = {"Blue/Novice", "Red/Intermediate", "Green/Advanced", "Yellow/Pro"};
          Serial.printf("Instructions skipped - Difficulty selected: %d (%s)\n", selectedDifficulty, difficultyNames[selectedDifficulty]);

          // Play difficulty-specific instructions
          audioFinished = false;
          audio.playDifficultyInstructions(selectedDifficulty);
          stateTimer = now;
          gameState = DIFFICULTY_INSTRUCTIONS;
        } else {
          // Audio completed naturally - go to difficulty selection
          stateTimer = now;
          gameState = DIFFICULTY_SELECTION;
          Serial.println("Select difficulty: Press Blue/Red/Green/Yellow button");
        }
      }
      break;
    }

    case DIFFICULTY_SELECTION: {
      // Visual feedback: clockwise rotation (consistent input cue)
      static unsigned long selectionTimer = 0;
      static uint8_t selectionStep = 0;
      const Color clockwiseOrder[] = {BLUE, RED, GREEN, YELLOW};
      const uint16_t INPUT_ROTATION_DELAY_MS = 400;  // Medium speed

      if (now - selectionTimer > INPUT_ROTATION_DELAY_MS) {
        // Turn off all wing LEDs
        for (int i = 0; i < COLOR_COUNT; i++) {
          setLed((Color)i, false);
        }

        // Light up current wing in clockwise sequence
        setLed(clockwiseOrder[selectionStep % COLOR_COUNT], true);

        selectionStep++;
        selectionTimer = now;
      }

      // Wait for button press
      if (anyButtonPressed()) {
        selectedDifficulty = (DifficultyLevel)lastButtonPressed;

        const char* difficultyNames[] = {"Blue/Novice", "Red/Intermediate", "Green/Advanced", "Yellow/Pro"};
        Serial.printf("Difficulty selected: %d (%s)\n", selectedDifficulty, difficultyNames[selectedDifficulty]);

        audioFinished = false;
        audio.playDifficultyInstructions(selectedDifficulty);
        stateTimer = now;
        gameState = DIFFICULTY_INSTRUCTIONS;
      }
      break;
    }

    case DIFFICULTY_INSTRUCTIONS: {
      // Allow user to skip instructions by pressing any button, OR wait for audio completion
      bool buttonPressed = anyButtonPressed();  // Call only once to avoid consuming edge detection
      bool audioComplete = isAudioComplete(stateTimer, DIFFICULTY_INSTRUCTIONS_DURATION_MS);

      if (buttonPressed || audioComplete) {
        if (buttonPressed) {
          Serial.println("Difficulty instructions skipped by user - starting game immediately");
          audio.stop();  // Stop audio playback immediately

          // Skip AWAIT_START and go directly to game start
          gameStartSequence();
          initializeGame();
          gameState = SEQ_DISPLAY_INIT;
          Serial.println("Game starting!");
        } else {
          // Audio completed naturally - go to AWAIT_START
          stateTimer = now;
          gameState = AWAIT_START;
          Serial.println("Press any button to start game...");
        }
      }
      break;
    }

    case AWAIT_START: {
      // Show "ready to start" pulsing effect
      readyToStartEffect(now);
      
      if (anyButtonPressed()) {
        // Clear ready effect and show game start confirmation
        for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);
        gameStartSequence();
        
        initializeGame();
        gameState = SEQ_DISPLAY_INIT;
        Serial.println("Game starting!");
      }
      break;
    }
    
    case SEQ_DISPLAY_INIT: {
      currentStep = 0;
      ledOn = false;
      // Turn off all LEDs
      for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);

      // Small delay to let DFPlayer finish previous audio
      delay(250);
      audioFinished = false; // Reset flag before playing
      audio.playMyTurn();
      stateTimer = now;
      gameState = SEQ_DISPLAY_MYTURN;
      Serial.printf("Displaying sequence level %d\n", game.level);
      break;
    }

    case SEQ_DISPLAY_MYTURN: {
      // Wait for "My Turn" audio to complete (no skip - message is short and important)
      if (isAudioComplete(stateTimer, MY_TURN_DURATION_MS)) {
        // Small delay to let DFPlayer switch from /mp3/ to /01/ folder playback
        delay(200);
        stateTimer = now;
        gameState = SEQ_DISPLAY;
      }
      break;
    }
    
    case SEQ_DISPLAY: {
      if (!ledOn) {
        // Turn on LED for current step
        Color ledColor = (Color)game.seq[currentStep];
        auto settings = difficultyConfigs[selectedDifficulty];

        if (settings.alternatingLedAudio) {
          // Yellow level - alternating LED/audio presentation
          if (currentStep % 2 == 0) {
            // Even step: LED only (no audio)
            setLed(ledColor, true);
            Serial.printf("Step %d: LED=%d (SILENT - Yellow mode)\n", currentStep, ledColor);
          } else {
            // Odd step: Audio only (no LED)
            audio.playColorName(ledColor);  // Correct color
            Serial.printf("Step %d: Voice=%d (DARK - Yellow mode)\n", currentStep, ledColor);
          }
        } else {
          // Blue/Red/Green - normal presentation
          setLed(ledColor, true);

          // Play color with potential confuser
          Color voiceColor = generateConfuserColor(ledColor);
          audio.playColorName(voiceColor);

          Serial.printf("Step %d: LED=%d, Voice=%d%s\n",
                        currentStep, ledColor, voiceColor,
                        (ledColor != voiceColor) ? " (CONFUSER!)" : "");
        }

        stateTimer = now;
        ledOn = true;
      } else if (now - stateTimer > getCurrentCueOnMs(game.level)) {
        // Turn off LED after cue time (keep ledOn=true to prevent retriggering)
        setLed((Color)game.seq[currentStep], false);

        // Check if gap period is also complete
        if (now - stateTimer > getCurrentCueOnMs(game.level) + getCurrentCueGapMs(game.level)) {
          currentStep++;
          if (currentStep >= game.level) {
            // Sequence complete - play "Your Turn" and immediately allow input
            audio.playYourTurn(); // Play in background (non-blocking)
            hw_btn_reset_edges(); // Reset at start of input phase
            stateTimer = now;
            currentStep = 0;
            gameState = SEQ_INPUT; // Skip SEQ_DISPLAY_YOURTURN - go directly to input
            Serial.printf("INPUT DEBUG: Starting input phase at %lu ms (timeout: %lu ms)\n", now, getCurrentInputTimeout(game.level));
          } else {
            ledOn = false; // Ready for next step
          }
        }
      }
      break;
    }

    // SEQ_DISPLAY_YOURTURN removed - we now go directly to SEQ_INPUT
    // to allow player to start responding immediately after sequence display

    case SEQ_INPUT: {
      // Prevent double-detection with minimum time between button presses
      static unsigned long lastButtonDetectionTime = 0;

      // Debug: Raw button states - commented out to reduce clutter
      // static unsigned long lastButtonDebug = 0;
      // if (now - lastButtonDebug > 500) {
      //   Serial.printf("RAW BUTTONS: R=%d B=%d G=%d Y=%d (pins %d,%d,%d,%d)\n",
      //                 digitalRead(btnPins[RED]), digitalRead(btnPins[BLUE]),
      //                 digitalRead(btnPins[GREEN]), digitalRead(btnPins[YELLOW]),
      //                 btnPins[RED], btnPins[BLUE], btnPins[GREEN], btnPins[YELLOW]);
      //   lastButtonDebug = now;
      // }

      // Check for timeout
      unsigned long elapsed = now - stateTimer;
      if (elapsed > getCurrentInputTimeout(game.level)) {
        Serial.printf("TIMEOUT DEBUG: now=%lu, stateTimer=%lu, elapsed=%lu, timeout=%lu\n",
                      now, stateTimer, elapsed, getCurrentInputTimeout(game.level));
        audio.playTimeout();
        stateTimer = now;
        gameState = TIMEOUT_FEEDBACK;
        Serial.println("Input timeout!");
        break;
      }
      
            // Check for button press (with minimum interval to prevent double-detection)
      if (anyButtonPressed()) {
        if (now - lastButtonDetectionTime > MIN_BUTTON_INTERVAL_MS) {
          lastButtonDetectionTime = now;
          Color expectedColor = (Color)game.seq[currentStep];
          Serial.printf("BUTTON DEBUG: Pressed %d, Expected %d (step %d)\n", 
                        lastButtonPressed, expectedColor, currentStep);
          
          if (lastButtonPressed == expectedColor) {
            // Correct! Provide visual feedback only (audio too slow for rapid input)
            setLed(lastButtonPressed, true); // Brief wing LED feedback
            currentStep++;

            if (currentStep >= game.level) {
              // Level complete!
              audioFinished = false; // Reset flag before playing
              audio.playCorrect();
              game.score++; // Score = number of levels successfully completed
              stateTimer = now;
              gameState = CORRECT_FEEDBACK;
              Serial.printf("Level %d completed! Score: %d\n", game.level, game.score);
            } else {
              // Continue with next input - keep button LED on while button is held
              // Wait for button release before proceeding
              while (hw_btn_raw(lastButtonPressed)) {
                delay(10); // Small delay to prevent tight loop
              }
              setLed(lastButtonPressed, false);
              // Reset timeout with fresh timestamp (after button release)
              stateTimer = millis();
              Serial.printf("INPUT DEBUG: Timer reset at %lu ms for next step %d\n", stateTimer, currentStep);
            }
          } else {
            // Wrong!
            audioFinished = false; // Reset flag before playing
            audio.playWrong();
            stateTimer = now;
            gameState = WRONG_FEEDBACK;
            Serial.printf("Wrong! Expected %d, got %d\n", expectedColor, lastButtonPressed);
          }
        } else {
          Serial.printf("TIMING DEBUG: Button press ignored (too soon: %lums since last)\n", 
                        now - lastButtonDetectionTime);
        }
      }
      break;
    }
    
    case CORRECT_FEEDBACK: {
      // Play celebratory sparkle burst (only once when entering state)
      static bool celebrationPlayed = false;
      if (!celebrationPlayed) {
        sparkleBurstSequence(12, 60);  // Quick celebration
        celebrationPlayed = true;
      }

      if (isAudioComplete(stateTimer, FEEDBACK_DURATION_MS)) {
        celebrationPlayed = false;  // Reset for next time
        // Turn off all feedback LEDs
        for (int i = 0; i < COLOR_COUNT; i++) {
          setLed((Color)i, false);
        }

        auto settings = difficultyConfigs[selectedDifficulty];

        if (settings.regenerateSequenceEachTurn) {
          // Green level - generate completely new sequence
          game.level++;  // Increase length
          generateNewSequence(game.level);  // Fresh random sequence
          Serial.printf("Green level: Generated new sequence, length %d\n", game.level);
        } else {
          // Blue/Red/Yellow - extend existing sequence
          extendSequence();
        }

        // Speed progression now calculated automatically based on level
        if (game.level % 3 == 0) {
          Serial.printf("Level %d: Speed progression - cue=%lu gap=%lu timeout=%lu\n",
                        game.level, getCurrentCueOnMs(game.level), getCurrentCueGapMs(game.level), getCurrentInputTimeout(game.level));
        }

        gameState = SEQ_DISPLAY_INIT;
      }
      break;
    }

    case WRONG_FEEDBACK: {
      if (isAudioComplete(stateTimer, FEEDBACK_DURATION_MS)) {
        // Clear all LED feedback
        for (int i = 0; i < COLOR_COUNT; i++) {
          setLed((Color)i, false);
        }

        // Calculate personalized game over message based on difficulty and score
        uint8_t gameOverMsg = getGameOverMessage(selectedDifficulty, game.score);

        // Add delay to allow DFPlayer to finish previous audio before playing Game Over
        delay(400);
        audioFinished = false; // Reset flag before playing
        audio.playGameOver(gameOverMsg);
        stateTimer = now;
        gameState = GAME_OVER;
        Serial.printf("Game over! Difficulty: %d, Score: %d, Message: %d\n",
                      selectedDifficulty, game.score, gameOverMsg);
      }
      break;
    }

    case TIMEOUT_FEEDBACK: {
      if (isAudioComplete(stateTimer, FEEDBACK_DURATION_MS)) {
        // Clear all LEDs before game over
        for (int i = 0; i < COLOR_COUNT; i++) {
          setLed((Color)i, false);
        }

        // Calculate personalized game over message based on difficulty and score
        uint8_t gameOverMsg = getGameOverMessage(selectedDifficulty, game.score);

        // Add delay to allow DFPlayer to finish previous audio before playing Game Over
        delay(400);
        audioFinished = false; // Reset flag before playing
        audio.playGameOver(gameOverMsg);
        stateTimer = now;
        gameState = GAME_OVER;
        Serial.printf("Game over! Difficulty: %d, Score: %d, Message: %d\n",
                      selectedDifficulty, game.score, gameOverMsg);
      }
      break;
    }
    
    case GAME_OVER: {
      // Visual pattern during personalized game over message
      static bool patternPlayed = false;
      static unsigned long patternTimer = 0;

      if (!patternPlayed) {
        // Check if good score - use existing thresholds
        bool goodScore = false;
        if ((selectedDifficulty == NOVICE || selectedDifficulty == INTERMEDIATE) && game.score >= 8) {
          goodScore = true;
        } else if ((selectedDifficulty == ADVANCED || selectedDifficulty == PRO) && game.score >= 10) {
          goodScore = true;
        }

        if (goodScore) {
          // Impressive celebration!
          acceleratingChaseSequence();
          sparkleBurstSequence(20, 50);  // Extra sparkles!
        } else {
          // Gentle, encouraging diagonal cross
          diagonalCrossPattern(2, 500);
        }

        patternPlayed = true;
        patternTimer = now;
      }

      // Continue diagonal cross pattern periodically during audio for low scores
      if (!patternPlayed || (now - patternTimer > 2000)) {  // Every 2 seconds
        bool goodScore = ((selectedDifficulty <= INTERMEDIATE && game.score >= 8) ||
                          (selectedDifficulty >= ADVANCED && game.score >= 10));
        if (!goodScore) {
          diagonalCrossPattern(1, 500);  // Calm, mellow pattern
          patternTimer = now;
        }
      }

      // Allow user to skip game over message by pressing any button, OR wait for audio completion
      bool buttonPressed = anyButtonPressed();  // Call only once to avoid consuming edge detection
      bool audioComplete = isAudioComplete(stateTimer, GAME_OVER_DURATION_MS);

      if (buttonPressed || audioComplete) {
        if (buttonPressed) {
          Serial.println("Game over message skipped by user");
          audio.stop();  // Stop audio playback immediately
        }

        patternPlayed = false;  // Reset for next game

        // Personalized message finished, play general game over
        delay(GAME_OVER_MESSAGE_DELAY_MS);  // Short delay between messages
        audioFinished = false;
        audio.playGeneralGameOver();
        stateTimer = now;
        gameState = GENERAL_GAME_OVER;
        Serial.println("Playing general game over message...");
      }
      break;
    }

    case GENERAL_GAME_OVER: {
      // Slow clockwise rotation during general game over message
      static unsigned long generalPatternTimer = 0;
      static bool generalPatternStarted = false;

      if (!generalPatternStarted || (now - generalPatternTimer > 2400)) {  // Every 2.4 seconds (full rotation)
        clockwiseRotation(1, 600);  // One slow, calm rotation
        generalPatternTimer = now;
        generalPatternStarted = true;
      }

      // Allow user to skip general game over message by pressing any button, OR wait for audio completion
      bool buttonPressed = anyButtonPressed();  // Call only once to avoid consuming edge detection
      bool audioComplete = isAudioComplete(stateTimer, GAME_OVER_DURATION_MS);

      if (buttonPressed || audioComplete) {
        if (buttonPressed) {
          Serial.println("General game over message skipped by user");
          audio.stop();  // Stop audio playback immediately
        }

        generalPatternStarted = false;  // Reset for next game

        // General game over finished, move to post-game invite
        Serial.printf("Game over! Final score: %d\n", game.score);
        gameState = POST_GAME_INVITE;
        stateTimer = now;
      }
      break;
    }

    case POST_GAME_INVITE: {
      // Allow user to skip post-game invite entirely by pressing any button
      if (anyButtonPressed()) {
        Serial.println("Post-game invite skipped by user");
        audio.stop();  // Stop any audio playback

        // Reset delay flag and return to idle immediately
        static bool delayStarted = false;
        delayStarted = false;

        scheduleNextInvite();
        ambientTimer = now;
        effectChangeTimer = now;
        gameState = IDLE;
        Serial.println("Back to idle mode (skipped)");
        break;
      }

      // Add delay to ensure Game Over audio completes before playing invite
      static bool delayStarted = false;
      if (!delayStarted) {
        stateTimer = now;
        delayStarted = true;
        Serial.printf("Waiting %lu ms before post-game invite...\n", POST_GAME_INVITE_DELAY_MS);
        break;
      }

      // Wait configured delay before playing invite to ensure Game Over audio finishes
      if (now - stateTimer < POST_GAME_INVITE_DELAY_MS) {
        break;
      }

      // Play an invite message to encourage replay
      audioFinished = false; // Reset flag before playing
      audio.playInvite();
      inviteSequence(); // Visual invite
      Serial.println("Post-game invite: encouraging player to play again");

      // Now return to idle with ambient effects
      scheduleNextInvite();
      ambientTimer = now;
      effectChangeTimer = now;
      delayStarted = false; // Reset flag for next time
      gameState = IDLE;
      Serial.println("Back to idle mode");
      break;
    }
  }
}

// ---- Mode interface ----
void game_stop() {
  audio.stop();
#ifndef USE_WOKWI
  dfPlayerSerial.end();  // release UART1 so Party Mode can use it for MIDI
#endif
  for (int i = 0; i < COLOR_COUNT; i++) {
    setLed((Color)i, false);
  }
  gameState = IDLE;
  Serial.println("[GAME] Mode stopped.");
}
