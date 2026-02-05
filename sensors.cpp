/*
 * Sensors - Implementation
 */

#include "sensors.h"
#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>

DHT dht(DHT_PIN, DHT11);

BH1750 lightMeter;
bool lightInitialized = false;

void initSensors() {
  Serial.println("\n========================================");
  Serial.println("  SENSOR INITIALIZATION START");
  Serial.println("========================================");

  pinMode(PIR_PIN, INPUT);
  Serial.printf("[Sensors] PIR sensor initialized on GPIO%d\n", PIR_PIN);
  Serial.printf("[Sensors] PIR initial state: %s\n", digitalRead(PIR_PIN) ? "HIGH" : "LOW");

  Serial.printf("[Sensors] Initializing DHT11 on GPIO%d...\n", DHT_PIN);
  dht.begin();
  delay(2000);  // DHT11 stabilization time

  Serial.println("[Sensors] OK: DHT11 initialized (waiting 2s for stabilization)");
  Serial.printf("[Sensors] Initializing GY-30: SDA=GPIO%d, SCL=GPIO%d\n", GY30_SDA_PIN, GY30_SCL_PIN);
  Wire.begin(GY30_SDA_PIN, GY30_SCL_PIN);
  Wire.setClock(100000);
  delay(100);
  lightInitialized = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire);
  if (lightInitialized) {
    Serial.println("[Sensors] OK: GY-30 initialized");
  } else {
    Serial.println("[Sensors] FAIL: GY-30 initialization failed!");
    Serial.println("[Sensors]   Check SDA/SCL wiring and 3.3V power");
  }

  Serial.println("\n========================================");
  Serial.println("  SENSOR INITIALIZATION COMPLETE");
  Serial.println("========================================\n");

  delay(100);
}

bool readPIR() {
  return digitalRead(PIR_PIN) == HIGH;
}

bool readDHT11(float &temperature, float &humidity) {
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("[Sensors] FAIL: DHT11 read failed!");
    Serial.println("[Sensors]   Possible issues:");
    Serial.printf("[Sensors]   1. Check DHT11 connection to GPIO%d\n", DHT_PIN);
    Serial.println("[Sensors]   2. Add 4.7K-10K pull-up resistor (DATA to VCC)");
    Serial.println("[Sensors]   3. Check VCC = 3.3V or 5V");
    Serial.println("[Sensors]   4. Wait at least 2 seconds between reads");
    Serial.println("[Sensors]   5. Try another DHT11 sensor (may be damaged)");
    return false;
  }

  if (temperature < -40 || temperature > 80) {
    Serial.printf("[Sensors] FAIL: DHT11 temperature out of range: %.1fC\n", temperature);
    return false;
  }

  if (humidity < 0 || humidity > 100) {
    Serial.printf("[Sensors] FAIL: DHT11 humidity out of range: %.1f%%\n", humidity);
    return false;
  }

  return true;
}

bool readGY30(float &lux) {
  if (!lightInitialized) {
    Serial.println("[Sensors] GY-30 not initialized!");
    return false;
  }

  lux = lightMeter.readLightLevel();
  if (lux < 0.0f || isnan(lux)) {
    Serial.println("[Sensors] FAIL: GY-30 read failed!");
    return false;
  }

  return true;
}

SensorData readAllSensors() {
  SensorData data;
  data.motionDetected = readPIR();
  data.dht_valid = readDHT11(data.dht_temperature, data.dht_humidity);
  data.light_valid = readGY30(data.light_lux);

  return data;
}

void printSensorData(const SensorData &data) {
  Serial.println("\n========================================");
  Serial.println("  SENSOR DATA");
  Serial.println("========================================");
  Serial.println();

  // PIR Motion Sensor
  Serial.println("  PIR Motion Sensor:");
  Serial.println("  ----------------------------------------");
  Serial.printf("  Motion: %s\n", data.motionDetected ? "DETECTED" : "None");
  Serial.println();

  // DHT11 Temperature & Humidity
  Serial.println("  DHT11 (Temp & Humidity):");
  Serial.println("  ----------------------------------------");
  if (data.dht_valid) {
    Serial.printf("  Temperature: %.1fC\n", data.dht_temperature);
    Serial.printf("  Humidity: %.1f%%\n", data.dht_humidity);
  } else {
    Serial.println("  Status: READ FAILED");
  }
  Serial.println();

  // GY-30 Light Sensor
  Serial.println("  GY-30 (Light):");
  Serial.println("  ----------------------------------------");
  if (data.light_valid) {
    Serial.printf("  Lux: %.1f lx\n", data.light_lux);
  } else {
    Serial.println("  Status: READ FAILED or NOT INITIALIZED");
  }
  Serial.println("  ----------------------------------------");
  Serial.println();
}
