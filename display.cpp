#include "display.h"
#include "button_control.h"
#include "ac_control.h"
#include "ir_learning_enhanced.h"
#include "alarm_manager.h"
#include "sensors.h"
#include <WiFi.h>
#include <time.h>
#include <string.h>
#include "firebase_client.h"

TFT_eSPI tft = TFT_eSPI();

void initDisplay() {
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);
  tft.init();
  
  tft.setRotation(0);
  
  tft.fillScreen(COLOR_BG);
  Serial.println("显示屏初始化完成");
}

void showBootScreen() {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_TITLE);
  tft.setTextSize(3);
  tft.setCursor(30, 60);
  tft.println("ESP32-S3");
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(20, 110);
  tft.println("Smart AC");
  tft.setTextColor(COLOR_ON);
  tft.setTextSize(2);
  tft.setCursor(35, 160);
  tft.println("+ Voice");
  tft.drawCircle(120, 200, 25, COLOR_TITLE);
  tft.drawCircle(120, 200, 20, COLOR_ON);
}

void updateDisplay(const struct ACState& state) {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, 240, 35, COLOR_TITLE);
  tft.setTextColor(COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(45, 10);
  tft.println("AC REMOTE");
  
  int yPos = 40;
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(10, yPos);
  tft.print("Brand:");
  tft.setTextColor(COLOR_MODE);
  tft.setCursor(60, yPos);
  
  const char* shortNames[] = {"Daikin", "Mitsubishi", "Panasonic", "Gree", 
                               "Midea", "Haier", "Samsung", "LG", "Fujitsu", "Hitachi"};
  if (state.brand < 10) {
    tft.println(shortNames[state.brand]);
  }
  
  yPos = 60;
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(10, yPos);
  tft.print("Power:");
  
  if (state.power) {
    tft.setTextColor(COLOR_ON);
    tft.setCursor(100, yPos);
    tft.println("ON");
    tft.fillCircle(200, yPos + 8, 8, COLOR_ON);
  } else {
    tft.setTextColor(COLOR_OFF);
    tft.setCursor(100, yPos);
    tft.println("OFF");
    tft.fillCircle(200, yPos + 8, 8, COLOR_OFF);
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(1);
    tft.setCursor(30, 200);
    tft.println("Press '1' to turn ON");
    tft.setCursor(30, 215);
    tft.println("Press 'v' for Voice");
    return;
  }
  
  yPos = 100;
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, yPos);
  tft.print("Temp:");
  tft.setTextColor(COLOR_TEMP);
  tft.setTextSize(4);
  tft.setCursor(80, yPos - 5);
  tft.printf("%d", state.temperature);
  tft.setTextSize(3);
  tft.setCursor(150, yPos);
  tft.print("C");
  tft.drawCircle(145, yPos + 5, 4, COLOR_TEMP);
  
  yPos = 155;
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, yPos);
  tft.print("Mode:");
  tft.setTextColor(COLOR_MODE);
  tft.setCursor(100, yPos);
  const char* modeText[] = {"AUTO", "COOL", "HEAT", "DRY", "FAN"};
  tft.println(modeText[state.mode]);
  
  yPos = 190;
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, yPos);
  tft.print("Fan:");
  tft.setTextColor(COLOR_ON);
  tft.setCursor(100, yPos);
  const char* fanText[] = {"AUTO", "LOW", "MED", "HIGH"};
  tft.println(fanText[state.fanSpeed]);
  
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, 225);
  tft.print("'h' help  'v' voice");
}

void showStatusIndicator(const char* text, uint16_t color) {
  tft.fillRect(0, 220, 240, 20, COLOR_BG);
  tft.setTextColor(color);
  tft.setTextSize(1);
  tft.setCursor(10, 225);
  tft.print(text);
}

