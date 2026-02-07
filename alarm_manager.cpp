/*
 * Alarm Manager - Implementation
 */

#include "alarm_manager.h"
#include "speaker_control.h"
#include "button_control.h"
#include "firebase_client.h"
#include <Preferences.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#define ALARM_NAMESPACE "alarms"
#define ALARM_STORAGE_KEY "data"
#define ALARM_STORAGE_MAGIC 0x414C524D  // "ALRM"
#define ALARM_STORAGE_VERSION 1

#define ALARM_SNOOZE_MINUTES 5
#define ALARM_VOLUME 100
#define ALARM_TONE_INTERVAL_MS 700
#define ALARM_TONE_DURATION_MS 350

struct __attribute__((packed)) AlarmRecord {
  uint8_t hour;
  uint8_t minute;
  uint8_t enabled;
  uint8_t days;  // Bitmask: bit 0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat (0x7F = all days)
  char name[ALARM_NAME_LEN];
};

struct __attribute__((packed)) AlarmStorage {
  uint32_t magic;
  uint8_t version;
  uint8_t count;
  uint8_t nextId;
  AlarmRecord alarms[MAX_ALARMS];
};

static Preferences alarmPrefs;
static AlarmStorage alarmStore;
static int64_t lastTriggerMinute[MAX_ALARMS];

static bool alarmRinging = false;
static bool activeIsSnooze = false;
static int activeAlarmIndex = -1;
static char activeAlarmName[ALARM_NAME_LEN];

static char snoozeBaseName[ALARM_NAME_LEN];
static char snoozeAlarmName[ALARM_NAME_LEN];
static uint8_t snoozeCount = 0;
static bool snoozePending = false;
static time_t snoozeTime = 0;

static unsigned long lastToneMs = 0;
static uint8_t toneStep = 0;
static int previousVolume = -1;
static ScreenMode previousScreen = SCREEN_CLOCK;

static bool isTimeValid() {
  time_t now = time(nullptr);
  return now > 1700000000;
}

static void copyAlarmName(char* dest, const char* src) {
  if (!src || !src[0]) {
    dest[0] = '\0';
    return;
  }
  strncpy(dest, src, ALARM_NAME_LEN - 1);
  dest[ALARM_NAME_LEN - 1] = '\0';
}

static void buildDefaultAlarmName(char* dest) {
  uint8_t id = alarmStore.nextId;
  if (id == 0) {
    id = 1;
  }
  snprintf(dest, ALARM_NAME_LEN, "alarm clock %u", id);
  alarmStore.nextId = id + 1;
  if (alarmStore.nextId == 0) {
    alarmStore.nextId = 1;
  }
}

static void buildSnoozeAlarmName(char* dest, const char* baseName, uint8_t count) {
  if (!baseName || !baseName[0]) {
    snprintf(dest, ALARM_NAME_LEN, "alarm clock delay %u", count);
    return;
  }
  snprintf(dest, ALARM_NAME_LEN, "%s delay %u", baseName, count);
}

static void saveAlarmStore() {
  size_t written = alarmPrefs.putBytes(ALARM_STORAGE_KEY, &alarmStore, sizeof(alarmStore));
  if (written != sizeof(alarmStore)) {
    Serial.printf("[Alarm] FAIL: NVS write (%u/%u bytes)\n",
                  (unsigned)written, (unsigned)sizeof(alarmStore));
  }
}

static void resetAlarmStore() {
  memset(&alarmStore, 0, sizeof(alarmStore));
  alarmStore.magic = ALARM_STORAGE_MAGIC;
  alarmStore.version = ALARM_STORAGE_VERSION;
  alarmStore.count = 0;
  alarmStore.nextId = 1;
  saveAlarmStore();
}

static void loadAlarmStore() {
  size_t size = alarmPrefs.getBytesLength(ALARM_STORAGE_KEY);
  if (size != sizeof(alarmStore)) {
    resetAlarmStore();
    return;
  }

  alarmPrefs.getBytes(ALARM_STORAGE_KEY, &alarmStore, sizeof(alarmStore));
  if (alarmStore.magic != ALARM_STORAGE_MAGIC || alarmStore.version != ALARM_STORAGE_VERSION) {
    resetAlarmStore();
  }
}

static void stopActiveAlarmInternal(bool clearSnooze) {
  if (!alarmRinging) {
    return;
  }

  alarmRinging = false;
  activeIsSnooze = false;
  activeAlarmIndex = -1;
  activeAlarmName[0] = '\0';

  stopSpeaker();

  if (previousVolume >= 0) {
    setSpeakerVolume(previousVolume);
    previousVolume = -1;
  }

  if (clearSnooze) {
    snoozePending = false;
    snoozeTime = 0;
    snoozeAlarmName[0] = '\0';
    snoozeBaseName[0] = '\0';
    snoozeCount = 0;
  }

  ScreenMode returnScreen = previousScreen;
  if (returnScreen == SCREEN_ALARM) {
    returnScreen = SCREEN_CLOCK;
  }
  setScreen(returnScreen);
}

