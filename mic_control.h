/*
 * Microphone Control - Header
 * INMP441 I2S MEMS Microphone
 */

#ifndef MIC_CONTROL_H
#define MIC_CONTROL_H

#include "config.h"
#include <stdint.h>

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

// Test microphone - show voice levels in real-time
void testMicrophoneLevel();

// Record and playback test (3 seconds)
void recordAndPlayback();

#endif // MIC_CONTROL_H
