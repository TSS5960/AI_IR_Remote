/*
 * Enhanced IR Learning Module - Implementation
 * Key improvements: frequency detection, signal validation, multi-button support
 */

#include "ir_learning_enhanced.h"
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <IRsend.h>  // Required for IRsend class

#define EEPROM_SIZE 4096
#define EEPROM_MAGIC 0xABCE  // Different magic for enhanced version
#define EEPROM_VERSION 2
#define EEPROM_START_ADDR 100

extern IRrecv irrecv;
extern IRsend irsend;

// === Global State ===
LearnedDevice learnedDevices[MAX_LEARNED_DEVICES];
LearningSession session;

int currentSignalIndex = 0;  // Current signal (0-39)

// === Helper Functions: Convert between flat index and device/button ===

void signalToDeviceButton(int signalIndex, int* device, int* button) {
  if (signalIndex >= 0 && signalIndex < TOTAL_SIGNALS) {
    *device = signalIndex / MAX_BUTTONS_PER_DEVICE;
    *button = signalIndex % MAX_BUTTONS_PER_DEVICE;
  } else {
    *device = 0;
    *button = 0;
  }
}

int deviceButtonToSignal(int device, int button) {
  return device * MAX_BUTTONS_PER_DEVICE + button;
}

// === Initialization ===

void initIRLearningEnhanced() {
  Serial.println("[IR Learn+] Initializing ENHANCED IR learning module...");
  
  EEPROM.begin(EEPROM_SIZE);
  
  // Initialize all devices
  for (int i = 0; i < MAX_LEARNED_DEVICES; i++) {
    learnedDevices[i].hasData = false;
    learnedDevices[i].buttonCount = 0;
    learnedDevices[i].detectedCarrierFreq = 38.0;  // Default assumption
    learnedDevices[i].preferredRepeatCount = 3;
    snprintf(learnedDevices[i].deviceName, sizeof(learnedDevices[i].deviceName), 
             "Device %d", i + 1);
    
    for (int j = 0; j < MAX_BUTTONS_PER_DEVICE; j++) {
      learnedDevices[i].buttons[j].hasData = false;
    }
  }
  
  // Initialize session
  session.state = LEARN_IDLE;
  session.currentDeviceIndex = 0;
  session.currentButtonIndex = 0;
  session.captureCount = 0;
  session.timeout = 30000;  // 30 second timeout
  
  currentSignalIndex = 0;  // Start at signal 0
  
  loadLearnedDevicesEnhanced();
  
  Serial.println("[IR Learn+] ✓ Enhanced IR learning ready");
  Serial.println("[IR Learn+]   Mode: Flat 40-signal approach");
  Serial.printf("[IR Learn+]   Hardware frequency: %d kHz (cannot be changed)\n", IR_HARDWARE_FREQUENCY);
}

// === Signal Analysis Functions ===

/**
 * Detect carrier frequency from original remote (for informational purposes only)
 * Note: This is the frequency the ORIGINAL remote used.
 * We will still transmit at IR_HARDWARE_FREQUENCY due to hardware limitations.
 */
float detectCarrierFrequency(const decode_results& results) {
  // Analyze protocol type for frequency hints
  // (Raw timing analysis unreliable with most receivers)
  switch (results.decode_type) {
    case SONY:
      return 40.0;  // Sony uses 40kHz
    case RC5:
    case RC6:
      return 36.0;  // Philips RC5/RC6 use 36kHz
    case SAMSUNG:
    case NEC:
    case LG:
    case PANASONIC:
      return 38.0;  // Most common protocols use 38kHz
    default:
      return 38.0;  // Assume 38kHz for unknown protocols
  }
}

/**
 * Analyze signal quality - returns 0-100 score
 */