static void startAlarm(const char* name, bool isSnooze, int sourceIndex) {
  alarmRinging = true;
  activeIsSnooze = isSnooze;
  activeAlarmIndex = sourceIndex;
  copyAlarmName(activeAlarmName, name);

  if (!isSnooze) {
    copyAlarmName(snoozeBaseName, activeAlarmName);
    snoozeCount = 0;
  }

  previousScreen = getCurrentScreen();
  setScreen(SCREEN_ALARM);

  previousVolume = getSpeakerVolume();
  if (previousVolume < ALARM_VOLUME) {
    setSpeakerVolume(ALARM_VOLUME);
  }

  lastToneMs = 0;
  toneStep = 0;
  Serial.printf("[Alarm] Ringing: %s\n", activeAlarmName);
}

static void updateAlarmTone() {
  unsigned long nowMs = millis();
  if (nowMs - lastToneMs < ALARM_TONE_INTERVAL_MS) {
    return;
  }

  lastToneMs = nowMs;
  int frequency = (toneStep % 2 == 0) ? 1200 : 1600;
  toneStep++;
  playBeep(frequency, ALARM_TONE_DURATION_MS);
}

void initAlarmManager() {
  Serial.println("[Alarm] Initializing alarm manager...");
  alarmPrefs.begin(ALARM_NAMESPACE, false);
  loadAlarmStore();

  for (int i = 0; i < MAX_ALARMS; i++) {
    lastTriggerMinute[i] = -1;
  }

  activeAlarmName[0] = '\0';
  snoozeBaseName[0] = '\0';
  snoozeAlarmName[0] = '\0';
  Serial.printf("[Alarm] Loaded alarms: %u\n", alarmStore.count);
}

void handleAlarmManager() {
  if (alarmRinging) {
    if (getCurrentScreen() != SCREEN_ALARM) {
      setScreen(SCREEN_ALARM);
    }
    updateAlarmTone();
    return;
  }

  if (!isTimeValid()) {
    return;
  }

  time_t now = time(nullptr);
  time_t currentMinute = now / 60;

  if (snoozePending && now >= snoozeTime) {
    snoozePending = false;
    const char* ringName = snoozeAlarmName[0] ? snoozeAlarmName : snoozeBaseName;
    startAlarm(ringName, true, -1);
    return;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) {
    return;
  }

  for (int i = 0; i < alarmStore.count; i++) {
    AlarmRecord& alarm = alarmStore.alarms[i];
    if (!alarm.enabled) {
      continue;
    }
    if (alarm.hour == timeinfo.tm_hour && alarm.minute == timeinfo.tm_min) {
      if (lastTriggerMinute[i] == currentMinute) {
        continue;
      }
      // Check if alarm should ring on this day of week
      uint8_t dayBit = 1 << timeinfo.tm_wday;  // tm_wday: 0=Sun, 1=Mon, etc.
      if ((alarm.days & dayBit) == 0) {
        // Alarm not configured for this day
        continue;
      }
      lastTriggerMinute[i] = currentMinute;
      startAlarm(alarm.name, false, i);
      break;
    }
  }
}

bool isAlarmRinging() {
  return alarmRinging;
}

const char* getActiveAlarmName() {
  return activeAlarmName;
}

bool addAlarm(uint8_t hour, uint8_t minute, const char* name, uint8_t days) {
  if (hour > 23 || minute > 59) {
    Serial.println("[Alarm] FAIL: Invalid time");
    return false;
  }
  if (alarmStore.count >= MAX_ALARMS) {
    Serial.println("[Alarm] FAIL: Alarm list full");
    return false;
  }

  AlarmRecord& alarm = alarmStore.alarms[alarmStore.count];
  alarm.hour = hour;
  alarm.minute = minute;
  alarm.enabled = 1;
  alarm.days = (days == 0) ? 0x7F : days;  // Default to all days if 0

  Serial.printf("[Alarm] addAlarm name param: %s\n", (name && name[0]) ? name : "<default>");
  if (name && name[0]) {
    copyAlarmName(alarm.name, name);
  } else {
    char defaultName[ALARM_NAME_LEN];
    buildDefaultAlarmName(defaultName);
    copyAlarmName(alarm.name, defaultName);
  }

  alarmStore.count++;
  saveAlarmStore();

  Serial.printf("[Alarm] Added: %s at %02u:%02u\n", alarm.name, alarm.hour, alarm.minute);
  playActionTone();
  return true;
}

