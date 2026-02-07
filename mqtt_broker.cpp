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
      
      ACBrand brand = BRAND_PANASONIC; // Default
      if (strcasecmp(brandStr, "Daikin") == 0) brand = BRAND_DAIKIN;
      else if (strcasecmp(brandStr, "Mitsubishi") == 0) brand = BRAND_MITSUBISHI;
      else if (strcasecmp(brandStr, "Panasonic") == 0) brand = BRAND_PANASONIC;
      else if (strcasecmp(brandStr, "Gree") == 0 || strcasecmp(brandStr, "Greece") == 0) brand = BRAND_GREE;
      else if (strcasecmp(brandStr, "Midea") == 0) brand = BRAND_MIDEA;
      else if (strcasecmp(brandStr, "Haier") == 0) brand = BRAND_HAIER;
      else if (strcasecmp(brandStr, "Samsung") == 0) brand = BRAND_SAMSUNG;
      else if (strcasecmp(brandStr, "LG") == 0) brand = BRAND_LG;
      else if (strcasecmp(brandStr, "Fujitsu") == 0) brand = BRAND_FUJITSU;
      else if (strcasecmp(brandStr, "Hitachi") == 0) brand = BRAND_HITACHI;
      
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
      
      ACMode mode = MODE_AUTO;
      if (strcmp(modeStr, "cool") == 0) mode = MODE_COOL;
      else if (strcmp(modeStr, "heat") == 0) mode = MODE_HEAT;
      else if (strcmp(modeStr, "dry") == 0) mode = MODE_DRY;
      else if (strcmp(modeStr, "fan") == 0) mode = MODE_FAN;
      
      acSetMode(mode);
    }
    
    // Apply fan speed
    if (root.containsKey("fan_speed")) {
      const char* fanStr = root["fan_speed"];
      Serial.printf("[MQTT] -> Fan: %s\n", fanStr);
      
      FanSpeed fan = FAN_AUTO;
      if (strcmp(fanStr, "low") == 0) fan = FAN_LOW;
      else if (strcmp(fanStr, "medium") == 0 || strcmp(fanStr, "med") == 0) fan = FAN_MED;
      else if (strcmp(fanStr, "high") == 0) fan = FAN_HIGH;
      
      acSetFan(fan);
    }
    
    Serial.println("[MQTT] Multi-AC command processed");
    Serial.println("[MQTT] -------------------------------\n");
    return;
  }

  // Fall back to old command format for backwards compatibility
  const char* command = commandFrom(doc);
  if (command == nullptr) {
    Serial.println("[MQTT] FAIL: No command in payload");
    return;
  }

  Serial.printf("[MQTT] Command: %s\n", command);

  if (strcmp(command, "power_on") == 0) {
    acPowerOn();
  }
  else if (strcmp(command, "power_off") == 0) {
    acPowerOff();
  }
  else if (strcmp(command, "power_toggle") == 0) {
    acPowerToggle();
  }
  else if (strcmp(command, "temp_up") == 0) {
    acTempUp();
  }
  else if (strcmp(command, "temp_down") == 0) {
    acTempDown();
  }
  else if (strcmp(command, "set_temperature") == 0) {
    if (doc.containsKey("value")) {
      int temp = doc["value"];
      acSetTemp(temp);
    }
  }
  else if (strcmp(command, "set_mode") == 0) {
    if (doc.containsKey("value")) {
      const char* modeStr = doc["value"];

      ACMode mode = MODE_AUTO;
      if (strcmp(modeStr, "cool") == 0) mode = MODE_COOL;
      else if (strcmp(modeStr, "heat") == 0) mode = MODE_HEAT;
      else if (strcmp(modeStr, "dry") == 0) mode = MODE_DRY;
      else if (strcmp(modeStr, "fan") == 0) mode = MODE_FAN;

      acSetMode(mode);
    }
  }
  else if (strcmp(command, "set_fan") == 0) {
    if (doc.containsKey("value")) {
      const char* fanStr = doc["value"];

      FanSpeed fan = FAN_AUTO;
      if (strcmp(fanStr, "low") == 0) fan = FAN_LOW;
      else if (strcmp(fanStr, "medium") == 0) fan = FAN_MED;
      else if (strcmp(fanStr, "high") == 0) fan = FAN_HIGH;

      acSetFan(fan);
    }
  }
  else if (strcmp(command, "set_humidity_threshold") == 0) {
    if (doc.containsKey("value")) {
      float threshold = doc["value"];
      setAutoDryThreshold(threshold);
    } else {
      Serial.println("[MQTT] FAIL: Missing 'value' for humidity threshold");
    }
  }
  else if (strcmp(command, "set_light_threshold") == 0) {
    if (doc.containsKey("value")) {
      float threshold = doc["value"];
      setSleepLightThreshold(threshold);
    } else {
      Serial.println("[MQTT] FAIL: Missing 'value' for light threshold");
    }
  }
  else if (strcmp(command, "mode_cycle") == 0) {
    acModeCycle();
  }
  else if (strcmp(command, "fan_cycle") == 0) {
    acFanCycle();
  }
  else if (strcmp(command, "switch_brand") == 0) {
    ACState state = getACState();
    ACBrand newBrand = (ACBrand)((state.brand + 1) % BRAND_COUNT);
    setBrand(newBrand);
    Serial.printf("[MQTT] Brand switched to: %s\n", getBrandName(newBrand));
  }
  else if (strcmp(command, "set_brand") == 0) {
    if (doc.containsKey("value")) {
      const char* brandStr = doc["value"];

      ACBrand brand = BRAND_DAIKIN;
      if (strcmp(brandStr, "daikin") == 0) brand = BRAND_DAIKIN;
      else if (strcmp(brandStr, "mitsubishi") == 0) brand = BRAND_MITSUBISHI;
      else if (strcmp(brandStr, "panasonic") == 0) brand = BRAND_PANASONIC;
      else if (strcmp(brandStr, "gree") == 0) brand = BRAND_GREE;
      else if (strcmp(brandStr, "midea") == 0) brand = BRAND_MIDEA;
      else if (strcmp(brandStr, "haier") == 0) brand = BRAND_HAIER;
      else if (strcmp(brandStr, "samsung") == 0) brand = BRAND_SAMSUNG;
      else if (strcmp(brandStr, "lg") == 0) brand = BRAND_LG;
      else if (strcmp(brandStr, "fujitsu") == 0) brand = BRAND_FUJITSU;
      else if (strcmp(brandStr, "hitachi") == 0) brand = BRAND_HITACHI;
      else {
        Serial.printf("[MQTT] FAIL: Unknown brand: %s\n", brandStr);
        Serial.println("[MQTT] -------------------------------\n");
        return;
      }

      setBrand(brand);
      Serial.printf("[MQTT] Brand set to: %s\n", getBrandName(brand));
    }
  }
  else if (strcmp(command, "custom") == 0) {
    if (doc.containsKey("id")) {
      const char* idStr = doc["id"];
      int deviceId = atoi(idStr);

      if (deviceId < 1 || deviceId > 5) {
        Serial.printf("[MQTT] FAIL: Invalid device ID: %d (must be 1-5)\n", deviceId);
        Serial.println("[MQTT] -------------------------------\n");
        return;
      }

      int deviceIndex = deviceId - 1;
      LearnedDevice device = getLearnedDevice(deviceIndex);
      if (!device.hasData) {
        Serial.printf("[MQTT] FAIL: Device %d empty (no learned signal)\n", deviceId);
        Serial.println("[MQTT] -------------------------------\n");
        return;
      }

      Serial.printf("[MQTT] Sending custom signal from Device %d...\n", deviceId);
      sendLearnedSignal(deviceIndex);
      Serial.printf("[MQTT] OK: Custom signal sent (Device %d)\n", deviceId);
    } else {
      Serial.println("[MQTT] FAIL: Missing 'id' parameter");
      Serial.println("[MQTT]   Usage: {\"command\":\"custom\",\"id\":\"1\"}");
    }
  }
  else if (strcmp(command, "alarm_add") == 0) {
    JsonObjectConst fields = alarmFieldsFrom(doc);
    int hour = -1;
    int minute = -1;
    if (!readAlarmTime(fields, &hour, &minute)) {
      Serial.println("[MQTT] FAIL: Missing alarm time");
      Serial.println("[MQTT]   Usage: {\"command\":\"alarm_add\",\"hour\":7,\"minute\":30,\"name\":\"alarm\"}");
      Serial.println("[MQTT]          {\"command\":\"alarm_add\",\"time\":\"07:30\",\"name\":\"alarm\"}");
      Serial.println("[MQTT] -------------------------------\n");
      return;
    }

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
      Serial.println("[MQTT] FAIL: Invalid alarm time");
      Serial.println("[MQTT] -------------------------------\n");
      return;
    }

    const char* name = readAlarmName(fields);
    Serial.printf("[MQTT] Alarm add name: %s\n", name ? name : "<default>");
    if (name && name[0] == '\0') {
      name = nullptr;
    }

    // Read days parameter (default to all days if not specified)
    uint8_t days = 0x7F;  // Default to all days
    if (fields.containsKey("days")) {
      int daysValue = fields["days"];
      if (daysValue >= 0 && daysValue <= 127) {
        days = (uint8_t)daysValue;
      }
    }
    Serial.printf("[MQTT] Alarm days bitmask: 0x%02X\n", days);

    if (addAlarm((uint8_t)hour, (uint8_t)minute, name, days)) {
      publishAlarmsToFirebase("mqtt");
      Serial.println("[MQTT] OK: Alarm added");
    }
  }
  else if (strcmp(command, "alarm_update") == 0) {
    JsonObjectConst fields = alarmFieldsFrom(doc);
    int index = -1;
    if (!readAlarmIndex(fields, &index)) {
      Serial.println("[MQTT] FAIL: Missing alarm index");
      Serial.println("[MQTT]   Usage: {\"command\":\"alarm_update\",\"index\":1,\"hour\":7,\"minute\":30,\"name\":\"alarm\"}");
      Serial.println("[MQTT]          {\"command\":\"alarm_update\",\"index\":1,\"time\":\"07:30\",\"name\":\"alarm\"}");
      Serial.println("[MQTT] -------------------------------\n");
      return;
    }

    if (index < 1 || index > MAX_ALARMS) {
      Serial.println("[MQTT] FAIL: Invalid alarm index");
      Serial.println("[MQTT] -------------------------------\n");
      return;
    }
    AlarmInfo current;
    if (!getAlarmInfo((uint8_t)(index - 1), &current)) {
      Serial.println("[MQTT] FAIL: Alarm not found");
      Serial.println("[MQTT] -------------------------------\n");
      return;
    }

    int hour = current.hour;
    int minute = current.minute;
    applyOptionalTime(fields, &hour, &minute);
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
      Serial.println("[MQTT] FAIL: Invalid alarm time");
      Serial.println("[MQTT] -------------------------------\n");
      return;
    }

    const char* name = readAlarmName(fields);
    Serial.printf("[MQTT] Alarm update index=%d name: %s\n", index, name ? name : "<keep>");

    // Read days parameter (default to current value)
    uint8_t days = current.days;
    if (fields.containsKey("days")) {
      int daysValue = fields["days"];
      if (daysValue >= 0 && daysValue <= 127) {
        days = (uint8_t)daysValue;
      }
    }
    Serial.printf("[MQTT] Alarm days bitmask: 0x%02X\n", days);

    if (updateAlarm((uint8_t)(index - 1), (uint8_t)hour, (uint8_t)minute, name, days)) {
      // Handle enabled status if provided
      if (fields.containsKey("enabled")) {
        int enabledValue = fields["enabled"];
        setAlarmEnabled((uint8_t)(index - 1), enabledValue != 0);
      }
      publishAlarmsToFirebase("mqtt");
      Serial.println("[MQTT] OK: Alarm updated");
    }
  }
  else if (strcmp(command, "alarm_delete") == 0) {
    JsonObjectConst fields = alarmFieldsFrom(doc);
    int index = -1;
    if (!readAlarmIndex(fields, &index)) {
      Serial.println("[MQTT] FAIL: Missing alarm index");
      Serial.println("[MQTT]   Usage: {\"command\":\"alarm_delete\",\"index\":1}");
      Serial.println("[MQTT] -------------------------------\n");
      return;
    }

    if (index < 1 || index > MAX_ALARMS) {
      Serial.println("[MQTT] FAIL: Invalid alarm index");
      Serial.println("[MQTT] -------------------------------\n");
      return;
    }

    if (deleteAlarm((uint8_t)(index - 1))) {
      publishAlarmsToFirebase("mqtt");
      Serial.println("[MQTT] OK: Alarm deleted");
    }
  }
  else if (strcmp(command, "get_status") == 0) {
    publishMqttStatus(getACState());
  }
  else {
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