uint8_t analyzeSignalQuality(const decode_results& results) {
  uint8_t quality = 100;
  
  // Check 1: Minimum length
  if (results.rawlen < MIN_SIGNAL_LENGTH) {
    Serial.printf("[IR Learn+] ⚠ Signal too short: %d samples\n", results.rawlen);
    quality -= 30;
  }
  
  // Check 2: Protocol decoded successfully
  if (results.decode_type == UNKNOWN) {
    Serial.println("[IR Learn+] ⚠ Unknown protocol (not necessarily bad)");
    quality -= 10;
  }
  
  // Check 3: Timing consistency
  // Count irregular timings (outliers)
  if (results.rawlen > 10) {
    int irregularCount = 0;
    
    for (uint16_t i = 2; i < results.rawlen - 2; i++) {
      uint16_t current = results.rawbuf[i] * kRawTick;
      uint16_t prev = results.rawbuf[i-1] * kRawTick;
      uint16_t next = results.rawbuf[i+1] * kRawTick;
      
      // Check if this timing is significantly different from neighbors
      if (current > 100) {  // Ignore very short pulses
        float diffPrev = abs((int)current - (int)prev) / (float)current;
        float diffNext = abs((int)current - (int)next) / (float)current;
        
        if (diffPrev > 0.5 && diffNext > 0.5) {
          irregularCount++;
        }
      }
    }
    
    float noiseRatio = (float)irregularCount / results.rawlen;
    if (noiseRatio > MAX_NOISE_RATIO) {
      Serial.printf("[IR Learn+] ⚠ High noise ratio: %.1f%%\n", noiseRatio * 100);
      quality -= 20;
    }
  }
  
  // Check 4: Value is non-zero (for decoded protocols)
  if (results.decode_type != UNKNOWN && results.value == 0) {
    Serial.println("[IR Learn+] ⚠ Decoded value is 0");
    quality -= 15;
  }
  
  return max(0, (int)quality);
}

/**
 * Check if signal is a repeat code
 */
bool isRepeatSignal(const decode_results& results) {
  // NEC repeat code is very short
  if (results.decode_type == NEC && results.rawlen < 10) {
    return true;
  }
  
  // Check for repeat value
  if (results.value == 0xFFFFFFFF || results.value == 0xFFFFFFFFFFFFFFFF) {
    return true;
  }
  
  return false;
}

/**
 * Check if two timings match within tolerance
 */
bool timingsMatch(uint16_t t1, uint16_t t2, float tolerance) {
  if (t1 == 0 || t2 == 0) return false;
  
  float diff = abs((int)t1 - (int)t2);
  float avg = (t1 + t2) / 2.0;
  
  return (diff / avg) <= tolerance;
}

/**
 * Validate that multiple captures are consistent
 */
bool validateSignalConsistency(const decode_results captures[], uint8_t count) {
  if (count < 2) return true;
  
  const decode_results& first = captures[0];
  
  for (uint8_t i = 1; i < count; i++) {
    const decode_results& current = captures[i];
    
    // Check protocol match
    if (current.decode_type != first.decode_type) {
      Serial.printf("[IR Learn+] ⚠ Protocol mismatch: %d vs %d\n", 
                    first.decode_type, current.decode_type);
      return false;
    }
    
    // Check value match (for decoded protocols)
    if (first.decode_type != UNKNOWN) {
      if (current.value != first.value) {
        Serial.printf("[IR Learn+] ⚠ Value mismatch: 0x%llX vs 0x%llX\n",
                      first.value, current.value);
        return false;
      }
    }
    
    // Check length similarity (within 10%)
    int lenDiff = abs((int)current.rawlen - (int)first.rawlen);
    float lenRatio = (float)lenDiff / first.rawlen;
    
    if (lenRatio > 0.1) {
      Serial.printf("[IR Learn+] ⚠ Length mismatch: %d vs %d\n",
                    first.rawlen, current.rawlen);
      return false;
    }
  }
  
  Serial.printf("[IR Learn+] ✓ All %d captures consistent\n", count);
  return true;
}

// === Signal Management Functions ===

int getCurrentSignal() {
  return currentSignalIndex;
}

void setCurrentSignal(int signalIndex) {
  if (signalIndex >= 0 && signalIndex < TOTAL_SIGNALS) {
    currentSignalIndex = signalIndex;
  }
}

void startLearningSignal(int signalIndex) {
  char defaultName[16];
  snprintf(defaultName, sizeof(defaultName), "Signal_%d", signalIndex + 1);
  startLearningSignal(signalIndex, defaultName);
}