bool updateAlarm(uint8_t index, uint8_t hour, uint8_t minute, const char* name, uint8_t days) {
  if (index >= alarmStore.count) {
    Serial.println("[Alarm] FAIL: Invalid alarm index");
    return false;
  }
  if (hour > 23 || minute > 59) {
    Serial.println("[Alarm] FAIL: Invalid time");
    return false;
  }

  AlarmRecord& alarm = alarmStore.alarms[index];
  Serial.printf("[Alarm] updateAlarm #%u name param: %s\n",
                (unsigned)(index + 1),
                name ? name : "<keep>");
  Serial.printf("[Alarm] updateAlarm #%u before: %02u:%02u %s\n",
                (unsigned)(index + 1),
                alarm.hour,
                alarm.minute,
                alarm.name);
  alarm.hour = hour;
  alarm.minute = minute;
  alarm.days = (days == 0) ? 0x7F : days;  // Default to all days if 0
  if (name) {
    copyAlarmName(alarm.name, name);
  }

  saveAlarmStore();
  Serial.printf("[Alarm] Updated: %s at %02u:%02u\n", alarm.name, alarm.hour, alarm.minute);
  Serial.printf("[Alarm] updateAlarm #%u after: %02u:%02u %s\n",
                (unsigned)(index + 1),
                alarm.hour,
                alarm.minute,
                alarm.name);
  playActionTone();
  return true;
}

bool setAlarmEnabled(uint8_t index, bool enabled) {
  if (index >= alarmStore.count) {
    Serial.println("[Alarm] FAIL: Invalid alarm index");
    return false;
  }

  AlarmRecord& alarm = alarmStore.alarms[index];
  alarm.enabled = enabled ? 1 : 0;
  
  saveAlarmStore();
  Serial.printf("[Alarm] Alarm #%u %s\n", (unsigned)(index + 1), enabled ? "enabled" : "disabled");
  playActionTone();
  return true;
}

bool deleteAlarm(uint8_t index) {
  if (index >= alarmStore.count) {
    Serial.println("[Alarm] FAIL: Invalid alarm index");
    return false;
  }

  for (int i = index; i < alarmStore.count - 1; i++) {
    alarmStore.alarms[i] = alarmStore.alarms[i + 1];
  }
  alarmStore.count--;
  saveAlarmStore();

  Serial.printf("[Alarm] Deleted alarm %u\n", index + 1);
  playActionTone();
  return true;
}

int getAlarmCount() {
  return alarmStore.count;
}

bool getAlarmInfo(uint8_t index, AlarmInfo* out) {
  if (!out || index >= alarmStore.count) {
    return false;
  }

  AlarmRecord& alarm = alarmStore.alarms[index];
  out->hour = alarm.hour;
  out->minute = alarm.minute;
  out->enabled = alarm.enabled != 0;
  out->days = alarm.days;
  copyAlarmName(out->name, alarm.name);
  return true;
}

void printAlarms() {
  Serial.println("\n[Alarm] ==============================");
  Serial.printf("[Alarm] Total: %u\n", alarmStore.count);
  for (int i = 0; i < alarmStore.count; i++) {
    AlarmRecord& alarm = alarmStore.alarms[i];
    Serial.printf("[Alarm] %u) %02u:%02u %s%s\n",
                  i + 1,
                  alarm.hour,
                  alarm.minute,
                  alarm.enabled ? "ON  " : "OFF ",
                  alarm.name);
  }
  Serial.println("[Alarm] ==============================\n");
}

void snoozeActiveAlarm() {
  if (!alarmRinging) {
    return;
  }
  if (!isTimeValid()) {
    Serial.println("[Alarm] FAIL: Time not synced (cannot snooze)");
    return;
  }

  time_t now = time(nullptr);
  snoozeCount++;
  if (snoozeBaseName[0] == '\0') {
    copyAlarmName(snoozeBaseName, activeAlarmName);
  }
  buildSnoozeAlarmName(snoozeAlarmName, snoozeBaseName, snoozeCount);
  snoozeTime = now + (ALARM_SNOOZE_MINUTES * 60);
  snoozePending = true;

  Serial.printf("[Alarm] Snooze %u min: %s\n", ALARM_SNOOZE_MINUTES, snoozeAlarmName);
  stopActiveAlarmInternal(false);
}

void stopActiveAlarm() {
  if (!alarmRinging) {
    return;
  }
  Serial.println("[Alarm] Stopped");
  stopActiveAlarmInternal(true);
}

void publishAlarmsToFirebase(const char* source) {
  if (!isFirebaseConfigured()) {
    return;
  }

  if (source && strcmp(source, "mqtt") == 0) {
    Serial.println("[Alarm] publishAlarmsToFirebase (mqtt):");
    for (int i = 0; i < alarmStore.count && i < MAX_ALARMS; i++) {
      AlarmRecord& alarm = alarmStore.alarms[i];
      Serial.printf("[Alarm]   %d) %02u:%02u %s%s\n",
                    i + 1,
                    alarm.hour,
                    alarm.minute,
                    alarm.enabled ? "ON  " : "OFF ",
                    alarm.name);
    }
  }

  AlarmInfo snapshot[MAX_ALARMS];
  int count = alarmStore.count;
  if (count > MAX_ALARMS) {
    count = MAX_ALARMS;
  }

  for (int i = 0; i < count; i++) {
    AlarmRecord& alarm = alarmStore.alarms[i];
    snapshot[i].hour = alarm.hour;
    snapshot[i].minute = alarm.minute;
    snapshot[i].enabled = alarm.enabled != 0;
    snapshot[i].days = alarm.days;
    copyAlarmName(snapshot[i].name, alarm.name);
  }

  firebaseWriteAlarms(snapshot, count, source);
}
