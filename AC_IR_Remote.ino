/*
 * ESP32-S3 Smart AC Remote
 */

#include "config.h"
#include "display.h"
#include "ir_control.h"
#include "ac_control.h"
#include "speaker_control.h"
#include "wifi_manager.h"
#include "firebase_client.h"
#include "button_control.h"
#include "ir_learning.h"
#include "sensors.h"
#include "alarm_manager.h"
#include "mqtt_broker.h"
#include <time.h>
#include <esp_sntp.h>
#include <ctype.h>
#include <stdio.h>

// power, temperature, mode, fanSpeed, brand
ACState acState = {false, 24, MODE_COOL, FAN_AUTO, BRAND_GREE};

// Operation mode
bool isLearning = false;

static bool ntpSynced = false;
static unsigned long ntpStartMs = 0;

static void startNtpSync() {
  sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  sntp_set_sync_interval(NTP_SYNC_INTERVAL_MS);
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
             NTP_SERVER_PRIMARY, NTP_SERVER_BACKUP1, NTP_SERVER_BACKUP2);
  ntpStartMs = millis();
  ntpSynced = false;
}

static void handleNtpSync() {
  if (ntpSynced) {
    return;
  }

  time_t now = time(nullptr);
  if (now > 1700000000) {
    ntpSynced = true;
    Serial.printf("[System] NTP synced in %lu ms\n", millis() - ntpStartMs);
    return;
  }

  if (millis() - ntpStartMs > NTP_SYNC_TIMEOUT_MS) {
    Serial.println("[System] NTP sync still pending (check UDP/123 or server)");
    ntpStartMs = millis();
  }
}

static void handleAutoDry(const SensorData& data) {
  static bool humidityAboveThreshold = false;
  static float lastThreshold = -1.0f;

  float threshold = getAutoDryThreshold();
  if (threshold != lastThreshold) {
    humidityAboveThreshold = false;
    lastThreshold = threshold;
  }

  if (threshold <= 0) {
    return;
  }

  if (!data.dht_valid) {
    return;
  }

  float humidity = data.dht_humidity;
  if (humidity < threshold - AUTO_DRY_HYSTERESIS) {
    humidityAboveThreshold = false;
    return;
  }

  if (!acState.power) {
    return;
  }

  if (humidityAboveThreshold) {
    return;
  }

  humidityAboveThreshold = true;
  if (acState.mode != MODE_DRY) {
    Serial.printf("[AutoDry] Humidity %.1f%% >= %.1f%%, switching to DRY\n",
                  humidity, threshold);
    acSetMode(MODE_DRY);
  }
}

static void handleSleepMode(const SensorData& data) {
  static bool lightBelowThreshold = false;
  static float lastThreshold = -1.0f;

  float threshold = getSleepLightThreshold();
  if (threshold != lastThreshold) {
    lightBelowThreshold = false;
    lastThreshold = threshold;
  }

  if (threshold <= 0) {
    return;
  }

  if (!data.light_valid) {
    return;
  }

  float lux = data.light_lux;
  if (lux > threshold + SLEEP_LIGHT_HYSTERESIS) {
    lightBelowThreshold = false;
    return;
  }

  if (!acState.power) {
    return;
  }

  if (lightBelowThreshold) {
    return;
  }

  lightBelowThreshold = true;
  if (acState.fanSpeed != FAN_LOW) {
    Serial.printf("[SleepMode] Light %.1f lx <= %.1f lx, lowering fan\n",
                  lux, threshold);
    acSetFan(FAN_LOW);
  }
}

