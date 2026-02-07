/*
 * MQTT Broker Control - Implementation
 */

#include "mqtt_broker.h"

#if USE_MQTT_BROKER

#include "ac_control.h"
#include "ir_learning.h"
#include "alarm_manager.h"
#include <stdio.h>

WiFiClient net = WiFiClient();
MQTTClient client = MQTTClient(1024);

bool mqttInitialized = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 5000; // 5 seconds

namespace {

bool parseTimeString(const char* value, int* outHour, int* outMinute) {
  if (!value || !value[0] || !outHour || !outMinute) {
    return false;
  }

  int hour = -1;
  int minute = -1;
  if (sscanf(value, "%d:%d", &hour, &minute) != 2) {
    return false;
  }

  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return false;
  }

  *outHour = hour;
  *outMinute = minute;
  return true;
}

bool readAlarmIndex(JsonObjectConst obj, int* outIndex) {
  if (!outIndex) {
    return false;
  }

  if (obj.containsKey("index")) {
    *outIndex = obj["index"];
    return true;
  }

  if (obj.containsKey("id")) {
    *outIndex = obj["id"];
    return true;
  }

  return false;
}

const char* readAlarmName(JsonObjectConst obj) {
  if (obj.containsKey("name")) {
    return obj["name"];
  }
  if (obj.containsKey("alarmName")) {
    return obj["alarmName"];
  }
  return nullptr;
}

bool readAlarmTime(JsonObjectConst obj, int* outHour, int* outMinute) {
  if (!outHour || !outMinute) {
    return false;
  }

  if (obj.containsKey("hour") && obj.containsKey("minute")) {
    *outHour = obj["hour"];
    *outMinute = obj["minute"];
    return true;
  }

  const char* timeValue = obj.containsKey("time") ? obj["time"] : nullptr;
  return parseTimeString(timeValue, outHour, outMinute);
}

void applyOptionalTime(JsonObjectConst obj, int* hour, int* minute) {
  if (!hour || !minute) {
    return;
  }

  if (obj.containsKey("hour")) {
    *hour = obj["hour"];
  }
  if (obj.containsKey("minute")) {
    *minute = obj["minute"];
  }

  const char* timeValue = obj.containsKey("time") ? obj["time"] : nullptr;
  int parsedHour = -1;
  int parsedMinute = -1;
  if (parseTimeString(timeValue, &parsedHour, &parsedMinute)) {
    *hour = parsedHour;
    *minute = parsedMinute;
  }
}

JsonObjectConst alarmFieldsFrom(JsonDocument& doc) {
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.containsKey("data") && root["data"].is<JsonObjectConst>()) {
    return root["data"].as<JsonObjectConst>();
  }
  if (root.containsKey("params") && root["params"].is<JsonObjectConst>()) {
    return root["params"].as<JsonObjectConst>();
  }
  if (root.containsKey("payload") && root["payload"].is<JsonObjectConst>()) {
    return root["payload"].as<JsonObjectConst>();
  }
  return root;
}

const char* commandFrom(JsonDocument& doc) {
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.containsKey("command")) {
    return root["command"];
  }
  if (root.containsKey("data") && root["data"].is<JsonObjectConst>()) {
    JsonObjectConst data = root["data"].as<JsonObjectConst>();
    if (data.containsKey("command")) {
      return data["command"];
    }
  }
  if (root.containsKey("params") && root["params"].is<JsonObjectConst>()) {
    JsonObjectConst params = root["params"].as<JsonObjectConst>();
    if (params.containsKey("command")) {
      return params["command"];
    }
  }
  if (root.containsKey("payload") && root["payload"].is<JsonObjectConst>()) {
    JsonObjectConst payload = root["payload"].as<JsonObjectConst>();
    if (payload.containsKey("command")) {
      return payload["command"];
    }
  }
  return nullptr;
}

}  // namespace

// Brand name mapping table
struct BrandMapping {
  const char* name;
  ACBrand brand;
};

