/*
 * Party Mode Proof of Concept
 *
 * Purpose: Validate beat detection feasibility for electronic music
 * Approach: I2S audio input → Dual envelope detection → Beat detection → LED feedback
 *
 * Hardware:
 * - I2S Input: BCLK=26, LRCK=25, DATA=22 (44.1 kHz SPDIF)
 * - LED Output: Blue=23, Red=19, Green=18, Yellow=5
 *
 * Success Criteria:
 * - >80% beat detection accuracy on kick loops
 * - <100ms visual latency (feels synchronized)
 * - Stable operation for >5 minutes
 *
 * See PARTY_MODE_POC_DESIGN.md for complete documentation
 */

#include <Arduino.h>
#include <driver/i2s.h>

// =============================================================================
// CONFIGURATION
// =============================================================================

// I2S Configuration (44.1 kHz SPDIF input)
#define I2S_PORT          I2S_NUM_0
#define I2S_SAMPLE_RATE   44100
#define I2S_BCLK_PIN      26
#define I2S_LRCK_PIN      25
#define I2S_DATA_PIN      22

// Audio Buffer Configuration
#define SAMPLE_BUFFER_SIZE 512        // Smaller buffer for faster response (~11.6ms at 44.1kHz)

// Dual Envelope Detection Configuration (inspired by working sketch)
#define FAST_ENVELOPE_ALPHA   0.35    // Fast follower - tracks transients
#define SLOW_ENVELOPE_ALPHA   0.02    // Slow baseline - tracks average level
#define PEAK_DECAY_FACTOR     0.9992  // Peak decay per update (~0.08% decay)
#define TRANSIENT_THRESHOLD   0.26    // Schmitt trigger ON (normalized 0-1) - RAISED to reduce sensitivity
#define TRANSIENT_OFF         0.10    // Schmitt trigger OFF (normalized 0-1) - LOWERED further to hold longer

// Frequency Discrimination (kick vs hi-hat)
#define KICK_WEIGHT           0.7     // Weight for low-frequency content
#define HIHAT_REJECTION       1.5     // Kick energy must be this much higher than hi-hat

// Tempo Tracking Configuration
#define MIN_BEAT_INTERVAL     370     // ms - absolute minimum time between beats (~162 BPM max) - blocks 354ms doubles
#define MIN_GAP_PERCENT       0.60    // Minimum gap as % of locked period (60% = 300ms for 500ms period)
#define PHASE_GATE_PERCENT    0.30    // ±30% phase window when locked
#define REFRACTORY_MS         280     // Minimum refractory period (anti-chatter)

// LED Configuration (using existing hardware pins)
#define LED_BLUE          23
#define LED_RED           19
#define LED_GREEN         18
#define LED_YELLOW        5

// Debug Configuration
#define ENABLE_SERIAL_DEBUG   true
#define SERIAL_BAUD_RATE      115200

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

void onBeatDetected(unsigned long beatTime);
void triggerLEDPattern();
void updateTempoTracking(unsigned long beatTime);
float calculateTempoFromHistory();
bool isWithinPhaseWindow(unsigned long now);

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================

// I2S buffer
int32_t i2s_read_buffer[SAMPLE_BUFFER_SIZE];

// Dual envelope detection state
float fastEnvelope = 0.0f;       // Fast follower (tracks transients)
float slowEnvelope = 0.0f;       // Slow baseline (tracks average level)
float peakValue = 1e-6f;         // Peak tracking for normalization
float transientValue = 0.0f;     // Normalized transient (0-1 range)

// Frequency discrimination state
float kickEnergy = 0.0f;         // Low-frequency energy
float hihatEnergy = 0.0f;        // High-frequency energy

// Beat detection state (Schmitt trigger)
bool schmittState = false;       // Schmitt trigger state (ON/OFF)
unsigned long lastBeatTime = 0;  // Timestamp of last detected beat
unsigned long refractoryUntil = 0; // Refractory period end time

