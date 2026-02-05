/*
 * Speaker Control - Implementation
 * Voice feedback using MAX98357 I2S amplifier
 */

#include "speaker_control.h"
#include <Arduino.h>
#include <driver/i2s.h>

// I2S configuration for MAX98357
#define I2S_SPEAKER_PORT I2S_NUM_1

bool speakerInitialized = false;
int currentVolume = SPEAKER_VOLUME;

// Initialize I2S for speaker
void initSpeaker() {
  Serial.println("\n[Speaker] Initializing MAX98357...");
  
  // I2S configuration for MAX98357
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SPEAKER_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  
  // Pin configuration for MAX98357
  i2s_pin_config_t pin_config = {
    .bck_io_num = SPK_BCLK_PIN,      // GPIO15
    .ws_io_num = SPK_LRCLK_PIN,      // GPIO16
    .data_out_num = SPK_SD_PIN,      // GPIO7
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  
  // Install I2S driver
  esp_err_t err = i2s_driver_install(I2S_SPEAKER_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[Speaker] FAIL: I2S driver install failed: %d\n", err);
    return;
  }
  
  err = i2s_set_pin(I2S_SPEAKER_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("[Speaker] FAIL: I2S pin config failed: %d\n", err);
    return;
  }
  
  // Set clock
  i2s_set_clk(I2S_SPEAKER_PORT, SPEAKER_SAMPLE_RATE, 
              I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  
  speakerInitialized = true;
  Serial.println("[Speaker] OK: MAX98357 initialized");
  Serial.printf("[Speaker]   BCLK: GPIO%d\n", SPK_BCLK_PIN);
  Serial.printf("[Speaker]   LRCLK: GPIO%d\n", SPK_LRCLK_PIN);
  Serial.printf("[Speaker]   DIN: GPIO%d\n", SPK_SD_PIN);
  Serial.printf("[Speaker]   Sample rate: %d Hz\n", SPEAKER_SAMPLE_RATE);
  
  // Play startup beep
  delay(500);
  playBeep(1000, 100);
  delay(100);
  playBeep(1200, 100);
  Serial.println("[Speaker] OK: Startup sound played");
}

// Play a simple beep
void playBeep(int frequency, int duration) {
  if (!speakerInitialized) {
    Serial.println("[Speaker] FAIL: Not initialized");
    return;
  }
  
  const int sampleRate = SPEAKER_SAMPLE_RATE;
  const int samples = (sampleRate * duration) / 1000;
  int16_t* buffer = (int16_t*)malloc(samples * sizeof(int16_t) * 2); // Stereo
  
  if (!buffer) {
    Serial.println("[Speaker] FAIL: Memory allocation failed");
    return;
  }
  
  // Generate sine wave
  for (int i = 0; i < samples; i++) {
    float t = (float)i / sampleRate;
    int16_t sample = (int16_t)(sin(2.0 * PI * frequency * t) * 8000 * currentVolume / 100);
    buffer[i * 2] = sample;     // Left channel
    buffer[i * 2 + 1] = sample; // Right channel
  }
  
  // Write to I2S
  size_t bytes_written = 0;
  i2s_write(I2S_SPEAKER_PORT, buffer, samples * sizeof(int16_t) * 2, 
            &bytes_written, portMAX_DELAY);
  
  free(buffer);
}

// Play a short action confirmation tone
void playActionTone() {
  playBeep(1500, 60);
}

// Play voice feedback
void playVoice(VoiceFeedback voice) {
  if (!speakerInitialized) return;
  
  // Voice names for debugging
  const char* voiceNames[] = {
    "Power ON", "Power OFF", "Temp Up", "Temp Down",
    "Cooling", "Heating", "Dry", "Fan", "Auto",
    "Fan Low", "Fan Med", "Fan High", "Fan Auto", "Ready"
  };
  
  Serial.printf("[Speaker] Voice: %s\n", voiceNames[voice]);
  
  // Play different beep patterns for different voices
  switch(voice) {
    case VOICE_POWER_ON:
      playBeep(800, 100);
      delay(50);
      playBeep(1000, 100);
      delay(50);
      playBeep(1200, 150);
      break;
      
    case VOICE_POWER_OFF:
      playBeep(1200, 100);
      delay(50);
      playBeep(1000, 100);
      delay(50);
      playBeep(800, 150);
      break;
      
    case VOICE_TEMP_UP:
      playBeep(1000, 80);
      delay(40);
      playBeep(1200, 80);
      break;
      
    case VOICE_TEMP_DOWN:
      playBeep(1200, 80);
      delay(40);
      playBeep(1000, 80);
      break;
      
    case VOICE_MODE_COOL:
      playBeep(900, 100);
      delay(50);
      playBeep(900, 100);
      break;
      
    case VOICE_MODE_HEAT:
      playBeep(1100, 100);
      delay(50);
      playBeep(1100, 100);
      break;
      
    case VOICE_MODE_DRY:
      playBeep(1000, 150);
      break;
      
    case VOICE_MODE_FAN:
      playBeep(1050, 150);
      break;
      
    case VOICE_MODE_AUTO:
      playBeep(950, 150);
      break;
      
    case VOICE_FAN_LOW:
      playBeep(800, 200);
      break;
      
    case VOICE_FAN_MED:
      playBeep(900, 200);
      break;
      
    case VOICE_FAN_HIGH:
      playBeep(1000, 200);
      break;
      
    case VOICE_FAN_AUTO:
      playBeep(850, 200);
      break;
      
    case VOICE_READY:
      playBeep(1000, 100);
      delay(50);
      playBeep(1200, 100);
      break;
      
    default:
      playBeep(1000, 100);
      break;
  }
}

// Play temperature (beeps = temp / 5)
void playTemperature(int temp) {
  if (!speakerInitialized) return;
  
  Serial.printf("[Speaker] Temperature: %dC\n", temp);
  
  // Play beeps (one beep per 5 degrees)
  int beeps = temp / 5;
  for (int i = 0; i < beeps; i++) {
    playBeep(1000, 80);
    delay(100);
  }
}

// Set volume (0-100)
void setSpeakerVolume(int volume) {
  currentVolume = constrain(volume, 0, 100);
  Serial.printf("[Speaker] Volume set to: %d%%\n", currentVolume);
}

// Get current volume
int getSpeakerVolume() {
  return currentVolume;
}

// Stop playback
void stopSpeaker() {
  if (speakerInitialized) {
    i2s_zero_dma_buffer(I2S_SPEAKER_PORT);
  }
}

// Test speaker with different sounds
void testSpeaker() {
  Serial.println("\n[Speaker] Testing speaker...");
  Serial.println("          Playing test sounds:");
  
  Serial.println("  1. Low beep (500 Hz)");
  playBeep(500, 200);
  delay(300);
  
  Serial.println("  2. Mid beep (1000 Hz)");
  playBeep(1000, 200);
  delay(300);
  
  Serial.println("  3. High beep (2000 Hz)");
  playBeep(2000, 200);
  delay(300);
  
  Serial.println("  4. Ascending tones");
  for (int freq = 500; freq <= 2000; freq += 100) {
    playBeep(freq, 50);
    delay(50);
  }
  delay(300);
  
  Serial.println("  5. Voice samples");
  playVoice(VOICE_POWER_ON);
  delay(500);
  playVoice(VOICE_POWER_OFF);
  delay(500);
  playVoice(VOICE_TEMP_UP);
  delay(500);
  
  Serial.println("[Speaker] OK: Test complete!\n");
}
