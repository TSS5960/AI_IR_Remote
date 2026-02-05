/*
 * Alarm Manager - Header
 * Persistent alarms with snooze support
 */

#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <Arduino.h>

#define MAX_ALARMS 5
#define ALARM_NAME_LEN 32

struct AlarmInfo {
  uint8_t hour;
  uint8_t minute;
  bool enabled;
  char name[ALARM_NAME_LEN];
};

void initAlarmManager();
void handleAlarmManager();

bool isAlarmRinging();
const char* getActiveAlarmName();

bool addAlarm(uint8_t hour, uint8_t minute, const char* name);
bool updateAlarm(uint8_t index, uint8_t hour, uint8_t minute, const char* name);
bool deleteAlarm(uint8_t index);
int getAlarmCount();
bool getAlarmInfo(uint8_t index, AlarmInfo* out);
void printAlarms();

void snoozeActiveAlarm();
void stopActiveAlarm();
void publishAlarmsToFirebase(const char* source);

#endif // ALARM_MANAGER_H
