/*
 * WiFi Manager - Implementation
 */

#include "wifi_manager.h"
#include "ir_learning_enhanced.h"
#include <ArduinoJson.h>

#define WIFI_CONFIG_FILE "/wifi_config.txt"
#define AP_SSID "ESP32_AC_Remote"
#define AP_PASSWORD "12345678"

WebServer server(80);
WiFiCredentials wifiCreds;
bool portalActive = false;

// HTML for configuration page
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 AC Remote - WiFi Setup</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
      padding: 20px;
    }
    .container {
      background: white;
      padding: 40px;
      border-radius: 20px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
      max-width: 450px;
      width: 100%;
    }
    h1 {
      color: #667eea;
      text-align: center;
      margin-bottom: 10px;
      font-size: 28px;
    }
    .subtitle {
      text-align: center;
      color: #666;
      margin-bottom: 30px;
      font-size: 14px;
    }
    .form-group {
      margin-bottom: 20px;
    }
    label {
      display: block;
      margin-bottom: 8px;
      color: #333;
      font-weight: 600;
      font-size: 14px;
    }
    input {
      width: 100%;
      padding: 14px;
      border: 2px solid #e0e0e0;
      border-radius: 10px;
      font-size: 16px;
      transition: all 0.3s;
    }
    input:focus {
      outline: none;
      border-color: #667eea;
      box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
    }
    button {
      width: 100%;
      padding: 16px;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      border: none;
      border-radius: 10px;
      font-size: 18px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s;
      margin-top: 10px;
    }
    button:hover {
      transform: translateY(-2px);
      box-shadow: 0 10px 25px rgba(102, 126, 234, 0.4);
    }
    button:active {
      transform: translateY(0);
    }
    .info {
      background: #f0f4ff;
      padding: 16px;
      border-radius: 10px;
      margin-bottom: 25px;
      color: #555;
      font-size: 14px;
      border-left: 4px solid #667eea;
    }
    .success {
      background: #d4edda;
      color: #155724;
      padding: 16px;
      border-radius: 10px;
      margin-top: 20px;
      display: none;
      border-left: 4px solid #28a745;
      animation: slideIn 0.3s;
    }
    @keyframes slideIn {
      from { opacity: 0; transform: translateY(-10px); }
      to { opacity: 1; transform: translateY(0); }
    }
    .spinner {
      display: none;
      border: 3px solid #f3f3f3;
      border-top: 3px solid #667eea;
      border-radius: 50%;
      width: 20px;
      height: 20px;
      animation: spin 1s linear infinite;
      margin: 0 auto;
    }
    @keyframes spin {
      0% { transform: rotate(0deg); }
      100% { transform: rotate(360deg); }
    }
    .icon { font-size: 48px; text-align: center; margin-bottom: 20px; }
  </style>
</head>
<body>
  <div class="container">
    <div class="icon">📡</div>
    <h1>WiFi Configuration</h1>
    <div class="subtitle">ESP32 Smart AC Remote</div>
    
    <div class="info">
      <strong>📌 Setup Instructions:</strong><br>
      1. Enter your WiFi network name (SSID)<br>
      2. Enter your WiFi password<br>
      3. Click Save to connect device<br>
      4. Device will restart and connect to Firebase
    </div>
    
    <form id="wifiForm">
      <div class="form-group">
        <label for="ssid">🌐 WiFi Network (SSID)</label>
        <input type="text" id="ssid" name="ssid" required 
               placeholder="Enter your WiFi name" autocomplete="off">
      </div>
      
      <div class="form-group">
        <label for="password">🔐 WiFi Password</label>
        <input type="password" id="password" name="password" required 
               placeholder="Enter your WiFi password" autocomplete="off">
      </div>
      
      <button type="submit" id="submitBtn">
        <span id="btnText">💾 Save & Connect</span>
        <div class="spinner" id="spinner"></div>
      </button>
    </form>
    
    <div class="success" id="successMsg">
      <strong>✅ Success!</strong><br>
      Configuration saved successfully!<br>
      Device will restart in 3 seconds...
    </div>
  </div>
  
  <script>
    document.getElementById('wifiForm').addEventListener('submit', function(e) {
      e.preventDefault();
      
      const ssid = document.getElementById('ssid').value;
      const password = document.getElementById('password').value;
      const submitBtn = document.getElementById('submitBtn');
      const btnText = document.getElementById('btnText');
      const spinner = document.getElementById('spinner');
      
      // Show loading
      btnText.style.display = 'none';
      spinner.style.display = 'block';
      submitBtn.disabled = true;
      
      fetch('/save', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password)
      })
      .then(response => response.text())
      .then(data => {
        document.getElementById('successMsg').style.display = 'block';
        document.getElementById('wifiForm').style.display = 'none';
      })
      .catch(error => {
        alert('Error: ' + error);
        btnText.style.display = 'block';
        spinner.style.display = 'none';
        submitBtn.disabled = false;
      });
    });
  </script>
