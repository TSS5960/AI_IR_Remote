/*
 * Edge Impulse Wake Word Detection - Implementation
 * Uses trained "Hey Bob" model for wake word detection
 */

#include "ei_wake_word.h"
#include "mic_control.h"
#include <Arduino.h>

// Edge Impulse inference library
#include <Hey_Bob_inferencing.h>

// ========== Configuration ==========

// Confidence threshold for wake word detection (0.0 - 1.0)
static float confidenceThreshold = 0.85f;  // 85% confidence required

// Cooldown between detections to prevent rapid re-triggering
static const unsigned long DETECTION_COOLDOWN_MS = 2000;  // 2 seconds

// Update interval for inference (throttle to reduce CPU usage)
static const unsigned long UPDATE_INTERVAL_MS = 100;  // 100ms between updates

// ========== State Variables ==========

static bool eiInitialized = false;
static bool eiActive = false;
static EIWakeWordCallback wakeCallback = nullptr;
static unsigned long lastDetectionTime = 0;
static unsigned long lastUpdateTime = 0;

// Audio buffer for inference
static int16_t sampleBuffer[EI_CLASSIFIER_RAW_SAMPLE_COUNT];
static size_t sampleIndex = 0;
static bool bufferReady = false;

// ========== Helper Functions ==========

/**
 * Callback function to provide audio data to Edge Impulse
 * Converts int16 samples to float (-1.0 to 1.0)
 */
static int get_audio_signal_data(size_t offset, size_t length, float *out_ptr) {
  for (size_t i = 0; i < length; i++) {
    out_ptr[i] = (float)sampleBuffer[offset + i] / 32768.0f;
  }
  return 0;
}

/**
 * Run inference on the current audio buffer
 * Returns the confidence score for "hey_bob" class
 */
static float runInference() {
  // Create signal from audio buffer
  signal_t signal;
  signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  signal.get_data = get_audio_signal_data;

  // Run the classifier
  ei_impulse_result_t result;
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);

  if (err != EI_IMPULSE_OK) {
    Serial.printf("[EI] Inference error: %d\n", err);
    return 0.0f;
  }

  // Find the "hey_bob" class and return its confidence
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (strcmp(result.classification[i].label, "hey_bob") == 0) {
      return result.classification[i].value;
    }
  }

  // If "hey_bob" class not found, return 0
  return 0.0f;
}

// ========== Public API ==========

