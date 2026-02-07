/*
 * AWS IoT MQTT Control - Implementation
 */

#include "config.h"

#if USE_AWS_IOT

#include "aws_mqtt.h"
#include "ac_control.h"
#include "ir_learning_enhanced.h"

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(256);

bool awsInitialized = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 5000; // 5 seconds

// Initialize AWS IoT connection
void initAWS() {
  Serial.println("\n========================================");
  Serial.println("  AWS IoT Core Initialization");
  Serial.println("========================================");
  
  // Configure WiFiClientSecure to use AWS IoT device credentials
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);
  
  // Connect to AWS IoT MQTT broker
  client.begin(AWS_IOT_ENDPOINT, 8883, net);
  
  // Set message handler
  client.onMessage(messageHandler);
  
  Serial.printf("[AWS] Endpoint: %s\n", AWS_IOT_ENDPOINT);
  Serial.printf("[AWS] Thing Name: %s\n", THINGNAME);
  Serial.printf("[AWS] Publish Topic: %s\n", AWS_IOT_PUBLISH_TOPIC);
  Serial.printf("[AWS] Subscribe Topic: %s\n", AWS_IOT_SUBSCRIBE_TOPIC);
  
  awsInitialized = true;
  
  // Try to connect
  connectAWS();
}

// Connect to AWS IoT Core
bool connectAWS() {
  if (!awsInitialized) {
    Serial.println("[AWS] FAIL: Not initialized");
    return false;
  }
  
  Serial.print("[AWS] Connecting to AWS IoT Core");
  
  int attempts = 0;
  while (!client.connect(THINGNAME) && attempts < 5) {
    Serial.print(".");
    delay(1000);
    attempts++;
  }
  Serial.println();
  
  if (!client.connected()) {
    Serial.println("[AWS] FAIL: Connection failed!");
    Serial.println("[AWS]   Check:");
    Serial.println("[AWS]   1. WiFi connection");
    Serial.println("[AWS]   2. AWS IoT endpoint");
    Serial.println("[AWS]   3. Certificates");
    Serial.println("[AWS]   4. Policy permissions");
    return false;
  }
  
  Serial.println("[AWS] OK: Connected to AWS IoT Core!");
  
  // Subscribe to command topic
  if (client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC)) {
    Serial.printf("[AWS] OK: Subscribed to: %s\n", AWS_IOT_SUBSCRIBE_TOPIC);
  } else {
    Serial.printf("[AWS] FAIL: Failed to subscribe to: %s\n", AWS_IOT_SUBSCRIBE_TOPIC);
  }
  
  Serial.println("========================================\n");
  
  // Publish initial status
  publishACStatus(getACState());
  
  return true;
}

// Publish AC status to AWS
void publishACStatus(const ACState& state) {
  if (!client.connected()) {
    return;
  }
  
  // Create JSON document
  StaticJsonDocument<256> doc;
  
  doc["device"] = THINGNAME;
  doc["timestamp"] = millis();
  doc["power"] = state.power;
  doc["temperature"] = state.temperature;
  
  // Mode string
  const char* modes[] = {"auto", "cool", "heat", "dry", "fan"};
  doc["mode"] = modes[state.mode];
  
  // Fan speed string
  const char* fanSpeeds[] = {"auto", "low", "medium", "high"};
  doc["fan_speed"] = fanSpeeds[state.fanSpeed];
  
  // Brand string
  doc["brand"] = getBrandName(state.brand);
  
  // Serialize to string
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  
  // Publish
  if (client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer)) {
    Serial.println("[AWS] OK: Status published");
    Serial.printf("[AWS]   %s\n", jsonBuffer);
  } else {
    Serial.println("[AWS] FAIL: Failed to publish status");
  }
}