const BrandMapping BRAND_MAPPINGS[] = {
  {"Daikin", BRAND_DAIKIN},
  {"Mitsubishi", BRAND_MITSUBISHI},
  {"Panasonic", BRAND_PANASONIC},
  {"Gree", BRAND_GREE},
  {"Greece", BRAND_GREE},
  {"Midea", BRAND_MIDEA},
  {"Haier", BRAND_HAIER},
  {"Samsung", BRAND_SAMSUNG},
  {"LG", BRAND_LG},
  {"Fujitsu", BRAND_FUJITSU},
  {"Hitachi", BRAND_HITACHI}
};

ACBrand parseBrand(const char* brandStr) {
  for (size_t i = 0; i < sizeof(BRAND_MAPPINGS) / sizeof(BRAND_MAPPINGS[0]); i++) {
    if (strcasecmp(brandStr, BRAND_MAPPINGS[i].name) == 0) {
      return BRAND_MAPPINGS[i].brand;
    }
  }
  return BRAND_PANASONIC; // Default
}

// Mode name mapping table
struct ModeMapping {
  const char* name;
  ACMode mode;
};

const ModeMapping MODE_MAPPINGS[] = {
  {"auto", MODE_AUTO},
  {"cool", MODE_COOL},
  {"heat", MODE_HEAT},
  {"dry", MODE_DRY},
  {"fan", MODE_FAN}
};

ACMode parseMode(const char* modeStr) {
  for (size_t i = 0; i < sizeof(MODE_MAPPINGS) / sizeof(MODE_MAPPINGS[0]); i++) {
    if (strcmp(modeStr, MODE_MAPPINGS[i].name) == 0) {
      return MODE_MAPPINGS[i].mode;
    }
  }
  return MODE_AUTO; // Default
}

// Fan speed mapping table
struct FanMapping {
  const char* name;
  FanSpeed speed;
};

const FanMapping FAN_MAPPINGS[] = {
  {"auto", FAN_AUTO},
  {"low", FAN_LOW},
  {"medium", FAN_MED},
  {"med", FAN_MED},
  {"high", FAN_HIGH}
};

FanSpeed parseFanSpeed(const char* fanStr) {
  for (size_t i = 0; i < sizeof(FAN_MAPPINGS) / sizeof(FAN_MAPPINGS[0]); i++) {
    if (strcmp(fanStr, FAN_MAPPINGS[i].name) == 0) {
      return FAN_MAPPINGS[i].speed;
    }
  }
  return FAN_AUTO; // Default
}

// ========================================
// Command Handler Functions
// ========================================

