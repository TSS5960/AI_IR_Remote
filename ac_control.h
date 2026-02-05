/*
 * AC Unified Control Layer - Header
 * Single control interface for all control methods
 */

#ifndef AC_CONTROL_H
#define AC_CONTROL_H

#include "config.h"

// Command types
enum ACCommandType {
  CMD_POWER_TOGGLE,
  CMD_POWER_ON,
  CMD_POWER_OFF,
  CMD_TEMP_UP,
  CMD_TEMP_DOWN,
  CMD_SET_TEMP,
  CMD_MODE_CYCLE,
  CMD_SET_MODE,
  CMD_FAN_CYCLE,
  CMD_SET_FAN,
  CMD_SET_ALL
};

// Command structure
struct ACCommand {
  ACCommandType type;
  
  bool hasPowerValue;
  bool powerValue;
  
  bool hasTempValue;
  int tempValue;
  
  bool hasModeValue;
  ACMode modeValue;
  
  bool hasFanValue;
  FanSpeed fanValue;
  
  ACCommand() : type(CMD_POWER_TOGGLE), 
                hasPowerValue(false), powerValue(false),
                hasTempValue(false), tempValue(24),
                hasModeValue(false), modeValue(MODE_COOL),
                hasFanValue(false), fanValue(FAN_AUTO) {}
  
  ACCommand(ACCommandType t) : ACCommand() { type = t; }
};

// Core control function
void executeACCommand(ACCommand cmd);

// Convenience functions
void acPowerToggle();
void acPowerOn();
void acPowerOff();
void acTempUp();
void acTempDown();
void acSetTemp(int temp);
void acModeCycle();
void acSetMode(ACMode mode);
void acFanCycle();
void acSetFan(FanSpeed fan);
void acSetAll(bool power, int temp, ACMode mode, FanSpeed fan);

// State query
ACState getACState();
String getACStateString();

// Brand control
void setBrand(ACBrand brand);

// Utility functions
const char* getBrandName(ACBrand brand);

// Auto dry threshold (humidity)
void setAutoDryThreshold(float threshold);
float getAutoDryThreshold();

// Sleep mode light threshold
void setSleepLightThreshold(float threshold);
float getSleepLightThreshold();

#endif