static void printHeartbeat() {
  Serial.println("\n[Status] ------------------------------");
  Serial.printf("[Status] WiFi: %s\n", getWiFiStatus().c_str());
  Serial.printf("[Status] Firebase: %s\n", getFirebaseStatus().c_str());
  Serial.printf("[Status] MQTT: %s\n", getMqttStatus().c_str());
  Serial.printf("[Status] AC: %s\n", getACStateString().c_str());

  if (ntpSynced) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
      char buf[24];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
      Serial.printf("[Status] Time: %s\n", buf);
    } else {
      Serial.println("[Status] NTP: synced (time unavailable)");
    }
  } else {
    Serial.println("[Status] NTP: pending");
  }
  Serial.println("[Status] ------------------------------\n");
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);
  delay(1000);
  
  Serial.println("\n");
  Serial.println("========================================");
  Serial.println("  ESP32-S3 Smart AC Remote v3.0");
  Serial.println("  Firebase + WiFi + Voice Feedback");
  Serial.println("========================================");
  Serial.println();
  
  // Initialize display
  initDisplay();
  showBootScreen();
  delay(2000);
  
  // Initialize IR
  initIR();
  
  // Initialize speaker
  initSpeaker();
  
  // Initialize buttons
  initButtons();

  // Initialize alarms (persistent storage)
  initAlarmManager();
  
  // Initialize IR learning
  initIRLearning();

  // Initialize sensors
  initSensors();

  // Set default screen to Clock
  setScreen(SCREEN_CLOCK);
  Serial.println("[System] Default screen set to CLOCK");
  
  // Initialize WiFi Manager
  initWiFiManager();
  
  // If WiFi connected, initialize Firebase
  if (WiFi.status() == WL_CONNECTED) {
    initFirebase();
    publishAlarmsToFirebase("boot");

    initMqttBroker();
    
    // Initialize NTP for real time
    Serial.println("[System] Initializing NTP time sync...");
    startNtpSync();

    if (isFirebaseConfigured()) {
      // Queue initial state to Firebase
      firebaseQueueState(acState, "boot");
    } else {
      Serial.println("[Firebase] Not configured (update firebase_config.h)");
    }
    Serial.println("[System] OK: NTP configured");
  } else {
    Serial.println("[System] WiFi not connected");
    Serial.println("[System] Configure WiFi to enable Firebase");
  }
  
  // Show help
  printHelp();
  
  Serial.println("\n========================================");
  Serial.println("  System Ready!");
  Serial.println("========================================");
  Serial.println();
  
  // Show status
  printStatus();
}

void loop() {
  // Handle WiFi Manager (if in config portal mode)
  handleWiFiManager();
  
  // Handle Firebase background tasks (if any)
  handleFirebase();
  handleMqttBroker();

  // Periodic status log
  static unsigned long lastHeartbeatMs = 0;
  if (millis() - lastHeartbeatMs > 10000) {
    printHeartbeat();
    lastHeartbeatMs = millis();
  }

  // Alarm manager (ring + snooze)
  handleAlarmManager();
  
  // Handle button inputs
  handleButtons();
  
  // Check IR learning (if on IR learning screen)
  if (getCurrentScreen() == SCREEN_IR_LEARN) {
    checkIRReceive();
  }
  
  // Update clock screen animation if currently on clock screen
  static unsigned long lastClockUpdate = 0;
  extern ScreenMode getCurrentScreen();
  if (getCurrentScreen() == SCREEN_CLOCK && millis() - lastClockUpdate > 1000) {
    updateScreenDisplay();
    lastClockUpdate = millis();
  }

  // Update sensors screen periodically (throttled sensor reads in drawSensorsScreen)
  static unsigned long lastSensorsScreenUpdate = 0;
  if (getCurrentScreen() == SCREEN_SENSORS && millis() - lastSensorsScreenUpdate > 1000) {
    updateScreenDisplay();
    lastSensorsScreenUpdate = millis();
  }

  // Read sensors periodically (every 15 seconds)
  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 15000) {
    SensorData data = readAllSensors();
    printSensorData(data);
    handleAutoDry(data);
    handleSleepMode(data);
    if (isFirebaseConfigured()) {
      firebaseQueueStatus(getACState(), data, "periodic");
    }
    lastSensorRead = millis();
  }
  
  // Learning mode: check for IR signals
  if (isLearning) {
    checkReceivedSignal();
  }
  
  // Check serial commands
  if (Serial.available() > 0) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      handleCommand(line);
    }
  }
  
  handleNtpSync();

  delay(10);
}

