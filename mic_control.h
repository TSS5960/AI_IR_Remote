/*
 * Microphone Control - Header
 * INMP441 I2S MEMS Microphone
 */

#ifndef MIC_CONTROL_H
#define MIC_CONTROL_H

#include "config.h"
#include <stdint.h>

// Wake word callback function type
typedef void (*WakeWordCallback)(void);

// Initialize microphone
bool initMicrophone();

// Start recording audio
bool startRecording();

// Stop recording audio
void stopRecording();

// Read audio samples (blocking call)
// Returns number of bytes read
size_t readAudioSamples(int16_t* buffer, size_t bufferSize);

// Check if microphone is initialized
bool isMicrophoneReady();

// Get current audio level (0-100)
int getAudioLevel();

// Deinitialize microphone
void deinitMicrophone();

// ========== Wake Word Detection ==========

// Start wake word detection (runs in background)
// Continuously monitors audio for wake word pattern
bool startWakeWordDetection();

// Stop wake word detection
void stopWakeWordDetection();

// Set callback function to be called when wake word is detected
void setWakeWordCallback(WakeWordCallback callback);

// Check if wake word detection is active
bool isWakeWordDetectionActive();

// Update wake word detection (call this in loop())
void updateWakeWordDetection();

// Train/capture the wake word pattern (say "Hey Bob" when calling this)
void trainWakeWord();

#endif // MIC_CONTROL_H
