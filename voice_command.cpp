/*
 * Voice Command Processing - Implementation
 *
 * Handles the complete voice command pipeline using Groq APIs
 */

#include "voice_command.h"
#include "groq_config.h"
#include "mic_control.h"
#include "ac_control.h"
#include "ir_learning_enhanced.h"
#include "speaker_control.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

// External declarations for global variables and functions from main sketch
extern ACState acState;
extern void sendACState(const ACState& state);

// External wake word functions
extern void stopEIWakeWord();
extern bool startEIWakeWord();

// External NeoPixel from main sketch
extern Adafruit_NeoPixel pixels;

// ==============================================================================
// Constants
// ==============================================================================

#define SAMPLE_RATE 16000
#define MAX_RECORD_SAMPLES (SAMPLE_RATE * VOICE_MAX_RECORD_MS / 1000)

// ==============================================================================
// State Variables
// ==============================================================================

static bool voiceInitialized = false;
static VoiceCommandState currentState = VOICE_IDLE;
static VoiceCommandResult lastResult;
static VoiceCommandCallback resultCallback = nullptr;

// Recording buffer (allocated in PSRAM)
static int16_t* recordBuffer = nullptr;
static size_t recordedSamples = 0;
static unsigned long recordStartTime = 0;
static unsigned long silenceStartTime = 0;

// ==============================================================================
// System Prompt for LLM
// ==============================================================================

static const char* LLM_SYSTEM_PROMPT = R"(You are a smart home assistant. Parse user commands and return JSON.

Available actions:
- ac_on: Turn on air conditioner
- ac_off: Turn off air conditioner
- ac_temp: Set temperature (value: 16-30)
- ac_mode: Set mode (value: cool, heat, dry, fan, auto)
- ir_send: Send learned IR signal (value: 1-40)

Respond with ONLY valid JSON in this format:
{"actions": [{"type": "action_name", "value": optional_value}]}

Examples:
- "turn on the AC" -> {"actions": [{"type": "ac_on"}]}
- "set to 22 degrees" -> {"actions": [{"type": "ac_temp", "value": 22}]}
- "it's hot" -> {"actions": [{"type": "ac_on"}, {"type": "ac_mode", "value": "cool"}]})";

// ==============================================================================
// Helper Functions
// ==============================================================================

/**
 * Set LED color for feedback
 */
