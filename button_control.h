/*
 * Button Control - Header
 * Joystick control system
 */

#ifndef BUTTON_CONTROL_H
#define BUTTON_CONTROL_H

#include <Arduino.h>
#include "config.h"

// Joystick pin definitions (QYF-860)
#define JOY_X_PIN   1   // VRx
#define JOY_Y_PIN   2   // VRy
#define JOY_SW_PIN  18  // SW (click)

#define DEBOUNCE_DELAY 50

enum ScreenMode {
  SCREEN_VOLUME = 0,
  SCREEN_CLOCK = 1,
  SCREEN_NETWORK = 2,
  SCREEN_AC = 3,
  SCREEN_IR_LEARN = 4,
  SCREEN_SENSORS = 5,
  SCREEN_ALARM = 6,
  SCREEN_COUNT = 6  // Normal screen count (exclude alarm)
};

void initButtons();

void handleButtons();

ScreenMode getCurrentScreen();

void setScreen(ScreenMode mode);

#endif // BUTTON_CONTROL_H