// Tempo tracking state machine
enum TempoState {
  TEMPO_SEARCHING,    // No tempo established, detecting any onset
  TEMPO_ACQUIRING,    // Collecting beats to establish tempo
  TEMPO_LOCKED,       // Tempo locked, using prediction + phase gating
  TEMPO_LOST          // Lost lock, re-acquiring
};

TempoState tempoState = TEMPO_SEARCHING;
unsigned long beatHistory[8];       // Timestamps of last 8 beats
uint8_t beatHistoryCount = 0;       // Number of beats collected
unsigned long nextBeatPrediction = 0; // Predicted time of next beat
float lockedBPM = 120.0;            // Locked tempo (BPM)
float lockedInterval = 500.0;       // Locked beat interval (ms)
uint8_t missedBeatsCount = 0;      // Consecutive missed beats counter
float phaseError = 0.0f;            // Phase error for debug

// Statistics
unsigned long totalBeatsDetected = 0;
unsigned long lastStatsTime = 0;

// LED state
uint8_t currentLED = 0;             // For rotating pattern (Phase 2)
const uint8_t ledPins[] = {LED_BLUE, LED_RED, LED_GREEN, LED_YELLOW};
unsigned long ledOnTime = 0;        // When LED was turned on
const unsigned long LED_PULSE_MS = 50; // Non-blocking LED pulse duration

// =============================================================================
// SETUP FUNCTIONS
// =============================================================================

void setupI2S() {
  Serial.println("Initializing I2S...");

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRCK_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DATA_PIN
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("ERROR: I2S driver install failed: %d\n", err);
    return;
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("ERROR: I2S pin config failed: %d\n", err);
    return;
  }

  Serial.println("I2S initialized successfully");
  Serial.printf("Sample rate: %d Hz\n", I2S_SAMPLE_RATE);
  Serial.printf("Buffer size: %d samples (~%.1f ms)\n", SAMPLE_BUFFER_SIZE,
                (SAMPLE_BUFFER_SIZE * 1000.0) / I2S_SAMPLE_RATE);
}

void setupLEDs() {
  Serial.println("Initializing LEDs...");

  for (int i = 0; i < 4; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }

  // Test flash all LEDs
  for (int i = 0; i < 4; i++) {
    digitalWrite(ledPins[i], HIGH);
    delay(100);
    digitalWrite(ledPins[i], LOW);
  }

  Serial.println("LEDs initialized");
}

// =============================================================================
// AUDIO CAPTURE & DUAL ENVELOPE DETECTION
// =============================================================================