static void setLED(uint8_t r, uint8_t g, uint8_t b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

/**
 * Create WAV header for audio data
 */
static void createWavHeader(uint8_t* header, size_t sampleCount) {
  uint32_t dataSize = sampleCount * 2;  // 16-bit samples
  uint32_t fileSize = 36 + dataSize;

  // RIFF header
  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  header[4] = fileSize & 0xFF;
  header[5] = (fileSize >> 8) & 0xFF;
  header[6] = (fileSize >> 16) & 0xFF;
  header[7] = (fileSize >> 24) & 0xFF;
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';

  // fmt subchunk
  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  header[20] = 1; header[21] = 0;  // PCM
  header[22] = 1; header[23] = 0;  // Mono
  header[24] = SAMPLE_RATE & 0xFF;
  header[25] = (SAMPLE_RATE >> 8) & 0xFF;
  header[26] = (SAMPLE_RATE >> 16) & 0xFF;
  header[27] = (SAMPLE_RATE >> 24) & 0xFF;
  uint32_t byteRate = SAMPLE_RATE * 2;
  header[28] = byteRate & 0xFF;
  header[29] = (byteRate >> 8) & 0xFF;
  header[30] = (byteRate >> 16) & 0xFF;
  header[31] = (byteRate >> 24) & 0xFF;
  header[32] = 2; header[33] = 0;  // Block align
  header[34] = 16; header[35] = 0;  // Bits per sample

  // data subchunk
  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  header[40] = dataSize & 0xFF;
  header[41] = (dataSize >> 8) & 0xFF;
  header[42] = (dataSize >> 16) & 0xFF;
  header[43] = (dataSize >> 24) & 0xFF;
}

/**
 * Calculate RMS of audio samples
 */
static int32_t calculateRMS(int16_t* samples, size_t count) {
  if (count == 0) return 0;

  int64_t sum = 0;
  for (size_t i = 0; i < count; i++) {
    int32_t val = samples[i];
    sum += val * val;
  }
  return sqrt(sum / count);
}

// ==============================================================================
// Groq API Functions
// ==============================================================================

/**
 * Send audio to Groq Whisper API for transcription
 */
static bool callWhisperAPI(String& transcription) {
  Serial.println("[Voice] Calling Whisper API...");

  WiFiClientSecure client;
  client.setInsecure();  // Skip certificate verification for simplicity

  if (!client.connect(GROQ_API_HOST, 443)) {
    Serial.println("[Voice] ERROR: Failed to connect to Groq API");
    return false;
  }

  // Create WAV data
  size_t wavSize = 44 + (recordedSamples * 2);
  uint8_t wavHeader[44];
  createWavHeader(wavHeader, recordedSamples);

  // Multipart form boundary
  String boundary = "----ESP32VoiceCommand";

  // Build form data parts
  String formStart = "--" + boundary + "\r\n";
  formStart += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
  formStart += "Content-Type: audio/wav\r\n\r\n";

  String formModel = "\r\n--" + boundary + "\r\n";
  formModel += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
  formModel += GROQ_WHISPER_MODEL;

  String formLang = "\r\n--" + boundary + "\r\n";
  formLang += "Content-Disposition: form-data; name=\"language\"\r\n\r\n";
  formLang += "en";

  String formEnd = "\r\n--" + boundary + "--\r\n";

  // Calculate total content length
  size_t contentLength = formStart.length() + 44 + (recordedSamples * 2) +
                         formModel.length() + formLang.length() + formEnd.length();

  // Send HTTP request
  client.print("POST " + String(GROQ_WHISPER_ENDPOINT) + " HTTP/1.1\r\n");
  client.print("Host: " + String(GROQ_API_HOST) + "\r\n");
  client.print("Authorization: Bearer " + String(GROQ_API_KEY) + "\r\n");
  client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
  client.print("Content-Length: " + String(contentLength) + "\r\n");
  client.print("Connection: close\r\n\r\n");

  // Send form data
  client.print(formStart);

  // Send WAV header
  client.write(wavHeader, 44);

  // Send audio data in chunks
  const size_t chunkSize = 4096;
  uint8_t* audioData = (uint8_t*)recordBuffer;
  size_t audioSize = recordedSamples * 2;
  size_t sent = 0;

  while (sent < audioSize) {
    size_t toSend = min(chunkSize, audioSize - sent);
    client.write(audioData + sent, toSend);
    sent += toSend;
    yield();  // Prevent watchdog timeout
  }

  client.print(formModel);
  client.print(formLang);
  client.print(formEnd);

  // Wait for response
  unsigned long timeout = millis() + GROQ_API_TIMEOUT_MS;
  while (!client.available() && millis() < timeout) {
    delay(10);
  }

  if (!client.available()) {
    Serial.println("[Voice] ERROR: Whisper API timeout");
    client.stop();
    return false;
  }

  // Read response
  String response = "";
  bool headersEnded = false;

  while (client.available() || millis() < timeout) {
    if (client.available()) {
      String line = client.readStringUntil('\n');

      if (!headersEnded) {
        if (line == "\r" || line.length() == 0) {
          headersEnded = true;
        }
      } else {
        response += line;
      }
    }

    if (!client.connected() && !client.available()) break;
  }

  client.stop();

  // Parse JSON response
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);

  if (error) {
    Serial.printf("[Voice] ERROR: Failed to parse Whisper response: %s\n", error.c_str());
    Serial.println("[Voice] Response: " + response);
    return false;
  }

  if (doc.containsKey("error")) {
    String errorMsg = doc["error"]["message"].as<String>();
    Serial.println("[Voice] ERROR: Whisper API error: " + errorMsg);
    return false;
  }

  transcription = doc["text"].as<String>();
  transcription.trim();

  Serial.println("[Voice] Transcription: \"" + transcription + "\"");
  return true;
}

/**
 * Send text to Groq LLaMA API for intent parsing
 */
