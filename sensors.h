/*
 * Sensors - Header
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "config.h"

struct SensorData {
  bool motionDetected;

  float dht_temperature;
  float dht_humidity;
  bool dht_valid;

  float light_lux;
  bool light_valid;
};

void initSensors();

SensorData readAllSensors();

bool readPIR();

bool readDHT11(float &temperature, float &humidity);

bool readGY30(float &lux);

void printSensorData(const SensorData &data);

void testSensors();

#endif // SENSORS_H
