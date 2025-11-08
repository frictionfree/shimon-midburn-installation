#ifndef SHIMON_H
#define SHIMON_H

// =============================================================================
// SHIMON BUTTERFLY SIMON SAYS GAME - CONFIGURATION HEADER
// =============================================================================
// This file contains all user-configurable settings for the Shimon game.
// Modify these values to customize the game experience without touching the main code.
// =============================================================================

// --- Hardware Pin Configuration ---
// Modify these pins to match your wiring setup
constexpr uint8_t BTN_RED    = 13;  // Red button input pin (GPIO13)
constexpr uint8_t BTN_BLUE   = 21;  // Blue button input pin (GPIO21)  
constexpr uint8_t BTN_GREEN  = 14;  // Green button input pin (GPIO14)
constexpr uint8_t BTN_YELLOW = 33;  // Yellow button input pin (GPIO33)

// Button LED pins for illuminated arcade buttons
constexpr uint8_t BTN_LED_RED    = 23;  // Red button LED pin (GPIO23)
constexpr uint8_t BTN_LED_BLUE   = 22;  // Blue button LED pin (GPIO22)
constexpr uint8_t BTN_LED_GREEN  = 32;  // Green button LED pin (GPIO32)
constexpr uint8_t BTN_LED_YELLOW = 27;  // Yellow button LED pin (GPIO27)

constexpr uint8_t LED_RED    = 19;  // Red LED pin (GPIO19)
constexpr uint8_t LED_BLUE   = 25;  // Blue LED pin (GPIO25)
constexpr uint8_t LED_GREEN  = 18;  // Green LED pin (GPIO18)
constexpr uint8_t LED_YELLOW = 26;  // Yellow LED pin (GPIO26)

constexpr uint8_t LED_SERVICE = 2;  // Service/heartbeat LED pin (onboard LED)

#ifndef USE_WOKWI
// DFPlayer Mini pins (for real hardware only)
constexpr uint8_t DFPLAYER_RX = 16;  // ESP32 pin connected to DFPlayer TX
constexpr uint8_t DFPLAYER_TX = 17;  // ESP32 pin connected to DFPlayer RX
#endif

// --- Game Timing Configuration ---
// Adjust these values to change game difficulty and pacing

// Sequence Display Timing
constexpr unsigned long CUE_ON_MS_DEFAULT = 450;    // How long each LED stays on (ms)
constexpr unsigned long CUE_ON_MS_MIN = 250;        // Minimum LED on-time at high levels
constexpr unsigned long CUE_GAP_MS_DEFAULT = 250;   // Gap between LED cues (ms)
constexpr unsigned long CUE_GAP_MS_MIN = 120;       // Minimum gap at high levels

// Player Input Settings  
constexpr unsigned long INPUT_TIMEOUT_MS_DEFAULT = 3000;  // Time limit for player input
constexpr unsigned long INPUT_TIMEOUT_MS_MIN = 1800;      // Minimum timeout at high levels

// Difficulty Progression
constexpr float SPEED_STEP = 0.97f;              // Speed multiplier per level (0.8-0.98 recommended) - slower progression
constexpr uint8_t MAX_SEQUENCE_LENGTH = 64;      // Maximum sequence length
constexpr uint8_t MAX_SAME_COLOR = 2;            // Max consecutive identical colors in sequence

// --- Invite System Configuration ---
// Control how often the game invites players when idle
constexpr unsigned long INVITE_INTERVAL_MIN_SEC = 20;   // Minimum time between invites (seconds)
constexpr unsigned long INVITE_INTERVAL_MAX_SEC = 45;   // Maximum time between invites (seconds)
constexpr unsigned long FIRST_INVITE_DELAY_SEC = 5;     // First invite delay after boot (for testing)

// --- Audio Configuration ---
// Note: Only applies to real hardware with DFPlayer Mini
constexpr uint8_t DFPLAYER_VOLUME = 25;          // Volume level (0-30)
constexpr uint8_t DFPLAYER_EQ = 0;               // EQ setting (0=Normal, 1=Pop, 2=Rock, etc.)