namespace CommandHandlers {

void handlePowerOn(JsonObjectConst obj) { acPowerOn(); }
void handlePowerOff(JsonObjectConst obj) { acPowerOff(); }
void handlePowerToggle(JsonObjectConst obj) { acPowerToggle(); }
void handleTempUp(JsonObjectConst obj) { acTempUp(); }
void handleTempDown(JsonObjectConst obj) { acTempDown(); }
void handleModeCycle(JsonObjectConst obj) { acModeCycle(); }
void handleFanCycle(JsonObjectConst obj) { acFanCycle(); }

void handleSetTemperature(JsonObjectConst obj) {
  if (obj.containsKey("value")) acSetTemp(obj["value"]);
  else Serial.println("[MQTT] FAIL: Missing 'value'");
}

void handleSetMode(JsonObjectConst obj) {
  if (obj.containsKey("value")) acSetMode(parseMode(obj["value"]));
  else Serial.println("[MQTT] FAIL: Missing 'value'");
}

void handleSetFan(JsonObjectConst obj) {
  if (obj.containsKey("value")) acSetFan(parseFanSpeed(obj["value"]));
  else Serial.println("[MQTT] FAIL: Missing 'value'");
}

void handleSetHumidityThreshold(JsonObjectConst obj) {
  if (obj.containsKey("value")) setAutoDryThreshold(obj["value"]);
  else Serial.println("[MQTT] FAIL: Missing 'value'");
}

void handleSetLightThreshold(JsonObjectConst obj) {
  if (obj.containsKey("value")) setSleepLightThreshold(obj["value"]);
  else Serial.println("[MQTT] FAIL: Missing 'value'");
}

void handleSwitchBrand(JsonObjectConst obj) {
  ACState state = getACState();
  ACBrand newBrand = (ACBrand)((state.brand + 1) % BRAND_COUNT);
  setBrand(newBrand);
  Serial.printf("[MQTT] Brand switched to: %s\n", getBrandName(newBrand));
}

void handleSetBrand(JsonObjectConst obj) {
  if (obj.containsKey("value")) {
    setBrand(parseBrand(obj["value"]));
    Serial.printf("[MQTT] Brand set to: %s\n", getBrandName(parseBrand(obj["value"])));
  }
}

void handleCustom(JsonObjectConst obj) {
  if (!obj.containsKey("id")) {
    Serial.println("[MQTT] FAIL: Missing 'id'");
    return;
  }
  int deviceId = atoi(obj["id"]);
  if (deviceId < 1 || deviceId > 5) {
    Serial.printf("[MQTT] FAIL: Invalid ID: %d\n", deviceId);
    return;
  }
  int deviceIndex = deviceId - 1;
  if (!getLearnedDevice(deviceIndex).hasData) {
    Serial.printf("[MQTT] FAIL: Device %d empty\n", deviceId);
    return;
  }
  Serial.printf("[MQTT] Sending Device %d...\n", deviceId);
  sendLearnedSignal(deviceIndex);
  Serial.printf("[MQTT] OK: Device %d sent\n", deviceId);
}

void handleAlarmAdd(JsonObjectConst obj) {
  int hour, minute;
  if (!readAlarmTime(obj, &hour, &minute) || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    Serial.println("[MQTT] FAIL: Invalid time");
    return;
  }
  const char* name = readAlarmName(obj);
  uint8_t days = obj.containsKey("days") ? (uint8_t)obj["days"].as<int>() : 0x7F;
  if (addAlarm(hour, minute, name, days)) {
    publishAlarmsToFirebase("mqtt");
    Serial.println("[MQTT] OK: Alarm added");
  }
}

void handleAlarmUpdate(JsonObjectConst obj) {
  int index;
  if (!readAlarmIndex(obj, &index) || index < 1 || index > MAX_ALARMS) {
    Serial.println("[MQTT] FAIL: Invalid index");
    return;
  }
  AlarmInfo current;
  if (!getAlarmInfo(index - 1, &current)) {
    Serial.println("[MQTT] FAIL: Alarm not found");
    return;
  }
  int hour = current.hour, minute = current.minute;
  applyOptionalTime(obj, &hour, &minute);
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    Serial.println("[MQTT] FAIL: Invalid time");
    return;
  }
  uint8_t days = obj.containsKey("days") ? (uint8_t)obj["days"].as<int>() : current.days;
  if (updateAlarm(index - 1, hour, minute, readAlarmName(obj), days)) {
    if (obj.containsKey("enabled")) setAlarmEnabled(index - 1, obj["enabled"].as<int>() != 0);
    publishAlarmsToFirebase("mqtt");
    Serial.println("[MQTT] OK: Alarm updated");
  }
}

void handleAlarmDelete(JsonObjectConst obj) {
  int index;
  if (!readAlarmIndex(obj, &index) || index < 1 || index > MAX_ALARMS) {
    Serial.println("[MQTT] FAIL: Invalid index");
    return;
  }
  if (deleteAlarm(index - 1)) {
    publishAlarmsToFirebase("mqtt");
    Serial.println("[MQTT] OK: Alarm deleted");
  }
}

void handleGetStatus(JsonObjectConst obj) { publishMqttStatus(getACState()); }

}  // namespace CommandHandlers

// ========================================
// Command Dispatcher
// ========================================

struct CommandHandler {
  const char* name;
  void (*handler)(JsonObjectConst);
};