// Handle commands
void handleCommand(const String& line) {
  if (line.length() == 0) {
    return;
  }

  char cmd = line.charAt(0);
  char cmdLower = (char)tolower(cmd);
  const char* args = line.c_str() + 1;
  while (*args == ' ') {
    args++;
  }

  switch(cmdLower) {
    case '1':  // Power toggle
      acPowerToggle();
      break;
      
    case '2':  // Temperature +
      acTempUp();
      break;
      
    case '3':  // Temperature -
      acTempDown();
      break;
      
    case '4':  // Mode cycle
      acModeCycle();
      break;
      
    case '5':  // Fan speed cycle
      acFanCycle();
      break;
      
    case '9':  // Switch brand
      setBrand((ACBrand)((acState.brand + 1) % BRAND_COUNT));
      break;
      
    case 'l':  // Enter learning mode
      if (!isLearning) {
        isLearning = true;
        startLearnMode();
      }
      break;
      
    case 'q':  // Exit learning mode
      if (isLearning) {
        isLearning = false;
        stopLearnMode();
      }
      break;
      
    case 's':  // Switch screen (manual test)
      {
        extern void setScreen(ScreenMode mode);
        extern ScreenMode getCurrentScreen();
        ScreenMode current = getCurrentScreen();
        ScreenMode next = (ScreenMode)((current + 1) % SCREEN_COUNT);
        Serial.printf("[System] Manual screen switch: %d -> %d\n", current, next);
        setScreen(next);
      }
      break;
      
    case 't':  // Test speaker
      testSpeaker();
      break;

    case 'm':  // Manual sensor test
      {
        SensorData data = readAllSensors();
        printSensorData(data);
      }
      break;

    case 'w':  // WiFi status
      printStatus();
      break;
      
    case 'r': {  // Reset WiFi config
      Serial.println("\n[System] WARNING: Reset WiFi Configuration?");
      Serial.println("[System] Press 'y' to confirm, any other key to cancel");
      
      unsigned long startTime = millis();
      while (millis() - startTime < 5000) {  // 5 second timeout
        if (Serial.available()) {
          char confirm = Serial.read();
          while (Serial.available()) Serial.read();  // Clear buffer
          
          if (confirm == 'y' || confirm == 'Y') {
            clearWiFiConfig();
            Serial.println("[System] WiFi config cleared!");
            Serial.println("[System] Please restart device to reconfigure");
          } else {
            Serial.println("[System] Reset cancelled");
          }
          return;
        }
        delay(100);
      }
      Serial.println("[System] Reset cancelled (timeout)");
      break;
    }
      
    case 'h':
      printHelp();
      break;

    case 'a': {  // Add alarm: a HH MM [name]
      int hour = -1;
      int minute = -1;
      char name[ALARM_NAME_LEN] = {0};
      int parsed = sscanf(args, "%d %d %[^\n]", &hour, &minute, name);
      if (parsed < 2) {
        Serial.println("[Alarm] Usage: a HH MM [name]");
        break;
      }
      const char* alarmName = (parsed >= 3) ? name : nullptr;
      addAlarm((uint8_t)hour, (uint8_t)minute, alarmName);
      break;
    }

    case 'u': {  // Update alarm: u INDEX HH MM [name]
      int index = -1;
      int hour = -1;
      int minute = -1;
      char name[ALARM_NAME_LEN] = {0};
      int parsed = sscanf(args, "%d %d %d %[^\n]", &index, &hour, &minute, name);
      if (parsed < 3) {
        Serial.println("[Alarm] Usage: u INDEX HH MM [name]");
        break;
      }
      const char* alarmName = (parsed >= 4) ? name : nullptr;
      updateAlarm((uint8_t)(index - 1), (uint8_t)hour, (uint8_t)minute, alarmName);
      break;
    }

    case 'd': {  // Delete alarm: d INDEX
      int index = -1;
      int parsed = sscanf(args, "%d", &index);
      if (parsed < 1) {
        Serial.println("[Alarm] Usage: d INDEX");
        break;
      }
      deleteAlarm((uint8_t)(index - 1));
      break;
    }

    case 'p':  // Print alarms
      printAlarms();
      break;
      
    case '\n':
    case '\r':
      // Ignore newline
      break;
      
    default:
      Serial.println("[System] Unknown command, press 'h' for help");
      break;
  }
}

