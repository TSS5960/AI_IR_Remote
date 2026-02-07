/*
 * IR Learning Module - Implementation
 */

#include "ir_learning.h"
#include "ir_control.h"
#include <EEPROM.h>

#define EEPROM_SIZE 2048
#define EEPROM_MAGIC 0xABCD
#define EEPROM_START_ADDR 100

extern IRrecv irrecv;
extern decode_results results;

LearnedDevice learnedDevices[MAX_LEARNED_DEVICES];

int currentDeviceIndex = 0;

LearnState learnState = LEARN_IDLE;

decode_results tempResults;

void initIRLearning() {
  Serial.println("[IR Learn] Initializing IR learning module...");
  
  EEPROM.begin(EEPROM_SIZE);
  
  for (int i = 0; i < MAX_LEARNED_DEVICES; i++) {
    learnedDevices[i].hasData = false;
    learnedDevices[i].rawDataLen = 0;
  }
  
  loadLearnedDevices();
  
  currentDeviceIndex = 0;
  learnState = LEARN_IDLE;
  
  Serial.println("[IR Learn] OK: IR learning module initialized");
}

int getCurrentLearnDevice() {
  return currentDeviceIndex;
}

void setCurrentLearnDevice(int index) {
  if (index >= 0 && index < MAX_LEARNED_DEVICES) {
    currentDeviceIndex = index;
    Serial.printf("[IR Learn] Device selected: %d\n", index + 1);
  }
}

void nextLearnDevice() {
  currentDeviceIndex = (currentDeviceIndex + 1) % MAX_LEARNED_DEVICES;
  Serial.printf("[IR Learn] Switched to device: %d\n", currentDeviceIndex + 1);
}

LearnState getLearnState() {
  return learnState;
}

void startLearning() {
  learnState = LEARN_WAITING;
  Serial.println("[IR Learn] ----------------------------------------\n");
  Serial.printf("[IR Learn] Waiting for IR signal (Device %d)...\n", currentDeviceIndex + 1);
  Serial.printf("[IR Learn] Point remote at receiver and press a button\n");
  Serial.println("[IR Learn] ----------------------------------------\n");
  
  irrecv.resume();
}

void checkIRReceive() {
  if (learnState != LEARN_WAITING) {
    return;
  }
  
  if (irrecv.decode(&results)) {
    Serial.println("\n[IR Learn] ----------------------------------------");
    Serial.println("[IR Learn] OK: Signal received!");
    Serial.println("[IR Learn] ----------------------------------------");
    
    LearnedDevice* device = &learnedDevices[currentDeviceIndex];
    device->hasData = true;
    device->protocol = results.decode_type;
    device->value = results.value;
    device->address = results.address;
    device->command = results.command;
    
    device->rawDataLen = min(results.rawlen, (uint16_t)MAX_IR_BUFFER_SIZE);
    
    if (results.rawlen > MAX_IR_BUFFER_SIZE) {
      Serial.printf("[IR Learn] WARN: WARNING: Signal too long!\n");
      Serial.printf("[IR Learn]   Original length: %d\n", results.rawlen);
      Serial.printf("[IR Learn]   Buffer size: %d\n", MAX_IR_BUFFER_SIZE);
      Serial.printf("[IR Learn]   Data will be TRUNCATED - may not work!\n");
      Serial.println("[IR Learn]   Consider increasing MAX_IR_BUFFER_SIZE in ir_learning.h");
    }
    
    for (uint16_t i = 0; i < device->rawDataLen; i++) {
      device->rawData[i] = results.rawbuf[i] * kRawTick;
    }
    
    Serial.printf("[IR Learn] Protocol: %s\n", typeToString(device->protocol).c_str());
    Serial.printf("[IR Learn] Value: 0x%llX\n", device->value);
    Serial.printf("[IR Learn] Address: 0x%X\n", device->address);
    Serial.printf("[IR Learn] Command: 0x%X\n", device->command);
    Serial.printf("[IR Learn] Raw length: %d (stored: %d)\n", results.rawlen, device->rawDataLen);
    
    if (device->rawDataLen > 0) {
      Serial.print("[IR Learn] Raw data preview: ");
      int previewLen = min(20, (int)device->rawDataLen);
      for (int i = 0; i < previewLen; i++) {
        Serial.printf("%d ", device->rawData[i]);
      }
      if (device->rawDataLen > 20) {
        Serial.printf("... (+%d more)", device->rawDataLen - 20);
      }
      Serial.println();
    }
    
    Serial.println("[IR Learn] ----------------------------------------");
    Serial.println("[IR Learn] Click joystick to SAVE this signal");
    Serial.println("[IR Learn] ----------------------------------------\n");
    
    learnState = LEARN_RECEIVED;
    
    irrecv.resume();
  }
}

