/*
 * Configuration File
 * Pin definitions and constants
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ========== Feature Flags ==========

// Set to 1 to enable legacy AWS IoT module
#define USE_AWS_IOT 0

// Set to 1 to enable MQTT broker control (EMQX public broker by default)
#define USE_MQTT_BROKER 1

// ========== MQTT Broker Configuration ==========

#define MQTT_BROKER_HOST "broker.emqx.io"
#define MQTT_BROKER_PORT 1883
#define MQTT_PUBLISH_TOPIC "ac/status"
#define MQTT_SUBSCRIBE_TOPIC "ac/command"
#define MQTT_CLIENT_ID "ESP32_AC_Remote_001"

// ========== Pin Definitions ==========

// Display pins (ST7789)
#define TFT_BL_PIN    42    // Backlight
// Other pins defined in User_Setup.h
// SCLK: 21, MOSI: 47, DC: 40, RST: 45

// IR pins
#define IR_TX_PIN     8     // IR transmitter
#define IR_RX_PIN     9     // IR receiver

// Sensor pins
#define PIR_PIN       10    // PIR motion sensor
#define DHT_PIN       17    // DHT11 temperature & humidity sensor
#define GY30_SDA_PIN  14    // GY-30 (BH1750) I2C data
#define GY30_SCL_PIN  13    // GY-30 (BH1750) I2C clock

// Speaker pins (I2S MAX98357)
#define SPK_SD_PIN    7     // Speaker DIN (data)
#define SPK_BCLK_PIN  15    // Speaker BCLK (bit clock)
#define SPK_LRCLK_PIN 16    // Speaker LRCLK (left-right clock)

// ========== AC Parameters ==========

#define AC_TEMP_MIN   16    // Minimum temperature
#define AC_TEMP_MAX   30    // Maximum temperature
#define AC_TEMP_DEFAULT 24  // Default temperature

// Auto dry mode (humidity threshold)
#define AUTO_DRY_THRESHOLD_DEFAULT 65
#define AUTO_DRY_THRESHOLD_MIN 40
#define AUTO_DRY_THRESHOLD_MAX 85
#define AUTO_DRY_HYSTERESIS 3

// Sleep mode (light threshold to lower fan speed)
#define SLEEP_LIGHT_THRESHOLD_DEFAULT 30
#define SLEEP_LIGHT_THRESHOLD_MIN 1
#define SLEEP_LIGHT_THRESHOLD_MAX 1000
#define SLEEP_LIGHT_HYSTERESIS 5

// AC modes
enum ACMode {
  MODE_AUTO = 0,    // Auto
  MODE_COOL = 1,    // Cooling
  MODE_HEAT = 2,    // Heating
  MODE_DRY = 3,     // Dehumidify
  MODE_FAN = 4      // Fan only
};

// Fan speeds
enum FanSpeed {
  FAN_AUTO = 0,     // Auto
  FAN_LOW = 1,      // Low
  FAN_MED = 2,      // Medium
  FAN_HIGH = 3      // High
};

// Supported AC brands
enum ACBrand {
  BRAND_DAIKIN = 0,      // Daikin
  BRAND_MITSUBISHI,      // Mitsubishi
  BRAND_PANASONIC,       // Panasonic
  BRAND_GREE,            // Gree
  BRAND_MIDEA,           // Midea
  BRAND_HAIER,           // Haier
  BRAND_SAMSUNG,         // Samsung
  BRAND_LG,              // LG
  BRAND_FUJITSU,         // Fujitsu
  BRAND_HITACHI,         // Hitachi
  BRAND_COUNT            // Total brand count
};

// AC state structure
struct ACState {
  bool power;           // Power state
  int temperature;      // Temperature (16-30°C)
  ACMode mode;          // Mode
  FanSpeed fanSpeed;    // Fan speed
  ACBrand brand;        // Brand
};

// ========== Display Parameters ==========

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240

// Color definitions
#define COLOR_BG      0x0000  // Black background
#define COLOR_TEXT    0xFFFF  // White text
#define COLOR_TITLE   0x07FF  // Cyan title
#define COLOR_TEMP    0xFFE0  // Yellow temperature
#define COLOR_ON      0x07E0  // Green (power on)
#define COLOR_OFF     0xF800  // Red (power off)
#define COLOR_MODE    0xFBE0  // Orange mode

// ========== Speaker Parameters ==========

#define SPEAKER_SAMPLE_RATE   16000   // Sample rate 16kHz
#define SPEAKER_VOLUME        50      // Default volume (0-100)

// ========== NTP Time Configuration ==========

// If NTP is slow, switch to a closer/local server (e.g. ntp.aliyun.com).
#define NTP_SERVER_PRIMARY "time.cloudflare.com"
#define NTP_SERVER_BACKUP1 "time.nist.gov"
#define NTP_SERVER_BACKUP2 "pool.ntp.org"

#define NTP_SYNC_INTERVAL_MS 15000
#define NTP_SYNC_TIMEOUT_MS 10000

#define GMT_OFFSET_SEC     28800      // GMT+8 (Malaysia/Singapore) = 8 * 3600
#define DAYLIGHT_OFFSET_SEC 0         // No daylight saving

#endif // CONFIG_H