// Print help
void printHelp() {
  Serial.println("\n========================================");
  Serial.println("  Command List");
  Serial.println("========================================");
  Serial.println();
  Serial.println("  AC Control:");
  Serial.println("  ----------------------------------------");
  Serial.println("  1 - Power ON/OFF");
  Serial.println("  2 - Temperature + (16-30C)");
  Serial.println("  3 - Temperature -");
  Serial.println("  4 - Mode cycle (Auto/Cool/Heat/Dry/Fan)");
  Serial.println("  5 - Fan speed cycle (Auto/Low/Med/High)");
  Serial.println("  9 - Switch AC brand");
  Serial.println();
  Serial.println("  System:");
  Serial.println("  ----------------------------------------");
  Serial.println("  s - Switch screen (manual test)");
  Serial.println("  t - Test speaker");
  Serial.println("  m - Read sensors (PIR, DHT11, GY-30)");
  Serial.println("  l - Enter IR learning mode");
  Serial.println("  q - Exit special mode");
  Serial.println("  w - Show WiFi/Firebase/MQTT status");
  Serial.println("  r - Reset WiFi configuration");
  Serial.println("  h - Show this help");
  Serial.println();
  Serial.println("  Alarm:");
  Serial.println("  ----------------------------------------");
  Serial.println("  a HH MM [name] - Add alarm");
  Serial.println("  u INDEX HH MM [name] - Update alarm");
  Serial.println("  d INDEX - Delete alarm");
  Serial.println("  p - List alarms");
  Serial.println();
  Serial.println("  Current Settings:");
  Serial.println("  ----------------------------------------");
  Serial.printf("  Brand: %s\n", getBrandName(acState.brand));
  Serial.printf("  WiFi: %s\n", getWiFiStatus().c_str());
  Serial.printf("  Firebase: %s\n", getFirebaseStatus().c_str());
  Serial.printf("  MQTT: %s\n", getMqttStatus().c_str());
  Serial.println("========================================\n");
}

// Print status
void printStatus() {
  Serial.println("\n========================================");
  Serial.println("  System Status");
  Serial.println("========================================");
  Serial.println();
  
  // WiFi Status
  Serial.println("  WiFi:");
  Serial.println("  ----------------------------------------");
  Serial.printf("  Status: %s\n", getWiFiStatus().c_str());
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  Signal: %d dBm\n", WiFi.RSSI());
  }
  Serial.println();
  
  // Firebase Status
  Serial.println("  Firebase:");
  Serial.println("  ----------------------------------------");
  Serial.printf("  Status: %s\n", getFirebaseStatus().c_str());
  Serial.println();

  // MQTT Status
  Serial.println("  MQTT:");
  Serial.println("  ----------------------------------------");
  Serial.printf("  Status: %s\n", getMqttStatus().c_str());
  Serial.println();
  
  // AC Status
  Serial.println("  Air Conditioner:");
  Serial.println("  ----------------------------------------");
  Serial.printf("  Power: %s\n", acState.power ? "ON" : "OFF");
  if (acState.power) {
    Serial.printf("  Temperature: %dC\n", acState.temperature);
    const char* modes[] = {"Auto", "Cool", "Heat", "Dry", "Fan"};
    Serial.printf("  Mode: %s\n", modes[acState.mode]);
    const char* speeds[] = {"Auto", "Low", "Med", "High"};
    Serial.printf("  Fan: %s\n", speeds[acState.fanSpeed]);
  }
  Serial.printf("  Brand: %s\n", getBrandName(acState.brand));
  Serial.println();
  
  Serial.println("========================================\n");
}
