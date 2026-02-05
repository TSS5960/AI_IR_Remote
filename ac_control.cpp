/*
 * AC Unified Control Layer - Implementation
 */

#include "ac_control.h"
#include "ir_control.h"
#include "display.h"
#include "speaker_control.h"
#include "firebase_client.h"
#include "mqtt_broker.h"

extern ACState acState;
static float autoDryThreshold = AUTO_DRY_THRESHOLD_DEFAULT;
static float sleepLightThreshold = SLEEP_LIGHT_THRESHOLD_DEFAULT;

void executeACCommand(ACCommand cmd) {
  bool stateChanged = false;
  VoiceFeedback voiceFeedback = VOICE_READY;
  bool playVoiceNow = false;
  
  Serial.print("\n[Control] Executing: ");
  
  switch(cmd.type) {
    case CMD_POWER_TOGGLE:
      Serial.println("Power toggle");
      acState.power = !acState.power;
      Serial.printf("       -> AC: %s\n", acState.power ? "ON" : "OFF");
      voiceFeedback = acState.power ? VOICE_POWER_ON : VOICE_POWER_OFF;
      playVoiceNow = true;
      stateChanged = true;
      break;
      
    case CMD_POWER_ON:
      Serial.println("Power on");
      if (!acState.power) {
        acState.power = true;
        Serial.println("       -> AC powered on");
        voiceFeedback = VOICE_POWER_ON;
        playVoiceNow = true;
        stateChanged = true;
      } else {
        Serial.println("       -> Already ON");
      }
      break;
      
    case CMD_POWER_OFF:
      Serial.println("Power off");
      if (acState.power) {
        acState.power = false;
        Serial.println("       -> AC powered off");
        voiceFeedback = VOICE_POWER_OFF;
        playVoiceNow = true;
        stateChanged = true;
      } else {
        Serial.println("       -> Already OFF");
      }
      break;
      
    case CMD_TEMP_UP:
      Serial.println("Temperature +1");
      if (acState.power && acState.temperature < AC_TEMP_MAX) {
        acState.temperature++;
        Serial.printf("       -> Temperature: %dC\n", acState.temperature);
        voiceFeedback = VOICE_TEMP_UP;
        playVoiceNow = true;
        stateChanged = true;
      } else if (!acState.power) {
        Serial.println("       WARN: Please turn on first!");
      } else {
        Serial.printf("       WARN: Max temperature (%dC)\n", AC_TEMP_MAX);
      }
      break;
      
    case CMD_TEMP_DOWN:
      Serial.println("Temperature -1");
      if (acState.power && acState.temperature > AC_TEMP_MIN) {
        acState.temperature--;
        Serial.printf("       -> Temperature: %dC\n", acState.temperature);
        voiceFeedback = VOICE_TEMP_DOWN;
        playVoiceNow = true;
        stateChanged = true;
      } else if (!acState.power) {
        Serial.println("       WARN: Please turn on first!");
      } else {
        Serial.printf("       WARN: Min temperature (%dC)\n", AC_TEMP_MIN);
      }
      break;
      
    case CMD_SET_TEMP:
      Serial.printf("Set temperature: %dC\n", cmd.tempValue);
      if (acState.power && cmd.hasTempValue) {
        acState.temperature = constrain(cmd.tempValue, AC_TEMP_MIN, AC_TEMP_MAX);
        Serial.printf("       -> Temperature set to: %dC\n", acState.temperature);
        playTemperature(acState.temperature);
        stateChanged = true;
      } else if (!acState.power) {
        Serial.println("       WARN: Please turn on first!");
      }
      break;
      
    case CMD_MODE_CYCLE:
      Serial.println("Mode cycle");
      if (acState.power) {
        acState.mode = (ACMode)((acState.mode + 1) % 5);
        const char* modes[] = {"Auto", "Cool", "Heat", "Dry", "Fan"};
        Serial.printf("       -> Mode: %s\n", modes[acState.mode]);
        
        VoiceFeedback modeVoices[] = {VOICE_MODE_AUTO, VOICE_MODE_COOL, 
                                      VOICE_MODE_HEAT, VOICE_MODE_DRY, VOICE_MODE_FAN};
        voiceFeedback = modeVoices[acState.mode];
        playVoiceNow = true;
        stateChanged = true;
      } else {
        Serial.println("       WARN: Please turn on first!");
      }
      break;
      
    case CMD_SET_MODE:
      Serial.println("Set mode");
      if (acState.power && cmd.hasModeValue) {
        acState.mode = cmd.modeValue;
        const char* modes[] = {"Auto", "Cool", "Heat", "Dry", "Fan"};
        Serial.printf("       -> Mode: %s\n", modes[acState.mode]);
        
        VoiceFeedback modeVoices[] = {VOICE_MODE_AUTO, VOICE_MODE_COOL, 
                                      VOICE_MODE_HEAT, VOICE_MODE_DRY, VOICE_MODE_FAN};
        voiceFeedback = modeVoices[acState.mode];
        playVoiceNow = true;
        stateChanged = true;
      } else if (!acState.power) {
        Serial.println("       WARN: Please turn on first!");
      }
      break;
      
    case CMD_FAN_CYCLE:
      Serial.println("Fan speed cycle");
      if (acState.power) {
        acState.fanSpeed = (FanSpeed)((acState.fanSpeed + 1) % 4);
        const char* speeds[] = {"Auto", "Low", "Med", "High"};
        Serial.printf("       -> Fan: %s\n", speeds[acState.fanSpeed]);
        
        VoiceFeedback fanVoices[] = {VOICE_FAN_AUTO, VOICE_FAN_LOW, 
                                     VOICE_FAN_MED, VOICE_FAN_HIGH};
        voiceFeedback = fanVoices[acState.fanSpeed];
        playVoiceNow = true;
        stateChanged = true;
      } else {
        Serial.println("       WARN: Please turn on first!");
      }
      break;
      
    case CMD_SET_FAN:
      Serial.println("Set fan speed");
      if (acState.power && cmd.hasFanValue) {
        acState.fanSpeed = cmd.fanValue;
        const char* speeds[] = {"Auto", "Low", "Med", "High"};
        Serial.printf("       -> Fan: %s\n", speeds[acState.fanSpeed]);
        
        VoiceFeedback fanVoices[] = {VOICE_FAN_AUTO, VOICE_FAN_LOW, 
                                     VOICE_FAN_MED, VOICE_FAN_HIGH};
        voiceFeedback = fanVoices[acState.fanSpeed];
        playVoiceNow = true;
        stateChanged = true;
      } else if (!acState.power) {
        Serial.println("       WARN: Please turn on first!");
      }
      break;
      
    case CMD_SET_ALL:
      Serial.println("Set all parameters");
      if (cmd.hasPowerValue) {
        acState.power = cmd.powerValue;
        Serial.printf("       -> Power: %s\n", acState.power ? "ON" : "OFF");
      }
      if (cmd.hasTempValue) {
        acState.temperature = constrain(cmd.tempValue, AC_TEMP_MIN, AC_TEMP_MAX);
        Serial.printf("       -> Temperature: %dC\n", acState.temperature);
      }
      if (cmd.hasModeValue) {
        acState.mode = cmd.modeValue;
        const char* modes[] = {"Auto", "Cool", "Heat", "Dry", "Fan"};
        Serial.printf("       -> Mode: %s\n", modes[acState.mode]);
      }
      if (cmd.hasFanValue) {
        acState.fanSpeed = cmd.fanValue;
        const char* speeds[] = {"Auto", "Low", "Med", "High"};
        Serial.printf("       -> Fan: %s\n", speeds[acState.fanSpeed]);
      }
      stateChanged = true;
      break;
  }
  
  if (stateChanged) {
    Serial.println("[Control] State updated, performing actions...");
    
    // Send IR signal
    sendACState(acState);
    
    // Update display
    updateDisplay(acState);
    
    // Play voice feedback
    if (playVoiceNow) {
      playVoice(voiceFeedback);
    }
    
    // Store to Firebase
    if (isFirebaseConfigured()) {
      firebaseQueueState(acState, "state_update");
    }

    publishMqttStatus(acState);
    
    Serial.println("[Control] Complete\n");
  }
}

