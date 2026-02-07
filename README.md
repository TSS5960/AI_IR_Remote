# ESP32-S3 Smart AC Remote Control with Voice Integration(Not Up to data more function was added)

An intelligent air conditioner remote control system based on ESP32-S3 with voice recording, IR learning, cloud connectivity, and multi-sensor integration.

## 🌟 Features

### Core Functionality
- **WiFi Captive Portal**: Easy WiFi setup via web interface (no hardcoded credentials)
- **IR Control**: Send IR commands to control air conditioners
- **IR Learning**: Capture and replay any IR remote signals
- **Voice Recording**: Record audio and play back through speaker
- **Experimental Wake Word**: Basic audio pattern matching (currently unreliable)
- **Multi-Screen Display**: OLED interface with 6 different screens
- **Environmental Monitoring**: Temperature, humidity, light, and motion sensors
- **Cloud Integration**: MQTT broker and Firebase real-time database
- **Web Dashboard**: Cloudflare-hosted control panel
- **Alarm System**: Time-based alarms with speaker notifications

### Voice Features
- **Record & Playback**: 3-second audio recording with instant playback
- **Voice Level Monitor**: Real-time audio RMS visualization
- **Experimental Wake Word**: Prototype audio pattern matching (not production-ready)
  - Simple waveform correlation approach with significant limitations
  - Highly sensitive to volume, distance, speed, and background noise
  - Requires exact match of training conditions
- **LED Feedback**: NeoPixel RGB LED indicates voice activity

## 🔧 Hardware Components

### Main Controller
- **ESP32-S3 Dev Module** - Dual-core microcontroller with WiFi

### Audio System
- **INMP441 I2S MEMS Microphone** (GPIO4=WS, GPIO5=SCK, GPIO6=SD)
- **MAX98357 I2S Amplifier + Speaker** (GPIO15=BCLK, GPIO16=LRCLK, GPIO7=DIN)
- **Adafruit NeoPixel RGB LED** (GPIO48)

### Display & Input
- **OLED Display SSD1306** (128x64, I2C on GPIO13/14)
- **Analog Joystick** (X/Y axes + button)

### Sensors
- **GY-30 Light Sensor** (I2C)
- **DHT11 Temperature & Humidity Sensor**
- **PIR Motion Sensor**

### IR System
- **IR Transmitter LED** (GPIO12)
- **IR Receiver Module** (GPIO11)

## 📁 Project Structure

```
AC_IR_Remote/
├── AC_IR_Remote.ino          # Main firmware
├── config.h                   # Pin assignments and configuration
├── secrets.h                  # Firebase credentials (not in git)
├── secrets.h.template         # Template for secrets configuration
├── firebase_config.h          # Firebase paths (not in git)
├── firebase_config.h.template # Template for Firebase configuration
├── ac_control.cpp/h           # AC IR control logic
├── ir_control.cpp/h           # IR transmitter/receiver
├── ir_learning.cpp/h          # IR learning mode
├── mic_control.cpp/h          # Microphone and voice processing
├── speaker_control.cpp/h      # Audio playback
├── button_control.cpp/h       # Joystick input handling
├── display.cpp/h              # OLED screen management
├── sensors.cpp/h              # Environmental sensors
├── alarm_manager.cpp/h        # Alarm system
├── wifi_manager.cpp/h         # WiFi connectivity
├── mqtt_broker.cpp/h          # MQTT client
├── firebase_client.cpp/h      # Firebase integration
└── cloudflare_dashboard/      # Web dashboard
    ├── src/index.js           # Cloudflare Worker
    ├── public/
    │   ├── index.html         # Dashboard UI
    │   ├── app.js             # Frontend logic
    │   └── styles.css         # Styling
    ├── wrangler.toml          # Cloudflare configuration (not in git)
    └── wrangler.toml.template # Template for Cloudflare configuration
```

## 🚀 Getting Started

### 1. Service Setup

