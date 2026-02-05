/*
 * IR Learning Module - Header
 */

#ifndef IR_LEARNING_H
#define IR_LEARNING_H

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

#define MAX_LEARNED_DEVICES 5

// Supports long signals (e.g. air conditioners).
#define MAX_IR_BUFFER_SIZE 1024

enum LearnState {
  LEARN_IDLE,
  LEARN_WAITING,
  LEARN_RECEIVED,
  LEARN_SAVED
};

struct LearnedDevice {
  bool hasData;
  decode_type_t protocol;
  uint64_t value;
  uint16_t rawData[MAX_IR_BUFFER_SIZE];
  uint16_t rawDataLen;
  uint16_t address;
  uint16_t command;
};

void initIRLearning();

int getCurrentLearnDevice();

void setCurrentLearnDevice(int index);

void nextLearnDevice();

LearnState getLearnState();

void startLearning();

void checkIRReceive();

void saveLearnedSignal();

void sendLearnedSignal(int deviceIndex);

LearnedDevice getLearnedDevice(int index);

void clearLearnedDevice(int index);

void loadLearnedDevices();

void saveLearnedDevices();

const char* getLearnedDeviceName(int index);

#endif // IR_LEARNING_H
