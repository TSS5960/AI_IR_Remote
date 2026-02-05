/*
 * Firebase Realtime Database Client - Implementation
 */

#include "firebase_client.h"
#include "ac_control.h"
#include "alarm_manager.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <time.h>

namespace {

bool firebaseInitialized = false;
unsigned long lastWriteMs = 0;
int lastHttpStatus = 0;
String lastError;
unsigned long lastSendAttemptMs = 0;
bool pendingState = false;
bool pendingStatus = false;
ACState pendingStateValue = {false, 0, MODE_AUTO, FAN_AUTO, BRAND_DAIKIN};
ACState pendingStatusState = {false, 0, MODE_AUTO, FAN_AUTO, BRAND_DAIKIN};
SensorData pendingStatusSensors = {};
char pendingSource[24] = "state";
char pendingStatusSource[24] = "status";

bool configureClient(WiFiClientSecure& client) {
  if (FIREBASE_ALLOW_INSECURE) {
    client.setInsecure();
    return true;
  }

  if (strlen(FIREBASE_ROOT_CA) == 0) {
    lastError = "TLS CA missing";
    return false;
  }

  client.setCACert(FIREBASE_ROOT_CA);
  return true;
}

String urlEncode(const char* value) {
  static const char* hex = "0123456789ABCDEF";
  String encoded;
  while (*value) {
    char c = *value++;
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      encoded += c;
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

String normalizeBaseUrl() {
  String base = String(FIREBASE_DB_URL);
  base.trim();
  if (base.endsWith("/")) {
    base.remove(base.length() - 1);
  }
  return base;
}

String buildUrl(const String& path) {
  String url = normalizeBaseUrl() + path;
  if (strlen(FIREBASE_AUTH) > 0) {
    url += "?auth=" + urlEncode(FIREBASE_AUTH);
  }
  return url;
}

void fillStateDoc(JsonDocument& doc, const ACState& state) {
  const char* modes[] = {"auto", "cool", "heat", "dry", "fan"};
  const char* fans[] = {"auto", "low", "medium", "high"};

  doc["device"] = FIREBASE_DEVICE_ID;
  doc["power"] = state.power;
  doc["temperature"] = state.temperature;
  doc["mode"] = modes[state.mode];
  doc["fan_speed"] = fans[state.fanSpeed];
  doc["brand"] = getBrandName(state.brand);
  doc["auto_dry_threshold"] = getAutoDryThreshold();
  doc["auto_dry_enabled"] = getAutoDryThreshold() > 0.0f;
  doc["sleep_light_threshold"] = getSleepLightThreshold();
  doc["sleep_light_enabled"] = getSleepLightThreshold() > 0.0f;

  time_t now = time(nullptr);
  if (now > 100000) {
    doc["timestamp"] = static_cast<long>(now);
  } else {
    doc["timestamp"] = 0;
  }
  doc["uptime_ms"] = millis();
}

void fillStatusDoc(JsonDocument& doc, const ACState& state, const SensorData& sensors) {
  fillStateDoc(doc, state);

  JsonObject sensorObj = doc.createNestedObject("sensors");
  sensorObj["motion"] = sensors.motionDetected;

  JsonObject dhtObj = sensorObj.createNestedObject("dht");
  dhtObj["valid"] = sensors.dht_valid;
  if (sensors.dht_valid) {
    dhtObj["temperature"] = sensors.dht_temperature;
    dhtObj["humidity"] = sensors.dht_humidity;
  }

  JsonObject lightObj = sensorObj.createNestedObject("light");
  lightObj["valid"] = sensors.light_valid;
  if (sensors.light_valid) {
    lightObj["lux"] = sensors.light_lux;
  }

}

bool sendJson(const String& url, const String& payload, bool usePut, const char* label) {
  if (!firebaseInitialized) {
    lastError = "Not initialized";
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    lastError = "WiFi disconnected";
    return false;
  }

  WiFiClientSecure client;
  if (!configureClient(client)) {
    return false;
  }

  HTTPClient http;
  http.setTimeout(FIREBASE_TIMEOUT_MS);

  if (!http.begin(client, url)) {
    lastError = "HTTP begin failed";
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  const char* method = usePut ? "PUT" : "POST";
  const char* target = label == nullptr ? "payload" : label;
  unsigned long startMs = millis();
  Serial.printf("[Firebase] %s %s (%u bytes)\n", method, target, payload.length());
  int code = usePut ? http.PUT(payload) : http.POST(payload);
  lastHttpStatus = code;
  http.end();

  if (code >= 200 && code < 300) {
    lastWriteMs = millis();
    lastError = "";
    Serial.printf("[Firebase] %s %s -> HTTP %d (%lu ms)\n",
                  method, target, code, millis() - startMs);
    return true;
  }

  lastError = "HTTP " + String(code);
  Serial.printf("[Firebase] %s %s failed: HTTP %d (%lu ms)\n",
                method, target, code, millis() - startMs);
  return false;
}

}  // namespace

void initFirebase() {
  firebaseInitialized = true;
  lastWriteMs = 0;
  lastHttpStatus = 0;
  lastError = "";
  lastSendAttemptMs = 0;
  pendingState = false;
  pendingStatus = false;
  pendingSource[0] = '\0';
  pendingStatusSource[0] = '\0';
  Serial.println("[Firebase] Client initialized");
}

bool isFirebaseConfigured() {
  return strlen(FIREBASE_DB_URL) > 0;
}

bool isFirebaseConnected() {
  if (lastWriteMs == 0) {
    return false;
  }
  return (millis() - lastWriteMs) < FIREBASE_STATUS_TTL_MS;
}

String getFirebaseStatus() {
  if (!isFirebaseConfigured()) {
    return "Not configured";
  }
  if (WiFi.status() != WL_CONNECTED) {
    return "WiFi disconnected";
  }
  if (isFirebaseConnected()) {
    return "Connected (recent write)";
  }
  if (lastError.length() > 0) {
    return "Error: " + lastError;
  }
  if (lastHttpStatus != 0) {
    return "Idle (last HTTP " + String(lastHttpStatus) + ")";
  }
  return "Idle";
}

bool firebaseWriteState(const ACState& state) {
  if (!isFirebaseConfigured()) {
    lastError = "Not configured";
    return false;
  }

  StaticJsonDocument<256> doc;
  fillStateDoc(doc, state);

  String payload;
  serializeJson(doc, payload);

  String path = "/devices/" + String(FIREBASE_DEVICE_ID) + "/state.json";
  return sendJson(buildUrl(path), payload, true, "state");
}

bool firebaseWriteStateWithSensors(const ACState& state, const SensorData& sensors) {
  if (!isFirebaseConfigured()) {
    lastError = "Not configured";
    return false;
  }

  StaticJsonDocument<512> doc;
  fillStatusDoc(doc, state, sensors);

  String payload;
  serializeJson(doc, payload);

  String path = "/devices/" + String(FIREBASE_DEVICE_ID) + "/state.json";
  return sendJson(buildUrl(path), payload, true, "state+sensors");
}

bool firebaseWriteAlarms(const AlarmInfo* alarms, int count, const char* source) {
  if (!isFirebaseConfigured()) {
    lastError = "Not configured";
    return false;
  }

  int safeCount = count;
  if (safeCount < 0) {
    safeCount = 0;
  }
  if (safeCount > MAX_ALARMS) {
    safeCount = MAX_ALARMS;
  }

  StaticJsonDocument<1024> doc;
  doc["device"] = FIREBASE_DEVICE_ID;
  doc["count"] = safeCount;

  time_t now = time(nullptr);
  if (now > 100000) {
    doc["timestamp"] = static_cast<long>(now);
  } else {
    doc["timestamp"] = 0;
  }

  if (source && source[0]) {
    doc["source"] = source;
  }

  JsonArray alarmArray = doc.createNestedArray("alarms");
  for (int i = 0; i < safeCount; i++) {
    JsonObject alarmObj = alarmArray.createNestedObject();
    alarmObj["hour"] = alarms[i].hour;
    alarmObj["minute"] = alarms[i].minute;
    alarmObj["enabled"] = alarms[i].enabled;
    alarmObj["name"] = alarms[i].name;
  }

  String payload;
  serializeJson(doc, payload);

  String path = "/devices/" + String(FIREBASE_DEVICE_ID) + "/alarms.json";
  return sendJson(buildUrl(path), payload, true, "alarms");
}

bool firebaseAppendStatus(const ACState& state, const SensorData& sensors, const char* label) {
  if (!isFirebaseConfigured()) {
    lastError = "Not configured";
    return false;
  }

  StaticJsonDocument<512> doc;
  fillStatusDoc(doc, state, sensors);
  doc["source"] = label == nullptr ? "status" : label;

  String payload;
  serializeJson(doc, payload);

  String path = "/devices/" + String(FIREBASE_DEVICE_ID) + "/status_history.json";
  const char* target = label == nullptr ? "status_history" : label;
  return sendJson(buildUrl(path), payload, false, target);
}

bool firebaseAppendEvent(const ACState& state, const char* source) {
  if (!FIREBASE_ENABLE_EVENTS) {
    return true;
  }

  if (!isFirebaseConfigured()) {
    lastError = "Not configured";
    return false;
  }

  StaticJsonDocument<256> doc;
  fillStateDoc(doc, state);
  doc["source"] = source == nullptr ? "unknown" : source;

  String payload;
  serializeJson(doc, payload);

  String path = "/devices/" + String(FIREBASE_DEVICE_ID) + "/events.json";
  const char* label = source == nullptr ? "event" : source;
  return sendJson(buildUrl(path), payload, false, label);
}

void firebaseQueueState(const ACState& state, const char* source) {
  pendingStateValue = state;
  pendingState = true;

  if (source == nullptr) {
    snprintf(pendingSource, sizeof(pendingSource), "state");
  } else {
    snprintf(pendingSource, sizeof(pendingSource), "%s", source);
  }
}

void firebaseQueueStatus(const ACState& state, const SensorData& sensors, const char* source) {
  pendingStatusState = state;
  pendingStatusSensors = sensors;
  pendingStatus = true;

  if (source == nullptr) {
    snprintf(pendingStatusSource, sizeof(pendingStatusSource), "status");
  } else {
    snprintf(pendingStatusSource, sizeof(pendingStatusSource), "%s", source);
  }
}

void handleFirebase() {
  if (!pendingState && !pendingStatus) {
    return;
  }
  if (!isFirebaseConfigured()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (millis() - lastSendAttemptMs < FIREBASE_SEND_INTERVAL_MS) {
    return;
  }

  lastSendAttemptMs = millis();
  if (pendingState) {
    if (firebaseWriteState(pendingStateValue)) {
      if (FIREBASE_ENABLE_EVENTS) {
        firebaseAppendEvent(pendingStateValue, pendingSource);
      }
      pendingState = false;
    }
    return;
  }

  if (pendingStatus) {
    // Update state.json with sensors for dashboard
    if (firebaseWriteStateWithSensors(pendingStatusState, pendingStatusSensors)) {
      // Also append to history
      firebaseAppendStatus(pendingStatusState, pendingStatusSensors, pendingStatusSource);
      pendingStatus = false;
    }
  }
}