// Multi-screen display system
void updateScreenDisplay() {
  extern ScreenMode getCurrentScreen();
  ScreenMode screen = getCurrentScreen();

  tft.fillScreen(COLOR_BG);
  
  switch(screen) {
    case SCREEN_VOLUME:
      Serial.println("[Display] Drawing VOLUME screen");
      drawVolumeScreen();
      break;
    case SCREEN_CLOCK:
      drawClockScreen();
      break;
    case SCREEN_NETWORK:
      Serial.println("[Display] Drawing NETWORK screen");
      drawNetworkScreen();
      break;
    case SCREEN_AC:
      Serial.println("[Display] Drawing AC screen");
      drawACScreen();
      break;
    case SCREEN_IR_LEARN:
      Serial.println("[Display] Drawing IR LEARN screen");
      drawIRLearnScreen();
      break;
    case SCREEN_SENSORS:
      Serial.println("[Display] Drawing SENSORS screen");
      drawSensorsScreen();
      break;
    case SCREEN_ALARM:
      Serial.println("[Display] Drawing ALARM screen");
      drawAlarmScreen();
      break;
    default:
      Serial.printf("[Display] ERROR: Unknown screen %d\n", screen);
      break;
  }
}

// Draw volume control screen
void drawVolumeScreen() {
  extern int getSpeakerVolume();
  int volume = getSpeakerVolume();
  
  // Title
  tft.setTextColor(COLOR_TITLE);
  tft.setTextSize(3);
  tft.setCursor(40, 20);
  tft.println("VOLUME");
  
  // Volume percentage
  tft.setTextColor(COLOR_TEMP);
  tft.setTextSize(5);
  tft.setCursor(70, 80);
  tft.printf("%d%%", volume);
  
  // Volume bar background
  int barX = 30;
  int barY = 150;
  int barWidth = 180;
  int barHeight = 30;
  tft.drawRect(barX, barY, barWidth, barHeight, COLOR_TEXT);
  
  // Volume bar fill
  int fillWidth = (barWidth - 4) * volume / 100;
  uint16_t fillColor = volume > 70 ? COLOR_ON : (volume > 30 ? COLOR_TEMP : COLOR_MODE);
  tft.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, fillColor);
  
  // Instructions
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, 200);
  tft.println("JOY UP/DOWN: VOL +/-5%");
  tft.setCursor(10, 212);
  tft.println("JOY L/R: SWITCH");
}

// Draw clock/date screen with animation
void drawClockScreen() {
  // Title
  tft.setTextColor(COLOR_TITLE);
  tft.setTextSize(2);
  tft.setCursor(80, 10);
  tft.println("CLOCK");
  
  // Get real time from NTP
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) {
    // Fallback to uptime if NTP not available
    unsigned long totalSeconds = millis() / 1000;
    int hours = (totalSeconds / 3600) % 24;
    int minutes = (totalSeconds / 60) % 60;
    int seconds = totalSeconds % 60;
    
    // Time display (uptime)
    tft.setTextColor(COLOR_TEMP);
    tft.setTextSize(4);
    tft.setCursor(30, 60);
    tft.printf("%02d:%02d:%02d", hours, minutes, seconds);
    
    // Date (placeholder)
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(2);
    tft.setCursor(40, 120);
    tft.println("Syncing...");
    tft.setCursor(50, 145);
    tft.println("NO NTP");
  } else {
    // Time display (real time)
    tft.setTextColor(COLOR_TEMP);
    tft.setTextSize(4);
    tft.setCursor(30, 60);
    tft.printf("%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    // Date display (real date)
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(2);
    tft.setCursor(30, 120);
    tft.printf("%04d-%02d-%02d", 
               timeinfo.tm_year + 1900, 
               timeinfo.tm_mon + 1, 
               timeinfo.tm_mday);
    
    // Day of week
    const char* days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    tft.setCursor(70, 145);
    tft.println(days[timeinfo.tm_wday]);
  }
  
  // Animation: Pulsing heart icon
  static int pulsePhase = 0;
  pulsePhase = (pulsePhase + 1) % 60;
  int heartSize = 15 + (pulsePhase < 30 ? pulsePhase / 3 : (60 - pulsePhase) / 3);
  
  int heartX = 120;
  int heartY = 190;
  
  // Draw heart shape
  tft.fillCircle(heartX - heartSize/2, heartY, heartSize/2, TFT_RED);
  tft.fillCircle(heartX + heartSize/2, heartY, heartSize/2, TFT_RED);
  tft.fillTriangle(
    heartX - heartSize, heartY,
    heartX + heartSize, heartY,
    heartX, heartY + heartSize * 1.5,
    TFT_RED
  );
  
  // Info text
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(65, 225);
  tft.println("GMT+8 Time");
}