void startLearningSignal(int signalIndex, const char* signalName) {
  if (signalIndex < 0 || signalIndex >= TOTAL_SIGNALS) {
    Serial.println("[IR Learn+] ✗ Invalid signal index");
    return;
  }
  
  // Convert to device/button
  int deviceIdx, buttonIdx;
  signalToDeviceButton(signalIndex, &deviceIdx, &buttonIdx);
  
  currentSignalIndex = signalIndex;
  session.currentDeviceIndex = deviceIdx;
  session.currentButtonIndex = buttonIdx;
  session.captureCount = 0;
  session.state = LEARN_WAITING;
  session.waitStartTime = millis();
  
  LearnedDevice* device = &learnedDevices[deviceIdx];
  LearnedButton* button = &device->buttons[buttonIdx];
  
  strncpy(button->buttonName, signalName, sizeof(button->buttonName) - 1);
  button->buttonName[sizeof(button->buttonName) - 1] = '\0';
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.printf("║ Learning Signal %d/40\n", signalIndex + 1);
  Serial.println("╠════════════════════════════════════════╣");
  Serial.println("║ Instructions:");
  Serial.println("║ 1. Point remote at IR receiver");
  Serial.println("║ 2. Press the button you want to learn");
  Serial.println("║ 3. Keep pressing until 3 captures done");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  irrecv.resume();
}

/**
 * Main receive loop - call this repeatedly
 * Returns true when learning is complete (success or failure)
 */
bool checkIRReceiveEnhanced() {
  if (session.state != LEARN_WAITING && session.state != LEARN_RECEIVING) {
    return false;
  }
  
  // Check timeout
  if (millis() - session.waitStartTime > session.timeout) {
    Serial.println("\n[IR Learn+] ✗ Timeout - no signal received");
    session.state = LEARN_ERROR;
    return true;
  }
  
  decode_results results;
  if (irrecv.decode(&results)) {
    // Skip repeat codes on first capture
    if (session.captureCount == 0 && isRepeatSignal(results)) {
      Serial.println("[IR Learn+] ⟳ Skipping repeat code, press button again");
      irrecv.resume();
      return false;
    }
    
    session.state = LEARN_RECEIVING;
    
    // Store capture
    session.captures[session.captureCount] = results;
    session.captureCount++;
    
    Serial.printf("[IR Learn+] 📡 Capture %d/3 received\n", session.captureCount);
    Serial.printf("             Protocol: %s\n", typeToString(results.decode_type).c_str());
    Serial.printf("             Value: 0x%llX\n", results.value);
    Serial.printf("             Length: %d samples\n", results.rawlen);
    
    // Check if done
    if (session.captureCount >= 3) {
      session.state = LEARN_ANALYZING;
      
      Serial.println("\n[IR Learn+] 🔍 Analyzing captures...");
      
      // Validate consistency
      if (!validateSignalConsistency(session.captures, 3)) {
        Serial.println("[IR Learn+] ✗ Captures are inconsistent!");
        Serial.println("[IR Learn+]   Try again - press same button 3 times");
        session.state = LEARN_ERROR;
        return true;
      }
      
      // Analyze quality
      uint8_t quality = analyzeSignalQuality(session.captures[0]);
      Serial.printf("[IR Learn+] Signal quality: %d/100\n", quality);
      
      if (quality < 50) {
        Serial.println("[IR Learn+] ⚠ Warning: Low signal quality!");
        Serial.println("[IR Learn+]   Signal may not work reliably");
      }
      
      // Detect carrier frequency (informational only)
      float detectedFreq = detectCarrierFrequency(session.captures[0]);
      bool freqMismatch = (abs((int)detectedFreq - (int)IR_HARDWARE_FREQUENCY) > 2);
      
      Serial.printf("[IR Learn+] Original remote frequency: %.1f kHz\n", detectedFreq);
      Serial.printf("[IR Learn+] Our hardware frequency: %d kHz\n", IR_HARDWARE_FREQUENCY);
      
      if (freqMismatch) {
        Serial.println("[IR Learn+] ⚠ WARNING: Frequency mismatch detected!");
        Serial.println("[IR Learn+]   Signal will be sent at hardware frequency (may reduce range)");
        Serial.println("[IR Learn+]   This is normal - most devices are tolerant of frequency variance");
      }
      
      // Store the signal
      LearnedDevice* device = &learnedDevices[session.currentDeviceIndex];
      LearnedButton* button = &device->buttons[session.currentButtonIndex];
      
      button->hasData = true;
      button->protocol = session.captures[0].decode_type;
      button->value = session.captures[0].value;
      button->address = session.captures[0].address;
      button->command = session.captures[0].command;
      button->bits = session.captures[0].bits;
      
      // Store raw data
      button->rawDataLen = min(session.captures[0].rawlen, (uint16_t)MAX_IR_BUFFER_SIZE);
      for (uint16_t i = 0; i < button->rawDataLen; i++) {
        button->rawData[i] = session.captures[0].rawbuf[i] * kRawTick;
      }
      
      // Store metadata
      button->metadata.captureTimestamp = millis();
      button->metadata.signalQuality = quality;
      button->metadata.detectedCarrierFreq = detectedFreq;
      button->metadata.frequencyMismatch = freqMismatch;
      button->metadata.isRepeatCode = false;
      
      // Update device settings
      device->detectedCarrierFreq = detectedFreq;
      if (!device->hasData) {
        device->hasData = true;
        device->primaryProtocol = button->protocol;
      }
      
      Serial.println("\n╔════════════════════════════════════════╗");
      Serial.printf("║ ✓ Signal learned successfully!\n");
      Serial.println("╠════════════════════════════════════════╣");
      Serial.printf("║ Button: %s\n", button->buttonName);
      Serial.printf("║ Quality: %d/100\n", quality);
      Serial.printf("║ Original: %.1f kHz | Hardware: %d kHz\n", detectedFreq, IR_HARDWARE_FREQUENCY);
      if (freqMismatch) {
        Serial.printf("║ ⚠ Frequency mismatch (usually OK)\n");
      }
      Serial.println("╚════════════════════════════════════════╝\n");
      
      session.state = LEARN_RECEIVED;
      return true;
    }
    
    // Wait for next capture
    Serial.printf("[IR Learn+] 👉 Press same button again (%d more times)\n", 
                  3 - session.captureCount);
    session.state = LEARN_WAITING;
    irrecv.resume();
    return false;
  }
  
  return false;
}