void acPowerToggle() { ACCommand cmd(CMD_POWER_TOGGLE); executeACCommand(cmd); }
void acPowerOn() { ACCommand cmd(CMD_POWER_ON); executeACCommand(cmd); }
void acPowerOff() { ACCommand cmd(CMD_POWER_OFF); executeACCommand(cmd); }
void acTempUp() { ACCommand cmd(CMD_TEMP_UP); executeACCommand(cmd); }
void acTempDown() { ACCommand cmd(CMD_TEMP_DOWN); executeACCommand(cmd); }

void acSetTemp(int temp) {
  ACCommand cmd(CMD_SET_TEMP);
  cmd.hasTempValue = true;
  cmd.tempValue = temp;
  executeACCommand(cmd);
}

void acModeCycle() { ACCommand cmd(CMD_MODE_CYCLE); executeACCommand(cmd); }

void acSetMode(ACMode mode) {
  ACCommand cmd(CMD_SET_MODE);
  cmd.hasModeValue = true;
  cmd.modeValue = mode;
  executeACCommand(cmd);
}

void acFanCycle() { ACCommand cmd(CMD_FAN_CYCLE); executeACCommand(cmd); }

void acSetFan(FanSpeed fan) {
  ACCommand cmd(CMD_SET_FAN);
  cmd.hasFanValue = true;
  cmd.fanValue = fan;
  executeACCommand(cmd);
}