#### Firebase Realtime Database
1. Go to [Firebase Console](https://console.firebase.google.com/)
2. Create a new project or select existing one
3. Navigate to **Realtime Database** → Create Database
4. Choose **Start in test mode** (for development)
5. Get your database URL (e.g., `your-project.firebaseio.com`)
6. Get authentication token:
   - Go to **Project Settings** → **Service Accounts**
   - Click **Database secrets** → Show/Add secret
   - Copy the secret token

#### MQTT Broker
The project uses a public MQTT broker by default:
- **Broker**: `broker.emqx.io`
- **Port**: 1883 (non-SSL) or 8883 (SSL)
- **Topics**:
  - Publish: `ac/status`
  - Subscribe: `ac/command`

**Alternative MQTT Brokers:**
- [HiveMQ Cloud](https://www.hivemq.com/mqtt-cloud-broker/) - Free tier available
- [CloudMQTT](https://www.cloudmqtt.com/) - Managed MQTT hosting
- Self-hosted: Mosquitto on Raspberry Pi/VPS

#### Cloudflare Workers (Optional)
1. Sign up at [Cloudflare](https://dash.cloudflare.com/)
2. Install Wrangler CLI: `npm install -g wrangler`
3. Login: `wrangler login`
4. Copy configuration template:
   ```bash
   cd cloudflare_dashboard
   cp wrangler.toml.template wrangler.toml
   ```
5. Edit `wrangler.toml` and update `FIREBASE_DB_URL` with your Firebase database URL
6. Configure secrets in Cloudflare dashboard or via CLI:
   ```bash
   wrangler secret put FIREBASE_AUTH
   ```

### 2. Hardware Setup

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

**Sensors (I2C Bus):**
```
OLED + GY-30 → ESP32-S3
  VCC → 3.3V
  GND → GND
  SDA → GPIO13
  SCL → GPIO14
```

### 3. Software Setup

#### Arduino IDE Configuration
1. Install ESP32 board support: https://espressif.github.io/arduino-esp32/
2. Select Board: **ESP32S3 Dev Module**
3. Install required libraries:
   - `Adafruit_NeoPixel`
   - `Adafruit_GFX` + `Adafruit_SSD1306`
   - `IRremote` or `IRremoteESP8266`
   - `DHT sensor library`
   - `BH1750` (for GY-30)
   - `Firebase ESP32` (optional, if using Firebase)
   - `PubSubClient` (optional, if using MQTT)

#### Configure Firebase (Optional)
If you want to use Firebase features, you'll need to create your own configuration files:

1. Copy templates and create your configuration files:
```bash
cp firebase_config.h.template firebase_config.h
cp secrets.h.template secrets.h
```

2. Edit `firebase_config.h` with your database URL:
```cpp
#define FIREBASE_DB_URL "https://your-project-id-default-rtdb.firebaseio.com"
```

3. Edit `secrets.h` with your Firebase authentication token:
```cpp
#define FIREBASE_AUTH "your-database-secret"  // From Firebase setup step
```

**Note:** These files are in `.gitignore` and will not be committed to the repository.

#### Upload Firmware
1. Connect ESP32-S3 via USB
2. Select correct port in Arduino IDE
3. Upload `AC_IR_Remote.ino`
4. Open Serial Monitor at **115200 baud**

#### WiFi Setup (Captive Portal)
The device creates its own WiFi access point for initial setup:

1. **First boot** - ESP32 creates WiFi AP: `ESP32_AC_Remote` (or similar)
2. **Connect to AP** - WiFi password: `12345678`
3. **Configure WiFi** - Browser should auto-open configuration page
   - If not, navigate to `http://192.168.4.1`
   - Enter your WiFi SSID and password
   - Click Save
4. **Device reboots** and connects to your WiFi network

**Reset WiFi Credentials:**
- Use the joystick to access WiFi reset function
- Device will restart in AP mode for reconfiguration

### 4. Web Dashboard (Optional)

#### Local Development
```bash
cd cloudflare_dashboard
npm install
npx wrangler dev
```

#### Deploy to Cloudflare
```bash
npx wrangler deploy
```

#### Environment Variables
Configure secrets via CLI:
- `FIREBASE_AUTH` - Firebase authentication token

Set with: `wrangler secret put FIREBASE_AUTH`

## 🎮 Usage

### Serial Commands

**Voice Commands:**
- `voice` - Show real-time voice level monitor
- `repeat` - Record 3 seconds and play back
- `train` - Record audio pattern (experimental, unreliable)
- `wake_start` - Start pattern detection (experimental)
- `wake_stop` - Stop pattern detection

**IR Commands:**
- `learn` - Enter IR learning mode
- `send DEVICE` - Send learned IR command
- `list` - List all learned IR commands

**AC Control:**
- `on` / `off` - Turn AC on/off
- `+` / `-` - Increase/decrease temperature
- `m [mode]` - Change mode (cool/heat/dry/fan/auto)
- `f [speed]` - Set fan speed (low/med/high/auto)

**System Commands:**
- `h` - Show help
- `s` - Switch screen manually
- `test` - Test speaker
- `wifi` - Show WiFi status
- `mqtt` - Show MQTT status

**WiFi Management:**
- Use joystick to access WiFi reset menu
- Resets credentials and restarts in AP mode for reconfiguration

**Alarm Management:**
- `a HH MM [name]` - Add alarm (e.g., `a 07 30 Morning`)
- `u INDEX HH MM [name]` - Update alarm
- `d INDEX` - Delete alarm
- `p` - List all alarms

### Display Screens

Navigate using joystick (left/right):
1. **Volume** - Speaker volume control
2. **Clock** - Current time display
3. **Network** - WiFi and MQTT status
4. **AC Control** - Temperature and mode
5. **IR Learn** - IR learning interface
6. **Sensors** - Environmental data

### Experimental Voice Pattern Detection

**⚠️ Note**: This feature is experimental and has reliability issues. Consider it a learning/prototype feature.

1. Train the system:
```
> train
Recording will start immediately...
[Say your phrase clearly]
Training complete!
```

2. Start detection:
```
> wake_start
Detection started (may have false positives/negatives)
```

3. If detected correctly - LED lights up white for 3 seconds + beep

**Known Issues:**
- High false positive/negative rate
- Requires identical volume and distance from training
- Background noise causes failures
- No tolerance for speed or pitch variations

## 🔬 Technical Details

### Voice Pattern Detection (Experimental)
- **Method**: Simple waveform correlation (not suitable for production)
- **Training**: Records 1.5 seconds (24,000 samples at 16kHz)
- **Detection**: Rolling buffer with Pearson correlation coefficient
- **Threshold**: 65% similarity (often too strict or too loose)
- **Sample Rate**: 16kHz mono
- **Latency**: ~100ms detection interval
- **Limitations**: 
  - No MFCC or proper feature extraction
  - No noise filtering or normalization
  - No time warping tolerance (DTW)
  - Highly sensitive to environmental conditions
  - **Not recommended for actual use** - consider it a learning demonstration

### MQTT Topics
- **Publish**: `ac/status` - Device status updates
- **Subscribe**: `ac/command` - Remote commands

### Memory Usage
- **Wake Word Pattern**: ~48KB (24,000 samples × 2 bytes)
- **Audio Buffer**: ~48KB (rolling buffer)
- **Total Audio Memory**: ~96KB

## 🐛 Troubleshooting

### Microphone Issues
- Check GPIO connections (WS=4, SCK=5, SD=6)
- Ensure L/R pin connected to GND
- Verify 3.3V power supply
- Use `voice` command to test levels

### Speaker Problems
- Confirm I2S_NUM_1 not conflicting
- Check 5V power for MAX98357
- Test with `test` command

### Wake Word Not Working Properly
- **Expected behavior**: This feature is experimental and unreliable
- Try retraining with `train` command
- Must speak at EXACT same volume/distance as training
- Adjust correlation threshold in code (currently 0.65, may need tuning)
- **Recommended**: Use physical button or better voice recognition service instead

### WiFi Connection Failed
- Reset WiFi using joystick and reconfigure via captive portal
- Ensure 2.4GHz WiFi network (ESP32 doesn't support 5GHz)
- Check that WiFi password was entered correctly during setup
- Connect to ESP32 AP (`12345678`) and reconfigure if needed
- Monitor serial output for error messages

## 📝 Configuration

### Pin Assignments (config.h)
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

// Display (I2C)
#define DISPLAY_SDA_PIN 13
#define DISPLAY_SCL_PIN 14

// IR
#define IR_SEND_PIN 12
#define IR_RECV_PIN 11
```

### Audio Settings
```cpp
#define MIC_SAMPLE_RATE 16000
#define SPEAKER_SAMPLE_RATE 44100
#define WAKE_WORD_SAMPLES 24000  // 1.5 seconds
#define CORRELATION_THRESHOLD 0.65
```

## 🔮 Future Enhancements

**Voice Recognition Improvements (High Priority):**
- [ ] Replace simple correlation with MFCC + DTW algorithm
- [ ] Add proper noise filtering and audio normalization
- [ ] Implement TensorFlow Lite for on-device wake word (Edge Impulse)
- [ ] Or use cloud-based wake word service

**Voice Assistant Features:**
- [ ] Cloud-based Speech-to-Text integration (Wit.ai, Google Speech API)
- [ ] LLM integration for voice commands (OpenAI, Gemini)
- [ ] Text-to-Speech responses (gTTS, Eleven Labs)

**Smart Home Integration:**
- [ ] Smart plug control (Home Assistant/IFTTT)
- [ ] Multi-device control
- [ ] Voice activity detection improvements

## 📄 License

This project is open source and available for educational purposes.

## 🤝 Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

## 👨‍💻 Author

Sheng

---

**Note**: This is a hobby/learning project. The wake word detection feature is a **prototype demonstration only** and is not reliable for real-world use. For production voice control, use professional services like Google Assistant SDK, Alexa Voice Service, or TensorFlow Lite models trained with proper datasets.