void saveLearnedSignal() {
  if (learnState != LEARN_RECEIVED) {
    Serial.println("[IR Learn] FAIL: No signal to save");
    return;
  }
  
  saveLearnedDevices();
  
  Serial.printf("[IR Learn] OK: Signal saved to Device %d\n", currentDeviceIndex + 1);
  learnState = LEARN_SAVED;
  
  delay(2000);
  learnState = LEARN_IDLE;
}

void sendLearnedSignal(int deviceIndex) {
  if (deviceIndex < 0 || deviceIndex >= MAX_LEARNED_DEVICES) {
    Serial.println("[IR Learn] FAIL: Invalid device index");
    return;
  }
  
  LearnedDevice* device = &learnedDevices[deviceIndex];
  
  if (!device->hasData) {
    Serial.printf("[IR Learn] FAIL: Device %d has no data\n", deviceIndex + 1);
    return;
  }
  
  Serial.println("\n[IR Learn] ----------------------------------------");
  Serial.printf("[IR Learn] Sending signal from Device %d\n", deviceIndex + 1);
  Serial.println("[IR Learn] ----------------------------------------");
  Serial.printf("[IR Learn] Protocol: %s\n", typeToString(device->protocol).c_str());
  Serial.printf("[IR Learn] Value: 0x%llX\n", device->value);
  Serial.printf("[IR Learn] Raw data length: %d\n", device->rawDataLen);
  
  if (device->rawDataLen > 0) {
    Serial.print("[IR Learn] Raw data preview: ");
    int previewLen = min(20, (int)device->rawDataLen);
    for (int i = 0; i < previewLen; i++) {
      Serial.printf("%d ", device->rawData[i]);
    }
    if (device->rawDataLen > 20) {
      Serial.print("...");
    }
    Serial.println();
  }
  
  extern IRsend irsend;
  
  if (device->rawDataLen == 0) {
    Serial.println("[IR Learn] FAIL: No raw data to send");
    return;
  }
  
  if (device->rawDataLen > MAX_IR_BUFFER_SIZE) {
    Serial.printf("[IR Learn] WARN: Warning: Data length %d exceeds buffer size %d, will be truncated\n", 
                  device->rawDataLen, MAX_IR_BUFFER_SIZE);
  }
  
  Serial.println("[IR Learn] Transmitting IR signal...");
  irsend.sendRaw(device->rawData, device->rawDataLen, 38);  // 38kHz
  
  Serial.println("[IR Learn] OK: Signal transmission complete");
  Serial.println("[IR Learn] ----------------------------------------\n");
}

LearnedDevice getLearnedDevice(int index) {
  if (index >= 0 && index < MAX_LEARNED_DEVICES) {
    return learnedDevices[index];
  }
  LearnedDevice empty;
  empty.hasData = false;
  return empty;
}

void clearLearnedDevice(int index) {
  if (index >= 0 && index < MAX_LEARNED_DEVICES) {
    learnedDevices[index].hasData = false;
    learnedDevices[index].rawDataLen = 0;
    saveLearnedDevices();
    Serial.printf("[IR Learn] Device %d cleared\n", index + 1);
  }
}