void saveLearnedSignal() {
  if (session.state != LEARN_RECEIVED) {
    Serial.println("[IR Learn+] ✗ No signal to save");
    return;
  }
  
  // Update button count
  LearnedDevice* device = &learnedDevices[session.currentDeviceIndex];
  
  // Count active buttons
  uint8_t count = 0;
  for (int i = 0; i < MAX_BUTTONS_PER_DEVICE; i++) {
    if (device->buttons[i].hasData) count++;
  }
  device->buttonCount = count;
  
  // Save to EEPROM
  saveDeviceIncremental(session.currentDeviceIndex);
  
  Serial.printf("[IR Learn+] ✓ Signal %d saved to EEPROM\n", currentSignalIndex + 1);
  session.state = LEARN_SAVED;
  
  delay(1000);
  session.state = LEARN_IDLE;
}

void cancelLearning() {
  session.state = LEARN_IDLE;
  session.captureCount = 0;
  Serial.println("[IR Learn+] Learning cancelled");
}

// === Playback Functions ===

bool sendSignal(int signalIndex) {
  if (signalIndex < 0 || signalIndex >= TOTAL_SIGNALS) {
    Serial.println("[IR Learn+] ✗ Invalid signal index");
    return false;
  }
  
  int deviceIdx, buttonIdx;
  signalToDeviceButton(signalIndex, &deviceIdx, &buttonIdx);
  
  LearnedDevice* device = &learnedDevices[deviceIdx];
  LearnedButton* button = &device->buttons[buttonIdx];
  
  if (!button->hasData) {
    Serial.printf("[IR Learn+] ✗ Signal %d not learned\n", signalIndex + 1);
    return false;
  }
  
  Serial.printf("\n[IR Learn+] 📤 Sending Signal %d: %s\n", 
                signalIndex + 1, button->buttonName);
  Serial.printf("             Protocol: %s\n", typeToString(button->protocol).c_str());
  Serial.printf("             Hardware frequency: %d kHz\n", IR_HARDWARE_FREQUENCY);
  
  if (button->metadata.frequencyMismatch) {
    Serial.printf("             (Original was %.1f kHz - using hardware freq)\n", 
                  button->metadata.detectedCarrierFreq);
  }
  
  // Always use hardware frequency (cannot be changed in software!)
  irsend.sendRaw(button->rawData, button->rawDataLen, IR_HARDWARE_FREQUENCY);
  
  Serial.println("[IR Learn+] ✓ Signal transmitted");
  return true;
}

