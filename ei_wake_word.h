/*
 * Edge Impulse Wake Word Detection
 * Uses trained "Hey Bob" model for wake word detection
 */

#ifndef EI_WAKE_WORD_H
#define EI_WAKE_WORD_H

#include <stdint.h>

// Callback type for wake word detection
typedef void (*EIWakeWordCallback)(float confidence);

// Initialize Edge Impulse wake word detection
// Returns true if successful
bool initEIWakeWord();

// Set callback function for wake word detection
void setEIWakeWordCallback(EIWakeWordCallback callback);

// Update wake word detection - call this in loop()
// Continuously monitors audio and runs inference
void updateEIWakeWord();

// Check if Edge Impulse is initialized and ready
bool isEIReady();

// Get the confidence threshold (for debugging)
float getEIConfidenceThreshold();

// Set the confidence threshold (0.0 - 1.0)
void setEIConfidenceThreshold(float threshold);

// Start/stop wake word detection
bool startEIWakeWord();
void stopEIWakeWord();

// Check if wake word detection is active
bool isEIWakeWordActive();

#endif // EI_WAKE_WORD_H
