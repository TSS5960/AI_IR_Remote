/*
 * Button Control - Implementation
 */

#include "button_control.h"
#include "display.h"
#include "speaker_control.h"
#include "wifi_manager.h"
#include "ir_learning_enhanced.h"
#include "alarm_manager.h"
#include <WiFi.h>

ScreenMode currentScreen = SCREEN_CLOCK;

struct ButtonState {
  bool lastState;
  unsigned long lastDebounceTime;
  bool pressed;
};

enum JoyDirection {
  JOY_NEUTRAL = 0,
  JOY_LEFT,
  JOY_RIGHT,
  JOY_UP,
  JOY_DOWN
};

static ButtonState joyButton = {HIGH, 0, false};

static int joyCenterX = 2048;
static int joyCenterY = 2048;
static JoyDirection lastJoyDir = JOY_NEUTRAL;
static JoyDirection pendingJoyDir = JOY_NEUTRAL;
static unsigned long lastJoyActionMs = 0;
static unsigned long pendingJoySinceMs = 0;
static unsigned long neutralSinceMs = 0;
static unsigned long lastNetworkClickMs = 0;
static bool joyArmed = true;

static const int JOY_DEADZONE = 500;
static const int JOY_TRIGGER_THRESHOLD = 900;
static const int JOY_AXIS_MARGIN = 120;
static const unsigned long JOY_ACTION_COOLDOWN_MS = 200;
static const unsigned long JOY_STABLE_MS = 80;
static const unsigned long JOY_NEUTRAL_ARM_MS = 150;
static const unsigned long JOY_DOUBLE_CLICK_MS = 500;
static const unsigned long JOY_LOG_INTERVAL_MS = 1000;
static const int JOY_CALIBRATION_SAMPLES = 8;

static const char* joyDirName(JoyDirection dir) {
  switch (dir) {
    case JOY_LEFT:
      return "LEFT";
    case JOY_RIGHT:
      return "RIGHT";
    case JOY_UP:
      return "UP";
    case JOY_DOWN:
      return "DOWN";
    default:
      return "NEUTRAL";
  }
}

static void readJoystickRaw(int* x, int* y) {
  long sumX = 0;
  long sumY = 0;
  const int samples = 3;

  for (int i = 0; i < samples; i++) {
    sumX += analogRead(JOY_X_PIN);
    sumY += analogRead(JOY_Y_PIN);
  }

  *x = (int)(sumX / samples);
  *y = (int)(sumY / samples);
}

static void calibrateJoystick() {
  long sumX = 0;
  long sumY = 0;

  for (int i = 0; i < JOY_CALIBRATION_SAMPLES; i++) {
    sumX += analogRead(JOY_X_PIN);
    sumY += analogRead(JOY_Y_PIN);
    delay(5);
  }

  joyCenterX = (int)(sumX / JOY_CALIBRATION_SAMPLES);
  joyCenterY = (int)(sumY / JOY_CALIBRATION_SAMPLES);
}

static JoyDirection resolveDirectionFromDelta(int dx, int dy) {
  int ax = abs(dx);
  int ay = abs(dy);

  if (ax < JOY_DEADZONE && ay < JOY_DEADZONE) {
    return JOY_NEUTRAL;
  }

  if (ax >= ay + JOY_AXIS_MARGIN) {
    if (ax < JOY_TRIGGER_THRESHOLD) {
      return JOY_NEUTRAL;
    }
    return (dx < 0) ? JOY_LEFT : JOY_RIGHT;
  }

  if (ay >= ax + JOY_AXIS_MARGIN) {
    if (ay < JOY_TRIGGER_THRESHOLD) {
      return JOY_NEUTRAL;
    }
    return (dy < 0) ? JOY_UP : JOY_DOWN;
  }

  return JOY_NEUTRAL;
}

static JoyDirection readJoystickDirection() {
  int x = 0;
  int y = 0;
  readJoystickRaw(&x, &y);
  int dx = x - joyCenterX;
  int dy = y - joyCenterY;

  return resolveDirectionFromDelta(dx, dy);
}

static JoyDirection pollJoystickDirection() {
  JoyDirection dir = readJoystickDirection();
  unsigned long now = millis();

  if (dir == JOY_NEUTRAL) {
    lastJoyDir = JOY_NEUTRAL;
    pendingJoyDir = JOY_NEUTRAL;
    if (neutralSinceMs == 0) {
      neutralSinceMs = now;
    }
    if (!joyArmed && (now - neutralSinceMs) >= JOY_NEUTRAL_ARM_MS) {
      joyArmed = true;
    }
    return JOY_NEUTRAL;
  }

  neutralSinceMs = 0;

  if (!joyArmed) {
    return JOY_NEUTRAL;
  }

  if (dir != pendingJoyDir) {
    pendingJoyDir = dir;
    pendingJoySinceMs = now;
    return JOY_NEUTRAL;
  }

  if (now - pendingJoySinceMs < JOY_STABLE_MS) {
    return JOY_NEUTRAL;
  }

  if (now - lastJoyActionMs < JOY_ACTION_COOLDOWN_MS) {
    return JOY_NEUTRAL;
  }

  lastJoyDir = dir;
  lastJoyActionMs = now;
  joyArmed = false;
  return dir;
}

