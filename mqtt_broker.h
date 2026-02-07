/*
 * MQTT Broker Control - Header
 * Handles connection to a standard MQTT broker (EMQX by default)
 */

#ifndef MQTT_BROKER_H
#define MQTT_BROKER_H

#include "config.h"

#if USE_MQTT_BROKER

#include <WiFi.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>

// Initialize MQTT broker connection
void initMqttBroker();

// Connect to MQTT broker
bool connectMqttBroker();

// Publish AC status to MQTT broker
void publishMqttStatus(const ACState& state);

// Publish IR signals to Firebase (for web dashboard)
void publishIRSignalsToFirebase();

// Handle incoming MQTT messages
void mqttMessageHandler(String &topic, String &payload);

// MQTT loop (call in main loop)
void handleMqttBroker();

// Check if connected to MQTT broker
bool isMqttConnected();

// Get MQTT connection status
String getMqttStatus();

#else

inline void initMqttBroker() {}
inline bool connectMqttBroker() { return false; }
inline void publishMqttStatus(const ACState&) {}
inline void publishIRSignalsToFirebase() {}
inline void mqttMessageHandler(String &, String &) {}
inline void handleMqttBroker() {}
inline bool isMqttConnected() { return false; }
inline String getMqttStatus() { return "Disabled"; }

#endif // USE_MQTT_BROKER

#endif // MQTT_BROKER_H