void loadLearnedDevices() {
  Serial.println("[IR Learn] Loading devices from EEPROM...");
  
  uint16_t magic;
  EEPROM.get(EEPROM_START_ADDR, magic);
  
  if (magic != EEPROM_MAGIC) {
    Serial.println("[IR Learn] No valid data found, initializing...");
    magic = EEPROM_MAGIC;
    EEPROM.put(EEPROM_START_ADDR, magic);
    
    for (int i = 0; i < MAX_LEARNED_DEVICES; i++) {
      learnedDevices[i].hasData = false;
    }
    
    EEPROM.commit();
    return;
  }
  
  int addr = EEPROM_START_ADDR + sizeof(uint16_t);
  for (int i = 0; i < MAX_LEARNED_DEVICES; i++) {
    uint8_t hasData;
    EEPROM.get(addr, hasData);
    addr += sizeof(uint8_t);
    
    learnedDevices[i].hasData = (hasData == 1);
    
    if (learnedDevices[i].hasData) {
      EEPROM.get(addr, learnedDevices[i].protocol);
      addr += sizeof(decode_type_t);
      
      EEPROM.get(addr, learnedDevices[i].value);
      addr += sizeof(uint64_t);
      
      EEPROM.get(addr, learnedDevices[i].address);
      addr += sizeof(uint16_t);
      EEPROM.get(addr, learnedDevices[i].command);
      addr += sizeof(uint16_t);
      
      EEPROM.get(addr, learnedDevices[i].rawDataLen);
      addr += sizeof(uint16_t);
      
      for (uint16_t j = 0; j < learnedDevices[i].rawDataLen && j < MAX_IR_BUFFER_SIZE; j++) {
        EEPROM.get(addr, learnedDevices[i].rawData[j]);
        addr += sizeof(uint16_t);
      }
      
      Serial.printf("[IR Learn] Device %d loaded: %s\n", i + 1, 
                    typeToString(learnedDevices[i].protocol).c_str());
    } else {
      addr += sizeof(decode_type_t) + sizeof(uint64_t) + 
              sizeof(uint16_t) * 2 + sizeof(uint16_t) + 
              sizeof(uint16_t) * MAX_IR_BUFFER_SIZE;
    }
  }
  
  Serial.println("[IR Learn] OK: Devices loaded");
}

void saveLearnedDevices() {
  Serial.println("[IR Learn] Saving devices to EEPROM...");
  
  uint16_t magic = EEPROM_MAGIC;
  EEPROM.put(EEPROM_START_ADDR, magic);
  
  int addr = EEPROM_START_ADDR + sizeof(uint16_t);
  for (int i = 0; i < MAX_LEARNED_DEVICES; i++) {
    uint8_t hasData = learnedDevices[i].hasData ? 1 : 0;
    EEPROM.put(addr, hasData);
    addr += sizeof(uint8_t);
    
    EEPROM.put(addr, learnedDevices[i].protocol);
    addr += sizeof(decode_type_t);
    
    EEPROM.put(addr, learnedDevices[i].value);
    addr += sizeof(uint64_t);
    
    EEPROM.put(addr, learnedDevices[i].address);
    addr += sizeof(uint16_t);
    EEPROM.put(addr, learnedDevices[i].command);
    addr += sizeof(uint16_t);
    
    EEPROM.put(addr, learnedDevices[i].rawDataLen);
    addr += sizeof(uint16_t);
    
    for (uint16_t j = 0; j < MAX_IR_BUFFER_SIZE; j++) {
      EEPROM.put(addr, learnedDevices[i].rawData[j]);
      addr += sizeof(uint16_t);
    }
  }
  
  EEPROM.commit();
  Serial.println("[IR Learn] OK: Devices saved");
}

const char* getLearnedDeviceName(int index) {
  static char name[20];
  if (index >= 0 && index < MAX_LEARNED_DEVICES) {
    if (learnedDevices[index].hasData) {
      snprintf(name, sizeof(name), "Device %d (%s)", 
               index + 1, 
               typeToString(learnedDevices[index].protocol).c_str());
    } else {
      snprintf(name, sizeof(name), "Device %d (Empty)", index + 1);
    }
  } else {
    snprintf(name, sizeof(name), "Invalid");
  }
  return name;
}
