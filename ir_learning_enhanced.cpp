/*
 * Enhanced IR Learning Module - Implementation
 * Key improvements: frequency detection, signal validation, multi-button support
 */

#include "ir_learning_enhanced.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <IRsend.h>  // Required for IRsend class
#include <esp_task_wdt.h>  // Watchdog timer

#define IR_SIGNALS_FILE "/ir_signals.dat"
#define IR_CONFIG_VERSION 3

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
  
  // Initialize SPIFFS if not already done
  if (!SPIFFS.begin(true)) {
    Serial.println("[IR Learn+] ✗ SPIFFS initialization failed!");
    return;
  }
  
  // Calculate and display memory usage
  size_t buttonSize = sizeof(LearnedButton);
  size_t deviceSize = sizeof(LearnedDevice);
  size_t totalSize = MAX_LEARNED_DEVICES * deviceSize + sizeof(uint16_t) + sizeof(uint8_t);
  
  Serial.printf("[IR Learn+] Button size: %d bytes\n", buttonSize);
  Serial.printf("[IR Learn+] Device size: %d bytes\n", deviceSize);
  Serial.printf("[IR Learn+] Total storage: %d bytes\n", totalSize);
  Serial.println("[IR Learn+] ✓ Using SPIFFS for persistent storage");
  
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
  session.timeout = 30000;  // 30 second timeout - ensure it's always set
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
  
  Serial.printf("[IR Learn+] 🔧 Calling irrecv.resume() to enable receiver...\n");
  Serial.printf("[IR Learn+] 🔧 IR receiver pin: GPIO%d\n", IR_RX_PIN);
  Serial.printf("[IR Learn+] 🔧 State set to: LEARN_WAITING (%d)\n", session.state);
  Serial.printf("[IR Learn+] 🔧 Timeout set to: %lu ms\n", session.timeout);
  Serial.printf("[IR Learn+] 🔧 Wait start time: %lu\n", session.waitStartTime);
  irrecv.resume();
}

/**
 * Main receive loop - call this repeatedly
 * Returns true when learning is complete (success or failure)
 */