bool sendSignalByName(const char* signalName) {
  for (int i = 0; i < TOTAL_SIGNALS; i++) {
    int deviceIdx, buttonIdx;
    signalToDeviceButton(i, &deviceIdx, &buttonIdx);
    
    LearnedButton* button = &learnedDevices[deviceIdx].buttons[buttonIdx];
    
    if (button->hasData && strcmp(button->buttonName, signalName) == 0) {
      return sendSignal(i);
    }
  }
  
  Serial.printf("[IR Learn+] ✗ Signal '%s' not found\n", signalName);
  return false;
}

void sendSignalWithRepeats(int signalIndex, uint8_t repeatCount) {
  for (uint8_t i = 0; i < repeatCount; i++) {
    sendSignal(signalIndex);
    if (i < repeatCount - 1) delay(100);  // Delay between repeats
  }
}

// === Signal Information Functions ===

void setSignalName(int signalIndex, const char* name) {
  if (signalIndex < 0 || signalIndex >= TOTAL_SIGNALS) {
    Serial.println("[IR Learn+] ✗ Invalid signal index");
    return;
  }
  
  int deviceIdx, buttonIdx;
  signalToDeviceButton(signalIndex, &deviceIdx, &buttonIdx);
  
  LearnedButton* button = &learnedDevices[deviceIdx].buttons[buttonIdx];
  
  strncpy(button->buttonName, name, sizeof(button->buttonName) - 1);
  button->buttonName[sizeof(button->buttonName) - 1] = '\0';
  
  saveDeviceIncremental(deviceIdx);
  
  Serial.printf("[IR Learn+] ✓ Signal %d renamed to: %s\n", signalIndex + 1, name);
}

const char* getSignalName(int signalIndex) {
  if (signalIndex < 0 || signalIndex >= TOTAL_SIGNALS) {
    return "Invalid";
  }
  
  int deviceIdx, buttonIdx;
  signalToDeviceButton(signalIndex, &deviceIdx, &buttonIdx);
  
  return learnedDevices[deviceIdx].buttons[buttonIdx].buttonName;
}

bool isSignalLearned(int signalIndex) {
  if (signalIndex < 0 || signalIndex >= TOTAL_SIGNALS) {
    return false;
  }
  
  int deviceIdx, buttonIdx;
  signalToDeviceButton(signalIndex, &deviceIdx, &buttonIdx);
  
  return learnedDevices[deviceIdx].buttons[buttonIdx].hasData;
}

int countLearnedSignals() {
  int count = 0;
  
  for (int i = 0; i < TOTAL_SIGNALS; i++) {
    if (isSignalLearned(i)) {
      count++;
    }
  }
  
  return count;
}

// === Getter Functions ===

LearnState getLearnState() {
  return session.state;
}

LearnedButton getSignal(int signalIndex) {
  LearnedButton empty = {};
  
  if (signalIndex < 0 || signalIndex >= TOTAL_SIGNALS) {
    return empty;
  }
  
  int deviceIdx, buttonIdx;
  signalToDeviceButton(signalIndex, &deviceIdx, &buttonIdx);
  
  return learnedDevices[deviceIdx].buttons[buttonIdx];
}

void clearSignal(int signalIndex) {
  if (signalIndex < 0 || signalIndex >= TOTAL_SIGNALS) {
    return;
  }
  
  int deviceIdx, buttonIdx;
  signalToDeviceButton(signalIndex, &deviceIdx, &buttonIdx);
  
  learnedDevices[deviceIdx].buttons[buttonIdx].hasData = false;
  learnedDevices[deviceIdx].buttons[buttonIdx].rawDataLen = 0;
  
  // Update button count
  uint8_t count = 0;
  for (int i = 0; i < MAX_BUTTONS_PER_DEVICE; i++) {
    if (learnedDevices[deviceIdx].buttons[i].hasData) count++;
  }
  learnedDevices[deviceIdx].buttonCount = count;
  
  saveDeviceIncremental(deviceIdx);
  Serial.printf("[IR Learn+] ✓ Signal %d cleared\n", signalIndex + 1);
}

void clearAllSignals() {
  for (int i = 0; i < MAX_LEARNED_DEVICES; i++) {
    learnedDevices[i].hasData = false;
    learnedDevices[i].buttonCount = 0;
    for (int j = 0; j < MAX_BUTTONS_PER_DEVICE; j++) {
      learnedDevices[i].buttons[j].hasData = false;
    }
  }
  saveLearnedDevicesEnhanced();
  Serial.println("[IR Learn+] ✓ All signals cleared");
}

