/*
 * WiFi Manager - Captive Portal Configuration
 * Provides web-based WiFi credential setup
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>

// WiFi credentials storage
struct WiFiCredentials {
  char ssid[32];
  char password[64];
  bool valid;
};

// Initialize WiFi Manager
void initWiFiManager();

// Start AP mode for configuration
void startConfigPortal();

// Try to connect to saved WiFi
bool connectToWiFi();

// Check if WiFi is configured
bool isWiFiConfigured();

// Get current WiFi status
String getWiFiStatus();

// Clear saved WiFi credentials
void clearWiFiConfig();

// Handle web server in loop
void handleWiFiManager();

#endif // WIFI_MANAGER_H