static bool checkJoystickClick() {
  bool reading = digitalRead(JOY_SW_PIN);

  if (reading != joyButton.lastState) {
    joyButton.lastDebounceTime = millis();
    joyButton.lastState = reading;
  }

  if ((millis() - joyButton.lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading == LOW && !joyButton.pressed) {
      joyButton.pressed = true;
    }

    if (reading == HIGH && joyButton.pressed) {
      joyButton.pressed = false;
      return true;
    }
  }

  return false;
}

void initButtons() {
  Serial.println("\n========================================");
  Serial.println("  JOYSTICK INITIALIZATION START");
  Serial.println("========================================");

  analogReadResolution(12);
  pinMode(JOY_X_PIN, INPUT);
  pinMode(JOY_Y_PIN, INPUT);
  pinMode(JOY_SW_PIN, INPUT_PULLUP);

  // Serial.println("[Joystick] OK: Pins configured");
  // Serial.printf("[Joystick]   VRx: GPIO%d\n", JOY_X_PIN);
  // Serial.printf("[Joystick]   VRy: GPIO%d\n", JOY_Y_PIN);
  // Serial.printf("[Joystick]   SW : GPIO%d\n", JOY_SW_PIN);

  delay(100);
  calibrateJoystick();

  neutralSinceMs = millis();
  pendingJoyDir = JOY_NEUTRAL;
  joyArmed = true;
  lastJoyDir = JOY_NEUTRAL;

  // Serial.printf("[Joystick] Center calibrated: X=%d Y=%d\n", joyCenterX, joyCenterY);
  // Serial.printf("[Joystick] Deadzone=%d Trigger=%d Margin=%d\n",
  //               JOY_DEADZONE,
  //               JOY_TRIGGER_THRESHOLD,
  //               JOY_AXIS_MARGIN);
  // Serial.printf("[Joystick] SW initial state: %d (should be 1 when not pressed)\n",
  //               digitalRead(JOY_SW_PIN));

  // Serial.println("\n========================================");
  // Serial.println("  JOYSTICK INITIALIZATION COMPLETE");
  // Serial.println("========================================\n");
}

static void handleScreenSwitch(JoyDirection dir) {
  if (dir != JOY_LEFT && dir != JOY_RIGHT) {
    return;
  }
  if (currentScreen >= SCREEN_COUNT) {
    return;
  }

  ScreenMode oldScreen = currentScreen;

  if (dir == JOY_RIGHT) {
    currentScreen = (ScreenMode)((currentScreen + 1) % SCREEN_COUNT);
  } else {
    currentScreen = (ScreenMode)((currentScreen + SCREEN_COUNT - 1) % SCREEN_COUNT);
  }

  // Serial.println("========================================");
  // Serial.println("[Joystick] Screen switch detected!");
  // Serial.printf("[Joystick] Old screen: %d\n", oldScreen);
  // Serial.printf("[Joystick] New screen: %d\n", currentScreen);

  // const char* screenNames[] = {"VOLUME", "CLOCK", "NETWORK", "AC", "IR_LEARN", "SENSORS"};
  // Serial.printf("[Joystick] Switching: %s -> %s\n",
  //               screenNames[oldScreen], screenNames[currentScreen]);

  playBeep(1000, 50);
  delay(30);
  playBeep(1200, 50);

  updateScreenDisplay();
  // Serial.println("[Joystick] Screen updated!");
  // Serial.println("========================================");
}

static void adjustVolume(int delta) {
  int currentVol = getSpeakerVolume();
  int newVol = currentVol + delta;

  if (newVol > 100) {
    newVol = 100;
  }
  if (newVol < 0) {
    newVol = 0;
  }

  setSpeakerVolume(newVol);
  // Serial.printf("[Joystick] Volume: %d%% -> %d%%\n", currentVol, newVol);
  playBeep(delta > 0 ? 1200 : 1000, 100);
  updateScreenDisplay();
}

static void handleSignalStep(int step) {
  LearnState state = getLearnState();
  if (state != LEARN_IDLE && state != LEARN_SAVED) {
    return;
  }

  int currentSignal = getCurrentSignal();
  int nextSignal = currentSignal + step;
  
  // Wrap around: 0-39
  if (nextSignal < 0) {
    nextSignal = TOTAL_SIGNALS - 1;  // 39
  } else if (nextSignal >= TOTAL_SIGNALS) {
    nextSignal = 0;
  }
  
  if (nextSignal == currentSignal) {
    return;
  }

  setCurrentSignal(nextSignal);
  Serial.printf("[Joystick] IR signal: %d -> %d (of %d)\n", currentSignal + 1, nextSignal + 1, TOTAL_SIGNALS);
  playBeep(1000, 50);
  updateScreenDisplay();
}

