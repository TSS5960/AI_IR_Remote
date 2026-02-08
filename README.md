# ESP32-S3 Smart AC Remote Control with Voice Integration

An intelligent air conditioner remote control system based on ESP32-S3 with voice wake word detection (Edge Impulse), IR learning, cloud connectivity, and multi-sensor integration.

## Features

### Core Functionality
- **WiFi Captive Portal**: Easy WiFi setup via web interface (no hardcoded credentials)
- **IR Control**: Send IR commands to control air conditioners (10+ brands supported)
- **IR Learning**: Capture and replay any IR remote signals (up to 40 signals)
- **Voice Wake Word**: "Hey Bob" wake word detection using Edge Impulse ML
- **Multi-Screen Display**: TFT interface with 7 different screens
- **Environmental Monitoring**: Temperature, humidity, light, and motion sensors
- **Cloud Integration**: MQTT broker and Firebase real-time database
- **Web Dashboard**: Cloudflare-hosted control panel
- **Alarm System**: Time-based alarms with speaker notifications

### Voice Features
- **Edge Impulse Wake Word**: On-device ML model for "Hey Bob" detection
  - Runs entirely on ESP32 (no cloud required)
  - ~100ms inference latency
  - Adjustable confidence threshold (default 80%)
- **Speech Recognition**: Experimental integration with Groq console
  - Uses console.groq.com for speech-to-text transcription
  - Currently in experimental phase
  - Requires internet connection
- **Record & Playback**: 3-second audio recording with instant playback
- **Voice Level Monitor**: Real-time audio RMS visualization
- **LED Feedback**: NeoPixel RGB LED indicates wake word detection and processing states

## Hardware Components

### Main Controller
- **ESP32-S3-N16R8** - Dual-core 240MHz with 16MB Flash, 8MB PSRAM

### Audio System
- **INMP441 I2S MEMS Microphone** (GPIO4=WS, GPIO5=SCK, GPIO6=SD)
- **MAX98357 I2S Amplifier + Speaker** (GPIO15=BCLK, GPIO16=LRCLK, GPIO7=DIN)
- **Adafruit NeoPixel RGB LED** (GPIO48)

### Display & Input
- **ST7789 TFT Display** (240x280, SPI)
- **Analog Joystick** (X/Y axes + button)

### Sensors
- **BH1750 Light Sensor** (I2C)
- **DHT11 Temperature & Humidity Sensor**
- **PIR Motion Sensor**

### IR System
- **IR Transmitter LED** (GPIO12)
- **IR Receiver Module** (GPIO11)

## Project Structure

```
AC_IR_Remote/
├── AC_IR_Remote.ino          # Main firmware
├── config.h                   # Pin assignments and configuration
├── secrets.h                  # Firebase credentials (not in git)
├── secrets.h.template         # Template for secrets configuration
├── firebase_config.h          # Firebase paths (not in git)
├── firebase_config.h.template # Template for Firebase configuration
│
├── # Core modules
├── ac_control.cpp/h           # AC IR control logic
├── ir_control.cpp/h           # IR transmitter/receiver
├── ir_learning_enhanced.cpp/h # Enhanced IR learning mode
├── mic_control.cpp/h          # Microphone I2S driver
├── speaker_control.cpp/h      # Audio playback
├── button_control.cpp/h       # Joystick input handling
├── display.cpp/h              # TFT screen management
├── sensors.cpp/h              # Environmental sensors
├── alarm_manager.cpp/h        # Alarm system
│
├── # Edge Impulse Wake Word
├── ei_wake_word.cpp/h         # Edge Impulse integration
│
├── # Network modules
├── wifi_manager.cpp/h         # WiFi connectivity
├── mqtt_broker.cpp/h          # MQTT client
├── firebase_client.cpp/h      # Firebase integration
│
├── # Training Tools
├── EiWakeWordTraining/        # Audio sample collection tools
│   ├── audio_capture.ino      # ESP32 sketch for recording samples
│   └── capture_samples.py     # Python script to save WAV files
│
├── guide/                     # Documentation (gitignored)
│   └── EDGE_IMPULSE_GUIDE.md  # Complete Edge Impulse setup guide
│
└── cloudflare_dashboard/      # Web dashboard
    ├── src/index.js           # Cloudflare Worker
    ├── public/
    │   ├── index.html         # Dashboard UI
    │   ├── app.js             # Frontend logic
    │   └── styles.css         # Styling
    ├── wrangler.toml          # Cloudflare configuration (not in git)
    └── wrangler.toml.template # Template for Cloudflare configuration
```

## Getting Started

