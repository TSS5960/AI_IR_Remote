/*
 * Speaker Control - Header
 * Voice feedback using MAX98357 I2S amplifier
 */

#ifndef SPEAKER_CONTROL_H
#define SPEAKER_CONTROL_H

#include "config.h"

// Voice feedback types
enum VoiceFeedback {
  VOICE_POWER_ON,
  VOICE_POWER_OFF,
  VOICE_TEMP_UP,
  VOICE_TEMP_DOWN,
  VOICE_MODE_COOL,
  VOICE_MODE_HEAT,
  VOICE_MODE_DRY,
  VOICE_MODE_FAN,
  VOICE_MODE_AUTO,
  VOICE_FAN_LOW,
  VOICE_FAN_MED,
  VOICE_FAN_HIGH,
  VOICE_FAN_AUTO,
  VOICE_READY
};

// Initialize speaker
void initSpeaker();

// Play voice feedback
void playVoice(VoiceFeedback voice);

// Play a simple beep (for testing or fallback)
void playBeep(int frequency, int duration);

// Play a short action confirmation tone
void playActionTone();

// Play temperature announcement
void playTemperature(int temp);

// Set speaker volume (0-100)
void setSpeakerVolume(int volume);

// Stop current playback
void stopSpeaker();

// Test speaker with different sounds
void testSpeaker();

// Get speaker volume
int getSpeakerVolume();

#endif // SPEAKER_CONTROL_H