// Legacy device-based access (for compatibility)

LearnedDevice getLearnedDevice(int index) {
  if (index >= 0 && index < MAX_LEARNED_DEVICES) {
    return learnedDevices[index];
  }
  LearnedDevice empty = {};
  return empty;
}

LearnedButton getLearnedButton(int deviceIndex, int buttonIndex) {
  if (deviceIndex >= 0 && deviceIndex < MAX_LEARNED_DEVICES &&
      buttonIndex >= 0 && buttonIndex < MAX_BUTTONS_PER_DEVICE) {
    return learnedDevices[deviceIndex].buttons[buttonIndex];
  }
  LearnedButton empty = {};
  return empty;
}

void clearLearnedDevice(int deviceIndex) {
  if (deviceIndex >= 0 && deviceIndex < MAX_LEARNED_DEVICES) {
    learnedDevices[deviceIndex].hasData = false;
    learnedDevices[deviceIndex].buttonCount = 0;
    for (int i = 0; i < MAX_BUTTONS_PER_DEVICE; i++) {
      learnedDevices[deviceIndex].buttons[i].hasData = false;
    }
    saveDeviceIncremental(deviceIndex);
    Serial.printf("[IR Learn+] ✓ Device %d cleared\n", deviceIndex + 1);
  }
}

void clearLearnedButton(int deviceIndex, int buttonIndex) {
  if (deviceIndex >= 0 && deviceIndex < MAX_LEARNED_DEVICES &&
      buttonIndex >= 0 && buttonIndex < MAX_BUTTONS_PER_DEVICE) {
    learnedDevices[deviceIndex].buttons[buttonIndex].hasData = false;
    
    // Update button count
    uint8_t count = 0;
    for (int i = 0; i < MAX_BUTTONS_PER_DEVICE; i++) {
      if (learnedDevices[deviceIndex].buttons[i].hasData) count++;
    }
    learnedDevices[deviceIndex].buttonCount = count;
    
    saveDeviceIncremental(deviceIndex);
    Serial.printf("[IR Learn+] ✓ Button cleared\n");
  }
}

// === Storage Functions (Optimized) ===

void saveDeviceIncremental(int deviceIndex) {
  if (deviceIndex < 0 || deviceIndex >= MAX_LEARNED_DEVICES) return;
  
  Serial.printf("[IR Learn+] 💾 Saving device %d...\n", deviceIndex);
  
  // Calculate address for this device
  int addr = EEPROM_START_ADDR + sizeof(uint16_t) + sizeof(uint8_t);
  addr += deviceIndex * (sizeof(LearnedDevice));
  
  // Save entire device structure
  EEPROM.put(addr, learnedDevices[deviceIndex]);
  EEPROM.commit();
  
  Serial.println("[IR Learn+] ✓ Device saved");
}

void saveLearnedDevicesEnhanced() {
  Serial.println("[IR Learn+] 💾 Saving all devices...");
  
  // Write magic and version
  uint16_t magic = EEPROM_MAGIC;
  uint8_t version = EEPROM_VERSION;
  
  EEPROM.put(EEPROM_START_ADDR, magic);
  EEPROM.put(EEPROM_START_ADDR + sizeof(uint16_t), version);
  
  // Save all devices
  int addr = EEPROM_START_ADDR + sizeof(uint16_t) + sizeof(uint8_t);
  for (int i = 0; i < MAX_LEARNED_DEVICES; i++) {
    EEPROM.put(addr, learnedDevices[i]);
    addr += sizeof(LearnedDevice);
  }
  
  EEPROM.commit();
  Serial.println("[IR Learn+] ✓ All devices saved");
}