### 1. Hardware Setup

**Audio Connections:**
```
INMP441 → ESP32-S3
  VDD → 3.3V
  GND → GND
  WS  → GPIO4
  SCK → GPIO5
  SD  → GPIO6
  L/R → GND

MAX98357 → ESP32-S3
  VIN → 5V
  GND → GND
  BCLK → GPIO15
  LRC  → GPIO16
  DIN  → GPIO7

NeoPixel → ESP32-S3
  VDD → 5V
  GND → GND
  DIN → GPIO48
```

### 2. Software Setup

#### Arduino IDE Configuration
1. Install ESP32 board support: https://espressif.github.io/arduino-esp32/
2. Select Board: **ESP32S3 Dev Module**
3. Install required libraries:
   - `Adafruit_NeoPixel`
   - `TFT_eSPI`
   - `IRremoteESP8266`
   - `DHT sensor library`
   - `BH1750`
   - `Firebase ESP32 Client`
   - `PubSubClient`
   - **Edge Impulse library** (see Wake Word Setup below)

#### Upload Firmware
1. Connect ESP32-S3 via USB
2. Select correct port in Arduino IDE
3. Upload `AC_IR_Remote.ino`
4. Open Serial Monitor at **115200 baud**

### 3. Wake Word Setup (Edge Impulse)

The wake word detection uses Edge Impulse for on-device machine learning. Follow these steps to train your own "Hey Bob" model:

#### Step 1: Collect Training Samples

Use the tools in `EiWakeWordTraining/` folder:

1. **Upload the capture sketch:**
   ```
   Open EiWakeWordTraining/audio_capture.ino in Arduino IDE
   Upload to ESP32
   Close Serial Monitor after upload
   ```

2. **Install Python dependencies:**
   ```bash
   pip install pyserial
   ```

3. **Run the capture script:**
   ```bash
   cd EiWakeWordTraining
   python capture_samples.py
   ```

4. **Record samples interactively:**
   ```
   Commands:
     1 or h  - Record 'hey_bob' sample (say "Hey Bob")
     2 or n  - Record 'noise' sample (background sounds)
     3 or u  - Record 'unknown' sample (other words)
     t       - Test microphone level
     s       - Show sample counts
     q       - Quit
   ```

5. **Recommended sample counts:**
   - `hey_bob`: 50-100 samples (vary your voice, distance, speed)
   - `noise`: 200+ samples (silence, fan noise, TV, typing)
   - `unknown`: 30-50 samples ("Hello", "Hey John", "OK Google", etc.)

6. **Samples are saved to:** `EiWakeWordTraining/training_samples/`

**Important:** Close Arduino IDE Serial Monitor before running Python script - only one program can use the COM port.

#### Step 2: Train on Edge Impulse

