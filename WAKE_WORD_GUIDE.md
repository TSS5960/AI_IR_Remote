# Wake Word Detection - "Hey Bob"

## Overview
Your AC Remote now has voice activation! The system continuously listens for the wake word **"Hey Bob"** and triggers an action when detected.

## How It Works

### Hardware
- **INMP441 Microphone** on I2S_NUM_0
  - GPIO4 → WS (Word Select)
  - GPIO5 → SCK (Serial Clock)
  - GPIO6 → SD (Serial Data)

### Default Behavior
When you say **"Hey Bob"**, the system will:
1. Turn ON the built-in LED (LED_BUILTIN)
2. Play a confirmation beep
3. Keep LED on for 3 seconds
4. Turn OFF the LED automatically

## Getting Started

### 1. Upload the Code
The wake word detection starts automatically when the system boots.

### 2. Test It Out
Simply say **"Hey Bob"** clearly, and watch for:
- Serial message: `*** WAKE WORD DETECTED: Hey Bob! ***`
- Built-in LED lights up for 3 seconds
- Audio confirmation beep

### 3. Train Custom Pattern (Optional)

For better accuracy, you can train the system with your voice:

```cpp
// In Arduino Serial Monitor, send command:
train
```

Or call the function programmatically:
```cpp
trainWakeWord();
```

Then say "Hey Bob" clearly when prompted.

## Customization

### Change the Action

Edit the `onWakeWordDetected()` callback function in [AC_IR_Remote.ino](AC_IR_Remote.ino):

```cpp
void onWakeWordDetected() {
  Serial.println("\n*** WAKE WORD DETECTED: Hey Bob! ***");
  
  // Your custom action here!
  // Examples:
  
  // Turn on AC
  acSetPower(true);
  
  // Turn on display
  setScreen(SCREEN_CLOCK);
  
  // Trigger specific function
  // yourCustomFunction();
  
  // LED control (already included)
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  wakeWordLedActive = true;
  wakeWordLedOnTime = millis();
  
  playActionTone();
}
```

### Adjust Detection Sensitivity

In [mic_control.cpp](mic_control.cpp), modify:

```cpp
// Line ~227: Cooldown between detections
static const unsigned long WAKE_WORD_COOLDOWN = 2000; // 2 seconds

// Line ~271: Pattern matching threshold
if (matchScore >= 6) { // Lower = more sensitive, Higher = less false positives
```

### Change Wake Word Pattern

The system uses a simple audio energy pattern detector. To detect a different phrase:

1. Train with the new phrase:
   ```cpp
   trainWakeWord();  // Say your new phrase
   ```

2. Or modify the default pattern in [mic_control.cpp](mic_control.cpp):
   ```cpp
   // Line ~263: Default pattern
   int pattern[PATTERN_BUFFER_SIZE] = {5, 8, 12, 45, 50, 25, 48, 52, 15, 8};
   ```

## API Reference

### Functions Available

```cpp
// Start wake word detection (called automatically in setup)
bool startWakeWordDetection();

// Stop wake word detection
void stopWakeWordDetection();

// Check if active
bool isWakeWordDetectionActive();

// Train custom pattern
void trainWakeWord();

// Set your own callback function
void setWakeWordCallback(WakeWordCallback callback);
```

### Example: Multiple Actions

```cpp
void onWakeWordDetected() {
  Serial.println("Wake word detected!");
  
  // Flash LED pattern
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }
  
  // Turn on AC at comfortable temperature
  acSetPower(true);
  acSetTemp(24);
  acSetMode(MODE_COOL);
  
  // Show status on display
  setScreen(SCREEN_MAIN);
  
  // Voice feedback
  playVoice(VOICE_READY);
}
```

## Troubleshooting

### LED doesn't light up
1. Check Serial Monitor for `*** WAKE WORD DETECTED ***` message
2. Verify microphone is initialized: Look for `[Microphone] OK: INMP441 initialized successfully`
3. Check wiring: GPIO4=WS, GPIO5=SCK, GPIO6=SD

### False detections
- Increase cooldown time
- Train with your voice pattern
- Adjust match threshold (increase the value)

### Not detecting
- Speak louder and clearer
- Train the wake word pattern
- Move closer to the microphone
- Decrease match threshold (lower the value)
- Check microphone connection

### Training doesn't work
- Ensure microphone is initialized
- Speak clearly and at normal volume
- Say "Hey Bob" smoothly (not too fast or slow)
- Try multiple training attempts

## Performance Notes

- **Latency**: ~200-500ms from speaking to detection
- **CPU Usage**: Minimal (runs in main loop)
- **Memory**: ~256 bytes for audio buffers
- **Power**: No significant impact (microphone is always on)

## Advanced: Integration with Cloud Speech Recognition

For more accurate wake word detection, you can integrate with cloud services:

1. **Google Cloud Speech-to-Text**
2. **AWS Transcribe**
3. **Azure Speech Services**

When wake word is detected, stream audio to cloud service for full command recognition.

Example flow:
```
"Hey Bob" → LED lights → Start recording → 
Stream to cloud → Get command → Execute action
```

## Serial Commands

Add these to your serial command handler:

```cpp
if (cmd == "train") {
  trainWakeWord();
}
if (cmd == "wake_start") {
  startWakeWordDetection();
}
if (cmd == "wake_stop") {
  stopWakeWordDetection();
}
```

---

**Note**: The current implementation uses simple audio energy pattern matching. For production use or better accuracy, consider integrating a dedicated wake word detection library like:
- Porcupine (Picovoice)
- Snowboy
- PocketSphinx
- Edge Impulse trained model
