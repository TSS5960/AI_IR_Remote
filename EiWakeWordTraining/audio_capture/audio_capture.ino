/*
 * Audio Capture for Edge Impulse Training
 *
 * This is a standalone sketch for collecting audio samples
 * to train your "Hey Bob" wake word model on Edge Impulse.
 *
 * Hardware: ESP32 + INMP441 I2S Microphone
 * Pins: WS=GPIO4, SCK=GPIO5, SD=GPIO6
 *
 * Usage:
 * 1. Upload this sketch to ESP32
 * 2. Open Serial Monitor (115200 baud)
 * 3. Use commands to record samples:
 *    - "hey_bob"  : Record wake word sample
 *    - "noise"    : Record background noise
 *    - "unknown"  : Record other speech
 *    - "test"     : Test microphone levels
 * 4. Run capture_samples.py on PC to save WAV files
 * 5. Upload WAV files to Edge Impulse
 */

#include <driver/i2s.h>

// ========== Pin Configuration ==========
#define MIC_WS_PIN    4     // Word Select (LRCLK)
#define MIC_SCK_PIN   5     // Serial Clock (BCLK)
#define MIC_SD_PIN    6     // Serial Data (DOUT)

// ========== Audio Configuration ==========
#define SAMPLE_RATE     16000   // 16kHz - matches Edge Impulse default
#define DURATION_MS     1500    // 1.5 seconds per sample
#define TOTAL_SAMPLES   (SAMPLE_RATE * DURATION_MS / 1000)  // 24000 samples

// I2S configuration
#define I2S_PORT I2S_NUM_0

// ========== Global Variables ==========
static bool micInitialized = false;
static String currentLabel = "";

// ========== Function Declarations ==========
bool initMicrophone();
void recordAndOutputWAV(const char* label);
void testMicrophoneLevel();
size_t readAudioSamples(int16_t* buffer, size_t count);

// ========== Setup ==========
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n");
  Serial.println("========================================");
  Serial.println("  Edge Impulse Audio Capture Tool");
  Serial.println("  For 'Hey Bob' Wake Word Training");
  Serial.println("========================================");
  Serial.println();

  // Initialize microphone
  if (initMicrophone()) {
    Serial.println("[OK] Microphone initialized");
  } else {
    Serial.println("[ERROR] Microphone initialization failed!");
    Serial.println("Check wiring: WS=GPIO4, SCK=GPIO5, SD=GPIO6");
  }

  Serial.println();
  Serial.println("Commands:");
  Serial.println("  hey_bob  - Record wake word sample");
  Serial.println("  noise    - Record background noise");
  Serial.println("  unknown  - Record other speech/words");
  Serial.println("  test     - Test microphone levels");
  Serial.println();
  Serial.println("Waiting for command...");
}

// ========== Main Loop ==========
void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();

    if (command == "hey_bob" || command == "heybob") {
      recordAndOutputWAV("hey_bob");
    }
    else if (command == "noise" || command == "background") {
      recordAndOutputWAV("noise");
    }
    else if (command == "unknown" || command == "other") {
      recordAndOutputWAV("unknown");
    }
    else if (command == "test" || command == "mic_test") {
      testMicrophoneLevel();
    }
    else if (command == "help") {
      Serial.println("\nCommands:");
      Serial.println("  hey_bob  - Record wake word sample");
      Serial.println("  noise    - Record background noise");
      Serial.println("  unknown  - Record other speech/words");
      Serial.println("  test     - Test microphone levels");
    }
    else if (command.length() > 0) {
      Serial.printf("Unknown command: %s\n", command.c_str());
      Serial.println("Type 'help' for available commands");
    }
  }
}

// ========== Microphone Initialization ==========
bool initMicrophone() {
  // I2S configuration for INMP441
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // INMP441 outputs 32-bit
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  // Pin configuration
  i2s_pin_config_t pin_config = {
    .bck_io_num = MIC_SCK_PIN,
    .ws_io_num = MIC_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD_PIN
  };

  // Install and configure I2S driver
  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S driver install failed: %d\n", err);
    return false;
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("I2S pin config failed: %d\n", err);
    i2s_driver_uninstall(I2S_PORT);
    return false;
  }

  // Start I2S
  i2s_start(I2S_PORT);
  i2s_zero_dma_buffer(I2S_PORT);

  micInitialized = true;
  return true;
}

// ========== Read Audio Samples ==========
size_t readAudioSamples(int16_t* buffer, size_t count) {
  size_t samplesRead = 0;
  int32_t sample32;
  size_t bytesRead;

  for (size_t i = 0; i < count; i++) {
    esp_err_t err = i2s_read(I2S_PORT, &sample32, sizeof(int32_t), &bytesRead, portMAX_DELAY);
    if (err != ESP_OK || bytesRead != sizeof(int32_t)) {
      break;
    }
    // Convert 32-bit to 16-bit (take upper 16 bits)
    buffer[i] = (int16_t)(sample32 >> 16);
    samplesRead++;
  }

  return samplesRead;
}