void loadLearnedDevicesEnhanced() {
  Serial.println("[IR Learn+] 📂 Loading devices from EEPROM...");
  
  uint16_t magic;
  uint8_t version;
  
  EEPROM.get(EEPROM_START_ADDR, magic);
  EEPROM.get(EEPROM_START_ADDR + sizeof(uint16_t), version);
  
  if (magic != EEPROM_MAGIC) {
    Serial.println("[IR Learn+] No valid data found, initializing...");
    saveLearnedDevicesEnhanced();
    return;
  }
  
  if (version != EEPROM_VERSION) {
    Serial.printf("[IR Learn+] ⚠ Version mismatch (found: %d, expected: %d)\n", 
                  version, EEPROM_VERSION);
    Serial.println("[IR Learn+] Re-initializing storage...");
    saveLearnedDevicesEnhanced();
    return;
  }
  
  // Load all devices
  int addr = EEPROM_START_ADDR + sizeof(uint16_t) + sizeof(uint8_t);
  int loadedCount = 0;
  
  for (int i = 0; i < MAX_LEARNED_DEVICES; i++) {
    EEPROM.get(addr, learnedDevices[i]);
    addr += sizeof(LearnedDevice);
    
    if (learnedDevices[i].hasData) {
      Serial.printf("[IR Learn+] ✓ Device %d: %s (%d buttons)\n", 
                    i + 1, learnedDevices[i].deviceName, learnedDevices[i].buttonCount);
      loadedCount++;
    }
  }
  
  Serial.printf("[IR Learn+] ✓ Loaded %d devices\n", loadedCount);
}

// === Diagnostic Functions ===

void printSignalDetails(int signalIndex) {
  if (signalIndex < 0 || signalIndex >= TOTAL_SIGNALS) {
    Serial.println("[IR Learn+] Invalid signal index");
    return;
  }
  
  LearnedButton button = getSignal(signalIndex);
  
  if (!button.hasData) {
    Serial.printf("[IR Learn+] Signal %d is empty\n", signalIndex + 1);
    return;
  }
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.printf("║ Signal %d Details\n", signalIndex + 1);
  Serial.println("╠════════════════════════════════════════╣");
  Serial.printf("║ Name: %s\n", button.buttonName);
  Serial.printf("║ Protocol: %s\n", typeToString(button.protocol).c_str());
  Serial.printf("║ Value: 0x%llX\n", button.value);
  Serial.printf("║ Address: 0x%X\n", button.address);
  Serial.printf("║ Command: 0x%X\n", button.command);
  Serial.printf("║ Bits: %d\n", button.bits);
  Serial.println("╠════════════════════════════════════════╣");
  Serial.printf("║ Signal Quality: %d/100\n", button.metadata.signalQuality);
  Serial.printf("║ Original Freq: %.1f kHz (Hardware: %d kHz)\n", 
                button.metadata.detectedCarrierFreq, IR_HARDWARE_FREQUENCY);
  Serial.printf("║ Raw Length: %d samples\n", button.rawDataLen);
  Serial.println("╚════════════════════════════════════════╝\n");
}

void printAllSignals() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║      All IR Signals (1-40)               ║");
  Serial.println("╠════════════════════════════════════════╣");
  
  int learnedCount = 0;
  
  for (int i = 0; i < TOTAL_SIGNALS; i++) {
    LearnedButton button = getSignal(i);
    
    if (button.hasData) {
      Serial.printf("║ [%02d] %-20s Q:%3d/100 ║\n",
                    i + 1,
                    button.buttonName,
                    button.metadata.signalQuality);
      learnedCount++;
    }
  }
  
  Serial.println("╠════════════════════════════════════════╣");
  Serial.printf("║ Total: %d/40 signals learned          ║\n", learnedCount);
  Serial.println("╚════════════════════════════════════════╝\n");
}

void printSignalDiagnostics(const decode_results& results) {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║ Signal Diagnostics");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.printf("║ Protocol: %s\n", typeToString(results.decode_type).c_str());
  Serial.printf("║ Value: 0x%llX\n", results.value);
  Serial.printf("║ Raw Length: %d\n", results.rawlen);
  
  uint8_t quality = analyzeSignalQuality(results);
  Serial.printf("║ Quality: %d/100\n", quality);
  
  float carrier = detectCarrierFrequency(results);
  Serial.printf("║ Carrier: %.1f kHz\n", carrier);
  
  bool repeat = isRepeatSignal(results);
  Serial.printf("║ Is Repeat: %s\n", repeat ? "Yes" : "No");
  
  Serial.println("╚════════════════════════════════════════╝\n");
}

// === Export/Import (Stub - implement as needed) ===

String exportAllSignalsToJSON() {
  // TODO: Implement JSON export for all 40 signals
  return "{}";
}

bool importAllSignalsFromJSON(const String& json) {
  // TODO: Implement JSON import
  return false;
}