</body>
</html>
)rawliteral";

// Load WiFi credentials from SPIFFS
bool loadWiFiConfig() {
  if (!SPIFFS.exists(WIFI_CONFIG_FILE)) {
    Serial.println("[WiFi] No saved configuration");
    return false;
  }
  
  File file = SPIFFS.open(WIFI_CONFIG_FILE, "r");
  if (!file) {
    Serial.println("[WiFi] Failed to open config file");
    return false;
  }
  
  String ssid = file.readStringUntil('\n');
  String password = file.readStringUntil('\n');
  file.close();
  
  ssid.trim();
  password.trim();
  
  if (ssid.length() > 0) {
    ssid.toCharArray(wifiCreds.ssid, 32);
    password.toCharArray(wifiCreds.password, 64);
    wifiCreds.valid = true;
    
    Serial.println("[WiFi] OK: Configuration loaded");
    Serial.printf("[WiFi]   SSID: %s\n", wifiCreds.ssid);
    return true;
  }
  
  return false;
}

// Save WiFi credentials to SPIFFS
bool saveWiFiConfig(const char* ssid, const char* password) {
  File file = SPIFFS.open(WIFI_CONFIG_FILE, "w");
  if (!file) {
    Serial.println("[WiFi] FAIL: Failed to save config");
    return false;
  }
  
  file.println(ssid);
  file.println(password);
  file.close();
  
  Serial.println("[WiFi] OK: Configuration saved");
  Serial.printf("[WiFi]   SSID: %s\n", ssid);
  return true;
}

// Handle root page
void handleRoot() {
  server.send(200, "text/html", CONFIG_HTML);
}

// Handle save configuration
void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    
    Serial.println("\n[WiFi] Saving new configuration:");
    Serial.printf("[WiFi]   SSID: %s\n", ssid.c_str());
    
    saveWiFiConfig(ssid.c_str(), password.c_str());
    
    server.send(200, "text/plain", "OK");
    
    Serial.println("[WiFi] Restarting in 3 seconds...");
    delay(3000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

// IR Signal API - Get all signals
void handleGetSignals() {
  DynamicJsonDocument doc(8192);
  JsonArray signals = doc.createNestedArray("signals");
  
  for (int i = 0; i < TOTAL_SIGNALS; i++) {
    JsonObject signal = signals.createNestedObject();
    signal["index"] = i;
    signal["id"] = i + 1;  // 1-based ID for display
    signal["name"] = getSignalName(i);
    signal["learned"] = isSignalLearned(i);
    
    if (isSignalLearned(i)) {
      LearnedButton btn = getSignal(i);
      signal["protocol"] = typeToString(btn.protocol);
      signal["value"] = String((unsigned long long)btn.value, HEX);
      signal["bits"] = btn.bits;
    }
  }
  
  doc["total"] = TOTAL_SIGNALS;
  doc["learned"] = countLearnedSignals();
  
  String response;
  serializeJson(doc, response);
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", response);
  
  Serial.printf("[API] Sent %d signals (%d learned)\n", TOTAL_SIGNALS, countLearnedSignals());
}

// IR Signal API - Update signal name
void handleUpdateSignalName() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  int signalIndex = doc["index"];
  const char* newName = doc["name"];
  
  if (signalIndex < 0 || signalIndex >= TOTAL_SIGNALS) {
    server.send(400, "application/json", "{\"error\":\"Invalid signal index\"}");
    return;
  }
  
  if (newName == nullptr || strlen(newName) == 0) {
    server.send(400, "application/json", "{\"error\":\"Name cannot be empty\"}");
    return;
  }
  
  setSignalName(signalIndex, newName);
  saveLearnedDevicesEnhanced();  // Save to EEPROM
  
  DynamicJsonDocument response(256);
  response["success"] = true;
  response["index"] = signalIndex;
  response["name"] = getSignalName(signalIndex);
  
  String responseStr;
  serializeJson(response, responseStr);
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", responseStr);
  
  Serial.printf("[API] Updated signal %d name to: %s\n", signalIndex + 1, newName);
}

// IR Signal API - Send signal
void handleSendSignal() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  int signalIndex = doc["index"];
  
  if (signalIndex < 0 || signalIndex >= TOTAL_SIGNALS) {
    server.send(400, "application/json", "{\"error\":\"Invalid signal index\"}");
    return;
  }
  
  if (!isSignalLearned(signalIndex)) {
    server.send(400, "application/json", "{\"error\":\"Signal not learned\"}");
    return;
  }
  
  bool success = sendSignal(signalIndex);
  
  DynamicJsonDocument response(256);
  response["success"] = success;
  response["index"] = signalIndex;
  response["name"] = getSignalName(signalIndex);
  
  String responseStr;
  serializeJson(response, responseStr);
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(success ? 200 : 500, "application/json", responseStr);
  
  Serial.printf("[API] Sent signal %d: %s\n", signalIndex + 1, getSignalName(signalIndex));
}

