/*
 * Voice Feedback Module - Implementation
 *
 * Provides TTS feedback using Google Cloud TTS API (FREE: 4M chars/month)
 * and weather data from OpenWeatherMap
 */

#include "voice_feedback.h"
#include "groq_config.h"
#include "config.h"
#include "sensors.h"
#include "speaker_control.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include "mbedtls/base64.h"

// ==============================================================================
// Constants
// ==============================================================================

#define TTS_TIMEOUT_MS 30000
#define WEATHER_CACHE_DURATION_MS 300000  // Cache weather for 5 minutes
#define TTS_SAMPLE_RATE 24000             // Google Cloud TTS LINEAR16 sample rate
#define TTS_BUFFER_SIZE 8192              // Audio buffer for base64 decoding
#define TTS_AUDIO_BUFFER_SIZE 500000      // Max decoded audio size (in PSRAM) - ~10 seconds at 24kHz
#define TTS_RESPONSE_BUFFER_SIZE 700000   // Max HTTP response size (base64 WAV ~1.4x audio size)

// Use the same I2S port as speaker_control
#define TTS_I2S_PORT I2S_NUM_1

// ==============================================================================
// State Variables
// ==============================================================================

static bool feedbackInitialized = false;
static bool currentlySpeaking = false;
static WeatherData cachedWeather = {false};

// Audio buffers for TTS playback (in PSRAM)
static uint8_t* ttsAudioBuffer = nullptr;      // For decoded audio
static char* ttsBase64Buffer = nullptr;        // For base64 response

// External speaker state
extern bool speakerInitialized;

// Forward declaration for sensor speech formatting
static String formatSensorSpeechInternal(const SensorData& data);

// ==============================================================================
// I2S Reconfiguration for TTS Playback
// ==============================================================================

static bool reconfigureI2SForTTS() {
  // Uninstall current I2S driver to reconfigure for TTS sample rate
  i2s_driver_uninstall(TTS_I2S_PORT);

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = TTS_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // Mono for TTS
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = SPK_BCLK_PIN,
    .ws_io_num = SPK_LRCLK_PIN,
    .data_out_num = SPK_SD_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err = i2s_driver_install(TTS_I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[TTS] I2S driver install failed: %d\n", err);
    return false;
  }

  err = i2s_set_pin(TTS_I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("[TTS] I2S set pin failed: %d\n", err);
    return false;
  }

  return true;
}

