/*
 * Party Mode Proof of Concept
 *
 * Purpose: Validate beat detection feasibility for electronic music
 * Approach: I2S audio input → FFT analysis → Beat detection → LED feedback
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
#include <arduinoFFT.h>

// =============================================================================
// CONFIGURATION
// =============================================================================

// I2S Configuration (44.1 kHz SPDIF input)
#define I2S_PORT          I2S_NUM_0
#define I2S_SAMPLE_RATE   44100
#define I2S_BCLK_PIN      26
#define I2S_LRCK_PIN      25
#define I2S_DATA_PIN      22

// FFT Configuration
#define FFT_SIZE          2048        // Sample buffer size
#define SAMPLES           FFT_SIZE    // ArduinoFFT expects this naming

// Beat Detection Configuration
#define KICK_FREQ_MIN     60          // Hz - kick drum lower bound
#define KICK_FREQ_MAX     120         // Hz - kick drum upper bound
#define BEAT_THRESHOLD    1.5         // Multiplier above average energy
#define MIN_BEAT_INTERVAL 300         // ms - minimum time between beats (~200 BPM max)

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

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================

// FFT objects and buffers
ArduinoFFT<double> FFT = ArduinoFFT<double>();
double vReal[SAMPLES];              // Real part (input samples)
double vImag[SAMPLES];              // Imaginary part (zeros for real input)

// I2S buffer
int32_t i2s_read_buffer[FFT_SIZE];

// Beat detection state
float avgBassEnergy = 0;            // Running average of bass energy
float lastBassEnergy = 0;           // Previous bass energy (for peak detection)
unsigned long lastBeatTime = 0;     // Timestamp of last detected beat
float avgBPM = 120.0;               // Running average BPM estimate

// Statistics
unsigned long totalBeatsDetected = 0;
unsigned long lastStatsTime = 0;

// LED state
uint8_t currentLED = 0;             // For rotating pattern (Phase 2)
const uint8_t ledPins[] = {LED_BLUE, LED_RED, LED_GREEN, LED_YELLOW};

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
  Serial.printf("FFT size: %d samples\n", FFT_SIZE);
  Serial.printf("Frequency resolution: %.2f Hz per bin\n",
                (float)I2S_SAMPLE_RATE / FFT_SIZE);
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
// AUDIO CAPTURE & FFT
// =============================================================================

bool captureAudioSamples() {
  size_t bytes_read = 0;

  esp_err_t result = i2s_read(I2S_PORT,
                               i2s_read_buffer,
                               FFT_SIZE * sizeof(int32_t),
                               &bytes_read,
                               portMAX_DELAY);

  if (result != ESP_OK) {
    Serial.printf("ERROR: I2S read failed: %d\n", result);
    return false;
  }

  if (bytes_read < FFT_SIZE * sizeof(int32_t)) {
    Serial.printf("WARNING: Incomplete read, got %d bytes\n", bytes_read);
    return false;
  }

  // Convert I2S samples to FFT input (use left channel, normalize)
  for (int i = 0; i < FFT_SIZE; i++) {
    // I2S gives 32-bit samples, normalize to roughly -1.0 to 1.0 range
    vReal[i] = (double)i2s_read_buffer[i] / 2147483648.0;
    vImag[i] = 0.0;  // Imaginary part is zero for real input
  }

  return true;
}

void performFFT() {
  // Apply window function to reduce spectral leakage
  FFT.windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);

  // Compute FFT
  FFT.compute(vReal, vImag, SAMPLES, FFT_FORWARD);

  // Compute magnitudes
  FFT.complexToMagnitude(vReal, vImag, SAMPLES);
}

// =============================================================================
// BEAT DETECTION
// =============================================================================

void detectBeat() {
  // Calculate which FFT bins correspond to kick frequency range
  int binMin = (KICK_FREQ_MIN * FFT_SIZE) / I2S_SAMPLE_RATE;
  int binMax = (KICK_FREQ_MAX * FFT_SIZE) / I2S_SAMPLE_RATE;

  // Sum energy in kick frequency range
  float bassEnergy = 0;
  for (int i = binMin; i <= binMax; i++) {
    bassEnergy += vReal[i];
  }

  // Update running average (exponential moving average)
  // Higher alpha = faster adaptation, lower alpha = smoother
  const float alpha = 0.05;
  avgBassEnergy = (avgBassEnergy * (1.0 - alpha)) + (bassEnergy * alpha);

  // Detect peak (local maximum above threshold)
  unsigned long now = millis();
  bool isPeak = (bassEnergy > lastBassEnergy) &&
                (bassEnergy > avgBassEnergy * BEAT_THRESHOLD) &&
                (now - lastBeatTime > MIN_BEAT_INTERVAL);

  if (isPeak) {
    // Beat detected!
    onBeatDetected(now);
  }

  // Debug output (periodic, not every frame)
  static unsigned long lastDebugTime = 0;
  if (ENABLE_SERIAL_DEBUG && now - lastDebugTime > 1000) {
    Serial.printf("[%lu] Bass: %.1f, Avg: %.1f, Thresh: %.1f | BPM: %.1f | Beats: %lu\n",
                  now, bassEnergy, avgBassEnergy, avgBassEnergy * BEAT_THRESHOLD,
                  avgBPM, totalBeatsDetected);
    lastDebugTime = now;
  }

  lastBassEnergy = bassEnergy;
}

void onBeatDetected(unsigned long beatTime) {
  totalBeatsDetected++;

  // Calculate BPM if we have previous beat
  if (lastBeatTime > 0) {
    unsigned long interval = beatTime - lastBeatTime;
    float instantBPM = 60000.0 / interval;

    // Update running average BPM (exponential moving average)
    avgBPM = (avgBPM * 0.9) + (instantBPM * 0.1);

    if (ENABLE_SERIAL_DEBUG) {
      Serial.printf(">>> BEAT! Interval: %lu ms, Instant BPM: %.1f, Avg BPM: %.1f\n",
                    interval, instantBPM, avgBPM);
    }
  }

  lastBeatTime = beatTime;

  // Trigger LED pattern
  triggerLEDPattern();
}

// =============================================================================
// LED PATTERNS
// =============================================================================

void triggerLEDPattern() {
  // PHASE 1: Simple single LED blink (Blue)
  // Uncomment for basic beat validation
  digitalWrite(LED_BLUE, HIGH);
  delay(50);  // Short pulse
  digitalWrite(LED_BLUE, LOW);

  // PHASE 2: Rotating pattern (uncomment to enable)
  // Turn off all LEDs
  // for (int i = 0; i < 4; i++) {
  //   digitalWrite(ledPins[i], LOW);
  // }
  //
  // // Light up current LED
  // digitalWrite(ledPins[currentLED], HIGH);
  // delay(50);  // Short pulse
  // digitalWrite(ledPins[currentLED], LOW);
  //
  // // Rotate to next LED
  // currentLED = (currentLED + 1) % 4;
}

// =============================================================================
// MAIN PROGRAM
// =============================================================================

void setup() {
  // Initialize serial
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);  // Wait for serial to stabilize

  Serial.println("\n\n========================================");
  Serial.println("Party Mode POC v1.0");
  Serial.println("========================================");
  Serial.printf("FFT Size: %d samples\n", FFT_SIZE);
  Serial.printf("Sample Rate: %d Hz (44.1 kHz)\n", I2S_SAMPLE_RATE);
  Serial.printf("Frequency Resolution: %.2f Hz per bin\n",
                (float)I2S_SAMPLE_RATE / FFT_SIZE);
  Serial.printf("Kick Range: %d - %d Hz (bins %d - %d)\n",
                KICK_FREQ_MIN, KICK_FREQ_MAX,
                (KICK_FREQ_MIN * FFT_SIZE) / I2S_SAMPLE_RATE,
                (KICK_FREQ_MAX * FFT_SIZE) / I2S_SAMPLE_RATE);
  Serial.printf("Beat Threshold: %.2f\n", BEAT_THRESHOLD);
  Serial.printf("Min Beat Interval: %d ms\n", MIN_BEAT_INTERVAL);
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

  // Capture audio samples
  if (!captureAudioSamples()) {
    delay(10);  // Wait before retry
    return;
  }

  // Perform FFT analysis
  performFFT();

  // Detect beats
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
