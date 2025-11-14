#include <Arduino.h>
#include "shimon.h"

#ifndef USE_WOKWI
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#endif

enum Color { RED=0, BLUE=1, GREEN=2, YELLOW=3 };

// --- Game Data Structure ---
struct GameState {
  uint8_t seq[MAX_SEQUENCE_LENGTH]; // Color sequence storage
  uint8_t level;             // Current level (sequence length)
  uint8_t score;             // Player score
  uint8_t strikes;           // Number of mistakes
  unsigned long cueOnMs;     // Current LED on-time
  unsigned long cueGapMs;    // Current gap between cues
  unsigned long inputTimeout; // Current input timeout
};

// --- Runtime Configurable Features ---
bool ENABLE_AUDIO_CONFUSER = true; // Can be toggled in idle mode

// --- Pin Arrays (derived from configuration) ---
uint8_t ledPins[COLOR_COUNT] = {LED_RED, LED_BLUE, LED_GREEN, LED_YELLOW};
uint8_t btnPins[COLOR_COUNT] = {BTN_RED, BTN_BLUE, BTN_GREEN, BTN_YELLOW};
uint8_t btnLedPins[COLOR_COUNT] = {BTN_LED_RED, BTN_LED_BLUE, BTN_LED_GREEN, BTN_LED_YELLOW};