bool captureAndProcessAudio() {
  size_t bytes_read = 0;

  esp_err_t result = i2s_read(I2S_PORT,
                               i2s_read_buffer,
                               SAMPLE_BUFFER_SIZE * sizeof(int32_t),
                               &bytes_read,
                               portMAX_DELAY);

  if (result != ESP_OK) {
    Serial.printf("ERROR: I2S read failed: %d\n", result);
    return false;
  }

  if (bytes_read < SAMPLE_BUFFER_SIZE * sizeof(int32_t)) {
    Serial.printf("WARNING: Incomplete read, got %d bytes\n", bytes_read);
    return false;
  }

  // Calculate mean absolute value (MAV) amplitude
  // This is simpler and faster than RMS, works well for transient detection
  float sum_abs = 0;
  float sum_abs_low = 0;   // For kick detection (simple approach)
  float sum_abs_high = 0;  // For hi-hat detection (simple approach)

  for (int i = 0; i < SAMPLE_BUFFER_SIZE; i++) {
    // Normalize sample to -1.0 to 1.0 range
    float sample = (float)i2s_read_buffer[i] / 2147483648.0f;

    float abs_sample = fabsf(sample);
    sum_abs += abs_sample;

    // Simple frequency discrimination: alternate samples for high/low
    // This is a crude approximation but very fast
    // Low-frequency content changes slowly, high-frequency changes rapidly
    if (i > 0) {
      float diff = fabsf(sample - (float)i2s_read_buffer[i-1] / 2147483648.0f);
      // High diff = high frequency content (hi-hat)
      // Low diff = low frequency content (kick)
      if (diff < 0.1f) {
        sum_abs_low += abs_sample;  // Slowly changing = low freq
      } else {
        sum_abs_high += abs_sample; // Rapidly changing = high freq
      }
    }
  }

  // Calculate mean absolute value
  float amplitude = sum_abs / SAMPLE_BUFFER_SIZE;
  kickEnergy = sum_abs_low / SAMPLE_BUFFER_SIZE;
  hihatEnergy = sum_abs_high / SAMPLE_BUFFER_SIZE;

  // Dual envelope detection (key algorithm from working sketch)
  // Fast envelope tracks transients, slow envelope tracks baseline
  fastEnvelope += FAST_ENVELOPE_ALPHA * (amplitude - fastEnvelope);
  slowEnvelope += SLOW_ENVELOPE_ALPHA * (amplitude - slowEnvelope);

  // Transient = difference between fast and slow
  // Positive transient = sudden attack (kick, snare, etc.)
  float transient = fastEnvelope - slowEnvelope;
  if (transient < 0) transient = 0;  // Clip to 0 (no negative transients)

  // Peak tracking with decay for normalization
  peakValue *= PEAK_DECAY_FACTOR;  // Decay peak slowly
  if (transient > peakValue) {
    peakValue = transient;  // Update peak
  }

  // Normalize transient to 0-1 range
  transientValue = transient / (peakValue + 1e-6f);  // Prevent division by zero

  return true;
}

// =============================================================================
// TEMPO TRACKING
// =============================================================================

float calculateTempoFromHistory() {
  // Calculate average interval from beat history with outlier rejection
  if (beatHistoryCount < 2) return 120.0; // Default

  // First pass: calculate rough average
  float totalInterval = 0;
  uint8_t intervalCount = 0;

  for (int i = 1; i < beatHistoryCount; i++) {
    unsigned long interval = beatHistory[i] - beatHistory[i-1];
    // Reject obvious outliers (too fast or too slow for electronic music)
    if (interval > 400 && interval < 800) {  // ~75-150 BPM range (tighter for first pass)
      totalInterval += interval;
      intervalCount++;
    }
  }

  if (intervalCount == 0) {
    // Fallback: try wider range if nothing found in tight range
    for (int i = 1; i < beatHistoryCount; i++) {
      unsigned long interval = beatHistory[i] - beatHistory[i-1];
      if (interval > 350 && interval < 1000) {  // Wider range
        totalInterval += interval;
        intervalCount++;
      }
    }
  }

  if (intervalCount == 0) return 120.0;

  float avgInterval = totalInterval / intervalCount;
  return 60000.0 / avgInterval; // Convert ms interval to BPM
}

bool isWithinPhaseWindow(unsigned long now) {
  // Phase gating: only accept beats near predicted time when locked

  if (tempoState == TEMPO_SEARCHING || tempoState == TEMPO_ACQUIRING) {
    return true; // Always accept when searching/acquiring
  }

  if (tempoState == TEMPO_LOCKED && nextBeatPrediction > 0) {
    // Calculate phase (0.0 = exactly on time, ±0.5 = half period early/late)
    long timeDiff = (long)now - (long)nextBeatPrediction;
    float phase = (float)timeDiff / lockedInterval;

    // Accept if within ±30% of period (like working sketch)
    // Also accept if very late (approaching next beat cycle)
    if (fabsf(phase) < PHASE_GATE_PERCENT || fabsf(phase) > (1.0f - PHASE_GATE_PERCENT)) {
      phaseError = timeDiff;  // Store for debug
      return true;
    }
    return false;  // Reject: outside phase window
  }

  // TEMPO_LOST: wide window for re-acquisition
  return true;
}