// Draw network configuration screen
void drawNetworkScreen() {
  extern String getWiFiStatus();
  
  // Title
  tft.setTextColor(COLOR_TITLE);
  tft.setTextSize(2);
  tft.setCursor(50, 10);
  tft.println("NETWORK");
  
  int yPos = 45;
  
  // WiFi Status
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, yPos);
  tft.println("WiFi Status:");
  
  yPos += 15;
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(COLOR_ON);
    tft.setCursor(10, yPos);
    tft.printf("Connected to:");
    yPos += 12;
    tft.setCursor(10, yPos);
    tft.println(WiFi.SSID());
    yPos += 12;
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(10, yPos);
    tft.printf("IP: %s", WiFi.localIP().toString().c_str());
  } else {
    tft.setTextColor(COLOR_OFF);
    tft.setCursor(10, yPos);
    tft.println("Not Connected");
  }
  
  yPos += 25;
  
  // Firebase Status
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, yPos);
  tft.println("Firebase:");
  
  yPos += 15;
  String fbStatus = getFirebaseStatus();
  if (fbStatus.length() > 20) {
    fbStatus = fbStatus.substring(0, 20);
  }

  if (isFirebaseConnected()) {
    tft.setTextColor(COLOR_ON);
    tft.setCursor(10, yPos);
    tft.println("Connected");
  } else {
    tft.setTextColor(COLOR_OFF);
    tft.setCursor(10, yPos);
    tft.println(fbStatus);
  }
  
  yPos += 25;
  
  // Configuration Instructions
  tft.setTextColor(COLOR_MODE);
  tft.setTextSize(1);
  tft.setCursor(10, yPos);
  tft.println("Configure WiFi:");
  
  yPos += 15;
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(10, yPos);
  tft.println("1. Connect to:");
  yPos += 12;
  tft.setTextColor(COLOR_TEMP);
  tft.setCursor(20, yPos);
  tft.println("ESP32_AC_Remote");
  
  yPos += 15;
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(10, yPos);
  tft.println("2. Open browser:");
  yPos += 12;
  tft.setTextColor(COLOR_TEMP);
  tft.setCursor(20, yPos);
  tft.println("192.168.4.1");
  
  yPos += 20;
  tft.setTextColor(COLOR_OFF);
  tft.setTextSize(1);
  tft.setCursor(10, yPos);
  tft.println("DOUBLE CLICK: RESET WIFI");
}