// ---- Audio Playback Tracking ----
// Audio playback tracking (declared here for global access)
bool audioFinished = false;
unsigned long audioStartTime = 0;

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
  }
  
  void playMyTurn() {
    Serial.printf("[AUDIO] My Turn from /mp3/%04d.mp3\n", AUDIO_MY_TURN);
  }
  
  void playYourTurn() {
    Serial.printf("[AUDIO] Your Turn from /mp3/%04d.mp3\n", AUDIO_YOUR_TURN);
  }
  
  void playColorName(Color c) {
    Serial.printf("[AUDIO] Color name: %s from /%02d/%03d.mp3\n",
                  c==RED?"Red":c==BLUE?"Blue":c==GREEN?"Green":"Yellow",
                  AUDIO_COLOR_FOLDER, c+1);
  }
  
  void playCorrect() {
    Serial.printf("[AUDIO] Correct from /mp3/%04d.mp3\n", AUDIO_CORRECT);
  }

  void playWrong() {
    Serial.printf("[AUDIO] Wrong from /mp3/%04d.mp3\n", AUDIO_WRONG);
  }

  void playTimeout() {
    Serial.printf("[AUDIO] Timeout from /mp3/%04d.mp3\n", AUDIO_TIMEOUT);
  }

  void playGameOver() {
    Serial.printf("[AUDIO] Game Over from /mp3/%04d.mp3\n", AUDIO_GAME_OVER);
  }
  
  void playScore(uint8_t score) {
    Serial.printf("[AUDIO] Score: %d from /02/%03d.mp3\n", score, score);
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
    dfPlayer.play(inviteNum);
    Serial.printf("[AUDIO] Playing invite %d from /mp3/%04d.mp3 (DFPlayer.play(%d))\n", inviteNum, inviteNum, inviteNum);
  }
  
  void playInstructions() {
    if (!initialized) return;
    dfPlayer.play(AUDIO_INSTRUCTIONS);
    Serial.printf("[AUDIO] Playing instructions from /mp3/%04d.mp3 (DFPlayer.play(%d))\n", AUDIO_INSTRUCTIONS, AUDIO_INSTRUCTIONS);
  }
  
  void playMyTurn() {
    if (!initialized) return;
    dfPlayer.play(AUDIO_MY_TURN);
    Serial.printf("[AUDIO] My Turn from /mp3/%04d.mp3 (DFPlayer.play(%d))\n", AUDIO_MY_TURN, AUDIO_MY_TURN);
  }
  
  void playYourTurn() {
    if (!initialized) return;
    dfPlayer.play(AUDIO_YOUR_TURN);
    Serial.printf("[AUDIO] Your Turn from /mp3/%04d.mp3 (DFPlayer.play(%d))\n", AUDIO_YOUR_TURN, AUDIO_YOUR_TURN);
  }
  
  void playColorName(Color c) {
    if (!initialized) return;
    // Play from folder 01, files 001-004.mp3
    dfPlayer.playFolder(AUDIO_COLOR_FOLDER, c + 1);
    Serial.printf("[AUDIO] Color name: %s from /%02d/%03d.mp3 (DFPlayer.playFolder(%d, %d))\n",
                  c==RED?"Red":c==BLUE?"Blue":c==GREEN?"Green":"Yellow",
                  AUDIO_COLOR_FOLDER, c+1, AUDIO_COLOR_FOLDER, c+1);
  }
  
  void playCorrect() {
    if (!initialized) return;
    dfPlayer.play(AUDIO_CORRECT);
    Serial.printf("[AUDIO] Correct from /mp3/%04d.mp3 (DFPlayer.play(%d))\n", AUDIO_CORRECT, AUDIO_CORRECT);
  }
  
  void playWrong() {
    if (!initialized) return;
    dfPlayer.play(AUDIO_WRONG);
    Serial.printf("[AUDIO] Wrong from /mp3/%04d.mp3 (DFPlayer.play(%d))\n", AUDIO_WRONG, AUDIO_WRONG);
  }
  
  void playTimeout() {
    if (!initialized) return;
    dfPlayer.play(AUDIO_TIMEOUT);
    Serial.printf("[AUDIO] Timeout from /mp3/%04d.mp3 (DFPlayer.play(%d))\n", AUDIO_TIMEOUT, AUDIO_TIMEOUT);
  }
  
  void playGameOver() {
    if (!initialized) return;
    dfPlayer.play(AUDIO_GAME_OVER);
    Serial.printf("[AUDIO] Game Over from /mp3/%04d.mp3 (DFPlayer.play(%d))\n", AUDIO_GAME_OVER, AUDIO_GAME_OVER);
  }
  
  void playScore(uint8_t score) {
    if (!initialized) return;
    if (score <= 100) {
      // Play from folder 02, files 000-100.mp3
      dfPlayer.playFolder(2, score);
      Serial.printf("[AUDIO] Score: %d from /02/%03d.mp3 (DFPlayer.playFolder(2, %d))\n", score, score, score);
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
          Serial.printf("Audio finished: track %d\n", value);
          audioFinished = true; // Set flag when audio completes
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
} audio;
#endif

// ---- Game State Machine ----
enum GameFSM {
  IDLE,                 // Waiting for player, periodic invites
  INSTRUCTIONS,         // Playing instructions
  AWAIT_START,          // Waiting for start button
  SEQ_DISPLAY_INIT,     // Initialize sequence display
  SEQ_DISPLAY_MYTURN,   // Playing "My Turn" audio
  SEQ_DISPLAY,          // Showing LED sequence with audio
  SEQ_DISPLAY_YOURTURN, // Playing "Your Turn" audio
  SEQ_INPUT,            // Waiting for player input
  CORRECT_FEEDBACK,     // Playing correct sound
  WRONG_FEEDBACK,       // Playing wrong sound
  TIMEOUT_FEEDBACK,     // Playing timeout sound
  GAME_OVER,            // Game over state
  SCORE_DISPLAY,        // Optional score announcement
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

// Button debouncing
bool buttonStates[4] = {false, false, false, false};
bool lastButtonStates[4] = {true, true, true, true};
unsigned long buttonDebounceTime[4] = {0, 0, 0, 0};

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
  // Normal logic for current-sourcing LEDs: HIGH = ON, LOW = OFF
  digitalWrite(ledPins[c], on ? HIGH : LOW);
  // Debug output for LED state changes
  //Serial.printf("LED %s (pin %d) -> %s\n",
  //              c==RED?"RED":c==BLUE?"BLUE":c==GREEN?"GREEN":"YELLOW",
  //              ledPins[c], on ? "ON" : "OFF");
}

static inline void setBtnLed(Color c, bool on) {
  // Control illuminated button LEDs: HIGH = ON, LOW = OFF
  digitalWrite(btnLedPins[c], on ? HIGH : LOW);
  Serial.printf("Button LED %s (pin %d) -> %s\n", 
                c==RED?"RED":c==BLUE?"BLUE":c==GREEN?"GREEN":"YELLOW",
                btnLedPins[c], on ? "ON" : "OFF");
}

static inline bool pressed(Color c) { 
  return digitalRead(btnPins[c]) == LOW; 
}

void initializeGame() {
  game.level = 1;
  game.score = 0;
  game.strikes = 0;
  game.cueOnMs = CUE_ON_MS_DEFAULT;
  game.cueGapMs = CUE_GAP_MS_DEFAULT;
  game.inputTimeout = INPUT_TIMEOUT_MS_DEFAULT;
  
  // Initialize first sequence element
  game.seq[0] = random(0, COLOR_COUNT);
  
  Serial.printf("Game initialized: Level %d, First color: %d\n", game.level, game.seq[0]);
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

void extendSequence() {
  if (game.level < MAX_SEQUENCE_LENGTH) {
    game.seq[game.level] = generateNextColor();
    game.level++;
    
    // Increase difficulty only every 3 levels for more gradual progression
    if (game.level % 3 == 0) {
      game.cueOnMs = max(CUE_ON_MS_MIN, (unsigned long)(game.cueOnMs * SPEED_STEP));
      game.cueGapMs = max(CUE_GAP_MS_MIN, (unsigned long)(game.cueGapMs * SPEED_STEP));
      Serial.printf("Sequence extended to level %d, speeds INCREASED: cue=%lu gap=%lu\n", 
                    game.level, game.cueOnMs, game.cueGapMs);
    } else {
      Serial.printf("Sequence extended to level %d, speeds unchanged: cue=%lu gap=%lu\n", 
                    game.level, game.cueOnMs, game.cueGapMs);
    }
  }
}

// ---- Button Input Handling ----
void updateButtonStates() {
  unsigned long now = millis();
  
  for (int i = 0; i < COLOR_COUNT; i++) {
    bool rawState = pressed((Color)i);
    
    // Debouncing: only update if stable for debounce time
    if (rawState != lastButtonStates[i]) {
      buttonDebounceTime[i] = now;
    } else if (now - buttonDebounceTime[i] > BUTTON_DEBOUNCE_MS) {
      buttonStates[i] = rawState;
    }
    
    lastButtonStates[i] = rawState;
  }
}

bool getButtonPress(Color c, bool reset = false) {
  // Returns true only on the first frame of a button press (edge detection)
  static bool lastProcessedStates[4] = {false, false, false, false};
  
  if (reset) {
    // Reset all edge detection states
    for (int i = 0; i < 4; i++) {
      lastProcessedStates[i] = false;
    }
    return false;
  }
  
  bool pressed = buttonStates[c] && !lastProcessedStates[c];
  lastProcessedStates[c] = buttonStates[c];
  return pressed;
}

void resetButtonEdgeDetection() {
  getButtonPress(RED, true); // Reset the edge detection state
}

bool anyButtonPressed() {
  for (int i = 0; i < COLOR_COUNT; i++) {
    if (getButtonPress((Color)i)) {
      lastButtonPressed = (Color)i;
      Serial.printf("BUTTON DETECTED: %s button pressed (pin %d)\n", 
                    i==RED?"RED":i==BLUE?"BLUE":i==GREEN?"GREEN":"YELLOW", btnPins[i]);
      return true;
    }
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
  
  // All flash together
  for (int flash = 0; flash < 4; flash++) {
    for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, true);
    delay(BOOT_FLASH_DELAY_MS);
    for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);
    delay(BOOT_FLASH_DELAY_MS);
  }
  
  Serial.println("Boot sequence complete!");
}

void inviteSequence() {
  Serial.println("Playing invite LED sequence...");
  
  // Quick attention-grabbing sequence
  // Double flash all colors
  for (int flash = 0; flash < 2; flash++) {
    for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, true);
    delay(INVITE_FLASH_DELAY_MS);
    for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);
    delay(INVITE_FLASH_DELAY_MS);
  }
  
  // Spinning pattern
  for (int spin = 0; spin < 8; spin++) {
    setLed((Color)(spin % COLOR_COUNT), true);
    delay(INVITE_SPIN_DELAY_MS);
    setLed((Color)(spin % COLOR_COUNT), false);
  }
  
  // Final all flash
  for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, true);
  delay(300); // Final invite flash
  for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);
}