1. Create account at [edgeimpulse.com](https://edgeimpulse.com)
2. Create new project: "Hey Bob Wake Word"
3. Upload WAV files from `training_samples/` folders with correct labels
4. Create Impulse:
   - Window size: 1500ms, Stride: 500ms, Frequency: 16000Hz
   - Processing: Audio (MFCC)
   - Learning: Classification or Transfer Learning
5. Train model (target >90% accuracy)
6. Deploy as Arduino library (Quantized int8)
7. Download the `.zip` file

#### Step 3: Install Library

1. In Arduino IDE: **Sketch** → **Include Library** → **Add .ZIP Library**
2. Select the downloaded Edge Impulse library
3. Re-upload `AC_IR_Remote.ino`

The wake word detection will automatically start on boot.

### 4. WiFi Setup (Captive Portal)

1. **First boot** - ESP32 creates WiFi AP: `ESP32_AC_Remote`
2. **Connect to AP** - WiFi password: `12345678`
3. **Configure WiFi** - Browser auto-opens configuration page
   - If not, navigate to `http://192.168.4.1`
   - Enter your WiFi SSID and password
4. **Device reboots** and connects to your WiFi network

### 5. Firebase Setup (Optional)

1. Copy templates:
   ```bash
   cp firebase_config.h.template firebase_config.h
   cp secrets.h.template secrets.h
   ```

2. Edit with your Firebase credentials

## Usage

### Serial Commands

**Wake Word (Edge Impulse):**
- `voice` - Show real-time voice level monitor
- `repeat` - Record 3 seconds and play back
- `wake_start` - Start wake word detection
- `wake_stop` - Stop wake word detection
- `wake_threshold` - Show current confidence threshold
- `wake_threshold N` - Set threshold (0-100%)

**IR Commands:**
- `learn` - Enter IR learning mode
- `I1` to `I40` - Send learned IR signal 1-40
- `irtest` - Test IR receiver

**AC Control:**
- `1` / `0` - Turn AC on/off
- `+` / `-` - Increase/decrease temperature
- `m` - Cycle through modes (cool/heat/dry/fan/auto)
- `f` - Cycle through fan speeds

**System Commands:**
- `h` - Show help
- `s` - Show system status
- `test` - Test speaker

**Alarm Management:**
- `a HH MM [name]` - Add alarm (e.g., `a 07 30 Morning`)
- `u INDEX HH MM [name]` - Update alarm
- `d INDEX` - Delete alarm
- `p` - List all alarms

### Display Screens

Navigate using joystick (left/right):
1. **Volume** - Speaker volume control
2. **Clock** - Current time display
3. **Network** - WiFi and Firebase status
4. **AC Control** - Temperature and mode
5. **IR Learn** - IR learning interface
6. **Sensors** - Environmental data
7. **Alarm** - Active alarm display

### Wake Word Detection

When "Hey Bob" is detected:
1. Serial prints detection with confidence percentage
2. NeoPixel LED turns white for 3 seconds
3. Confirmation beep plays
4. **Experimental**: Voice command recording starts (green LED)
5. **Experimental**: Audio is sent to console.groq.com for transcription
6. **Note**: Intent parsing and command execution not yet implemented

**Current Status**: Wake word detection ✅ | Speech recognition ✅ (experimental) | Command execution ❌

**Tuning Tips:**
- Default threshold is 80% - increase if too many false positives
- Use `wake_threshold 90` for stricter detection
- Use `wake_threshold 70` if it's not detecting well
- Retrain model with more samples if accuracy is poor

## Technical Details

### Edge Impulse Wake Word
- **Model**: MFCC + Neural Network (Classification)
- **Sample Rate**: 16kHz mono
- **Window Size**: 1500ms
- **Inference**: ~100ms on ESP32-S3
- **Threshold**: 80% confidence (adjustable)
- **Cooldown**: 2 seconds between detections

### MQTT Topics
- **Publish**: `ac/status` - Device status updates
- **Subscribe**: `ac/command` - Remote commands

### Memory Usage
- Edge Impulse model: ~50-100KB (quantized int8)
- Audio inference buffer: ~48KB

## Troubleshooting

### Wake Word Not Detecting
- Check microphone connections (WS=4, SCK=5, SD=6)
- Use `voice` command to verify audio levels
- Lower threshold: `wake_threshold 70`
- Retrain model with more samples from your environment

### Too Many False Positives
- Increase threshold: `wake_threshold 90`
- Add more "noise" samples to training data
- Add more "unknown" speech samples

### Microphone Issues
- Ensure L/R pin connected to GND (left channel)
- Verify 3.3V power supply
- Check for I2S conflicts with speaker

### WiFi Connection Failed
- Reset WiFi using joystick double-click on Network screen
- Ensure 2.4GHz WiFi (ESP32 doesn't support 5GHz)
- Connect to AP (`12345678`) and reconfigure

## Pin Configuration (config.h)

```cpp
// Microphone (INMP441)
#define MIC_WS_PIN 4
#define MIC_SCK_PIN 5
#define MIC_SD_PIN 6

// Speaker (MAX98357)
#define SPK_BCLK_PIN 15
#define SPK_LRCLK_PIN 16
#define SPK_SD_PIN 7

// NeoPixel LED
#define NEOPIXEL_PIN 48

// IR
#define IR_TX_PIN 12
#define IR_RX_PIN 11
```

## Future Enhancements (Experiment)

### Voice Command Pipeline (NLP + LLM)

**Current Status:**
- ✅ **Wake Word Detection**: "Hey Bob" detection working (Edge Impulse)
- ✅ **Speech Recognition**: Basic speech-to-text using console.groq.com (experimental)
- ❌ **Intent Parsing**: LLM integration for command understanding (not implemented)
- ❌ **Action Execution**: Voice command execution (not implemented)
- ❌ **Text-to-Speech**: Voice responses (not implemented)

The goal is to enable natural language voice commands like:
- "Hey Bob, it's too hot" → Lower AC temperature
- "Hey Bob, turn on the lights" → Control smart devices
- "Hey Bob, I'm leaving" → Run "away" scene

**Architecture:**
```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│ Wake Word   │ →  │ Listening   │ →  │ Speech-to-  │ →  │ LLM         │
│ "Hey Bob"   │    │ Mode        │    │ Text API    │    │ (LLaMA 8B)  │
│ (Edge       │    │ LED ON      │    │ (console.   │    │ Parse       │
│ Impulse)    │    │ Record cmd  │    │ groq.com)   │    │ Intent      │
│ ✅ Working  │    │ ✅ Working  │    │ ✅ Working  │    │ ❌ TODO     │
└─────────────┘    └─────────────┘    │ (Experimental)│    └─────────────┘
                          │                                      │
                          ▼                                      ▼
                   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
                   │ End of      │    │ Execute     │ ←  │ JSON        │
                   │ Speech      │    │ Command     │    │ Response    │
                   │ Detection   │    │ + TTS Reply │    │ {action:..} │
                   │ ✅ Working  │    │ ❌ TODO     │    │ ❌ TODO     │
                   └─────────────┘    └─────────────┘    └─────────────┘
```

**Current Flow (Implemented):**
1. Wake word "Hey Bob" detected (Edge Impulse) ✅
2. LED turns green → Listening mode starts ✅
3. Record user's voice command (until silence detected or max ~10 sec) ✅
4. LED turns blue → Processing starts ✅
5. Send audio to console.groq.com → Get text ✅ (experimental)
6. ❌ **STOP**: Intent parsing and execution not implemented

**Next Steps (Not Implemented):**
- Send text to LLM → Get structured JSON command
- Execute command (AC control, IR signal, etc.)
- Play TTS response through speaker

**Speech-to-Text (Current Implementation):**
- **console.groq.com**: Currently used for speech recognition (experimental)
  - Free tier available
  - High accuracy but requires internet connection
  - Currently in testing phase

**LLM for Intent Parsing (Future Options):**
- **Hugging Face Inference API**: Free tier, many models available
  - `meta-llama/Meta-Llama-3-8B-Instruct`
  - `mistralai/Mistral-7B-Instruct-v0.2`
  - `google/gemma-7b-it`
- **Groq API**: Free tier, very fast inference (LLaMA 3 8B)
- **Google Gemini API**: Free tier (15 RPM, 1M tokens/day)
- **Cloudflare Workers AI**: Free tier (10K tokens/day)
- **Ollama (self-hosted)**: Free, run LLaMA on your own server/PC

**Example LLM Prompt:**
```
You are a smart home assistant. Parse the user command and return JSON.
Available actions: ac_on, ac_off, ac_temp, ac_mode, ir_send, light_on, light_off

User: "turn on the AC and set it to 24 degrees"
Response: {"actions": [{"type": "ac_on"}, {"type": "ac_temp", "value": 24}]}
```

**Text-to-Speech Response:**
- **Google Cloud TTS API**: Natural voices, multiple languages
- **OpenAI TTS API**: Very natural, simple to use
- **Eleven Labs API**: High quality voice cloning
- **ESP32 local TTS**: Basic robotic voice, no cloud needed

### Smart Home API Integration

Use cloud APIs to control WiFi-compatible smart devices:

**Google Home API**
- Control Google Home compatible devices via API
- Works with Nest, Philips Hue, TP-Link, etc.
- OAuth2 authentication required

**Amazon Alexa API**
- Smart Home Skill API for device control
- Works with Ring, Ecobee, Sengled, etc.

**Tuya/Smart Life API**
- Control thousands of cheap WiFi devices
- Plugs, lights, switches, sensors
- Local API available (no cloud dependency)

**IFTTT Webhooks**
- Trigger any IFTTT applet via HTTP
- Connect to 700+ services

### Zigbee Integration
- **Zigbee Coordinator**: Add CC2652 or EFR32 Zigbee module
- **Zigbee2MQTT Bridge**: Control Zigbee devices through existing MQTT
- **Device Support**:
  - Smart plugs and switches
  - Light bulbs (Philips Hue, IKEA Tradfri)
  - Sensors (door/window, motion, temperature)
- **Local Control**: No cloud dependency, mesh network

### Additional Protocols
- **Matter**: Universal smart home standard (Google, Apple, Amazon compatible)
- **Bluetooth Mesh**: BLE-based device control
- **Thread**: IPv6-based mesh networking

### Advanced Voice Features
- **Multi-Wake Word**: Train additional wake words
- **Speaker Identification**: Recognize different users
- **Multi-language**: Train models for different languages

### Automation
- **Scene Control**: "Movie time" → Dim lights, set AC to 24°C
- **Scheduled Routines**: Time-based automation
- **Sensor Triggers**: Auto-adjust based on environment

## License

This project is open source and available for educational purposes.

## Author

Sheng

---

**Note**: This project uses Edge Impulse for on-device wake word detection. The ML model runs entirely on the ESP32 without requiring cloud connectivity for voice detection.