// Handle incoming MQTT messages
void messageHandler(String &topic, String &payload) {
  Serial.println("\n[AWS] ----------------------------------------");
  Serial.println("[AWS] Received MQTT Message");
  Serial.println("[AWS] ----------------------------------------");
  Serial.printf("[AWS] Topic: %s\n", topic.c_str());
  Serial.printf("[AWS] Payload: %s\n", payload.c_str());
  
  // Parse JSON payload
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload);
  
  if (error) {
    Serial.println("[AWS] FAIL: JSON parsing failed");
    Serial.printf("[AWS]   Error: %s\n", error.c_str());
    return;
  }
  
  // Extract command
  const char* command = doc["command"];
  
  if (command == nullptr) {
    Serial.println("[AWS] FAIL: No command in payload");
    return;
  }
  
  Serial.printf("[AWS] Command: %s\n", command);
  
  // Execute command based on type
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
  else if (strcmp(command, "mode_cycle") == 0) {
    acModeCycle();
  }
  else if (strcmp(command, "fan_cycle") == 0) {
    acFanCycle();
  }
  else if (strcmp(command, "switch_brand") == 0) {
    // Cycle to next brand
    ACState state = getACState();
    ACBrand newBrand = (ACBrand)((state.brand + 1) % BRAND_COUNT);
    setBrand(newBrand);
    Serial.printf("[AWS] Brand switched to: %s\n", getBrandName(newBrand));
  }
  else if (strcmp(command, "set_brand") == 0) {
    // Set specific brand
    if (doc.containsKey("value")) {
      const char* brandStr = doc["value"];
      
      ACBrand brand = BRAND_DAIKIN; // Default
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
        Serial.printf("[AWS] FAIL: Unknown brand: %s\n", brandStr);
        Serial.println("[AWS] ----------------------------------------\n");
        return;
      }
      
      setBrand(brand);
      Serial.printf("[AWS] Brand set to: %s\n", getBrandName(brand));
    }
  }
  // Custom IR signal (learned signals)
  else if (strcmp(command, "custom") == 0) {
    if (doc.containsKey("id")) {
      const char* idStr = doc["id"];
      int deviceId = atoi(idStr);
      
      if (deviceId < 1 || deviceId > 5) {
        Serial.printf("[AWS] FAIL: Invalid device ID: %d (must be 1-5)\n", deviceId);
        Serial.println("[AWS] ----------------------------------------\n");
        return;
      }
      
      // Backward compatibility: map device ID to first signal of that device group
      // Device 1 → Signal 0, Device 2 → Signal 8, Device 3 → Signal 16, etc.
      int signalIndex = (deviceId - 1) * MAX_BUTTONS_PER_DEVICE;
      
      if (!isSignalLearned(signalIndex)) {
        Serial.printf("[AWS] FAIL: Signal %d (Device %d) not learned\n", signalIndex, deviceId);
        Serial.println("[AWS] ----------------------------------------\n");
        return;
      }
      
      Serial.printf("[AWS] Sending Signal %d (compat: Device %d)...\n", signalIndex, deviceId);
      sendSignal(signalIndex);
      Serial.printf("[AWS] OK: Signal %d sent (Device %d)\n", signalIndex, deviceId);
    } else {
      Serial.println("[AWS] FAIL: Missing 'id' parameter");
      Serial.println("[AWS]   Usage: {\"command\":\"custom\",\"id\":\"1\"}");
    }
  }
  else {
    Serial.printf("[AWS] FAIL: Unknown command: %s\n", command);
  }
  
  Serial.println("[AWS] ----------------------------------------\n");
  
  // Publish updated status
  delay(500); // Wait for AC control to complete
  publishACStatus(getACState());
}

// MQTT loop (call in main loop)
void handleAWS() {
  if (!awsInitialized) {
    return;
  }
  
  // Handle MQTT client
  client.loop();
  
  // Auto-reconnect if disconnected
  if (!client.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = now;
      Serial.println("[AWS] Connection lost, attempting to reconnect...");
      connectAWS();
    }
  }
}

// Check if connected to AWS
bool isAWSConnected() {
  return client.connected();
}

// Get AWS connection status
String getAWSStatus() {
  if (!awsInitialized) {
    return "Not initialized";
  }
  
  if (client.connected()) {
    return "Connected to AWS IoT";
  }
  
  return "Disconnected";
}

#endif // USE_AWS_IOT