// Audio File Mapping (do not change unless you modify the SD card structure)
constexpr uint8_t AUDIO_INVITE_COUNT = 5;        // Number of invite audio files (0001-0005.mp3)
constexpr uint8_t AUDIO_INSTRUCTIONS = 6;        // Instructions file (0006.mp3)
constexpr uint8_t AUDIO_TIMEOUT = 7;             // Timeout message (0007.mp3)
constexpr uint8_t AUDIO_WRONG = 8;               // Wrong sound (0008.mp3)
constexpr uint8_t AUDIO_GAME_OVER = 9;           // Game over (0009.mp3)
constexpr uint8_t AUDIO_CORRECT = 10;            // Correct sound (0010.mp3)
constexpr uint8_t AUDIO_MY_TURN = 11;            // "My Turn" (0011.mp3)
constexpr uint8_t AUDIO_YOUR_TURN = 12;          // "Your Turn" (0012.mp3)

// --- Game Features Configuration ---
// Toggle these features on/off as desired

// Confuser Mode Settings
constexpr bool CONFUSER_MUST_DIFFER = true;      // If true, spoken color must differ from LED color
// Note: ENABLE_AUDIO_CONFUSER is runtime configurable via YELLOW button in idle mode

// Button Input Settings
constexpr unsigned long BUTTON_DEBOUNCE_MS = 15; // Button debounce time
constexpr unsigned long BUTTON_GUARD_MS = 50;    // Post-press guard time

// Visual Effects Settings
constexpr unsigned long AMBIENT_EFFECT_DURATION_SEC = 30;  // Time per ambient effect
constexpr unsigned long AMBIENT_UPDATE_INTERVALS[] = {     // Update intervals for each effect (ms)
    100,  // BREATHING
    800,  // SLOW_CHASE  
    500,  // TWINKLE
    200   // PULSE_WAVE
};

// Debug and Development Settings
constexpr bool ENABLE_DEBUG_OUTPUT = true;       // Enable verbose serial output
constexpr unsigned long DEBUG_INTERVAL_MS = 5000; // Debug message interval in idle

// --- Scoring System ---
// Configure how points are awarded
constexpr bool SCORE_BY_LEVEL = true;            // Award points equal to level completed
constexpr uint8_t BASE_SCORE_PER_LEVEL = 1;     // Base points per level (multiplied by level)
constexpr uint8_t BONUS_SCORE_HIGH_LEVEL = 5;   // Bonus points for levels > 10
constexpr uint8_t HIGH_LEVEL_THRESHOLD = 10;    // Level at which bonus scoring starts

// --- Advanced Configuration ---
// These settings are for fine-tuning the game experience

// Sequence Generation
constexpr bool ENSURE_VARIETY = true;            // Prevent too many consecutive same colors
constexpr uint8_t MIN_DIFFERENT_COLORS = 3;     // Minimum different colors in sequences > 8

// State Machine Timeouts
constexpr unsigned long INSTRUCTIONS_DURATION_MS = 2000;    // Instructions audio duration
constexpr unsigned long MY_TURN_DURATION_MS = 1000;         // "My Turn" audio duration  
constexpr unsigned long YOUR_TURN_DURATION_MS = 1000;       // "Your Turn" audio duration
constexpr unsigned long FEEDBACK_DURATION_MS = 1000;        // Correct/Wrong feedback duration
constexpr unsigned long GAME_OVER_DURATION_MS = 2000;       // Game over message duration
constexpr unsigned long SCORE_DISPLAY_DURATION_MS = 2000;   // Score announcement duration

// Visual Effect Durations (for blocking sequences like boot, invite, instructions)
constexpr unsigned long BOOT_WAVE_DELAY_MS = 150;           // Boot rainbow wave speed
constexpr unsigned long BOOT_FLASH_DELAY_MS = 200;          // Boot flash timing
constexpr unsigned long INVITE_FLASH_DELAY_MS = 200;        // Invite flash timing
constexpr unsigned long INVITE_SPIN_DELAY_MS = 150;         // Invite spin speed
constexpr unsigned long INSTRUCTIONS_PATTERN_DELAY_MS = 300; // Instructions pattern timing
constexpr unsigned long READY_PULSE_INTERVAL_MS = 150;      // Ready-to-start pulse speed
constexpr unsigned long START_BURST_DELAY_MS = 100;         // Game start burst timing

// =============================================================================
// END OF USER CONFIGURATION
// =============================================================================
// Do not modify anything below this line unless you understand the code structure
// =============================================================================

// Internal derived constants (calculated from above settings)
constexpr uint8_t COLOR_COUNT = 4;
constexpr uint8_t AMBIENT_EFFECT_COUNT = 4;

#endif // SHIMON_H