static void restoreI2SForBeeps() {
  // Restore I2S configuration for normal beep sounds
  i2s_driver_uninstall(TTS_I2S_PORT);

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SPEAKER_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // Stereo for beeps
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = SPK_BCLK_PIN,
    .ws_io_num = SPK_LRCLK_PIN,
    .data_out_num = SPK_SD_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(TTS_I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(TTS_I2S_PORT, &pin_config);
  i2s_set_clk(TTS_I2S_PORT, SPEAKER_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

// ==============================================================================
// Initialization
// ==============================================================================

bool initVoiceFeedback() {
  Serial.println("[TTS] Initializing voice feedback system (Google Cloud TTS)...");

  // Allocate audio buffer in PSRAM for decoded audio
  ttsAudioBuffer = (uint8_t*)ps_malloc(TTS_AUDIO_BUFFER_SIZE);
  if (!ttsAudioBuffer) {
    Serial.println("[TTS] ERROR: Failed to allocate audio buffer");
    return false;
  }

  // Allocate buffer for HTTP response (base64 JSON response)
  ttsBase64Buffer = (char*)ps_malloc(TTS_RESPONSE_BUFFER_SIZE);
  if (!ttsBase64Buffer) {
    Serial.println("[TTS] ERROR: Failed to allocate response buffer");
    free(ttsAudioBuffer);
    return false;
  }

  Serial.printf("[TTS] Allocated %dKB for audio, %dKB for response\n",
                TTS_AUDIO_BUFFER_SIZE / 1024, TTS_RESPONSE_BUFFER_SIZE / 1024);

  // Check if TTS is configured
  if (!isTTSConfigured()) {
    Serial.println("[TTS] WARNING: TTS API key not configured");
    Serial.println("[TTS] Add your Google Cloud API key to groq_config.h");
  }

  // Check if Weather is configured
  if (!isWeatherConfigured()) {
    Serial.println("[TTS] WARNING: Weather API key not configured");
    Serial.println("[TTS] Add your OpenWeatherMap API key to groq_config.h");
  }

  feedbackInitialized = true;
  Serial.println("[TTS] Voice feedback system initialized");
  return true;
}

// ==============================================================================
// TTS API Functions
// ==============================================================================

bool speakText(const String& text, bool blocking) {
  if (!feedbackInitialized) {
    Serial.println("[TTS] ERROR: Not initialized");
    return false;
  }

  if (!isTTSConfigured()) {
    Serial.println("[TTS] TTS not configured, using beep fallback");
    playActionTone();
    return false;
  }

  if (text.length() == 0) {
    return false;
  }

  Serial.println("[TTS] Speaking: \"" + text + "\"");
  currentlySpeaking = true;

  WiFiClientSecure client;
  client.setInsecure();  // Skip certificate verification

  if (!client.connect(TTS_API_HOST, 443)) {
    Serial.println("[TTS] ERROR: Failed to connect to Google Cloud TTS");
    currentlySpeaking = false;
    playBeep(200, 100);  // Error beep
    return false;
  }

  // Build Google Cloud TTS JSON request
  JsonDocument requestDoc;

  // Input configuration
  JsonObject input = requestDoc["input"].to<JsonObject>();
  input["text"] = text;

  // Voice configuration
  JsonObject voice = requestDoc["voice"].to<JsonObject>();
  voice["languageCode"] = TTS_LANGUAGE_CODE;
  voice["name"] = TTS_VOICE_NAME;

  // Audio configuration
  JsonObject audioConfig = requestDoc["audioConfig"].to<JsonObject>();
  audioConfig["audioEncoding"] = "LINEAR16";  // 16-bit PCM
  audioConfig["sampleRateHertz"] = TTS_SAMPLE_RATE;
  audioConfig["speakingRate"] = TTS_SPEAKING_RATE;
  audioConfig["pitch"] = TTS_PITCH;

  String requestBody;
  serializeJson(requestDoc, requestBody);

  // Build URL with API key
  String url = String(TTS_API_ENDPOINT) + "?key=" + TTS_API_KEY;

  // Send HTTP request
  client.print("POST " + url + " HTTP/1.1\r\n");
  client.print("Host: " + String(TTS_API_HOST) + "\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: " + String(requestBody.length()) + "\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print(requestBody);

  // Wait for response headers
  unsigned long timeout = millis() + TTS_TIMEOUT_MS;
  while (!client.available() && millis() < timeout) {
    delay(10);
  }

  if (!client.available()) {
    Serial.println("[TTS] ERROR: Google Cloud TTS timeout");
    client.stop();
    currentlySpeaking = false;
    return false;
  }

  // Read HTTP headers
  bool headersEnded = false;
  int httpStatus = 0;
  bool isChunked = false;
  int contentLength = -1;

  while (client.available() && !headersEnded) {
    String line = client.readStringUntil('\n');
    line.trim();

    // Parse HTTP status
    if (line.startsWith("HTTP/")) {
      int spaceIdx = line.indexOf(' ');
      if (spaceIdx > 0) {
        httpStatus = line.substring(spaceIdx + 1, spaceIdx + 4).toInt();
      }
    }

    // Check for chunked transfer encoding
    if (line.startsWith("Transfer-Encoding:") && line.indexOf("chunked") > 0) {
      isChunked = true;
    }

    // Get content length
    if (line.startsWith("Content-Length:")) {
      contentLength = line.substring(15).toInt();
    }

    if (line.length() == 0) {
      headersEnded = true;
    }
  }

  // Read response body into pre-allocated PSRAM buffer (avoids String fragmentation)
  size_t responseLen = 0;
  timeout = millis() + TTS_TIMEOUT_MS;

  if (isChunked) {
    // Handle chunked transfer encoding
    while (client.connected() && millis() < timeout) {
      // Wait for chunk size line
      while (!client.available() && client.connected() && millis() < timeout) {
        delay(1);
      }
      if (!client.available()) break;

      // Read chunk size line
      String chunkSizeLine = client.readStringUntil('\n');
      chunkSizeLine.trim();

      // Parse hex chunk size
      int chunkSize = (int)strtol(chunkSizeLine.c_str(), NULL, 16);
      if (chunkSize == 0) {
        break;  // End of chunks (0\r\n marks end)
      }

      // Read chunk data directly into PSRAM buffer
      while (chunkSize > 0 && client.connected() && millis() < timeout) {
        if (client.available()) {
          int toRead = min(chunkSize, (int)client.available());
          toRead = min(toRead, (int)(TTS_RESPONSE_BUFFER_SIZE - responseLen - 1));
          if (toRead <= 0) {
            Serial.println("[TTS] ERROR: Response buffer overflow");
            break;
          }
          int bytesRead = client.readBytes(ttsBase64Buffer + responseLen, toRead);
          responseLen += bytesRead;
          chunkSize -= bytesRead;
        } else {
          delay(1);  // Wait for more data
        }
      }

      // Skip trailing CRLF after chunk
      while (client.connected() && millis() < timeout) {
        if (client.available() >= 2) {
          client.read();  // \r
          client.read();  // \n
          break;
        }
        delay(1);
      }
    }
  } else {
    // Non-chunked response - read directly into buffer
    while (client.connected() && millis() < timeout) {
      if (client.available()) {
        int toRead = min((int)client.available(), (int)(TTS_RESPONSE_BUFFER_SIZE - responseLen - 1));
        if (toRead <= 0) break;
        int bytesRead = client.readBytes(ttsBase64Buffer + responseLen, toRead);
        responseLen += bytesRead;
      } else {
        delay(1);
      }
    }
  }
  ttsBase64Buffer[responseLen] = '\0';  // Null terminate
  client.stop();

  // Debug: show first part of response
  Serial.printf("[TTS] Response length: %d bytes, chunked: %s\n",
                responseLen, isChunked ? "yes" : "no");
  if (responseLen > 0) {
    char preview[101];
    strncpy(preview, ttsBase64Buffer, min((size_t)100, responseLen));
    preview[min((size_t)100, responseLen)] = '\0';
    Serial.printf("[TTS] Response start: %s\n", preview);
  }

  // Check for API errors
  if (httpStatus != 200) {
    Serial.printf("[TTS] ERROR: Google Cloud API returned status %d\n", httpStatus);
    Serial.printf("[TTS] Error: %s\n", ttsBase64Buffer);
    currentlySpeaking = false;
    playBeep(200, 100);  // Error beep
    return false;
  }

  // Extract base64 audio content directly (avoids JSON parsing memory issues)
  // Response format: {"audioContent": "base64_data_here..."}
  const char* audioMarker = "\"audioContent\": \"";
  char* audioStart = strstr(ttsBase64Buffer, audioMarker);
  if (!audioStart) {
    // Try without space after colon
    audioMarker = "\"audioContent\":\"";
    audioStart = strstr(ttsBase64Buffer, audioMarker);
  }

  if (!audioStart) {
    Serial.println("[TTS] ERROR: No audioContent found in response");
    char preview[201];
    strncpy(preview, ttsBase64Buffer, min((size_t)200, responseLen));
    preview[min((size_t)200, responseLen)] = '\0';
    Serial.printf("[TTS] First 200 chars: %s\n", preview);
    currentlySpeaking = false;
    playBeep(200, 100);
    return false;
  }

  // Move to start of base64 data
  const char* audioContent = audioStart + strlen(audioMarker);

  // Find the end of base64 data (closing quote)
  char* audioEnd = strchr((char*)audioContent, '"');
  if (!audioEnd) {
    Serial.println("[TTS] ERROR: Could not find end of audioContent");
    currentlySpeaking = false;
    playBeep(200, 100);
    return false;
  }

  // Null-terminate the base64 string in place
  *audioEnd = '\0';

  Serial.printf("[TTS] Extracted base64 audio: %d bytes\n", (int)(audioEnd - audioContent));

  size_t base64Len = strlen(audioContent);
  Serial.printf("[TTS] Received %d bytes of base64 audio\n", base64Len);

  // Decode base64 to PCM audio
  size_t decodedLen = 0;
  int ret = mbedtls_base64_decode(ttsAudioBuffer, TTS_AUDIO_BUFFER_SIZE,
                                   &decodedLen,
                                   (const unsigned char*)audioContent, base64Len);

  if (ret != 0) {
    Serial.printf("[TTS] ERROR: Base64 decode failed: %d\n", ret);
    currentlySpeaking = false;
    playBeep(200, 100);
    return false;
  }

  // Skip WAV header if present (44 bytes for standard WAV)
  // Google Cloud TTS with LINEAR16 returns raw PCM, no header
  uint8_t* audioData = ttsAudioBuffer;
  size_t audioLen = decodedLen;

  Serial.printf("[TTS] Decoded %d bytes of PCM audio\n", audioLen);

  // Reconfigure I2S for TTS sample rate (24kHz mono)
  if (!reconfigureI2SForTTS()) {
    Serial.println("[TTS] ERROR: Failed to configure I2S for TTS");
    currentlySpeaking = false;
    restoreI2SForBeeps();
    return false;
  }

  // Play audio through I2S
  size_t bytesWritten = 0;
  size_t totalWritten = 0;
  size_t chunkSize = 1024;

  while (totalWritten < audioLen) {
    size_t toWrite = min(chunkSize, audioLen - totalWritten);
    i2s_write((i2s_port_t)TTS_I2S_PORT, audioData + totalWritten, toWrite, &bytesWritten, portMAX_DELAY);
    totalWritten += bytesWritten;
    yield();
  }

  // Wait for audio buffer to drain
  if (blocking) {
    // Estimate playback time: bytes / (sample_rate * 2 bytes per sample)
    int playbackMs = (audioLen * 1000) / (TTS_SAMPLE_RATE * 2);
    delay(playbackMs + 100);  // Add buffer
  }

  // Restore I2S for normal beep sounds
  restoreI2SForBeeps();

  Serial.printf("[TTS] Finished playing %d bytes\n", totalWritten);

  // Clear buffers after use (free up PSRAM for other tasks, prevent stale data)
  memset(ttsBase64Buffer, 0, TTS_RESPONSE_BUFFER_SIZE);
  memset(ttsAudioBuffer, 0, TTS_AUDIO_BUFFER_SIZE);

  currentlySpeaking = false;
  return true;
}

void speakActionConfirm(const String& action) {
  String speech = action + ".";
  speakText(speech, true);
}

void speakWeather() {
  WeatherData data;

  if (!fetchWeatherData(&data)) {
    speakError("I couldn't get the weather information right now.");
    return;
  }

  String speech = formatWeatherSpeech(data);
  speakText(speech, true);
}

void speakSensorReadings() {
  SensorData data = readAllSensors();

  String speech = formatSensorSpeechInternal(data);
  speakText(speech, true);
}

void speakResponse(const String& response) {
  speakText(response, true);
}

void speakError(const String& error) {
  String speech = "Sorry, " + error;
  speakText(speech, true);
}

// ==============================================================================
// Weather API Functions
// ==============================================================================

bool fetchWeatherData(WeatherData* data) {
  if (!isWeatherConfigured()) {
    Serial.println("[Weather] ERROR: Weather API not configured");
    data->valid = false;
    return false;
  }

  // Check cache first
  if (cachedWeather.valid &&
      (millis() - cachedWeather.fetchTime) < WEATHER_CACHE_DURATION_MS) {
    *data = cachedWeather;
    Serial.println("[Weather] Using cached weather data");
    return true;
  }

  Serial.println("[Weather] Fetching weather data...");

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(WEATHER_API_HOST, 443)) {
    Serial.println("[Weather] ERROR: Failed to connect to Weather API");
    data->valid = false;
    return false;
  }

  // Build request URL
  String url = String(WEATHER_API_ENDPOINT) +
               "?lat=" + WEATHER_LATITUDE +
               "&lon=" + WEATHER_LONGITUDE +
               "&units=" + WEATHER_UNITS +
               "&appid=" + WEATHER_API_KEY;

  // Send HTTP request
  client.print("GET " + url + " HTTP/1.1\r\n");
  client.print("Host: " + String(WEATHER_API_HOST) + "\r\n");
  client.print("Connection: close\r\n\r\n");

  // Wait for response
  unsigned long timeout = millis() + 10000;
  while (!client.available() && millis() < timeout) {
    delay(10);
  }

  if (!client.available()) {
    Serial.println("[Weather] ERROR: Weather API timeout");
    client.stop();
    data->valid = false;
    return false;
  }

  // Skip headers
  bool headersEnded = false;
  while (client.available() && !headersEnded) {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) {
      headersEnded = true;
    }
  }

  // Read body
  String response = "";
  while (client.available()) {
    response += client.readString();
  }
  client.stop();

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);

  if (error) {
    Serial.printf("[Weather] ERROR: Failed to parse response: %s\n", error.c_str());
    data->valid = false;
    return false;
  }

  // Extract weather data
  data->temperature = doc["main"]["temp"].as<float>();
  data->feelsLike = doc["main"]["feels_like"].as<float>();
  data->humidity = doc["main"]["humidity"].as<int>();
  data->description = doc["weather"][0]["description"].as<String>();
  data->mainCondition = doc["weather"][0]["main"].as<String>();
  data->windSpeed = doc["wind"]["speed"].as<float>();
  data->fetchTime = millis();
  data->valid = true;

  // Cache the data
  cachedWeather = *data;

  Serial.printf("[Weather] Temperature: %.1f°C, %s\n",
                data->temperature, data->description.c_str());
  return true;
}

WeatherData* getCachedWeather() {
  return &cachedWeather;
}

String formatWeatherSpeech(const WeatherData& data) {
  String speech = "Right now, it's ";
  speech += String((int)round(data.temperature)) + " degrees";

  // Add feels like if significantly different
  if (abs(data.feelsLike - data.temperature) > 2) {
    speech += ", but it feels like ";
    speech += String((int)round(data.feelsLike)) + " degrees";
  }

  speech += " with " + data.description + ".";

  // Add humidity if high
  if (data.humidity > 70) {
    speech += " It's quite humid at " + String(data.humidity) + " percent.";
  }

  // Add wind if notable
  if (data.windSpeed > 5) {
    speech += " There's some wind at " + String((int)round(data.windSpeed * 3.6)) + " kilometers per hour.";
  }

  return speech;
}

// ==============================================================================
// Sensor Reading Functions (uses sensors.h SensorData)
// ==============================================================================

static String formatSensorSpeechInternal(const SensorData& data) {
  String speech = "The room ";

  // Temperature (from DHT11)
  if (data.dht_valid && !isnan(data.dht_temperature)) {
    speech += "temperature is " + String((int)round(data.dht_temperature)) + " degrees celsius";
  }

  // Humidity (from DHT11)
  if (data.dht_valid && !isnan(data.dht_humidity)) {
    speech += ", humidity is " + String((int)round(data.dht_humidity)) + " percent";
  }

  // Light level (from BH1750/GY-30)
  if (data.light_valid && data.light_lux >= 0) {
    speech += ", and the light level is ";
    if (data.light_lux < 50) {
      speech += "very low, it's quite dark";
    } else if (data.light_lux < 200) {
      speech += "dim";
    } else if (data.light_lux < 500) {
      speech += "moderate";
    } else if (data.light_lux < 1000) {
      speech += "bright";
    } else {
      speech += "very bright";
    }
  }

  speech += ".";

  Serial.printf("[TTS] Sensor speech: Temp=%.1f°C, Hum=%.1f%%, Light=%.1f lux\n",
                data.dht_temperature, data.dht_humidity, data.light_lux);

  return speech;
}

// ==============================================================================
// Status Functions
// ==============================================================================

bool isSpeaking() {
  return currentlySpeaking;
}

void stopSpeaking() {
  if (currentlySpeaking) {
    i2s_zero_dma_buffer((i2s_port_t)TTS_I2S_PORT);
    restoreI2SForBeeps();
    currentlySpeaking = false;
  }
}

bool isTTSConfigured() {
  String key = TTS_API_KEY;
  return key.length() > 10 && !key.startsWith("your-");
}

bool isWeatherConfigured() {
  String key = WEATHER_API_KEY;
  return key.length() > 10 && !key.startsWith("your-");
}