// Handle CORS preflight
void handleCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204);
}

// Initialize WiFi Manager
void initWiFiManager() {
  Serial.println("\n========================================");
  Serial.println("  WiFi Manager Initialization");
  Serial.println("========================================");
  
  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("[WiFi] FAIL: SPIFFS mount failed");
    return;
  }
  Serial.println("[WiFi] OK: SPIFFS mounted");
  
  // Load saved configuration
  if (loadWiFiConfig()) {
    // Try to connect
    if (connectToWiFi()) {
      Serial.println("[WiFi] OK: WiFi connected successfully");
      Serial.println("========================================\n");
      return;
    }
  }
  
  // Start configuration portal
  Serial.println("[WiFi] No valid WiFi configuration");
  Serial.println("[WiFi] Starting configuration portal...");
  startConfigPortal();
}

// Start AP mode for configuration
void startConfigPortal() {
  portalActive = true;
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  IPAddress IP = WiFi.softAPIP();
  
  Serial.println("\n========================================");
  Serial.println("  WiFi Configuration Portal Active");
  Serial.println("========================================");
  Serial.println();
  Serial.printf("  SSID: %s\n", AP_SSID);
  Serial.printf("  Password: %s\n", AP_PASSWORD);
  Serial.printf("  IP Address: %s\n", IP.toString().c_str());
  Serial.println();
  Serial.println("========================================");
  Serial.println("  Connection Instructions:");
  Serial.println("========================================");
  Serial.println("  1. Connect to WiFi: ESP32_AC_Remote");
  Serial.println("  2. Open browser");
  Serial.printf("  3. Navigate to: http://%s\n", IP.toString().c_str());
  Serial.println("  4. Enter your WiFi credentials");
  Serial.println("  5. Click Save & Connect");
  Serial.println("========================================\n");
  
  // Setup web server
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  
  // IR Signal API endpoints
  server.on("/api/signals", HTTP_OPTIONS, handleCORS);
  server.on("/api/signals", HTTP_GET, handleGetSignals);
  server.on("/api/signals/update", HTTP_OPTIONS, handleCORS);
  server.on("/api/signals/update", HTTP_POST, handleUpdateSignalName);
  server.on("/api/signals/send", HTTP_OPTIONS, handleCORS);
  server.on("/api/signals/send", HTTP_POST, handleSendSignal);
  
  server.begin();
  
  Serial.println("[WiFi] Web server started");
  Serial.println("[WiFi] API endpoints enabled");
}

// Try to connect to saved WiFi
bool connectToWiFi() {
  if (!wifiCreds.valid) {
    return false;
  }
  
  Serial.printf("[WiFi] Connecting to: %s", wifiCreds.ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiCreds.ssid, wifiCreds.password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] OK: Connected!");
    Serial.printf("[WiFi]   SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("[WiFi]   IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi]   Signal Strength: %d dBm\n", WiFi.RSSI());
    Serial.printf("[WiFi]   MAC Address: %s\n", WiFi.macAddress().c_str());
    
    // Start web server for API endpoints
    server.on("/api/signals", HTTP_OPTIONS, handleCORS);
    server.on("/api/signals", HTTP_GET, handleGetSignals);
    server.on("/api/signals/update", HTTP_OPTIONS, handleCORS);
    server.on("/api/signals/update", HTTP_POST, handleUpdateSignalName);
    server.on("/api/signals/send", HTTP_OPTIONS, handleCORS);
    server.on("/api/signals/send", HTTP_POST, handleSendSignal);
    server.begin();
    Serial.println("[WiFi] API server started");
    Serial.printf("[WiFi]   Accessible at: http://%s/api/signals\n", WiFi.localIP().toString().c_str());
    
    return true;
  }
  
  Serial.println("[WiFi] FAIL: Connection failed");
  Serial.println("[WiFi]   Possible reasons:");
  Serial.println("[WiFi]   - Wrong password");
  Serial.println("[WiFi]   - Network out of range");
  Serial.println("[WiFi]   - Router issues");
  return false;
}

// Handle web server in loop
void handleWiFiManager() {
  // Handle server in both portal mode and normal WiFi mode
  if (portalActive || WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }
}

// Check if WiFi is configured
bool isWiFiConfigured() {
  return wifiCreds.valid;
}

// Get current WiFi status
String getWiFiStatus() {
  if (portalActive) {
    return "Config Portal: " + String(AP_SSID);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    return "Connected: " + WiFi.SSID() + " (" + WiFi.localIP().toString() + ")";
  }
  
  return "Disconnected";
}

// Clear saved WiFi credentials
void clearWiFiConfig() {
  if (SPIFFS.exists(WIFI_CONFIG_FILE)) {
    SPIFFS.remove(WIFI_CONFIG_FILE);
  }
  wifiCreds.valid = false;
  Serial.println("[WiFi] Configuration cleared");
  Serial.println("[WiFi] Please restart device to reconfigure");
}
