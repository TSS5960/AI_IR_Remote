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

// Wake word pattern - actual audio waveform
#define WAKE_WORD_SAMPLES 24000  // 1.5 seconds at 16kHz
static int16_t* wakeWordPattern = nullptr;  // Dynamically allocated
static bool wakeWordTrained = false;

// Audio buffer for detection (rolling buffer)
#define AUDIO_BUFFER_SAMPLES 24000  // Same size as pattern
static int16_t* audioBuffer = nullptr;
static int audioBufferIndex = 0;

static const float CORRELATION_THRESHOLD = 0.65;  // Correlation coefficient threshold (0-1)

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
    .dma_buf_count = 4,                                   // Reduced from 8 to 4 (less interrupt load)
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
 * Calculate correlation coefficient between two audio signals
 * Returns value between 0 (no match) and 1 (perfect match)
 */
static float calculateCorrelation(int16_t* signal1, int16_t* signal2, size_t length) {
  // Calculate means
  float mean1 = 0, mean2 = 0;
  for (size_t i = 0; i < length; i++) {
    mean1 += signal1[i];
    mean2 += signal2[i];
  }
  mean1 /= length;
  mean2 /= length;
  
  // Calculate correlation coefficient
  float numerator = 0;
  float denom1 = 0, denom2 = 0;
  
  for (size_t i = 0; i < length; i++) {
    float diff1 = signal1[i] - mean1;
    float diff2 = signal2[i] - mean2;
    numerator += diff1 * diff2;
    denom1 += diff1 * diff1;
    denom2 += diff2 * diff2;
  }
  
  if (denom1 == 0 || denom2 == 0) return 0;
  
  float correlation = numerator / sqrt(denom1 * denom2);
  return abs(correlation);  // Return absolute value
}

/**
 * Check if current audio buffer matches wake word pattern using correlation
 */