// Draw AC control screen
void drawACScreen() {
  extern ACState getACState();
  ACState state = getACState();
  
  // Title
  tft.setTextColor(COLOR_TITLE);
  tft.setTextSize(2);
  tft.setCursor(50, 10);
  tft.println("AC CONTROL");
  
  int yPos = 45;
  
  // Power status
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, yPos);
  tft.print("Power:");
  
  if (state.power) {
    tft.setTextColor(COLOR_ON);
    tft.setCursor(100, yPos);
    tft.println("ON");
    tft.fillCircle(200, yPos + 8, 8, COLOR_ON);
  } else {
    tft.setTextColor(COLOR_OFF);
    tft.setCursor(100, yPos);
    tft.println("OFF");
    tft.fillCircle(200, yPos + 8, 8, COLOR_OFF);
  }
  
  yPos += 40;
  
  // Temperature
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, yPos);
  tft.print("Temp:");
  tft.setTextColor(COLOR_TEMP);
  tft.setTextSize(4);
  tft.setCursor(100, yPos - 5);
  tft.printf("%d", state.temperature);
  tft.setTextSize(2);
  tft.setCursor(150, yPos + 5);
  tft.print("C");
  tft.drawCircle(145, yPos + 10, 3, COLOR_TEMP);
  
  yPos += 45;
  
  // Mode
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, yPos);
  tft.print("Mode:");
  tft.setTextColor(COLOR_MODE);
  tft.setCursor(100, yPos);
  const char* modeText[] = {"AUTO", "COOL", "HEAT", "DRY", "FAN"};
  tft.println(modeText[state.mode]);
  
  yPos += 30;
  
  // Fan speed
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, yPos);
  tft.print("Fan:");
  tft.setTextColor(COLOR_ON);
  tft.setCursor(100, yPos);
  const char* fanText[] = {"AUTO", "LOW", "MED", "HIGH"};
  tft.println(fanText[state.fanSpeed]);
  
  yPos += 30;
  
  // Brand
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, yPos);
  tft.printf("Brand: %s", getBrandName(state.brand));
  
  // Instructions
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, 215);
  tft.println("Use Serial for control");
  tft.setCursor(10, 227);
  tft.println("JOY L/R: SWITCH");
}

// Draw IR Learning screen
void drawIRLearnScreen() {
  int currentDevice = getCurrentLearnDevice();
  LearnState state = getLearnState();
  LearnedDevice device = getLearnedDevice(currentDevice);
  
  // Title
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(3);
  tft.setCursor(20, 20);
  tft.println("IR LEARN");
  
  // Current device
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 60);
  tft.printf("Device: %d / 5", currentDevice + 1);
  
  // Device status
  int yPos = 90;
  if (device.hasData) {
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(10, yPos);
    tft.print("Status: Learned");
    
    yPos += 25;
    tft.setTextColor(TFT_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(10, yPos);
    
    String protocolStr = String(typeToString(device.protocol));
    if (protocolStr.length() > 20) {
      protocolStr = protocolStr.substring(0, 17) + "...";
    }
    tft.printf("Protocol: %s", protocolStr.c_str());
    
    yPos += 15;
    tft.setCursor(10, yPos);
    tft.printf("Value: 0x%llX", device.value);
  } else {
    tft.setTextColor(TFT_RED);
    tft.setCursor(10, yPos);
    tft.print("Status: Empty");
  }
  
  // Learning state
  yPos = 140;
  tft.setTextSize(2);
  
  switch(state) {
    case LEARN_IDLE:
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(10, yPos);
      tft.println("Ready");
      break;
      
    case LEARN_WAITING:
      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(10, yPos);
      tft.println("Waiting...");
      yPos += 25;
      tft.setTextSize(1);
      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(10, yPos);
      tft.println("Point remote & press");
      break;
      
    case LEARN_RECEIVED:
      tft.setTextColor(TFT_GREEN);
      tft.setCursor(10, yPos);
      tft.println("Received!");
      yPos += 25;
      tft.setTextSize(1);
      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(10, yPos);
      tft.println("Click to save");
      break;
      
    case LEARN_SAVED:
      tft.setTextColor(TFT_GREENYELLOW);
      tft.setCursor(10, yPos);
      tft.println("Saved!");
      break;
  }
  
  // Instructions
  yPos = 185;
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(5, yPos);
  tft.println("JOY UP/DOWN: DEVICE +/-");
  yPos += 12;
  tft.setCursor(5, yPos);
  tft.println("CLICK: LEARN/SAVE");
  yPos += 12;
  tft.setCursor(5, yPos);
  tft.println("JOY L/R: SWITCH");
}