bool checkIRReceiveEnhanced() {
  static unsigned long lastDebugMs = 0;
  
  if (session.state != LEARN_WAITING && session.state != LEARN_RECEIVING) {
    return false;
  }
  
  // Debug output every 2 seconds
  if (millis() - lastDebugMs > 2000) {
    Serial.printf("[IR Learn+] 🔍 Checking... State: %d, Elapsed: %lu ms\n", 
                  session.state, millis() - session.waitStartTime);
    Serial.printf("[IR Learn+]    Timeout setting: %lu ms\n", session.timeout);
    lastDebugMs = millis();
  }
  
  // Check timeout
  unsigned long elapsed = millis() - session.waitStartTime;
  if (elapsed > session.timeout) {
    Serial.printf("\n[IR Learn+] ✗ Timeout - no signal received (elapsed: %lu ms, timeout: %lu ms)\n", 
                  elapsed, session.timeout);
    session.state = LEARN_ERROR;
    return true;
  }
  
  decode_results results;
  if (irrecv.decode(&results)) {
    // Skip repeat codes on all captures (not just first)
    if (isRepeatSignal(results)) {
      Serial.println("[IR Learn+] ⟳ Skipping repeat code, release button and press again");
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
  Serial.println("[IR Learn+] 👉 Click joystick to continue to next signal");
  
  session.state = LEARN_SAVED;
  
  // Note: State will remain LEARN_SAVED until user clicks to advance
  // This allows time for user to see the success message
}

void cancelLearning() {
  session.state = LEARN_IDLE;
  session.captureCount = 0;
  Serial.println("[IR Learn+] Learning cancelled");
}

void resetLearningState() {
  session.state = LEARN_IDLE;
  session.captureCount = 0;
  Serial.println("[IR Learn+] Learning state reset to IDLE");
}

// === IR Monitoring Functions ===

/**
 * Continuous IR monitoring mode - shows all received signals in real-time
 * Returns true if user wants to exit (press any key)
 */
bool monitorIRSignals(unsigned long duration) {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║ IR Signal Monitor");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.println("║ Monitoring all IR signals...");
  if (duration > 0) {
    Serial.printf("║ Duration: %lu seconds\n", duration / 1000);
  } else {
    Serial.println("║ Press any key to stop");
  }
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // IR receiver is already enabled at startup - just resume
  irrecv.resume();
  
  unsigned long startTime = millis();
  unsigned long lastSignalTime = 0;
  int signalCount = 0;
  
  while (true) {
    // Check for exit
    if (Serial.available()) {
      while (Serial.available()) Serial.read();  // Clear buffer
      Serial.println("\n[Monitor] Stopped by user\n");
      return true;
    }
    
    // Check timeout
    if (duration > 0 && (millis() - startTime > duration)) {
      Serial.printf("\n[Monitor] Finished - %d signals received\n\n", signalCount);
      return false;
    }
    
    // Check for IR signal
    decode_results results;
    if (irrecv.decode(&results)) {
      signalCount++;
      unsigned long now = millis();
      unsigned long timeSinceLast = (lastSignalTime > 0) ? (now - lastSignalTime) : 0;
      lastSignalTime = now;
      
      Serial.println("\n╔════════════════════════════════════════╗");
      Serial.printf("║ Signal #%d | Time: %lu ms | Gap: %lu ms\n", 
                    signalCount, now - startTime, timeSinceLast);
      Serial.println("╚════════════════════════════════════════╝");
      
      // Protocol info
      Serial.printf("Protocol:  %s\n", typeToString(results.decode_type).c_str());
      
      // Value (if decoded)
      if (results.decode_type != UNKNOWN && results.value != 0) {
        Serial.printf("Value:     0x%llX\n", results.value);
        Serial.printf("Bits:      %d\n", results.bits);
        if (results.address != 0) {
          Serial.printf("Address:   0x%04X\n", results.address);
        }
        if (results.command != 0) {
          Serial.printf("Command:   0x%04X\n", results.command);
        }
      }
      
      // Raw data info
      Serial.printf("Raw Length: %d samples\n", results.rawlen);
      
      // Check if this is a repeat signal
      if (results.repeat || 
          (results.decode_type != UNKNOWN && results.value == 0xFFFFFFFFFFFFFFFF)) {
        Serial.println("Type:      REPEAT CODE");
      }
      
      // Show raw timing preview
      if (results.rawlen > 0) {
        Serial.print("Raw Preview: ");
        int displayLen = min(10, (int)results.rawlen);
        for (int i = 1; i < displayLen; i++) {
          Serial.printf("%d", results.rawbuf[i] * kRawTick);
          if (i < displayLen - 1) Serial.print(", ");
        }
        if (results.rawlen > 10) {
          Serial.printf("... (+%d)", results.rawlen - 10);
        }
        Serial.println();
        
        // Option to show full raw data
        Serial.println("\n>>> Type 'f' for FULL raw data, any other key to continue <<<");
      }
      
      Serial.println();
      
      // Check if user wants detailed view
      unsigned long waitStart = millis();
      bool showDetails = false;
      while (millis() - waitStart < 2000) {  // Wait 2 seconds for input
        if (Serial.available()) {
          char input = Serial.read();
          while (Serial.available()) Serial.read();  // Clear buffer
          
          if (input == 'f' || input == 'F') {
            showDetails = true;
            break;
          } else if (input == 'q' || input == 'Q') {
            Serial.println("\n[Monitor] Stopped by user\n");
            irrecv.resume();
            return true;
          } else {
            break;  // Any other key continues
          }
        }
        delay(10);
      }
      
      if (showDetails) {
        printDetailedSignal(&results);
      }
      
      irrecv.resume();
      delay(10);
    }
    
    delay(10);
  }
}

/**
 * Print detailed signal analysis for troubleshooting
 */
void printDetailedSignal(const decode_results* results) {
  Serial.println("\n╔══════════════════════════════════ DETAILED ANALYSIS ══════════════════════════════════╗");
  
  // Full protocol details
  Serial.println("║ PROTOCOL INFORMATION:");
  Serial.printf("║   Type: %s\n", typeToString(results->decode_type).c_str());
  if (results->decode_type != UNKNOWN) {
    Serial.printf("║   Value: 0x%llX\n", results->value);
    Serial.printf("║   Bits: %d\n", results->bits);
    if (results->address) Serial.printf("║   Address: 0x%04X\n", results->address);
    if (results->command) Serial.printf("║   Command: 0x%04X\n", results->command);
  }
  
  // Raw timing data - FULL
  Serial.println("║");
  Serial.println("║ FULL RAW TIMING DATA:");
  Serial.printf("║   Total samples: %d\n", results->rawlen);
  Serial.println("║   Format: [index] Mark/Space µs");
  Serial.println("║");
  
  for (uint16_t i = 1; i < results->rawlen; i++) {
    uint16_t timing = results->rawbuf[i] * kRawTick;
    const char* type = (i % 2 == 1) ? "MARK " : "SPACE";
    
    Serial.printf("║   [%3d] %s %5d", i, type, timing);
    
    // Visual bar representation
    Serial.print("  |");
    int barLen = timing / 100;  // Scale: 100us = 1 char
    barLen = min(barLen, 60);   // Max 60 chars
    for (int j = 0; j < barLen; j++) {
      Serial.print((i % 2 == 1) ? "█" : "░");
    }
    Serial.println();
    
    // Add line breaks every 10 entries for readability
    if (i % 10 == 0 && i < results->rawlen - 1) {
      Serial.println("║");
    }
  }
  
  // Summary statistics
  Serial.println("║");
  Serial.println("║ TIMING STATISTICS:");
  
  // Calculate total duration
  unsigned long totalDuration = 0;
  for (uint16_t i = 1; i < results->rawlen; i++) {
    totalDuration += results->rawbuf[i] * kRawTick;
  }
  Serial.printf("║   Total duration: %lu µs (%.2f ms)\n", totalDuration, totalDuration / 1000.0);
  
  // Find longest mark and space
  uint16_t longestMark = 0, longestSpace = 0;
  for (uint16_t i = 1; i < results->rawlen; i++) {
    uint16_t timing = results->rawbuf[i] * kRawTick;
    if (i % 2 == 1) {
      longestMark = max(longestMark, timing);
    } else {
      longestSpace = max(longestSpace, timing);
    }
  }
  Serial.printf("║   Longest MARK:  %d µs\n", longestMark);
  Serial.printf("║   Longest SPACE: %d µs\n", longestSpace);
  
  // Arduino code snippet for reproduction
  Serial.println("║");
  Serial.println("║ ARDUINO CODE (copy for replay):");
  Serial.println("║");
  Serial.printf("║   uint16_t rawData[%d] = {\n", results->rawlen - 1);
  Serial.print("║     ");
  for (uint16_t i = 1; i < results->rawlen; i++) {
    Serial.printf("%d", results->rawbuf[i] * kRawTick);
    if (i < results->rawlen - 1) {
      Serial.print(", ");
      if (i % 8 == 0) Serial.print("\n║     ");
    }
  }
  Serial.println("\n║   };");
  Serial.printf("║   irsend.sendRaw(rawData, %d, 38);  // 38kHz\n", results->rawlen - 1);
  
  Serial.println("╚════════════════════════════════════════════════════════════════════════════════════════╝\n");
}

// === Test Functions ===

/**
 * Test if IR receiver is working
 * Call this to verify hardware before learning
 */
void testIRReceiver() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║ IR Receiver Test Mode");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.println("║ Press any button on your remote");
  Serial.println("║ Waiting for 10 seconds...");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // IR receiver is already enabled at startup - just resume
  irrecv.resume();
  
  unsigned long startTime = millis();
  bool received = false;
  
  while (millis() - startTime < 10000) {
    decode_results testResults;
    if (irrecv.decode(&testResults)) {
      Serial.println("\n✓ IR RECEIVER IS WORKING!");
      Serial.printf("  Protocol: %s\n", typeToString(testResults.decode_type).c_str());
      Serial.printf("  Value: 0x%llX\n", testResults.value);
      Serial.printf("  Samples: %d\n", testResults.rawlen);
      received = true;
      irrecv.resume();
      delay(1000);  // Give time for user to see message
      break;
    }
    delay(10);
  }
  
  if (!received) {
    Serial.println("\n✗ NO IR SIGNAL RECEIVED!");
    Serial.println("  Check:");
    Serial.println("  - IR receiver is connected to GPIO9");
    Serial.println("  - Remote has batteries");
    Serial.println("  - Remote is pointed at receiver");
    Serial.println("  - IR LED on remote is working");
  }
  
  Serial.println();
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
  Serial.printf("             Value: 0x%llX\n", button->value);
  Serial.printf("             Bits: %d\n", button->bits);
  Serial.printf("             Hardware frequency: %d kHz\n", IR_HARDWARE_FREQUENCY);
  
  if (button->metadata.frequencyMismatch) {
    Serial.printf("             (Original was %.1f kHz - using hardware freq)\n", 
                  button->metadata.detectedCarrierFreq);
  }
  
  // Use protocol-specific send for better reliability
  bool sent = false;
  
  switch (button->protocol) {
    case decode_type_t::NEC:
      if (button->bits > 0 && button->value != 0xFFFFFFFFFFFFFFFF) {
        Serial.println("[IR Learn+] Using NEC protocol send");
        irsend.sendNEC(button->value, button->bits, 2);  // Send twice for better reliability
        sent = true;
      }
      break;
      
    case decode_type_t::SONY:
      if (button->bits > 0) {
        Serial.println("[IR Learn+] Using SONY protocol send");
        irsend.sendSony(button->value, button->bits);
        sent = true;
      }
      break;
      
    case decode_type_t::RC5:
      if (button->bits > 0) {
        Serial.println("[IR Learn+] Using RC5 protocol send");
        irsend.sendRC5(button->value, button->bits);
        sent = true;
      }
      break;
      
    case decode_type_t::RC6:
      if (button->bits > 0) {
        Serial.println("[IR Learn+] Using RC6 protocol send");
        irsend.sendRC6(button->value, button->bits);
        sent = true;
      }
      break;
      
    default:
      // Will fall through to raw send
      break;
  }
  
  // Fallback to raw send if protocol send not used
  if (!sent && button->rawDataLen > 0) {
    Serial.println("[IR Learn+] Using raw timing data");
    Serial.printf("             Raw data length: %d samples\n", button->rawDataLen);
    irsend.sendRaw(button->rawData, button->rawDataLen, IR_HARDWARE_FREQUENCY);
    sent = true;
  }
  
  if (!sent) {
    Serial.println("[IR Learn+] ✗ No valid data to send");
    return false;
  }
  
  // Resume receiver after sending (transmission pauses it)
  delay(100);
  irrecv.resume();
  
  Serial.println("[IR Learn+] ✓ Signal transmitted");
  Serial.println("[IR Learn+]   IR receiver resumed");
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
  // Just save all devices - SPIFFS write is fast enough
  saveLearnedDevicesEnhanced();
}

void saveLearnedDevicesEnhanced() {
  Serial.println("[IR Learn+] 💾 Saving all devices to SPIFFS...");
  
  esp_task_wdt_reset();
  
  File file = SPIFFS.open(IR_SIGNALS_FILE, "w");
  if (!file) {
    Serial.println("[IR Learn+] ✗ Failed to open file for writing!");
    return;
  }
  
  // Write version header
  uint8_t version = IR_CONFIG_VERSION;
  file.write(&version, sizeof(uint8_t));
  
  // Write all devices
  for (int i = 0; i < MAX_LEARNED_DEVICES; i++) {
    esp_task_wdt_reset();
    file.write((uint8_t*)&learnedDevices[i], sizeof(LearnedDevice));
  }
  
  file.close();
  
  Serial.println("[IR Learn+] ✓ All devices saved to SPIFFS");
  Serial.printf("[IR Learn+]   File size: %d bytes\n", 
                sizeof(uint8_t) + (MAX_LEARNED_DEVICES * sizeof(LearnedDevice)));
  
  esp_task_wdt_reset();
}

void loadLearnedDevicesEnhanced() {
  Serial.println("[IR Learn+] 📂 Loading devices from SPIFFS...");
  
  if (!SPIFFS.exists(IR_SIGNALS_FILE)) {
    Serial.println("[IR Learn+] No saved data found, starting fresh");
    return;
  }
  
  File file = SPIFFS.open(IR_SIGNALS_FILE, "r");
  if (!file) {
    Serial.println("[IR Learn+] ✗ Failed to open file for reading!");
    return;
  }
  
  // Read version
  uint8_t version;
  file.read((uint8_t*)&version, sizeof(uint8_t));
  
  if (version != IR_CONFIG_VERSION) {
    Serial.printf("[IR Learn+] ⚠ Version mismatch (found: %d, expected: %d)\n", 
                  version, IR_CONFIG_VERSION);
    file.close();
    return;
  }
  
  // Load all devices
  int loadedCount = 0;
  
  for (int i = 0; i < MAX_LEARNED_DEVICES; i++) {
    size_t bytesRead = file.read((uint8_t*)&learnedDevices[i], sizeof(LearnedDevice));
    
    if (bytesRead != sizeof(LearnedDevice)) {
      Serial.printf("[IR Learn+] ⚠ Incomplete read for device %d\n", i + 1);
      break;
    }
    
    if (learnedDevices[i].hasData) {
      Serial.printf("[IR Learn+] ✓ Device %d: %s (%d buttons)\n", 
                    i + 1, learnedDevices[i].deviceName, learnedDevices[i].buttonCount);
      loadedCount++;
      
      // Show which buttons are learned
      for (int j = 0; j < MAX_BUTTONS_PER_DEVICE; j++) {
        if (learnedDevices[i].buttons[j].hasData) {
          int signalNum = i * MAX_BUTTONS_PER_DEVICE + j + 1;
          Serial.printf("[IR Learn+]   - I%d: %s (Protocol: %s, Length: %d)\n",
                       signalNum,
                       learnedDevices[i].buttons[j].buttonName,
                       typeToString(learnedDevices[i].buttons[j].protocol).c_str(),
                       learnedDevices[i].buttons[j].rawDataLen);
        }
      }
    }
  }
  
  file.close();
  
  if (loadedCount == 0) {
    Serial.println("[IR Learn+] No learned devices found");
  } else {
    Serial.printf("[IR Learn+] ✓ Loaded %d devices with learned signals\n", loadedCount);
  }
}

// === EEPROM Verification ===

void verifyEEPROMData() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║ SPIFFS Data Verification");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  if (!SPIFFS.exists(IR_SIGNALS_FILE)) {
    Serial.println("File does not exist: ✗");
    return;
  }
  
  File file = SPIFFS.open(IR_SIGNALS_FILE, "r");
  if (!file) {
    Serial.println("Cannot open file: ✗");
    return;
  }
  
  uint8_t version;
  file.read((uint8_t*)&version, sizeof(uint8_t));
  file.close();
  
  Serial.printf("File exists: ✓\n");
  Serial.printf("Version:   %d (expected: %d) %s\n", 
                version, IR_CONFIG_VERSION, (version == IR_CONFIG_VERSION) ? "✓" : "✗");
  
  size_t fileSize = SPIFFS.open(IR_SIGNALS_FILE, "r").size();
  Serial.printf("File size: %d bytes\n", fileSize);
  Serial.printf("Device Size: %d bytes\n", sizeof(LearnedDevice));
  Serial.printf("Button Size: %d bytes\n", sizeof(LearnedButton));
  Serial.println();
  
  // Count learned signals
  int totalSignals = 0;
  for (int i = 0; i < MAX_LEARNED_DEVICES; i++) {
    for (int j = 0; j < MAX_BUTTONS_PER_DEVICE; j++) {
      if (learnedDevices[i].buttons[j].hasData) {
        totalSignals++;
      }
    }
  }
  
  Serial.printf("Total Learned Signals: %d / %d\n", totalSignals, TOTAL_SIGNALS);
  Serial.printf("Expected file size: ~%d bytes\n\n", 
                sizeof(uint8_t) + (MAX_LEARNED_DEVICES * sizeof(LearnedDevice)));
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
