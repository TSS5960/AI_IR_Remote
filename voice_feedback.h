/*
 * Voice Feedback Module - Header
 *
 * Provides Text-to-Speech (TTS) feedback for:
 * - Action confirmations ("Light turned on", "Temperature set to 24")
 * - Weather queries ("It's 32 degrees and sunny")
 * - Sensor readings ("The room temperature is 26 degrees")
 * - General conversational responses
 *
 * Uses OpenAI TTS API for natural voice synthesis
 */

#ifndef VOICE_FEEDBACK_H
#define VOICE_FEEDBACK_H

#include <Arduino.h>

// ==============================================================================
// Response Types
// ==============================================================================

enum ResponseType {
  RESPONSE_ACTION_CONFIRM,    // Action confirmation (short)
  RESPONSE_WEATHER,           // Weather information
  RESPONSE_SENSOR,            // Sensor reading
  RESPONSE_CONVERSATION,      // General conversation
  RESPONSE_ERROR              // Error message
};

// ==============================================================================
// Weather Data Structure
// ==============================================================================

struct WeatherData {
  bool valid;                 // Whether data is valid
  float temperature;          // Temperature in Celsius
  float feelsLike;            // Feels like temperature
  int humidity;               // Humidity percentage
  String description;         // Weather description (e.g., "clear sky")
  String mainCondition;       // Main condition (e.g., "Clear", "Rain")
  float windSpeed;            // Wind speed in m/s
  unsigned long fetchTime;    // When data was fetched
};

// Note: Sensor data uses SensorData struct from sensors.h

// ==============================================================================
// Public API
// ==============================================================================

/**
 * Initialize voice feedback system
 * @return true if initialization successful
 */
bool initVoiceFeedback();

/**
 * Speak text using TTS API
 * @param text Text to speak
 * @param blocking If true, wait for speech to complete
 * @return true if TTS request was successful
 */
bool speakText(const String& text, bool blocking = true);

/**
 * Speak action confirmation
 * @param action Action that was performed (e.g., "light on", "temperature 24")
 */
void speakActionConfirm(const String& action);

/**
 * Speak weather information
 * Fetches current weather and speaks it
 */
void speakWeather();

/**
 * Speak sensor readings
 * Reads current sensors and speaks the values
 */
void speakSensorReadings();

/**
 * Speak a general response
 * @param response The response text to speak
 */
void speakResponse(const String& response);

/**
 * Speak error message
 * @param error Error description
 */
void speakError(const String& error);

// ==============================================================================
// Weather API Functions
// ==============================================================================

/**
 * Fetch weather data from OpenWeatherMap API
 * @param data Pointer to WeatherData struct to fill
 * @return true if weather data was fetched successfully
 */
bool fetchWeatherData(WeatherData* data);

/**
 * Get cached weather data (if available and recent)
 * @return Pointer to cached weather data
 */
WeatherData* getCachedWeather();

/**
 * Format weather data as speech text
 * @param data Weather data to format
 * @return Formatted speech text
 */
String formatWeatherSpeech(const WeatherData& data);

// ==============================================================================
// Sensor Reading Functions
// ==============================================================================

// Note: Uses readAllSensors() from sensors.h internally

// ==============================================================================
// TTS Status Functions
// ==============================================================================

/**
 * Check if TTS is currently speaking
 * @return true if audio is playing
 */
bool isSpeaking();

/**
 * Stop current TTS playback
 */
void stopSpeaking();

/**
 * Check if TTS API is configured
 * @return true if API key is set
 */
bool isTTSConfigured();

/**
 * Check if Weather API is configured
 * @return true if API key is set
 */
bool isWeatherConfigured();

#endif // VOICE_FEEDBACK_H