void instructionsSequence() {
  Serial.println("Playing instructions LED sequence...");
  
  // Alternating pairs
  for (int i = 0; i < 6; i++) {
    if (i % 2 == 0) {
      setLed(RED, true);
      setLed(GREEN, true);
      setLed(BLUE, false);
      setLed(YELLOW, false);
    } else {
      setLed(RED, false);
      setLed(GREEN, false);
      setLed(BLUE, true);
      setLed(YELLOW, true);
    }
    delay(INSTRUCTIONS_PATTERN_DELAY_MS);
  }
  
  // Turn off all
  for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);
}

void readyToStartEffect(unsigned long now) {
  // Gentle pulsing of all LEDs to indicate "ready to start"
  static unsigned long readyTimer = 0;
  static uint8_t readyStep = 0;
  
  if (now - readyTimer > READY_PULSE_INTERVAL_MS) { // Update from config
    // Gentle pulse - not as slow as breathing, faster than idle
    float pulse = (sin(readyStep * 0.3) + 1.0) * 0.5; // 0 to 1
    bool ledState = pulse > 0.4; // Higher threshold for cleaner on/off
    
    // All LEDs pulse together in a warm, inviting way
    for (int i = 0; i < COLOR_COUNT; i++) {
      setLed((Color)i, ledState);
    }
    
    readyStep++;
    if (readyStep >= 42) readyStep = 0; // Shorter cycle than breathing
    readyTimer = now;
  }
}

