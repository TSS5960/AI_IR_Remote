/*
 * Microphone Control - Implementation
 * INMP441 I2S MEMS Microphone
 *
 * Note: Wake word detection is now handled by Edge Impulse (ei_wake_word.cpp)
 */

#include "mic_control.h"
#include <Arduino.h>
#include <driver/i2s.h>

// I2S configuration for INMP441
#define I2S_MIC_PORT I2S_NUM_0

// Microphone state
static bool micInitialized = false;
static bool recording = false;

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
