/*
 * Microphone Control - Implementation
 * INMP441 I2S MEMS Microphone
 */

#include "mic_control.h"
#include <Arduino.h>
#include <driver/i2s.h>

// I2S configuration for INMP441
#define I2S_MIC_PORT I2S_NUM_0

// Microphone state
static bool micInitialized = false;
static bool recording = false;

// Wake word detection state
static bool wakeWordDetectionActive = false;
static WakeWordCallback wakeWordCallback = nullptr;
static unsigned long lastWakeWordTime = 0;
static const unsigned long WAKE_WORD_COOLDOWN = 2000; // 2 second cooldown

// Wake word pattern (simplified audio energy pattern)
static int wakeWordPattern[10] = {0}; // Store energy pattern
static bool wakeWordTrained = false;

// Audio pattern buffer for detection
#define PATTERN_BUFFER_SIZE 10
static int audioPatternBuffer[PATTERN_BUFFER_SIZE] = {0};
static int patternIndex = 0;

/**
 * Initialize the INMP441 microphone
 */
bool initMicrophone() {
  Serial.println("\n[Microphone] Initializing INMP441...");
  
  // I2S configuration for INMP441
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),  // Master mode, receiving data
    .sample_rate = MIC_SAMPLE_RATE,                       // 16kHz sample rate
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,        // INMP441 outputs 32-bit data
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,         // Mono microphone
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,   // Standard I2S format
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,            // Interrupt level 1
    .dma_buf_count = 8,                                   // Number of DMA buffers
    .dma_buf_len = 64,                                    // Size of each DMA buffer
    .use_apll = false,                                    // Don't use APLL
    .tx_desc_auto_clear = false,                          // Not used for RX
    .fixed_mclk = 0                                       // Auto calculate MCLK
  };
  
  // Pin configuration for INMP441
  i2s_pin_config_t pin_config = {
    .bck_io_num = MIC_SCK_PIN,        // GPIO5 - Serial Clock (SCK)
    .ws_io_num = MIC_WS_PIN,          // GPIO4 - Word Select (WS)
    .data_out_num = I2S_PIN_NO_CHANGE,// Not used for input
    .data_in_num = MIC_SD_PIN         // GPIO6 - Serial Data (SD)
  };
  
  // Install I2S driver
  esp_err_t err = i2s_driver_install(I2S_MIC_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[Microphone] FAIL: I2S driver install failed: %d\n", err);
    return false;
  }
  
  // Set pin configuration
  err = i2s_set_pin(I2S_MIC_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("[Microphone] FAIL: I2S pin config failed: %d\n", err);
    i2s_driver_uninstall(I2S_MIC_PORT);
    return false;
  }
  
  // Clear DMA buffers
  i2s_zero_dma_buffer(I2S_MIC_PORT);
  
  micInitialized = true;
  Serial.println("[Microphone] OK: INMP441 initialized successfully");
  Serial.printf("[Microphone] Sample rate: %d Hz\n", MIC_SAMPLE_RATE);
  Serial.printf("[Microphone] Pins - WS:%d, SCK:%d, SD:%d\n", 
                MIC_WS_PIN, MIC_SCK_PIN, MIC_SD_PIN);
  
  return true;
}

/**
 * Start recording audio
 */
bool startRecording() {
  if (!micInitialized) {
    Serial.println("[Microphone] ERROR: Not initialized");
    return false;
  }
  
  if (recording) {
    Serial.println("[Microphone] Already recording");
    return true;
  }
  
  // Start I2S
  esp_err_t err = i2s_start(I2S_MIC_PORT);
  if (err != ESP_OK) {
    Serial.printf("[Microphone] FAIL: Start recording failed: %d\n", err);
    return false;
  }
  
  recording = true;
  Serial.println("[Microphone] Recording started");
  return true;
}

/**
 * Stop recording audio
 */
void stopRecording() {
  if (!recording) {
    return;
  }
  
  i2s_stop(I2S_MIC_PORT);
  i2s_zero_dma_buffer(I2S_MIC_PORT);
  
  recording = false;
  Serial.println("[Microphone] Recording stopped");
}

/**
 * Read audio samples from microphone
 * 
 * @param buffer Buffer to store samples (16-bit signed integers)
 * @param bufferSize Size of buffer in samples
 * @return Number of bytes read
 */