static bool callLLMAPI(const String& text, String& intentJson) {
  Serial.println("[Voice] Calling LLM API...");

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(GROQ_API_HOST, 443)) {
    Serial.println("[Voice] ERROR: Failed to connect to Groq API");
    return false;
  }

  // Build JSON request
  JsonDocument requestDoc;
  requestDoc["model"] = GROQ_LLM_MODEL;
  requestDoc["temperature"] = 0.1;
  requestDoc["max_tokens"] = 150;

  JsonArray messages = requestDoc["messages"].to<JsonArray>();

  JsonObject sysMsg = messages.add<JsonObject>();
  sysMsg["role"] = "system";
  sysMsg["content"] = LLM_SYSTEM_PROMPT;

  JsonObject userMsg = messages.add<JsonObject>();
  userMsg["role"] = "user";
  userMsg["content"] = text;

  String requestBody;
  serializeJson(requestDoc, requestBody);

  // Send HTTP request
  client.print("POST " + String(GROQ_LLM_ENDPOINT) + " HTTP/1.1\r\n");
  client.print("Host: " + String(GROQ_API_HOST) + "\r\n");
  client.print("Authorization: Bearer " + String(GROQ_API_KEY) + "\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: " + String(requestBody.length()) + "\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print(requestBody);

  // Wait for response
  unsigned long timeout = millis() + GROQ_API_TIMEOUT_MS;
  while (!client.available() && millis() < timeout) {
    delay(10);
  }

  if (!client.available()) {
    Serial.println("[Voice] ERROR: LLM API timeout");
    client.stop();
    return false;
  }

  // Read response
  String response = "";
  bool headersEnded = false;

  while (client.available() || millis() < timeout) {
    if (client.available()) {
      String line = client.readStringUntil('\n');

      if (!headersEnded) {
        if (line == "\r" || line.length() == 0) {
          headersEnded = true;
        }
      } else {
        response += line;
      }
    }

    if (!client.connected() && !client.available()) break;
  }

  client.stop();

  // Parse JSON response
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);

  if (error) {
    Serial.printf("[Voice] ERROR: Failed to parse LLM response: %s\n", error.c_str());
    return false;
  }

  if (doc.containsKey("error")) {
    String errorMsg = doc["error"]["message"].as<String>();
    Serial.println("[Voice] ERROR: LLM API error: " + errorMsg);
    return false;
  }

  intentJson = doc["choices"][0]["message"]["content"].as<String>();
  intentJson.trim();

  Serial.println("[Voice] Intent: " + intentJson);
  return true;
}

/**
 * Parse intent JSON and extract actions
 */
static bool parseIntent(const String& intentJson, VoiceCommandResult* result) {
  // Find JSON object in response (LLM might add extra text)
  int jsonStart = intentJson.indexOf('{');
  int jsonEnd = intentJson.lastIndexOf('}');

  if (jsonStart < 0 || jsonEnd < 0) {
    Serial.println("[Voice] ERROR: No JSON found in intent");
    return false;
  }

  String jsonStr = intentJson.substring(jsonStart, jsonEnd + 1);

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonStr);

  if (error) {
    Serial.printf("[Voice] ERROR: Failed to parse intent JSON: %s\n", error.c_str());
    return false;
  }

  result->actionCount = 0;
  JsonArray actions = doc["actions"];

  for (JsonObject action : actions) {
    if (result->actionCount >= 5) break;

    result->actions[result->actionCount].type = action["type"].as<String>();

    if (action.containsKey("value")) {
      if (action["value"].is<int>()) {
        result->actions[result->actionCount].value = action["value"].as<int>();
      } else if (action["value"].is<const char*>()) {
        // Convert string values like "cool" to mode numbers if needed
        String strVal = action["value"].as<String>();
        if (strVal == "cool") result->actions[result->actionCount].value = 0;
        else if (strVal == "heat") result->actions[result->actionCount].value = 1;
        else if (strVal == "dry") result->actions[result->actionCount].value = 2;
        else if (strVal == "fan") result->actions[result->actionCount].value = 3;
        else if (strVal == "auto") result->actions[result->actionCount].value = 4;
        else result->actions[result->actionCount].value = 0;
      }
      result->actions[result->actionCount].hasValue = true;
    } else {
      result->actions[result->actionCount].hasValue = false;
    }

    result->actionCount++;
  }

  return result->actionCount > 0;
}

/**
 * Execute parsed actions
 */
