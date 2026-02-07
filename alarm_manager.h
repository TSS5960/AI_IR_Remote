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
  uint8_t days;  // Bitmask: bit 0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat (0x7F = all days)
  char name[ALARM_NAME_LEN];
};

void initAlarmManager();
void handleAlarmManager();

bool isAlarmRinging();
const char* getActiveAlarmName();

bool addAlarm(uint8_t hour, uint8_t minute, const char* name, uint8_t days = 0x7F);
bool updateAlarm(uint8_t index, uint8_t hour, uint8_t minute, const char* name, uint8_t days = 0x7F);
bool setAlarmEnabled(uint8_t index, bool enabled);
bool deleteAlarm(uint8_t index);
int getAlarmCount();
bool getAlarmInfo(uint8_t index, AlarmInfo* out);
void printAlarms();

void snoozeActiveAlarm();
void stopActiveAlarm();
void publishAlarmsToFirebase(const char* source);

#endif // ALARM_MANAGER_H