size_t readAudioSamples(int16_t* buffer, size_t bufferSize) {
  if (!micInitialized || !recording) {
    return 0;
  }
  
  size_t bytesRead = 0;
  
  // INMP441 outputs 32-bit samples, but we only need 16-bit
  // Read 32-bit samples and convert to 16-bit
  int32_t sample32;
  size_t bytes;
  
  for (size_t i = 0; i < bufferSize; i++) {
    esp_err_t err = i2s_read(I2S_MIC_PORT, &sample32, sizeof(int32_t), &bytes, portMAX_DELAY);
    
    if (err != ESP_OK || bytes != sizeof(int32_t)) {
      break;
    }
    
    // Convert 32-bit to 16-bit by taking the upper 16 bits
    buffer[i] = (int16_t)(sample32 >> 16);
    bytesRead += sizeof(int16_t);
  }
  
  return bytesRead;
}

/**
 * Check if microphone is initialized and ready
 */
bool isMicrophoneReady() {
  return micInitialized;
}

/**
 * Get current audio level (0-100)
 * Reads a small buffer and calculates RMS level
 */
int getAudioLevel() {
  if (!micInitialized || !recording) {
    return 0;
  }
  
  const size_t sampleCount = 100;
  int16_t samples[sampleCount];
  
  size_t bytesRead = readAudioSamples(samples, sampleCount);
  if (bytesRead == 0) {
    return 0;
  }
  
  // Calculate RMS (Root Mean Square) level
  int64_t sum = 0;
  size_t samplesRead = bytesRead / sizeof(int16_t);
  
  for (size_t i = 0; i < samplesRead; i++) {
    int32_t val = samples[i];
    sum += val * val;
  }
  
  if (samplesRead == 0) {
    return 0;
  }
  
  int32_t rms = sqrt(sum / samplesRead);
  
  // Normalize to 0-100 range (assuming max 16-bit value)
  int level = (rms * 100) / 32768;
  
  return constrain(level, 0, 100);
}

/**
 * Deinitialize microphone and free resources
 */
void deinitMicrophone() {
  if (!micInitialized) {
    return;
  }
  
  stopRecording();
  i2s_driver_uninstall(I2S_MIC_PORT);
  
  micInitialized = false;
  Serial.println("[Microphone] Deinitialized");
}

// ========== Wake Word Detection Implementation ==========

/**
 * Calculate audio energy from samples
 */
static int calculateAudioEnergy(int16_t* samples, size_t count) {
  int64_t sum = 0;
  for (size_t i = 0; i < count; i++) {
    int32_t val = samples[i];
    sum += val * val;
  }
  int32_t rms = sqrt(sum / count);
  return (rms * 100) / 32768; // Normalize to 0-100
}

/**
 * Check if current audio pattern matches wake word
 * Simple pattern matching: looking for "Hey Bob" pattern
 * Pattern: [silence] [energy spike "Hey"] [brief pause] [energy spike "Bob"] [silence]
 */
static bool matchesWakeWordPattern() {
  if (!wakeWordTrained) {
    // Default pattern for "Hey Bob": two energy spikes with a dip
    // Pattern indices: 0-2 silence, 3-4 "Hey", 5 dip, 6-7 "Bob", 8-9 silence
    int pattern[PATTERN_BUFFER_SIZE] = {5, 8, 12, 45, 50, 25, 48, 52, 15, 8};
    
    // Simple correlation matching
    int matchScore = 0;
    for (int i = 0; i < PATTERN_BUFFER_SIZE; i++) {
      int diff = abs(audioPatternBuffer[i] - pattern[i]);
      if (diff < 20) { // Threshold for matching
        matchScore++;
      }
    }
    
    // Need at least 60% match
    return matchScore >= 6;
  } else {
    // Use trained pattern
    int matchScore = 0;
    for (int i = 0; i < PATTERN_BUFFER_SIZE; i++) {
      int diff = abs(audioPatternBuffer[i] - wakeWordPattern[i]);
      if (diff < 25) {
        matchScore++;
      }
    }
    return matchScore >= 7;
  }
}

/**
 * Start wake word detection
 */
bool startWakeWordDetection() {
  if (!micInitialized) {
    Serial.println("[WakeWord] ERROR: Microphone not initialized");
    return false;
  }
  
  if (wakeWordDetectionActive) {
    Serial.println("[WakeWord] Already active");
    return true;
  }
  
  // Start recording
  if (!startRecording()) {
    Serial.println("[WakeWord] Failed to start recording");
    return false;
  }
  
  wakeWordDetectionActive = true;
  patternIndex = 0;
  lastWakeWordTime = 0;
  
  Serial.println("[WakeWord] Detection started - listening for 'Hey Bob'");
  if (!wakeWordTrained) {
    Serial.println("[WakeWord] Using default pattern (call trainWakeWord() to customize)");
  } else {
    Serial.println("[WakeWord] Using trained pattern");
  }
  
  return true;
}

