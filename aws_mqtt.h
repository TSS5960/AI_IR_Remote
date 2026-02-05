/*
 * AWS IoT MQTT Control - Header
 * Handles connection to AWS IoT Core and MQTT communication
 */

#ifndef AWS_MQTT_H
#define AWS_MQTT_H

#include "config.h"

#if USE_AWS_IOT

#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

// Initialize AWS IoT connection
void initAWS();

// Connect to AWS IoT Core
bool connectAWS();

// Publish AC status to AWS
void publishACStatus(const ACState& state);

// Handle incoming MQTT messages
void messageHandler(String &topic, String &payload);

// MQTT loop (call in main loop)
void handleAWS();

// Check if connected to AWS
bool isAWSConnected();

// Get AWS connection status
String getAWSStatus();

#else

inline void initAWS() {}
inline bool connectAWS() { return false; }
inline void publishACStatus(const ACState&) {}
inline void messageHandler(String &, String &) {}
inline void handleAWS() {}
inline bool isAWSConnected() { return false; }
inline String getAWSStatus() { return "Disabled"; }

#endif // USE_AWS_IOT

#endif // AWS_MQTT_H