void updateTempoTracking(unsigned long beatTime) {
  // Add beat to history (circular buffer)
  if (beatHistoryCount < 8) {
    beatHistory[beatHistoryCount] = beatTime;
    beatHistoryCount++;
  } else {
    // Shift history and add new beat
    for (int i = 0; i < 7; i++) {
      beatHistory[i] = beatHistory[i+1];
    }
    beatHistory[7] = beatTime;
  }

  // State machine transitions
  switch (tempoState) {
    case TEMPO_SEARCHING:
      if (beatHistoryCount >= 4) {
        tempoState = TEMPO_ACQUIRING;
        Serial.println(">>> TEMPO: Acquiring...");
      }
      break;

    case TEMPO_ACQUIRING:
      if (beatHistoryCount >= 8) {
        // Establish tempo from history
        lockedBPM = calculateTempoFromHistory();
        lockedInterval = 60000.0 / lockedBPM;
        nextBeatPrediction = beatTime + (unsigned long)lockedInterval;
        tempoState = TEMPO_LOCKED;
        missedBeatsCount = 0;
        Serial.printf(">>> TEMPO: LOCKED at %.1f BPM (interval: %.1f ms)\n", lockedBPM, lockedInterval);
      }
      break;

    case TEMPO_LOCKED: {
      // Update tempo with phase error correction
      long error = (long)beatTime - (long)nextBeatPrediction;

      // Gradual tempo adjustment based on phase error (like PLL)
      if (abs(error) < 100) { // Only adjust for small errors
        float correction = error * 0.1; // 10% correction factor
        lockedInterval = lockedInterval + correction;
        lockedBPM = 60000.0 / lockedInterval;
      }

      // Predict next beat
      nextBeatPrediction = beatTime + (unsigned long)lockedInterval;
      missedBeatsCount = 0; // Reset miss counter
      phaseError = error;
      break;
    }

    case TEMPO_LOST:
      // Re-acquiring: collect new beats
      if (beatHistoryCount >= 4) {
        lockedBPM = calculateTempoFromHistory();
        lockedInterval = 60000.0 / lockedBPM;
        nextBeatPrediction = beatTime + (unsigned long)lockedInterval;
        tempoState = TEMPO_LOCKED;
        missedBeatsCount = 0;
        Serial.printf(">>> TEMPO: RE-LOCKED at %.1f BPM\n", lockedBPM);
      }
      break;
  }
}

// =============================================================================
// BEAT DETECTION
// =============================================================================