void gameStartSequence() {
  Serial.println("Game starting visual confirmation!");
  
  // Quick burst effect to show game is starting
  for (int burst = 0; burst < 3; burst++) {
    // All on
    for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, true);
    delay(START_BURST_DELAY_MS);
    // All off
    for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);
    delay(START_BURST_DELAY_MS);
  }
  
  // Final bright flash
  for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, true);
  delay(200); // Final game start flash
  for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);
  Serial.println("Game start sequence complete - all LEDs should be OFF");
}

// ---- Ambient Effects for Idle State ----
void updateAmbientEffects(unsigned long now) {
  // Change effect every configured duration
  if (now - effectChangeTimer > (AMBIENT_EFFECT_DURATION_SEC * 1000)) {
    currentAmbientEffect = (AmbientEffect)((currentAmbientEffect + 1) % AMBIENT_EFFECT_COUNT);
    effectChangeTimer = now;
    ambientStep = 0;
    ambientTimer = now;
    Serial.printf("Switching to ambient effect: %d\n", currentAmbientEffect);
  }
  
  switch (currentAmbientEffect) {
    case BREATHING: {
      // Gentle breathing effect - all LEDs slowly pulse together
      if (now - ambientTimer > AMBIENT_UPDATE_INTERVALS[BREATHING]) {
        float breath = (sin(ambientStep * 0.2) + 1.0) * 0.5; // 0 to 1
        bool ledState = breath > 0.3; // Threshold for on/off
        
        for (int i = 0; i < COLOR_COUNT; i++) {
          setLed((Color)i, ledState);
        }
        
        ambientStep++;
        if (ambientStep >= 63) ambientStep = 0; // Full cycle
        ambientTimer = now;
      }
      break;
    }
    
    case SLOW_CHASE: {
      // Slow color chase around the ring
      if (now - ambientTimer > AMBIENT_UPDATE_INTERVALS[SLOW_CHASE]) {
        // Turn off all
        for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);
        
        // Light up current LED
        setLed((Color)(ambientStep % COLOR_COUNT), true);
        
        ambientStep++;
        ambientTimer = now;
      }
      break;
    }
    
    case TWINKLE: {
      // Random twinkling effect
      if (now - ambientTimer > AMBIENT_UPDATE_INTERVALS[TWINKLE]) {
        // Turn off all first
        for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);
        
        // Light up 1-2 random LEDs
        int numLeds = random(1, 3);
        for (int i = 0; i < numLeds; i++) {
          Color randomColor = (Color)random(0, COLOR_COUNT);
          setLed(randomColor, true);
        }
        
        ambientTimer = now;
      }
      break;
    }
    
    case PULSE_WAVE: {
      // Wave pulse effect
      if (now - ambientTimer > AMBIENT_UPDATE_INTERVALS[PULSE_WAVE]) {
        // Turn off all
        for (int i = 0; i < COLOR_COUNT; i++) setLed((Color)i, false);
        
        // Create wave pattern
        int wave1 = ambientStep % 8;
        int wave2 = (ambientStep + 4) % 8;
        
        if (wave1 < COLOR_COUNT) setLed((Color)wave1, true);
        if (wave2 < COLOR_COUNT) setLed((Color)wave2, true);
        
        ambientStep++;
        ambientTimer = now;
      }
      break;
    }
  }
}