// Draw sensors screen (temperature / humidity / light)
void drawSensorsScreen() {
  static SensorData cachedData = {};
  static unsigned long lastReadMs = 0;

  unsigned long now = millis();
  const unsigned long refreshMs = 2500;

  if (lastReadMs == 0 || (now - lastReadMs) >= refreshMs) {
    cachedData = readAllSensors();
    lastReadMs = now;
  }

  // Title
  tft.setTextColor(COLOR_TITLE);
  tft.setTextSize(2);
  tft.setCursor(65, 10);
  tft.println("SENSORS");

  // Motion indicator
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(10, 35);
  tft.print("Motion:");
  if (cachedData.motionDetected) {
    tft.setTextColor(COLOR_ON);
    tft.setCursor(70, 35);
    tft.println("YES");
  } else {
    tft.setTextColor(COLOR_OFF);
    tft.setCursor(70, 35);
    tft.println("NO");
  }

  int yPos = 60;

  // Temperature
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, yPos);
  tft.print("Temp:");
  if (cachedData.dht_valid) {
    tft.setTextColor(COLOR_TEMP);
    tft.setTextSize(3);
    tft.setCursor(95, yPos - 5);
    tft.print(cachedData.dht_temperature, 1);
    tft.print("C");
  } else {
    tft.setTextColor(COLOR_OFF);
    tft.setTextSize(2);
    tft.setCursor(95, yPos);
    tft.print("N/A");
  }

  yPos += 55;

  // Humidity
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, yPos);
  tft.print("Hum:");
  if (cachedData.dht_valid) {
    tft.setTextColor(COLOR_TEMP);
    tft.setTextSize(3);
    tft.setCursor(95, yPos - 5);
    tft.print(cachedData.dht_humidity, 1);
    tft.print("%");
  } else {
    tft.setTextColor(COLOR_OFF);
    tft.setTextSize(2);
    tft.setCursor(95, yPos);
    tft.print("N/A");
  }

  yPos += 55;

  // Light
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, yPos);
  tft.print("Light:");
  if (cachedData.light_valid) {
    tft.setTextColor(COLOR_TEMP);
    tft.setTextSize(3);
    tft.setCursor(95, yPos - 5);
    if (cachedData.light_lux >= 1000.0f) {
      tft.print(cachedData.light_lux, 0);
      tft.print("lx");
    } else {
      tft.print(cachedData.light_lux, 1);
      tft.print("lx");
    }
  } else {
    tft.setTextColor(COLOR_OFF);
    tft.setTextSize(2);
    tft.setCursor(95, yPos);
    tft.print("N/A");
  }

  // Footer
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, 215);
  tft.printf("Updated: %lus ago", (now - lastReadMs) / 1000);
  tft.setCursor(10, 227);
  tft.println("JOY L/R: SWITCH");
}

// Draw alarm ringing screen
void drawAlarmScreen() {
  const char* alarmName = getActiveAlarmName();
  char displayName[ALARM_NAME_LEN];

  if (alarmName && alarmName[0]) {
    strncpy(displayName, alarmName, sizeof(displayName) - 1);
    displayName[sizeof(displayName) - 1] = '\0';
  } else {
    strncpy(displayName, "alarm", sizeof(displayName) - 1);
    displayName[sizeof(displayName) - 1] = '\0';
  }

  if (strlen(displayName) > 18) {
    displayName[15] = '.';
    displayName[16] = '.';
    displayName[17] = '.';
    displayName[18] = '\0';
  }

  // Title
  tft.setTextColor(TFT_RED);
  tft.setTextSize(3);
  tft.setCursor(45, 20);
  tft.println("ALARM");

  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, 70);
  tft.println("Name:");

  tft.setTextColor(COLOR_TEMP);
  tft.setCursor(10, 95);
  tft.println(displayName);

  // Instructions
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, 170);
  tft.println("JOY UP/DOWN: SNOOZE 5 MIN");
  tft.setCursor(10, 185);
  tft.println("CLICK: STOP ALARM");
  tft.setCursor(10, 210);
  tft.println("Other controls locked");
}