static void executeActions(VoiceCommandResult* result) {
  Serial.printf("[Voice] Executing %d actions...\n", result->actionCount);

  for (int i = 0; i < result->actionCount; i++) {
    VoiceAction& action = result->actions[i];

    Serial.printf("[Voice] Action %d: %s", i + 1, action.type.c_str());
    if (action.hasValue) {
      Serial.printf(" = %d", action.value);
    }
    Serial.println();

    if (action.type == "ac_on") {
      acState.power = true;
      sendACState(acState);
      playVoice(VOICE_POWER_ON);

    } else if (action.type == "ac_off") {
      acState.power = false;
      sendACState(acState);
      playVoice(VOICE_POWER_OFF);

    } else if (action.type == "ac_temp") {
      int temp = constrain(action.value, 16, 30);
      acState.temperature = temp;
      if (!acState.power) acState.power = true;
      sendACState(acState);
      playTemperature(temp);

    } else if (action.type == "ac_mode") {
      acState.mode = (ACMode)action.value;
      if (!acState.power) acState.power = true;
      sendACState(acState);
      // Play mode-specific feedback
      switch (action.value) {
        case 0: playVoice(VOICE_MODE_COOL); break;
        case 1: playVoice(VOICE_MODE_HEAT); break;
        case 2: playVoice(VOICE_MODE_DRY); break;
        case 3: playVoice(VOICE_MODE_FAN); break;
        case 4: playVoice(VOICE_MODE_AUTO); break;
        default: playActionTone(); break;
      }

    } else if (action.type == "ir_send") {
      int signalNum = constrain(action.value, 1, 40);
      sendSignal(signalNum - 1);  // Convert 1-40 to 0-39
      playActionTone();

    } else {
      Serial.println("[Voice] Unknown action type: " + action.type);
      playBeep(200, 100);  // Error beep
    }

    // Small delay between actions for audio clarity
    delay(100);
  }
}

// ==============================================================================
// Public API Implementation
// ==============================================================================

bool initVoiceCommand() {
  Serial.println("[Voice] Initializing voice command system...");

  // Allocate recording buffer in PSRAM
  recordBuffer = (int16_t*)ps_malloc(MAX_RECORD_SAMPLES * sizeof(int16_t));

  if (!recordBuffer) {
    Serial.println("[Voice] ERROR: Failed to allocate recording buffer");
    return false;
  }

  Serial.printf("[Voice] Allocated %d bytes for recording buffer\n",
                MAX_RECORD_SAMPLES * sizeof(int16_t));

  // Clear result
  memset(&lastResult, 0, sizeof(lastResult));

  voiceInitialized = true;
  currentState = VOICE_IDLE;

  Serial.println("[Voice] Voice command system initialized");
  return true;
}

void startVoiceCommand() {
  if (!voiceInitialized) {
    Serial.println("[Voice] ERROR: Not initialized");
    return;
  }

  if (currentState != VOICE_IDLE) {
    Serial.println("[Voice] Already processing a command");
    return;
  }

  Serial.println("[Voice] Starting voice command recording...");

  // Stop wake word detection to avoid microphone conflicts
  stopEIWakeWord();

  // LED blue = listening
  setLED(0, 0, 255);

  // Clear previous result
  memset(&lastResult, 0, sizeof(lastResult));

  // Start recording
  recordedSamples = 0;
  recordStartTime = millis();
  silenceStartTime = 0;

  startRecording();
  currentState = VOICE_LISTENING;

  Serial.println("[Voice] Say your command now!");
}