void setup() {
  // Initialize serial with explicit configuration for Wokwi
  Serial.begin(115200);
  while(!Serial && millis() < 3000) { delay(10); } // Wait for serial or timeout
  
  Serial.println();  // Send a newline to clear buffer
  Serial.println("=== SHIMON DEBUG: Serial Started ===");
  Serial.flush();
  
  // Verify serial is working with a visible countdown
  for(int i = 5; i > 0; i--) {
    Serial.printf("Boot countdown: %d\n", i);
    Serial.flush();
    delay(1000);
  }
  Serial.println("Serial test complete!");
  pinMode(LED_SERVICE, OUTPUT);
  Serial.println("Setting up pins...");
  for (auto p: ledPins) pinMode(p, OUTPUT);
  for (auto p: btnPins) {
    pinMode(p, INPUT_PULLUP);
    Serial.printf("Button pin %d set to INPUT_PULLUP\n", p);
  }
  for (auto p: btnLedPins) pinMode(p, OUTPUT);
  
  // LED test - flash each LED to verify they work
  Serial.println("Testing wing LEDs...");
  for (int i = 0; i < COLOR_COUNT; i++) {
    setLed((Color)i, true);
    delay(500);
    setLed((Color)i, false);
    delay(200);
  }
  Serial.println("Wing LED test complete!");
  
  // Button LED test
  Serial.println("Testing button LEDs...");
  for (int i = 0; i < COLOR_COUNT; i++) {
    setBtnLed((Color)i, true);
    delay(500);
    setBtnLed((Color)i, false);
    delay(200);
  }
  Serial.println("Button LED test complete!");
  Serial.println("Initializing audio...");
  audio.begin();
  Serial.println("Setting random seed...");
  randomSeed(esp_random());
  
  
  Serial.println("Shimon Game Ready - Butterfly Simon Says!");
  
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

void loop() {
  static unsigned long lastLoopDebug = 0;
  
  digitalWrite(LED_SERVICE, (millis() >> 9) & 1); // Heartbeat LED
  updateButtonStates(); // Handle button debouncing
  
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
      
      // Debug invite timing AND button states
      static unsigned long lastDebug = 0;
      if (now - lastDebug > DEBUG_INTERVAL_MS) {
        unsigned long remaining = nextInviteDelay - (now - lastInvite);
        Serial.printf("IDLE: Invite in %lu seconds (Effect: %d)\n",
                      remaining / 1000, currentAmbientEffect);
        // RAW BUTTON DEBUG: Show if buttons are being read at all
        Serial.printf("RAW BUTTONS: R=%d B=%d G=%d Y=%d (LOW=pressed, pins %d,%d,%d,%d)\n",
                      digitalRead(btnPins[RED]), digitalRead(btnPins[BLUE]),
                      digitalRead(btnPins[GREEN]), digitalRead(btnPins[YELLOW]),
                      btnPins[RED], btnPins[BLUE], btnPins[GREEN], btnPins[YELLOW]);
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
        // Special handling: if YELLOW button pressed in idle, toggle confuser mode
        if (lastButtonPressed == YELLOW) {
          ENABLE_AUDIO_CONFUSER = !ENABLE_AUDIO_CONFUSER;
          Serial.printf("Audio Confuser %s\n", ENABLE_AUDIO_CONFUSER ? "ENABLED" : "DISABLED");
          // Flash yellow LED to confirm
          for (int i = 0; i < 6; i++) {
            setLed(YELLOW, i % 2);
            delay(BUTTON_GUARD_MS * 3); // 3x guard time for visual feedback
          }
          break;
        }
        
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
      if (isAudioComplete(stateTimer, INSTRUCTIONS_DURATION_MS)) {
        gameState = AWAIT_START;
        Serial.println("Press any button to start game...");
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
        setLed(ledColor, true);

        // Play color with potential confuser
        Color voiceColor = generateConfuserColor(ledColor);
        audio.playColorName(voiceColor);

        Serial.printf("Step %d: LED=%d, Voice=%d%s\n",
                      currentStep, ledColor, voiceColor,
                      (ledColor != voiceColor) ? " (CONFUSER!)" : "");

        stateTimer = now;
        ledOn = true;
      } else if (now - stateTimer > game.cueOnMs) {
        // Turn off LED after cue time (keep ledOn=true to prevent retriggering)
        setLed((Color)game.seq[currentStep], false);

        // Check if gap period is also complete
        if (now - stateTimer > game.cueOnMs + game.cueGapMs) {
          currentStep++;
          if (currentStep >= game.level) {
            // Sequence complete
            audioFinished = false; // Reset flag before playing
            audio.playYourTurn();
            stateTimer = now;
            currentStep = 0;
            gameState = SEQ_DISPLAY_YOURTURN;
          } else {
            ledOn = false; // Ready for next step
          }
        }
      }
      break;
    }

    case SEQ_DISPLAY_YOURTURN: {
      if (isAudioComplete(stateTimer, YOUR_TURN_DURATION_MS)) {
        resetButtonEdgeDetection(); // Reset ONLY at start of input phase
        stateTimer = now;
        gameState = SEQ_INPUT;
        Serial.printf("INPUT DEBUG: Starting input phase at %lu ms (timeout: %lu ms)\n", now, game.inputTimeout);
      }
      break;
    }
    
    case SEQ_INPUT: {
      // Prevent double-detection with minimum time between button presses
      static unsigned long lastButtonDetectionTime = 0;

      // Debug: Check raw button states every 500ms
      static unsigned long lastButtonDebug = 0;
      if (now - lastButtonDebug > 500) {
        Serial.printf("RAW BUTTONS: R=%d B=%d G=%d Y=%d (pins %d,%d,%d,%d)\n",
                      digitalRead(btnPins[RED]), digitalRead(btnPins[BLUE]), 
                      digitalRead(btnPins[GREEN]), digitalRead(btnPins[YELLOW]),
                      btnPins[RED], btnPins[BLUE], btnPins[GREEN], btnPins[YELLOW]);
        lastButtonDebug = now;
      }
      
      // Check for timeout
      unsigned long elapsed = now - stateTimer;
      if (elapsed > game.inputTimeout) {
        Serial.printf("TIMEOUT DEBUG: now=%lu, stateTimer=%lu, elapsed=%lu, timeout=%lu\n", 
                      now, stateTimer, elapsed, game.inputTimeout);
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
            // Correct!
            setLed(lastButtonPressed, true); // Brief wing LED feedback
            setBtnLed(lastButtonPressed, true); // Turn on button LED
            currentStep++;

            if (currentStep >= game.level) {
              // Level complete!
              audioFinished = false; // Reset flag before playing
              audio.playCorrect();
              game.score += game.level; // Score based on level
              stateTimer = now;
              gameState = CORRECT_FEEDBACK;
              Serial.printf("Level %d completed! Score: %d\n", game.level, game.score);
            } else {
              // Continue with next input - keep button LED on while button is held
              // Wait for button release before proceeding
              while (pressed(lastButtonPressed)) {
                delay(10); // Small delay to prevent tight loop
              }
              setLed(lastButtonPressed, false);
              setBtnLed(lastButtonPressed, false);
              // Reset timeout with fresh timestamp (after button release)
              stateTimer = millis();
              Serial.printf("INPUT DEBUG: Timer reset at %lu ms for next step %d\n", stateTimer, currentStep);
            }
          } else {
            // Wrong!
            setBtnLed(lastButtonPressed, true); // Turn on button LED for wrong press
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
      // Keep button LED on while button is held during feedback
      if (!pressed(lastButtonPressed)) {
        // Button released, turn off button LED
        setBtnLed(lastButtonPressed, false);
      }

      if (isAudioComplete(stateTimer, FEEDBACK_DURATION_MS)) {
        // Turn off all feedback LEDs
        for (int i = 0; i < COLOR_COUNT; i++) {
          setLed((Color)i, false);
          setBtnLed((Color)i, false);
        }

        // Extend sequence and continue
        extendSequence();
        gameState = SEQ_DISPLAY_INIT;
      }
      break;
    }

    case WRONG_FEEDBACK: {
      // Keep button LED on while button is held during feedback
      if (!pressed(lastButtonPressed)) {
        // Button released, turn off button LED
        setBtnLed(lastButtonPressed, false);
      }

      if (isAudioComplete(stateTimer, FEEDBACK_DURATION_MS)) {
        // Clear all LED feedback (both wing and button LEDs)
        for (int i = 0; i < COLOR_COUNT; i++) {
          setLed((Color)i, false);
          setBtnLed((Color)i, false);
        }
        audioFinished = false; // Reset flag before playing
        audio.playGameOver();
        stateTimer = now;
        gameState = GAME_OVER;
      }
      break;
    }

    case TIMEOUT_FEEDBACK: {
      if (isAudioComplete(stateTimer, FEEDBACK_DURATION_MS)) {
        // Clear all LEDs before game over
        for (int i = 0; i < COLOR_COUNT; i++) {
          setLed((Color)i, false);
          setBtnLed((Color)i, false);
        }
        audioFinished = false; // Reset flag before playing
        audio.playGameOver();
        stateTimer = now;
        gameState = GAME_OVER;
      }
      break;
    }
    
    case GAME_OVER: {
      if (isAudioComplete(stateTimer, GAME_OVER_DURATION_MS)) {
        if (game.score > 0) {
          audioFinished = false; // Reset flag before playing
          audio.playScore(game.score);
          stateTimer = now;
          gameState = SCORE_DISPLAY;
        } else {
          // No score to announce, go to post-game invite
          gameState = POST_GAME_INVITE;
          stateTimer = now;
          Serial.println("No score, moving to post-game invite");
        }
      }
      break;
    }

    case SCORE_DISPLAY: {
      if (isAudioComplete(stateTimer, SCORE_DISPLAY_DURATION_MS)) {
        Serial.printf("Game over! Final score: %d\n", game.score);
        gameState = POST_GAME_INVITE;
        stateTimer = now;
      }
      break;
    }

    case POST_GAME_INVITE: {
      // Play an invite message to encourage replay
      audioFinished = false; // Reset flag before playing
      audio.playInvite();
      inviteSequence(); // Visual invite
      Serial.println("Post-game invite: encouraging player to play again");

      // Now return to idle with ambient effects
      scheduleNextInvite();
      ambientTimer = now;
      effectChangeTimer = now;
      gameState = IDLE;
      Serial.println("Back to idle mode");
      break;
    }
  }
}