void detectBeat() {
  unsigned long now = millis();

  // Check for missed beats (only when locked)
  if (tempoState == TEMPO_LOCKED && nextBeatPrediction > 0) {
    long timeSinceExpected = (long)now - (long)nextBeatPrediction;
    if (timeSinceExpected > 150) { // 150ms late = missed beat
      missedBeatsCount++;
      nextBeatPrediction = now + (unsigned long)lockedInterval; // Predict next

      if (missedBeatsCount >= 3) {
        // Lost lock after 3 consecutive misses
        tempoState = TEMPO_LOST;
        beatHistoryCount = 0; // Reset history
        Serial.println(">>> TEMPO: LOST LOCK (3 missed beats)");
      }
    }
  }

  // Frequency discrimination: check if kick energy dominates
  // This helps reject hi-hats and other high-frequency transients
  bool isKickLike = (kickEnergy > hihatEnergy * HIHAT_REJECTION) ||
                    (tempoState == TEMPO_SEARCHING || tempoState == TEMPO_ACQUIRING);
  // During acquisition, be less strict to establish initial tempo

  // Phase gating: only look for beats within expected window
  bool inPhaseWindow = isWithinPhaseWindow(now);

  // Refractory period check
  bool refractoryPassed = (now >= refractoryUntil);

  // Adaptive minimum gap (like working sketch: 55% of locked period)
  unsigned long minGap = MIN_BEAT_INTERVAL;  // Default absolute minimum
  if (tempoState == TEMPO_LOCKED || tempoState == TEMPO_ACQUIRING) {
    // Use adaptive gap based on locked tempo (55% of period)
    unsigned long adaptiveGap = (unsigned long)(lockedInterval * MIN_GAP_PERCENT);
    if (adaptiveGap > minGap) {
      minGap = adaptiveGap;  // Use stricter constraint when tempo is known
    }
  }

  // Schmitt trigger with hysteresis (prevents chattering)
  // ON threshold: 0.24, OFF threshold: 0.12 (lowered to prevent quick re-trigger)
  if (!schmittState) {
    // Trigger is OFF: look for onset
    if (transientValue > TRANSIENT_THRESHOLD &&
        isKickLike &&
        inPhaseWindow &&
        refractoryPassed &&
        (now - lastBeatTime > minGap)) {  // Use adaptive minimum gap

      // Beat detected!
      schmittState = true;
      onBeatDetected(now);
      updateTempoTracking(now);
      refractoryUntil = now + REFRACTORY_MS;  // Set refractory period
    }
  } else {
    // Trigger is ON: look for release (transient drops below OFF threshold)
    if (transientValue < TRANSIENT_OFF) {
      schmittState = false;  // Reset trigger for next beat
    }
  }

  // Debug output (periodic, not every frame)
  static unsigned long lastDebugTime = 0;
  if (ENABLE_SERIAL_DEBUG && now - lastDebugTime > 2000) {
    const char* state = schmittState ? "ON" : "OFF";
    const char* tempoStr = "";
    switch (tempoState) {
      case TEMPO_SEARCHING: tempoStr = "SEARCH"; break;
      case TEMPO_ACQUIRING: tempoStr = "ACQUIRING"; break;
      case TEMPO_LOCKED: tempoStr = "LOCKED"; break;
      case TEMPO_LOST: tempoStr = "LOST"; break;
    }

    if (tempoState == TEMPO_LOCKED) {
      long timeToNext = (long)nextBeatPrediction - (long)now;
      Serial.printf("[%lu] %s | Trans: %.3f | Kick: %.4f | HH: %.4f | Schmitt: %s | BPM: %.1f | Next: %ldms | Beats: %lu\n",
                    now, tempoStr, transientValue, kickEnergy, hihatEnergy, state,
                    lockedBPM, timeToNext, totalBeatsDetected);
    } else {
      Serial.printf("[%lu] %s | Trans: %.3f | Kick: %.4f | HH: %.4f | Schmitt: %s | Hist: %d | Beats: %lu\n",
                    now, tempoStr, transientValue, kickEnergy, hihatEnergy, state,
                    beatHistoryCount, totalBeatsDetected);
    }
    lastDebugTime = now;
  }
}

void onBeatDetected(unsigned long beatTime) {
  totalBeatsDetected++;

  // Calculate interval and BPM
  if (lastBeatTime > 0) {
    unsigned long interval = beatTime - lastBeatTime;
    float instantBPM = 60000.0 / interval;

    if (ENABLE_SERIAL_DEBUG) {
      if (tempoState == TEMPO_LOCKED) {
        Serial.printf(">>> BEAT #%lu @ %lu ms | Interval: %lu ms | Instant: %.1f BPM | Phase: %+.0f ms | Locked: %.1f BPM | Trans: %.3f\n",
                      totalBeatsDetected, beatTime, interval, instantBPM, phaseError, lockedBPM, transientValue);
      } else {
        Serial.printf(">>> BEAT #%lu @ %lu ms | Interval: %lu ms | Instant: %.1f BPM | Trans: %.3f\n",
                      totalBeatsDetected, beatTime, interval, instantBPM, transientValue);
      }
    }
  } else {
    // First beat
    Serial.printf(">>> FIRST BEAT #%lu @ %lu ms | Trans: %.3f\n", totalBeatsDetected, beatTime, transientValue);
  }

  lastBeatTime = beatTime;

  // Trigger LED pattern (non-blocking)
  triggerLEDPattern();
}

