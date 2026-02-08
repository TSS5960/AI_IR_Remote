/*
 * Voice Command Processing - Header
 *
 * Handles voice command pipeline:
 * 1. Record audio after wake word detection
 * 2. Send to Groq Whisper API for transcription
 * 3. Send text to Groq LLaMA for intent parsing
 * 4. Execute parsed commands
 */

#ifndef VOICE_COMMAND_H
#define VOICE_COMMAND_H

#include <Arduino.h>

// ==============================================================================
// Voice Command State
// ==============================================================================

enum VoiceCommandState {
  VOICE_IDLE,           // Waiting for wake word
  VOICE_LISTENING,      // Recording user command
  VOICE_PROCESSING,     // Sending to API
  VOICE_EXECUTING,      // Executing parsed command
  VOICE_ERROR           // Error occurred
};

// ==============================================================================
// Parsed Action Structure
// ==============================================================================

struct VoiceAction {
  String type;          // Action type: ac_on, ac_off, ac_temp, etc.
  int value;            // Optional value (temperature, IR signal number, etc.)
  bool hasValue;        // Whether value is set
};

// ==============================================================================
// Voice Command Result
// ==============================================================================

struct VoiceCommandResult {
  bool success;
  String transcription;     // What the user said
  String rawIntent;         // Raw LLM response
  VoiceAction actions[5];   // Up to 5 actions
  int actionCount;
  String errorMessage;
};

// ==============================================================================
// Public API
// ==============================================================================

/**
 * Initialize voice command system
 * Must be called after WiFi is connected
 * @return true if initialization successful
 */
bool initVoiceCommand();

/**
 * Start listening for voice command
 * Called after wake word is detected
 * LED turns green, starts recording
 */
void startVoiceCommand();

/**
 * Update voice command processing
 * Call this in loop() - handles state machine
 */
void updateVoiceCommand();

/**
 * Check if voice command system is busy
 * @return true if recording or processing
 */
bool isVoiceCommandBusy();

/**
 * Get current voice command state
 * @return current state
 */
VoiceCommandState getVoiceCommandState();

/**
 * Get last voice command result
 * @return pointer to last result
 */
VoiceCommandResult* getLastVoiceResult();

/**
 * Cancel current voice command
 * Stops recording and resets state
 */
void cancelVoiceCommand();

// ==============================================================================
// Callback Type
// ==============================================================================

/**
 * Callback when voice command processing is complete
 * @param result The processed result with actions
 */
typedef void (*VoiceCommandCallback)(VoiceCommandResult* result);

/**
 * Set callback for voice command completion
 * @param callback Function to call when processing is done
 */
void setVoiceCommandCallback(VoiceCommandCallback callback);

#endif // VOICE_COMMAND_H