const CommandHandler HANDLERS[] = {
  {"power_on", CommandHandlers::handlePowerOn},
  {"power_off", CommandHandlers::handlePowerOff},
  {"power_toggle", CommandHandlers::handlePowerToggle},
  {"temp_up", CommandHandlers::handleTempUp},
  {"temp_down", CommandHandlers::handleTempDown},
  {"mode_cycle", CommandHandlers::handleModeCycle},
  {"fan_cycle", CommandHandlers::handleFanCycle},
  {"switch_brand", CommandHandlers::handleSwitchBrand},
  {"get_status", CommandHandlers::handleGetStatus},
  {"set_temperature", CommandHandlers::handleSetTemperature},
  {"set_mode", CommandHandlers::handleSetMode},
  {"set_fan", CommandHandlers::handleSetFan},
  {"set_humidity_threshold", CommandHandlers::handleSetHumidityThreshold},
  {"set_light_threshold", CommandHandlers::handleSetLightThreshold},
  {"set_brand", CommandHandlers::handleSetBrand},
  {"custom", CommandHandlers::handleCustom},
  {"alarm_add", CommandHandlers::handleAlarmAdd},
  {"alarm_update", CommandHandlers::handleAlarmUpdate},
  {"alarm_delete", CommandHandlers::handleAlarmDelete}
};

bool dispatchCommand(const char* cmd, JsonObjectConst fields) {
  for (size_t i = 0; i < sizeof(HANDLERS) / sizeof(HANDLERS[0]); i++) {
    if (strcmp(cmd, HANDLERS[i].name) == 0) {
      HANDLERS[i].handler(fields);
      return true;
    }
  }
  return false;
}

// ========================================
// MQTT Connection Management
// ========================================