static void handleJoystickUp() {
  if (currentScreen == SCREEN_VOLUME) {
    adjustVolume(5);
  } else if (currentScreen == SCREEN_IR_LEARN) {
    handleSignalStep(1);
  }
}

static void handleJoystickDown() {
  if (currentScreen == SCREEN_VOLUME) {
    adjustVolume(-5);
  } else if (currentScreen == SCREEN_IR_LEARN) {
    handleSignalStep(-1);
  }
}

static void handleJoystickClick() {
  if (currentScreen == SCREEN_NETWORK) {
    unsigned long now = millis();
    if (now - lastNetworkClickMs <= JOY_DOUBLE_CLICK_MS) {
      lastNetworkClickMs = 0;
      Serial.println("[Joystick] WiFi reset requested (double click)");
      playBeep(800, 100);
      delay(100);
      playBeep(600, 100);

      clearWiFiConfig();
      updateScreenDisplay();

      delay(1000);
      Serial.println("[Joystick] Restarting device to enter WiFi config mode...");
      Serial.println("[Joystick] Connect to WiFi: ESP32_AC_Remote");
      Serial.println("[Joystick] Password: 12345678");
      delay(1000);
      ESP.restart();
    } else {
      lastNetworkClickMs = now;
    }
    return;
  }

  if (currentScreen == SCREEN_IR_LEARN) {
    LearnState state = getLearnState();
    if (state == LEARN_IDLE || state == LEARN_ERROR) {
      int currentSignal = getCurrentSignal();
      Serial.printf("[Joystick] Click: Start learning signal %d/%d\n", currentSignal + 1, TOTAL_SIGNALS);
      startLearningSignal(currentSignal);
      playBeep(800, 100);
      delay(50);
      playBeep(1000, 100);
      updateScreenDisplay();
    } else if (state == LEARN_SAVED) {
      // Signal was just learned and saved - advance to next
      Serial.println("[Joystick] Click: Advancing to next signal");
      int nextSignal = getCurrentSignal() + 1;
      if (nextSignal < TOTAL_SIGNALS) {
        setCurrentSignal(nextSignal);
        Serial.printf("[Joystick] Advanced to signal %d/%d\n", nextSignal + 1, TOTAL_SIGNALS);
        playBeep(1200, 100);
        delay(50);
        playBeep(1400, 100);
      } else {
        Serial.println("[Joystick] All signals learned!");
        playBeep(1400, 200);
      }
      // Reset state so we can learn the next signal
      resetLearningState();
      updateScreenDisplay();
    }
  }
}

static bool handleAlarmInputs(JoyDirection dirEvent, bool clickEvent) {
  if (!isAlarmRinging()) {
    return false;
  }

  if (dirEvent == JOY_UP || dirEvent == JOY_DOWN) {
    snoozeActiveAlarm();
  }
  if (clickEvent) {
    stopActiveAlarm();
  }

  return true;
}

void handleButtons() {
  static unsigned long lastJoyLogMs = 0;

  JoyDirection dirEvent = pollJoystickDirection();
  bool clickEvent = checkJoystickClick();
  unsigned long now = millis();

  if (now - lastJoyLogMs > JOY_LOG_INTERVAL_MS) {
    int rawX = 0;
    int rawY = 0;
    readJoystickRaw(&rawX, &rawY);
    int dx = rawX - joyCenterX;
    int dy = rawY - joyCenterY;
    JoyDirection rawDir = resolveDirectionFromDelta(dx, dy);
    // Serial.printf("[Joystick] Raw X=%d Y=%d dX=%d dY=%d SW=%d dir=%s screen=%d\n",
    //               rawX,
    //               rawY,
    //               dx,
    //               dy,
    //               digitalRead(JOY_SW_PIN),
    //               joyDirName(rawDir),
    //               currentScreen);
    lastJoyLogMs = now;
  }

  if (dirEvent != JOY_NEUTRAL) {
    // Serial.printf("[Joystick] Direction event: %s\n", joyDirName(dirEvent));
  }
  if (clickEvent) {
    // Serial.println("[Joystick] Click event");
  }

  if (handleAlarmInputs(dirEvent, clickEvent)) {
    return;
  }

  if (currentScreen != SCREEN_NETWORK) {
    lastNetworkClickMs = 0;
  }

  if (dirEvent == JOY_LEFT || dirEvent == JOY_RIGHT) {
    handleScreenSwitch(dirEvent);
  } else if (dirEvent == JOY_UP) {
    handleJoystickUp();
  } else if (dirEvent == JOY_DOWN) {
    handleJoystickDown();
  }

  if (clickEvent) {
    handleJoystickClick();
  }

}

ScreenMode getCurrentScreen() {
  return currentScreen;
}

void setScreen(ScreenMode mode) {
  Serial.printf("[Button Control] setScreen() called with mode: %d\n", mode);
  if (mode <= SCREEN_ALARM) {
    currentScreen = mode;
    Serial.printf("[Button Control] currentScreen set to: %d\n", currentScreen);
    updateScreenDisplay();
  } else {
    Serial.printf("[Button Control] ERROR: Invalid screen mode %d\n", mode);
  }
}