static bool matchesWakeWordPattern() {
  if (!wakeWordTrained || wakeWordPattern == nullptr || audioBuffer == nullptr) {
    return false;
  }
  
  // Create a linearized copy of circular buffer for correlation
  // The buffer is organized with audioBufferIndex pointing to the oldest sample
  int16_t* linearBuffer = (int16_t*)malloc(AUDIO_BUFFER_SAMPLES * sizeof(int16_t));
  if (linearBuffer == nullptr) {
    return false;
  }
  
  // Copy from current index to end
  size_t firstPart = AUDIO_BUFFER_SAMPLES - audioBufferIndex;
  memcpy(linearBuffer, &audioBuffer[audioBufferIndex], firstPart * sizeof(int16_t));
  
  // Copy from start to current index
  if (audioBufferIndex > 0) {
    memcpy(&linearBuffer[firstPart], audioBuffer, audioBufferIndex * sizeof(int16_t));
  }
  
  // Calculate correlation between linear buffer and trained pattern
  float correlation = calculateCorrelation(linearBuffer, wakeWordPattern, WAKE_WORD_SAMPLES);
  
  free(linearBuffer);
  
  Serial.printf("[WakeWord] Correlation: %.3f (threshold: %.2f)\n", correlation, CORRELATION_THRESHOLD);
  
  return correlation >= CORRELATION_THRESHOLD;
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
  
  // Check if wake word has been trained
  if (!wakeWordTrained || wakeWordPattern == nullptr) {
    Serial.println("[WakeWord] ERROR: No trained pattern found");
    Serial.println("[WakeWord] Please run 'train' command first");
    return false;
  }
  
  // Allocate audio buffer if needed
  if (audioBuffer == nullptr) {
    audioBuffer = (int16_t*)malloc(AUDIO_BUFFER_SAMPLES * sizeof(int16_t));
    if (audioBuffer == nullptr) {
      Serial.println("[WakeWord] ERROR: Failed to allocate audio buffer");
      return false;
    }
    // Clear buffer
    memset(audioBuffer, 0, AUDIO_BUFFER_SAMPLES * sizeof(int16_t));
    audioBufferIndex = 0;
  }
  
  // Start recording
  if (!startRecording()) {
    Serial.println("[WakeWord] Failed to start recording");
    return false;
  }
  
  wakeWordDetectionActive = true;
  lastWakeWordTime = 0;
  
  Serial.println("[WakeWord] Detection started - listening for 'Hey Bob'");
  Serial.printf("[WakeWord] Using trained pattern (%d samples, %.1fs)\\n", 
                WAKE_WORD_SAMPLES, (float)WAKE_WORD_SAMPLES / MIC_SAMPLE_RATE);
  Serial.printf("[WakeWord] Correlation threshold: %.2f\\n", CORRELATION_THRESHOLD);
  
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
 * This processes audio and detects the wake word pattern using correlation
 */
void updateWakeWordDetection() {
  if (!wakeWordDetectionActive || !wakeWordTrained) {
    return;
  }
  
  if (audioBuffer == nullptr) {
    return;  
  }
  
  // Throttle detection to avoid excessive I2S reads
  static unsigned long lastUpdateTime = 0;
  if (millis() - lastUpdateTime < 100) {
    return;
  }
  lastUpdateTime = millis();
  
  // Cooldown check to prevent multiple detections
  if (millis() - lastWakeWordTime < WAKE_WORD_COOLDOWN) {
    return;
  }
  
  // Read audio samples
  const size_t sampleCount = 512;
  int16_t samples[sampleCount];
  
  size_t bytesRead = readAudioSamples(samples, sampleCount);
  if (bytesRead == 0) {
    return;
  }
  
  // Add samples to rolling buffer
  for (size_t i = 0; i < sampleCount; i++) {
    audioBuffer[audioBufferIndex] = samples[i];
    audioBufferIndex = (audioBufferIndex + 1) % AUDIO_BUFFER_SAMPLES;
  }
  
  // Check correlation with trained pattern
  if (matchesWakeWordPattern()) {
    Serial.println("[WakeWord] *** DETECTED: Hey Bob! ***");
    lastWakeWordTime = millis();
    
    // Call callback if registered
    if (wakeWordCallback != nullptr) {
      wakeWordCallback();
    }
  }
}

/**
 * Train the wake word pattern by recording actual audio waveform
 * Call this function, then say "Hey Bob" clearly
 */
void trainWakeWord() {
  if (!micInitialized) {
    Serial.println("[WakeWord] ERROR: Microphone not initialized");
    return;
  }
  
  // Allocate memory for wake word pattern if needed
  if (wakeWordPattern == nullptr) {
    wakeWordPattern = (int16_t*)malloc(WAKE_WORD_SAMPLES * sizeof(int16_t));
    if (wakeWordPattern == nullptr) {
      Serial.println("[WakeWord] ERROR: Failed to allocate pattern memory");
      return;
    }
  }
  
  Serial.println("\n[WakeWord] === Training Mode ===");
  Serial.println("[WakeWord] >>> SAY 'HEY BOB' NOW! <<<");
  Serial.println("[WakeWord] Recording will start immediately...");
  
  // Ensure recording is active
  bool wasRecording = recording;
  if (!recording) {
    startRecording();
  }
  
  delay(500);  // Brief delay for user to prepare
  Serial.println("[WakeWord] Recording started!");
  
  // Record waveform immediately
  const size_t chunkSize = 512;
  int16_t samples[chunkSize];
  size_t samplesRecorded = 0;
  
  while (samplesRecorded < WAKE_WORD_SAMPLES) {
    size_t bytesRead = readAudioSamples(samples, chunkSize);
    if (bytesRead == 0) {
      delay(50);
      continue;
    }
    
    // Copy samples to pattern buffer
    size_t samplesToCopy = min((size_t)(bytesRead / sizeof(int16_t)), WAKE_WORD_SAMPLES - samplesRecorded);
    memcpy(&wakeWordPattern[samplesRecorded], samples, samplesToCopy * sizeof(int16_t));
    samplesRecorded += samplesToCopy;
    
    // Progress indicator
    if (samplesRecorded % 4000 == 0) {
      Serial.print(".");
    }
  }
  
  Serial.println();
  Serial.printf("[WakeWord] Recorded %d samples (%.1f seconds)\n", 
                samplesRecorded, (float)samplesRecorded / MIC_SAMPLE_RATE);
  
  wakeWordTrained = true;
  
  if (!wasRecording) {
    stopRecording();
  }
  
  Serial.println("[WakeWord] Training complete!");
  Serial.println("[WakeWord] Wake word pattern has been saved");
  Serial.println("[WakeWord] You can now use startWakeWordDetection()");
}

/**
 * Test microphone - show real-time audio levels
 */
void testMicrophoneLevel() {
  if (!micInitialized) {
    Serial.println("[Microphone] ERROR: Microphone not initialized");
    return;
  }
  
  Serial.println("\n[Microphone] === Voice Level Monitor ===");
  Serial.println("[Microphone] Showing raw audio RMS values...");
  Serial.println("[Microphone] Press any key to stop");
  Serial.println();
  
  // Ensure recording is active
  bool wasRecording = recording;
  if (!recording) {
    startRecording();
  }
  
  const size_t sampleCount = 64;
  int16_t samples[sampleCount];
  
  // Monitor for 10 seconds or until user presses a key
  unsigned long startTime = millis();
  while (millis() - startTime < 10000) {
    // Check if user wants to stop
    if (Serial.available() > 0) {
      Serial.read(); // Clear the input
      break;
    }
    
    size_t bytesRead = readAudioSamples(samples, sampleCount);
    if (bytesRead > 0) {
      // Calculate RMS directly
      int64_t sum = 0;
      for (size_t i = 0; i < sampleCount; i++) {
        int32_t val = samples[i];
        sum += val * val;
      }
      int32_t rms = sqrt(sum / sampleCount);
      int level = (rms * 100) / 32768; // Normalize to 0-100
      
      // Visual bar display
      Serial.print("[Microphone] RMS: ");
      Serial.printf("%5d (Level: %3d) ", rms, level);
      
      // Draw bar graph
      int barLength = (level > 100) ? 100 : level;
      for (int i = 0; i < barLength / 2; i++) {
        Serial.print("=");
      }
      
      Serial.println();
    }
    
    delay(100); // Update every 100ms
  }
  
  if (!wasRecording) {
    stopRecording();
  }
  
  Serial.println();
  Serial.println("[Microphone] Voice level monitor stopped");
}

/**
 * Record 3 seconds of audio and play it back
 */
void recordAndPlayback() {
  if (!micInitialized) {
    Serial.println("[Microphone] ERROR: Microphone not initialized");
    return;
  }
  
  // 3 seconds at 16kHz = 48,000 samples
  const size_t recordDuration = 3000; // milliseconds
  const size_t totalSamples = (MIC_SAMPLE_RATE * recordDuration) / 1000;
  
  // Allocate buffer for recording
  int16_t* recordBuffer = (int16_t*)malloc(totalSamples * sizeof(int16_t));
  if (!recordBuffer) {
    Serial.println("[Microphone] ERROR: Memory allocation failed");
    Serial.printf("[Microphone] Needed: %d bytes\n", totalSamples * sizeof(int16_t));
    return;
  }
  
  Serial.println("\n[Microphone] === Record & Playback Test ===");
  Serial.println("[Microphone] Recording for 3 seconds...");
  Serial.println("[Microphone] Speak now!");
  
  // Ensure recording is active
  bool wasRecording = recording;
  if (!recording) {
    startRecording();
  }
  
  // Record audio
  size_t samplesRecorded = 0;
  const size_t chunkSize = 512;
  unsigned long startTime = millis();
  
  while (samplesRecorded < totalSamples && (millis() - startTime) < recordDuration) {
    size_t samplesToRead = min(chunkSize, totalSamples - samplesRecorded);
    size_t bytesRead = readAudioSamples(&recordBuffer[samplesRecorded], samplesToRead);
    
    if (bytesRead > 0) {
      samplesRecorded += bytesRead / sizeof(int16_t);
      
      // Progress indicator
      if (samplesRecorded % 8000 == 0) {
        Serial.print(".");
      }
    }
  }
  
  Serial.println();
  Serial.printf("[Microphone] Recorded %d samples (%.1f seconds)\n", 
                samplesRecorded, (float)samplesRecorded / MIC_SAMPLE_RATE);
  
  if (!wasRecording) {
    stopRecording();
  }
  
  // Small delay before playback
  delay(500);
  
  // Play back through speaker
  Serial.println("[Speaker] Playing back recording...");
  
  // Convert mono to stereo and play
  const size_t playbackChunkSize = 1024;
  int16_t stereoBuffer[playbackChunkSize * 2];
  
  for (size_t i = 0; i < samplesRecorded; i += playbackChunkSize) {
    size_t samplesToPlay = min(playbackChunkSize, samplesRecorded - i);
    
    // Convert mono to stereo
    for (size_t j = 0; j < samplesToPlay; j++) {
      stereoBuffer[j * 2] = recordBuffer[i + j];      // Left channel
      stereoBuffer[j * 2 + 1] = recordBuffer[i + j];  // Right channel
    }
    
    // Write to speaker I2S (I2S_NUM_1)
    size_t bytesWritten = 0;
    i2s_write(I2S_NUM_1, stereoBuffer, samplesToPlay * sizeof(int16_t) * 2, 
              &bytesWritten, portMAX_DELAY);
    
    // Progress indicator
    if (i % 8000 == 0) {
      Serial.print(".");
    }
  }
  
  Serial.println();
  Serial.println("[Speaker] Playback complete!");
  
  // Free buffer
  free(recordBuffer);
  Serial.println("[Microphone] Record & Playback test finished");
}