// Initialize MQTT broker connection
void initMqttBroker() {
  Serial.println("\n========================================");
  Serial.println("  MQTT Broker Initialization");
  Serial.println("========================================");

  client.begin(MQTT_BROKER_HOST, MQTT_BROKER_PORT, net);
  client.onMessage(mqttMessageHandler);

  Serial.printf("[MQTT] Broker: %s:%d\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  Serial.printf("[MQTT] Client ID: %s\n", MQTT_CLIENT_ID);
  Serial.printf("[MQTT] Publish Topic: %s\n", MQTT_PUBLISH_TOPIC);
  Serial.printf("[MQTT] Subscribe Topic: %s\n", MQTT_SUBSCRIBE_TOPIC);

  mqttInitialized = true;
  connectMqttBroker();
}

// Connect to MQTT broker
bool connectMqttBroker() {
  if (!mqttInitialized) {
    Serial.println("[MQTT] FAIL: Not initialized");
    return false;
  }

  Serial.print("[MQTT] Connecting to broker");

  int attempts = 0;
  while (!client.connect(MQTT_CLIENT_ID) && attempts < 5) {
    Serial.print(".");
    delay(1000);
    attempts++;
  }
  Serial.println();

  if (!client.connected()) {
    Serial.println("[MQTT] FAIL: Connection failed");
    Serial.println("[MQTT]   Check:");
    Serial.println("[MQTT]   1. WiFi connection");
    Serial.println("[MQTT]   2. Broker host/port");
    return false;
  }

  Serial.println("[MQTT] OK: Connected to broker");

  if (client.subscribe(MQTT_SUBSCRIBE_TOPIC)) {
    Serial.printf("[MQTT] OK: Subscribed to: %s\n", MQTT_SUBSCRIBE_TOPIC);
  } else {
    Serial.printf("[MQTT] FAIL: Subscribe failed: %s\n", MQTT_SUBSCRIBE_TOPIC);
  }

  Serial.println("========================================\n");

  publishMqttStatus(getACState());
  return true;
}

// Publish AC status to MQTT broker
void publishMqttStatus(const ACState& state) {
  if (!client.connected()) {
    return;
  }

  StaticJsonDocument<256> doc;
  doc["device"] = MQTT_CLIENT_ID;
  doc["timestamp"] = millis();
  doc["power"] = state.power;
  doc["temperature"] = state.temperature;

  const char* modes[] = {"auto", "cool", "heat", "dry", "fan"};
  doc["mode"] = modes[state.mode];

  const char* fanSpeeds[] = {"auto", "low", "medium", "high"};
  doc["fan_speed"] = fanSpeeds[state.fanSpeed];

  doc["brand"] = getBrandName(state.brand);
  doc["auto_dry_threshold"] = getAutoDryThreshold();
  doc["sleep_light_threshold"] = getSleepLightThreshold();

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  if (client.publish(MQTT_PUBLISH_TOPIC, jsonBuffer)) {
    Serial.println("[MQTT] OK: Status published");
    Serial.printf("[MQTT]   %s\n", jsonBuffer);
  } else {
    Serial.println("[MQTT] FAIL: Publish status");
  }
}

// Handle incoming MQTT messages
void mqttMessageHandler(String &topic, String &payload) {
  Serial.println("\n[MQTT] -------------------------------");
  Serial.println("[MQTT] Received MQTT Message");
  Serial.println("[MQTT] -------------------------------");
  Serial.printf("[MQTT] Topic: %s\n", topic.c_str());
  Serial.printf("[MQTT] Payload: %s\n", payload.c_str());

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.println("[MQTT] FAIL: JSON parsing");
    Serial.printf("[MQTT]   Error: %s\n", error.c_str());
    return;
  }

  // Check if this is a multi-AC command format
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.containsKey("type") && strcmp(root["type"], "ac_control") == 0) {
    Serial.println("[MQTT] Multi-AC Control Message");
    
    // Extract AC ID (for future multi-AC support)
    const char* ac_id = root.containsKey("ac_id") ? root["ac_id"].as<const char*>() : "unknown";
    Serial.printf("[MQTT] AC ID: %s\n", ac_id);
    
    // Apply brand setting first (before other commands)
    if (root.containsKey("brand")) {
      const char* brandStr = root["brand"];
      Serial.printf("[MQTT] -> Brand: %s\n", brandStr);
      
      ACBrand brand = parseBrand(brandStr);
      setBrand(brand);
      Serial.printf("[MQTT] Brand set to: %s\n", getBrandName(brand));
    }
    
    // For now, just apply the state to the current AC
    // TODO: Load AC-specific configuration based on ac_id
    
    // Apply power state
    if (root.containsKey("power")) {
      const char* powerStr = root["power"];
      if (strcmp(powerStr, "on") == 0) {
        Serial.println("[MQTT] -> Power ON");
        acPowerOn();
      } else if (strcmp(powerStr, "off") == 0) {
        Serial.println("[MQTT] -> Power OFF");
        acPowerOff();
      }
    }
    
    // Apply temperature
    if (root.containsKey("temperature")) {
      int temp = root["temperature"];
      Serial.printf("[MQTT] -> Temperature: %d°C\n", temp);
      acSetTemp(temp);
    }
    
    // Apply mode
    if (root.containsKey("mode")) {
      const char* modeStr = root["mode"];
      Serial.printf("[MQTT] -> Mode: %s\n", modeStr);
      
      ACMode mode = parseMode(modeStr);
      acSetMode(mode);
    }
    
    // Apply fan speed
    if (root.containsKey("fan_speed")) {
      const char* fanStr = root["fan_speed"];
      Serial.printf("[MQTT] -> Fan: %s\n", fanStr);
      
      FanSpeed fan = parseFanSpeed(fanStr);
      acSetFan(fan);
    }
    
    Serial.println("[MQTT] Multi-AC command processed");
    Serial.println("[MQTT] -------------------------------\n");
    return;
  }

  // Fall back to old command format for backwards compatibility
  const char* command = commandFrom(doc);
  if (!command) {
    Serial.println("[MQTT] FAIL: No command in payload");
    return;
  }

  Serial.printf("[MQTT] Command: %s\n", command);

  JsonObjectConst fields = alarmFieldsFrom(doc);
  
  if (!dispatchCommand(command, fields)) {
    Serial.printf("[MQTT] FAIL: Unknown command: %s\n", command);
  }

  Serial.println("[MQTT] -------------------------------\n");
}

// MQTT loop (call in main loop)
void handleMqttBroker() {
  if (!mqttInitialized) {
    return;
  }

  // Call loop more frequently for better network handling
  client.loop();
  yield();  // Give WiFi stack time to process

  if (!client.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = now;
      Serial.println("[MQTT] Connection lost, attempting to reconnect...");
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MQTT] WiFi disconnected, waiting for WiFi...");
        return;
      }
      connectMqttBroker();
    }
  }
}

// Check if connected to MQTT broker
bool isMqttConnected() {
  return client.connected();
}

// Get MQTT connection status
String getMqttStatus() {
  if (!mqttInitialized) {
    return "Not initialized";
  }

  if (client.connected()) {
    return "Connected to MQTT broker";
  }

  return "Disconnected";
}

#endif // USE_MQTT_BROKER