void updateVoiceCommand() {
  if (!voiceInitialized) return;

  switch (currentState) {
    case VOICE_IDLE:
      // Nothing to do
      break;

    case VOICE_LISTENING: {
      // Check timeout
      if (millis() - recordStartTime > VOICE_MAX_RECORD_MS) {
        Serial.println("[Voice] Max recording time reached");
        stopRecording();
        currentState = VOICE_PROCESSING;
        setLED(0, 0, 255);  // Blue = processing
        break;
      }

      // Read audio samples
      const size_t chunkSize = 512;
      int16_t chunk[chunkSize];

      size_t bytesRead = readAudioSamples(chunk, chunkSize);
      size_t samplesRead = bytesRead / sizeof(int16_t);

      if (samplesRead > 0 && recordedSamples + samplesRead < MAX_RECORD_SAMPLES) {
        // Copy to buffer
        memcpy(&recordBuffer[recordedSamples], chunk, samplesRead * sizeof(int16_t));
        recordedSamples += samplesRead;

        // Check for silence (end of speech)
        int32_t rms = calculateRMS(chunk, samplesRead);

        if (rms < VOICE_SILENCE_THRESHOLD) {
          if (silenceStartTime == 0) {
            silenceStartTime = millis();
          } else if (millis() - silenceStartTime > VOICE_SILENCE_DURATION_MS) {
            // Silence detected - stop recording
            if (recordedSamples > SAMPLE_RATE / 2) {  // At least 0.5 seconds
              Serial.println("[Voice] End of speech detected");
              stopRecording();
              currentState = VOICE_PROCESSING;
              setLED(0, 0, 255);  // Blue = processing
            }
          }
        } else {
          silenceStartTime = 0;  // Reset silence timer
        }
      }
      break;
    }

    case VOICE_PROCESSING: {
      Serial.printf("[Voice] Processing %d samples (%.1f sec)\n",
                    recordedSamples, (float)recordedSamples / SAMPLE_RATE);

      // Keep blue while uploading to API
      setLED(0, 0, 255);  // Blue = uploading to API

      // Step 1: Transcribe audio
      String transcription;
      if (!callWhisperAPI(transcription)) {
        lastResult.success = false;
        lastResult.errorMessage = "Transcription failed";
        currentState = VOICE_ERROR;
        setLED(255, 0, 0);  // Red = error
        break;
      }
      lastResult.transcription = transcription;

      // Step 2: Parse intent
      String intentJson;
      if (!callLLMAPI(transcription, intentJson)) {
        lastResult.success = false;
        lastResult.errorMessage = "Intent parsing failed";
        currentState = VOICE_ERROR;
        setLED(255, 0, 0);
        break;
      }
      lastResult.rawIntent = intentJson;

      // Step 3: Extract actions
      if (!parseIntent(intentJson, &lastResult)) {
        lastResult.success = false;
        lastResult.errorMessage = "No valid actions found";
        currentState = VOICE_ERROR;
        setLED(255, 0, 0);
        break;
      }

      currentState = VOICE_EXECUTING;
      break;
    }

    case VOICE_EXECUTING: {
      // Execute the parsed actions
      executeActions(&lastResult);

      lastResult.success = true;
      lastResult.errorMessage = "";

      // Clear the audio buffer to free memory
      if (recordBuffer != nullptr) {
        memset(recordBuffer, 0, MAX_RECORD_SAMPLES * sizeof(int16_t));
      }
      recordedSamples = 0;

      // Success feedback - Red = response received
      setLED(255, 255, 255);  // White flash
      delay(500);
      setLED(0, 0, 0);    // LED off

      // Call callback if registered
      if (resultCallback != nullptr) {
        resultCallback(&lastResult);
      }

      // Restart wake word detection
      startEIWakeWord();

      currentState = VOICE_IDLE;
      break;
    }

    case VOICE_ERROR: {
      Serial.println("[Voice] ERROR: " + lastResult.errorMessage);

      // Clear the audio buffer to free memory
      if (recordBuffer != nullptr) {
        memset(recordBuffer, 0, MAX_RECORD_SAMPLES * sizeof(int16_t));
      }
      recordedSamples = 0;

      // Error feedback
      delay(500);
      setLED(0, 0, 0);  // LED off

      // Call callback if registered
      if (resultCallback != nullptr) {
        resultCallback(&lastResult);
      }

      // Restart wake word detection
      startEIWakeWord();

      currentState = VOICE_IDLE;
      break;
    }
  }
}

bool isVoiceCommandBusy() {
  return currentState != VOICE_IDLE;
}

VoiceCommandState getVoiceCommandState() {
  return currentState;
}

VoiceCommandResult* getLastVoiceResult() {
  return &lastResult;
}

void cancelVoiceCommand() {
  if (currentState == VOICE_LISTENING) {
    stopRecording();
  }

  // Clear the audio buffer to free memory
  if (recordBuffer != nullptr) {
    memset(recordBuffer, 0, MAX_RECORD_SAMPLES * sizeof(int16_t));
  }
  recordedSamples = 0;

  currentState = VOICE_IDLE;
  setLED(0, 0, 0);

  // Restart wake word detection
  startEIWakeWord();

  Serial.println("[Voice] Command cancelled");
}

void setVoiceCommandCallback(VoiceCommandCallback callback) {
  resultCallback = callback;
}