// =============================================================================
// LED PATTERNS
// =============================================================================

void triggerLEDPattern() {
  // PHASE 1: Simple single LED blink (Blue) - NON-BLOCKING
  // Turn on LED immediately when beat detected
  digitalWrite(LED_BLUE, HIGH);
  ledOnTime = millis();  // Record when we turned it on

  // PHASE 2: Rotating pattern (uncomment to enable)
  // // Turn on current LED
  // digitalWrite(ledPins[currentLED], HIGH);
  // ledOnTime = millis();
  //
  // // Rotate to next LED for next beat
  // currentLED = (currentLED + 1) % 4;
}

void updateLEDState() {
  // Non-blocking LED pulse: turn off after LED_PULSE_MS
  if (ledOnTime > 0 && (millis() - ledOnTime >= LED_PULSE_MS)) {
    digitalWrite(LED_BLUE, LOW);
    // PHASE 2: Uncomment for rotating pattern
    // for (int i = 0; i < 4; i++) {
    //   digitalWrite(ledPins[i], LOW);
    // }
    ledOnTime = 0;  // Mark as handled
  }
}

// =============================================================================
// MAIN PROGRAM
// =============================================================================

void setup() {
  // Initialize serial
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);  // Wait for serial to stabilize

  Serial.println("\n\n========================================");
  Serial.println("Party Mode POC v5.3 - Tightened Double-Trigger Protection");
  Serial.println("========================================");
  Serial.printf("Detection Method: Dual Envelope (Fast=%.2f, Slow=%.2f)\n",
                FAST_ENVELOPE_ALPHA, SLOW_ENVELOPE_ALPHA);
  Serial.printf("Schmitt Trigger: ON=%.2f, OFF=%.2f\n", TRANSIENT_THRESHOLD, TRANSIENT_OFF);
  Serial.printf("Refractory Period: %d ms\n", REFRACTORY_MS);
  Serial.printf("Minimum Gap: %d ms (adaptive: %.0f%% of period when locked)\n",
                MIN_BEAT_INTERVAL, MIN_GAP_PERCENT * 100);
  Serial.printf("Phase Gate: ±%.0f%% when locked\n", PHASE_GATE_PERCENT * 100);
  Serial.printf("Frequency Discrimination: Kick/HiHat ratio > %.1fx\n", HIHAT_REJECTION);
  Serial.printf("Buffer: %d samples (~%.1f ms)\n", SAMPLE_BUFFER_SIZE,
                (SAMPLE_BUFFER_SIZE * 1000.0) / I2S_SAMPLE_RATE);
  Serial.printf("Tempo Tracking: Enabled (4-state PLL)\n");
  Serial.println("========================================\n");

  // Initialize hardware
  setupI2S();
  setupLEDs();

  Serial.println("\nStarting beat detection...");
  Serial.println("Play music with strong kick drums (techno/house/trance)\n");

  lastStatsTime = millis();
}

void loop() {
  unsigned long loopStart = micros();

  // Update LED state (non-blocking pulse control)
  updateLEDState();

  // Capture and process audio (dual envelope detection)
  if (!captureAndProcessAudio()) {
    delay(10);  // Wait before retry
    return;
  }

  // Detect beats using dual envelope + Schmitt trigger
  detectBeat();

  // Performance monitoring (every 10 seconds)
  unsigned long now = millis();
  static unsigned long lastPerfTime = 0;
  if (now - lastPerfTime > 10000) {
    unsigned long loopTime = micros() - loopStart;
    Serial.printf("\n=== PERFORMANCE ===\n");
    Serial.printf("Loop time: %lu us (%.2f ms)\n", loopTime, loopTime / 1000.0);
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Total beats: %lu\n", totalBeatsDetected);
    Serial.printf("Uptime: %lu seconds\n\n", now / 1000);
    lastPerfTime = now;
  }
}