/**
 * Stop wake word detection
 */
void stopWakeWordDetection() {
  if (!wakeWordDetectionActive) {
    return;
  }
  
  stopRecording();
  wakeWordDetectionActive = false;
  Serial.println("[WakeWord] Detection stopped");
}

/**
 * Set callback function for wake word detection
 */
void setWakeWordCallback(WakeWordCallback callback) {
  wakeWordCallback = callback;
  Serial.println("[WakeWord] Callback registered");
}

/**
 * Check if wake word detection is active
 */
bool isWakeWordDetectionActive() {
  return wakeWordDetectionActive;
}

/**
 * Update wake word detection - call this in main loop()
 * This processes audio and detects the wake word pattern
 */
void updateWakeWordDetection() {
  if (!wakeWordDetectionActive) {
    return;
  }
  
  // Cooldown check to prevent multiple detections
  if (millis() - lastWakeWordTime < WAKE_WORD_COOLDOWN) {
    return;
  }
  
  // Read audio samples
  const size_t sampleCount = 64;
  int16_t samples[sampleCount];
  
  size_t bytesRead = readAudioSamples(samples, sampleCount);
  if (bytesRead == 0) {
    return;
  }
  
  // Calculate energy for this audio chunk
  int energy = calculateAudioEnergy(samples, sampleCount);
  
  // Update pattern buffer (circular buffer)
  audioPatternBuffer[patternIndex] = energy;
  patternIndex = (patternIndex + 1) % PATTERN_BUFFER_SIZE;
  
  // Check if pattern matches wake word
  if (matchesWakeWordPattern()) {
    Serial.println("[WakeWord] *** DETECTED: Hey Bob! ***");
    lastWakeWordTime = millis();
    
    // Call callback if registered
    if (wakeWordCallback != nullptr) {
      wakeWordCallback();
    }
    
    // Clear pattern buffer to prevent re-triggering
    for (int i = 0; i < PATTERN_BUFFER_SIZE; i++) {
      audioPatternBuffer[i] = 0;
    }
  }
}

/**
 * Train the wake word pattern
 * Call this function, then say "Hey Bob" clearly
 */
void trainWakeWord() {
  if (!micInitialized) {
    Serial.println("[WakeWord] ERROR: Microphone not initialized");
    return;
  }
  
  Serial.println("\n[WakeWord] === Training Mode ===");
  Serial.println("[WakeWord] Say 'Hey Bob' clearly when prompted...");
  Serial.println("[WakeWord] Starting in 3 seconds...");
  
  delay(3000);
  
  // Start recording
  bool wasRecording = recording;
  if (!wasRecording) {
    startRecording();
  }
  
  Serial.println("[WakeWord] >>> SAY 'HEY BOB' NOW! <<<");
  
  // Record pattern over 2 seconds
  int patternTemp[PATTERN_BUFFER_SIZE] = {0};
  const size_t sampleCount = 64;
  int16_t samples[sampleCount];
  
  unsigned long startTime = millis();
  int captureIndex = 0;
  
  while (millis() - startTime < 2000 && captureIndex < PATTERN_BUFFER_SIZE) {
    size_t bytesRead = readAudioSamples(samples, sampleCount);
    if (bytesRead > 0) {
      int energy = calculateAudioEnergy(samples, sampleCount);
      patternTemp[captureIndex] = energy;
      captureIndex++;
      Serial.printf("[WakeWord] Captured: %d (energy: %d)\n", captureIndex, energy);
    }
    delay(200); // 200ms intervals
  }
  
  // Save the pattern
  for (int i = 0; i < PATTERN_BUFFER_SIZE; i++) {
    wakeWordPattern[i] = patternTemp[i];
  }
  
  wakeWordTrained = true;
  
  Serial.println("[WakeWord] Training complete!");
  Serial.print("[WakeWord] Pattern: ");
  for (int i = 0; i < PATTERN_BUFFER_SIZE; i++) {
    Serial.printf("%d ", wakeWordPattern[i]);
  }
  Serial.println();
  
  if (!wasRecording) {
    stopRecording();
  }
  
  Serial.println("[WakeWord] You can now use startWakeWordDetection()");
}