// ========== Record and Output WAV ==========
void recordAndOutputWAV(const char* label) {
  if (!micInitialized) {
    Serial.println("[ERROR] Microphone not initialized!");
    return;
  }

  // Allocate buffer for recording
  int16_t* samples = (int16_t*)malloc(TOTAL_SAMPLES * sizeof(int16_t));
  if (!samples) {
    Serial.println("[ERROR] Memory allocation failed!");
    Serial.printf("Needed: %d bytes\n", TOTAL_SAMPLES * sizeof(int16_t));
    return;
  }

  Serial.println();
  Serial.printf("=== Recording '%s' sample ===\n", label);
  Serial.println("Get ready...");
  delay(500);

  Serial.println("3...");
  delay(500);
  Serial.println("2...");
  delay(500);
  Serial.println("1...");
  delay(500);

  if (strcmp(label, "hey_bob") == 0) {
    Serial.println(">>> SAY 'HEY BOB' NOW! <<<");
  } else if (strcmp(label, "noise") == 0) {
    Serial.println(">>> RECORDING BACKGROUND NOISE <<<");
  } else {
    Serial.println(">>> SPEAK NOW! <<<");
  }

  // Record audio
  size_t samplesRecorded = 0;
  const size_t chunkSize = 512;
  int16_t chunk[chunkSize];
  unsigned long startTime = millis();

  while (samplesRecorded < TOTAL_SAMPLES) {
    size_t toRead = min(chunkSize, (size_t)(TOTAL_SAMPLES - samplesRecorded));
    size_t read = readAudioSamples(chunk, toRead);

    if (read > 0) {
      memcpy(&samples[samplesRecorded], chunk, read * sizeof(int16_t));
      samplesRecorded += read;

      // Progress dots
      if (samplesRecorded % 4000 == 0) {
        Serial.print(".");
      }
    }
  }

  unsigned long recordTime = millis() - startTime;
  Serial.println();
  Serial.printf("Recorded %d samples in %lu ms\n", samplesRecorded, recordTime);

  // Output WAV data
  Serial.println();
  Serial.println("Outputting WAV data...");
  Serial.printf("---WAV_START:%s---\n", label);

  // WAV header (44 bytes)
  uint32_t dataSize = TOTAL_SAMPLES * sizeof(int16_t);
  uint32_t fileSize = 36 + dataSize;

  uint8_t header[44] = {
    // RIFF header
    'R', 'I', 'F', 'F',
    (uint8_t)(fileSize & 0xFF),
    (uint8_t)((fileSize >> 8) & 0xFF),
    (uint8_t)((fileSize >> 16) & 0xFF),
    (uint8_t)((fileSize >> 24) & 0xFF),
    'W', 'A', 'V', 'E',

    // fmt subchunk
    'f', 'm', 't', ' ',
    16, 0, 0, 0,              // Subchunk1 size (16 for PCM)
    1, 0,                      // Audio format (1 = PCM)
    1, 0,                      // Number of channels (1 = mono)
    (uint8_t)(SAMPLE_RATE & 0xFF),
    (uint8_t)((SAMPLE_RATE >> 8) & 0xFF),
    (uint8_t)((SAMPLE_RATE >> 16) & 0xFF),
    (uint8_t)((SAMPLE_RATE >> 24) & 0xFF),
    (uint8_t)((SAMPLE_RATE * 2) & 0xFF),      // Byte rate
    (uint8_t)(((SAMPLE_RATE * 2) >> 8) & 0xFF),
    (uint8_t)(((SAMPLE_RATE * 2) >> 16) & 0xFF),
    (uint8_t)(((SAMPLE_RATE * 2) >> 24) & 0xFF),
    2, 0,                      // Block align
    16, 0,                     // Bits per sample

    // data subchunk
    'd', 'a', 't', 'a',
    (uint8_t)(dataSize & 0xFF),
    (uint8_t)((dataSize >> 8) & 0xFF),
    (uint8_t)((dataSize >> 16) & 0xFF),
    (uint8_t)((dataSize >> 24) & 0xFF)
  };

  // Output header as hex
  for (int i = 0; i < 44; i++) {
    Serial.printf("%02X", header[i]);
  }

  // Output audio data as hex (little-endian)
  for (size_t i = 0; i < TOTAL_SAMPLES; i++) {
    Serial.printf("%02X%02X", samples[i] & 0xFF, (samples[i] >> 8) & 0xFF);

    // Yield periodically to prevent watchdog timeout
    if (i % 1000 == 0) {
      yield();
    }
  }

  Serial.println();
  Serial.println("---WAV_END---");
  Serial.println();

  // Free memory
  free(samples);

  Serial.printf("[OK] '%s' sample complete!\n", label);
  Serial.println("Run capture_samples.py on PC to save the WAV file");
  Serial.println();
  Serial.println("Waiting for next command...");
}

// ========== Test Microphone Level ==========
void testMicrophoneLevel() {
  if (!micInitialized) {
    Serial.println("[ERROR] Microphone not initialized!");
    return;
  }

  Serial.println();
  Serial.println("=== Microphone Level Test ===");
  Serial.println("Speak to see audio levels");
  Serial.println("Press any key to stop");
  Serial.println();

  const size_t sampleCount = 256;
  int16_t samples[sampleCount];

  unsigned long testDuration = 10000;  // 10 seconds
  unsigned long startTime = millis();

  while (millis() - startTime < testDuration) {
    // Check for stop command
    if (Serial.available()) {
      Serial.read();
      break;
    }

    // Read samples
    size_t read = readAudioSamples(samples, sampleCount);

    if (read > 0) {
      // Calculate RMS level
      int64_t sum = 0;
      for (size_t i = 0; i < read; i++) {
        int32_t val = samples[i];
        sum += val * val;
      }
      int32_t rms = sqrt(sum / read);
      int level = (rms * 100) / 32768;
      level = constrain(level, 0, 100);

      // Display bar graph
      Serial.printf("Level: %3d%% [", level);
      int bars = level / 2;
      for (int i = 0; i < 50; i++) {
        Serial.print(i < bars ? "=" : " ");
      }
      Serial.println("]");
    }

    delay(100);
  }

  Serial.println();
  Serial.println("Microphone test complete");
  Serial.println("Waiting for next command...");
}
