/*
 * Enhanced IR Learning Module - Header
 * Supports multiple protocols, frequency detection, and signal validation
 */

#ifndef IR_LEARNING_ENHANCED_H
#define IR_LEARNING_ENHANCED_H

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include "config.h"

// === Configuration ===
#define MAX_LEARNED_DEVICES 5
#define MAX_BUTTONS_PER_DEVICE 8  // Store up to 8 buttons per device
#define TOTAL_SIGNALS 40          // Total signals = 5 devices × 8 buttons
#define MAX_IR_BUFFER_SIZE 300    // Reduced from 1024 to prevent EEPROM overflow
#define MAX_EXTENDED_BUFFER 512   // Reduced from 2048

// Hardware Configuration - CANNOT be changed in software!
// Your IR receiver (e.g., TSOP4838) and LED are physically tuned to this frequency
#define IR_HARDWARE_FREQUENCY 38  // Your hardware frequency in kHz (typically 38)

// Signal quality thresholds
#define MIN_SIGNAL_LENGTH 10      // Minimum raw data length
#define MAX_NOISE_RATIO 0.3       // Max 30% irregular timings
#define TIMING_TOLERANCE 0.15     // ±15% tolerance for timing matching

// === Enhanced Structures ===

struct IRSignalMetadata {
  uint32_t captureTimestamp;     // When captured
  uint8_t signalQuality;         // 0-100 quality score
  float detectedCarrierFreq;     // Detected from original remote (for info only)
  bool frequencyMismatch;        // True if detected freq != hardware freq
  uint16_t repeatCount;          // Number of repeats detected
  bool isRepeatCode;             // Is this a repeat-only signal?
  uint8_t signalStrength;        // Relative signal strength
};

struct LearnedButton {
  bool hasData;
  char buttonName[16];           // e.g., "Power", "Vol+", "Ch1"
  
  // Protocol information
  decode_type_t protocol;
  uint64_t value;
  uint16_t address;
  uint16_t command;
  uint8_t bits;                  // Number of bits
  
  // Raw timing data
  uint16_t rawData[MAX_IR_BUFFER_SIZE];
  uint16_t rawDataLen;
  
  // Enhanced metadata
  IRSignalMetadata metadata;
  
  // Repeat handling
  uint16_t repeatData[64];       // Separate repeat sequence
  uint16_t repeatDataLen;
};

struct LearnedDevice {
  bool hasData;
  char deviceName[32];           // e.g., "Samsung TV", "LG AC"
  decode_type_t primaryProtocol; // Most common protocol for this device
  
  LearnedButton buttons[MAX_BUTTONS_PER_DEVICE];
  uint8_t buttonCount;
  
  // Device-wide settings
  float detectedCarrierFreq;     // Detected from original (info only)
  uint8_t preferredRepeatCount;  // How many times to repeat on send
};

enum LearnState {
  LEARN_IDLE,
  LEARN_WAITING,
  LEARN_RECEIVING,               // Signal being received
  LEARN_ANALYZING,               // Analyzing signal quality
  LEARN_RECEIVED,
  LEARN_SAVED,
  LEARN_ERROR                    // Invalid signal
};

struct LearningSession {
  LearnState state;
  int currentDeviceIndex;
  int currentButtonIndex;
  
  // Multi-capture for validation
  decode_results captures[3];    // Capture same signal 3 times for verification
  uint8_t captureCount;
  
  // Timeout handling
  unsigned long waitStartTime;
  unsigned long timeout;         // ms
};

// === Function Declarations ===

// Initialization
void initIRLearningEnhanced();

// Signal Management (Flat 0-39 approach)
int getCurrentSignal();                    // Get current signal index (0-39)
void setCurrentSignal(int signalIndex);    // Set current signal index (0-39)

// Signal Learning
void startLearningSignal(int signalIndex);           // Start learning signal by index
void startLearningSignal(int signalIndex, const char* signalName);  // With custom name
LearnState getLearnState();
bool checkIRReceiveEnhanced();  // Returns true when complete
void saveLearnedSignal();       // Save the learned signal
void cancelLearning();
void resetLearningState();      // Reset learning state to IDLE
void testIRReceiver();          // Test if IR receiver is working
bool monitorIRSignals(unsigned long duration = 0);  // Monitor all IR signals continuously
void printDetailedSignal(const decode_results* results);  // Print full signal analysis

// Signal Playback
bool sendSignal(int signalIndex);                    // Send by index (0-39)
bool sendSignalByName(const char* signalName);       // Send by name
void sendSignalWithRepeats(int signalIndex, uint8_t repeatCount);

// Signal Information
void setSignalName(int signalIndex, const char* name);  // Rename a signal
const char* getSignalName(int signalIndex);             // Get signal name
bool isSignalLearned(int signalIndex);                  // Check if learned
int countLearnedSignals();                              // Count total learned

// Signal Analysis
uint8_t analyzeSignalQuality(const decode_results& results);
float detectCarrierFrequency(const decode_results& results);
bool validateSignalConsistency(const decode_results captures[], uint8_t count);
bool isRepeatSignal(const decode_results& results);

// Timing Utilities
bool timingsMatch(uint16_t t1, uint16_t t2, float tolerance);
void normalizeTimings(uint16_t* timings, uint16_t length);
uint16_t findClosestTiming(uint16_t timing, const uint16_t* reference, uint16_t refLength);

// Data Management (Flat signal access)
LearnedButton getSignal(int signalIndex);           // Get signal info by index
void clearSignal(int signalIndex);                  // Clear a signal
void clearAllSignals();                             // Clear all signals

// Internal conversion helpers (for compatibility)
void signalToDeviceButton(int signalIndex, int* device, int* button);
int deviceButtonToSignal(int device, int button);

// Legacy device-based access (for internal use)
LearnedDevice getLearnedDevice(int index);
LearnedButton getLearnedButton(int deviceIndex, int buttonIndex);
void clearLearnedDevice(int deviceIndex);
void clearLearnedButton(int deviceIndex, int buttonIndex);

// Storage (optimized for EEPROM wear leveling)
void loadLearnedDevicesEnhanced();
void saveLearnedDevicesEnhanced();
void saveDeviceIncremental(int deviceIndex);  // Save only one device
void verifyEEPROMData();  // Verify EEPROM integrity and show stats

// Diagnostics
void printSignalDetails(int signalIndex);           // Print details for one signal
void printAllSignals();                             // Print all 40 signals
void printSignalDiagnostics(const decode_results& results);

// Export/Import (JSON format for backup)
String exportAllSignalsToJSON();
bool importAllSignalsFromJSON(const String& json);

#endif // IR_LEARNING_ENHANCED_H