bool initEIWakeWord() {
  Serial.println("[EI] Initializing Edge Impulse wake word detection...");

  // Verify microphone is ready
  if (!isMicrophoneReady()) {
    Serial.println("[EI] ERROR: Microphone not initialized");
    return false;
  }

  // Print model information
  Serial.printf("[EI] Model: %s\n", EI_CLASSIFIER_PROJECT_NAME);
  Serial.printf("[EI] Sample rate: %d Hz\n", EI_CLASSIFIER_FREQUENCY);
  Serial.printf("[EI] Frame size: %d samples\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT);
  Serial.printf("[EI] Frame length: %d ms\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT * 1000 / EI_CLASSIFIER_FREQUENCY);
  Serial.printf("[EI] Labels: ");
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    Serial.printf("%s", ei_classifier_inferencing_categories[i]);
    if (i < EI_CLASSIFIER_LABEL_COUNT - 1) Serial.print(", ");
  }
  Serial.println();
  Serial.printf("[EI] Confidence threshold: %.0f%%\n", confidenceThreshold * 100);

  // Verify sample rate matches
  if (EI_CLASSIFIER_FREQUENCY != 16000) {
    Serial.printf("[EI] WARNING: Model expects %d Hz, microphone is 16000 Hz\n", EI_CLASSIFIER_FREQUENCY);
  }

  // Clear the sample buffer
  memset(sampleBuffer, 0, sizeof(sampleBuffer));
  sampleIndex = 0;
  bufferReady = false;

  eiInitialized = true;
  Serial.println("[EI] Edge Impulse initialized successfully");

  return true;
}

void setEIWakeWordCallback(EIWakeWordCallback callback) {
  wakeCallback = callback;
  Serial.println("[EI] Wake word callback registered");
}

bool startEIWakeWord() {
  if (!eiInitialized) {
    Serial.println("[EI] ERROR: Not initialized, call initEIWakeWord() first");
    return false;
  }

  if (eiActive) {
    Serial.println("[EI] Already active");
    return true;
  }

  // Start recording audio
  if (!startRecording()) {
    Serial.println("[EI] ERROR: Failed to start recording");
    return false;
  }

  // Reset state
  sampleIndex = 0;
  bufferReady = false;
  lastDetectionTime = 0;
  lastUpdateTime = 0;

  eiActive = true;
  Serial.println("[EI] Wake word detection started - listening for 'Hey Bob'");

  return true;
}

void stopEIWakeWord() {
  if (!eiActive) {
    return;
  }

  stopRecording();
  eiActive = false;
  Serial.println("[EI] Wake word detection stopped");
}

void updateEIWakeWord() {
  if (!eiInitialized || !eiActive) {
    return;
  }

  // Throttle updates to reduce CPU usage
  unsigned long currentTime = millis();
  if (currentTime - lastUpdateTime < UPDATE_INTERVAL_MS) {
    return;
  }
  lastUpdateTime = currentTime;

  // Check cooldown
  if (currentTime - lastDetectionTime < DETECTION_COOLDOWN_MS) {
    return;
  }

  // Read audio samples from microphone
  const size_t chunkSize = 512;
  int16_t chunk[chunkSize];
  size_t bytesRead = readAudioSamples(chunk, chunkSize);

  if (bytesRead == 0) {
    return;
  }

  size_t samplesRead = bytesRead / sizeof(int16_t);

  // Add samples to the sliding window buffer
  // Shift buffer left and add new samples at the end
  if (sampleIndex + samplesRead >= EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
    // Buffer is full or will overflow - shift existing samples left
    size_t samplesToShift = samplesRead;
    if (samplesToShift > EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
      samplesToShift = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    }

    // Move existing samples to make room
    memmove(sampleBuffer,
            &sampleBuffer[samplesToShift],
            (EI_CLASSIFIER_RAW_SAMPLE_COUNT - samplesToShift) * sizeof(int16_t));

    // Add new samples at the end
    size_t copyStart = EI_CLASSIFIER_RAW_SAMPLE_COUNT - samplesRead;
    if (samplesRead > EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
      // More samples than buffer size - only copy last portion
      memcpy(&sampleBuffer[0],
             &chunk[samplesRead - EI_CLASSIFIER_RAW_SAMPLE_COUNT],
             EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t));
    } else {
      memcpy(&sampleBuffer[copyStart], chunk, samplesRead * sizeof(int16_t));
    }

    sampleIndex = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    bufferReady = true;
  } else {
    // Buffer not full yet - append samples
    memcpy(&sampleBuffer[sampleIndex], chunk, samplesRead * sizeof(int16_t));
    sampleIndex += samplesRead;

    // Check if buffer is now full
    if (sampleIndex >= EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
      bufferReady = true;
    }
  }

  // Run inference if buffer is ready
  if (bufferReady) {
    float confidence = runInference();

    // Check if wake word detected with sufficient confidence
    if (confidence >= confidenceThreshold) {
      Serial.printf("[EI] *** HEY BOB DETECTED! Confidence: %.1f%% ***\n", confidence * 100);

      lastDetectionTime = currentTime;

      // Clear the buffer to prevent re-detection of the same audio
      memset(sampleBuffer, 0, sizeof(sampleBuffer));
      sampleIndex = 0;
      bufferReady = false;

      // Call the callback if registered
      if (wakeCallback != nullptr) {
        wakeCallback(confidence);
      }
    }
  }
}

bool isEIReady() {
  return eiInitialized;
}

bool isEIWakeWordActive() {
  return eiActive;
}

float getEIConfidenceThreshold() {
  return confidenceThreshold;
}

void setEIConfidenceThreshold(float threshold) {
  if (threshold < 0.0f) threshold = 0.0f;
  if (threshold > 1.0f) threshold = 1.0f;
  confidenceThreshold = threshold;
  Serial.printf("[EI] Confidence threshold set to %.0f%%\n", threshold * 100);
}
