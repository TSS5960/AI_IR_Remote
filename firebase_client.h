/*
 * Firebase Realtime Database Client - Header
 * Stores device state and event logs via REST API.
 */

#ifndef FIREBASE_CLIENT_H
#define FIREBASE_CLIENT_H

#include <Arduino.h>
#include "config.h"
#include "firebase_config.h"
#include "sensors.h"

struct AlarmInfo;

// Initialize Firebase client state
void initFirebase();

// Returns true if Firebase settings are present
bool isFirebaseConfigured();

// Returns true if recent writes succeeded
bool isFirebaseConnected();

// Human-readable Firebase status text
String getFirebaseStatus();

// Write current AC state to Firebase
bool firebaseWriteState(const ACState& state);

// Write current AC state with sensors to Firebase
bool firebaseWriteStateWithSensors(const ACState& state, const SensorData& sensors);

// Write alarm list (overwrites)
bool firebaseWriteAlarms(const AlarmInfo* alarms, int count, const char* source);

// Append combined status (AC + sensors) to Firebase history
bool firebaseAppendStatus(const ACState& state, const SensorData& sensors, const char* label);

// Append an event entry to Firebase
bool firebaseAppendEvent(const ACState& state, const char* source);

// Queue a state update for async send
void firebaseQueueState(const ACState& state, const char* source);

// Queue a status update (AC + sensors) for async send
void firebaseQueueStatus(const ACState& state, const SensorData& sensors, const char* source);

// Optional periodic tasks (currently no-op)
void handleFirebase();

#endif // FIREBASE_CLIENT_H