void acSetAll(bool power, int temp, ACMode mode, FanSpeed fan) {
  ACCommand cmd(CMD_SET_ALL);
  cmd.hasPowerValue = true; cmd.powerValue = power;
  cmd.hasTempValue = true; cmd.tempValue = temp;
  cmd.hasModeValue = true; cmd.modeValue = mode;
  cmd.hasFanValue = true; cmd.fanValue = fan;
  executeACCommand(cmd);
}

ACState getACState() { return acState; }

void setBrand(ACBrand brand) {
  acState.brand = brand;
  Serial.printf("[AC] Brand set to: %s\n", getBrandName(brand));
  updateDisplay(acState);
  playBeep(1200, 100);
  if (isFirebaseConfigured()) {
    firebaseQueueState(acState, "brand_update");
  }
  publishMqttStatus(acState);
}

void setAutoDryThreshold(float threshold) {
  if (threshold <= 0) {
    autoDryThreshold = 0;
    Serial.println("[AutoDry] Disabled (threshold <= 0)");
    return;
  }

  float clamped = threshold;
  if (clamped < AUTO_DRY_THRESHOLD_MIN) {
    clamped = AUTO_DRY_THRESHOLD_MIN;
  }
  if (clamped > AUTO_DRY_THRESHOLD_MAX) {
    clamped = AUTO_DRY_THRESHOLD_MAX;
  }
  autoDryThreshold = clamped;
  Serial.printf("[AutoDry] Threshold set: %.1f%%\n", autoDryThreshold);
}

float getAutoDryThreshold() {
  return autoDryThreshold;
}

void setSleepLightThreshold(float threshold) {
  if (threshold <= 0) {
    sleepLightThreshold = 0;
    Serial.println("[SleepMode] Disabled (threshold <= 0)");
    return;
  }

  float clamped = threshold;
  if (clamped < SLEEP_LIGHT_THRESHOLD_MIN) {
    clamped = SLEEP_LIGHT_THRESHOLD_MIN;
  }
  if (clamped > SLEEP_LIGHT_THRESHOLD_MAX) {
    clamped = SLEEP_LIGHT_THRESHOLD_MAX;
  }
  sleepLightThreshold = clamped;
  Serial.printf("[SleepMode] Light threshold set: %.1f lx\n", sleepLightThreshold);
}

float getSleepLightThreshold() {
  return sleepLightThreshold;
}

String getACStateString() {
  String state = "AC State: ";
  state += acState.power ? "ON" : "OFF";
  if (acState.power) {
    state += " | Temp: " + String(acState.temperature) + "C";
    const char* modes[] = {"Auto", "Cool", "Heat", "Dry", "Fan"};
    state += " | Mode: " + String(modes[acState.mode]);
    const char* speeds[] = {"Auto", "Low", "Med", "High"};
    state += " | Fan: " + String(speeds[acState.fanSpeed]);
  }
  return state;
}

// Get brand name string
const char* getBrandName(ACBrand brand) {
  static const char* brandNames[] = {
    "Daikin",
    "Mitsubishi",
    "Panasonic",
    "Gree",
    "Midea",
    "Haier",
    "Samsung",
    "LG",
    "Fujitsu",
    "Hitachi"
  };
  
  if (brand < BRAND_COUNT) {
    return brandNames[brand];
  }
  return "Unknown";
}
