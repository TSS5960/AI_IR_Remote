// Example: Using the INMP441 Microphone
// 
// This example shows how to use the microphone functions defined in mic_control.h

// Basic usage in your main code:

void exampleMicrophoneUsage() {
  // 1. Microphone is automatically initialized in setup()
  
  // 2. Start recording
  if (startRecording()) {
    Serial.println("Recording started!");
    
    // 3. Read audio samples
    int16_t audioBuffer[MIC_BUFFER_SIZE];
    size_t bytesRead = readAudioSamples(audioBuffer, MIC_BUFFER_SIZE);
    
    Serial.printf("Read %d bytes of audio data\n", bytesRead);
    
    // 4. Get audio level (0-100)
    int level = getAudioLevel();
    Serial.printf("Audio level: %d%%\n", level);
    
    // 5. Stop recording when done
    stopRecording();
  }
}

// Example: Voice activation detection
void voiceActivationExample() {
  if (!isMicrophoneReady()) {
    Serial.println("Microphone not ready!");
    return;
  }
  
  startRecording();
  
  while (true) {
    int audioLevel = getAudioLevel();
    
    // Threshold for voice activation (adjust as needed)
    if (audioLevel > 20) {
      Serial.println("Voice detected!");
      // Handle voice command here
      break;
    }
    
    delay(100);  // Check every 100ms
  }
  
  stopRecording();
}

// Example: Audio streaming
void audioStreamExample() {
  const size_t BUFFER_SIZE = 512;
  int16_t audioBuffer[BUFFER_SIZE];
  
  if (!isMicrophoneReady()) {
    Serial.println("Microphone not ready!");
    return;
  }
  
  startRecording();
  
  // Stream for 10 seconds
  unsigned long startTime = millis();
  while (millis() - startTime < 10000) {
    size_t bytesRead = readAudioSamples(audioBuffer, BUFFER_SIZE);
    
    if (bytesRead > 0) {
      // Process audio data here
      // e.g., send to cloud for speech recognition
      // or perform local audio processing
      Serial.printf("Streaming: %d bytes\n", bytesRead);
    }
    
    delay(10);
  }
  
  stopRecording();
}

// Note: The microphone uses I2S_NUM_0, while the speaker uses I2S_NUM_1
// Both can operate simultaneously without conflicts
