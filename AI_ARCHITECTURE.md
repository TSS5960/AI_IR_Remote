# AI Home Automation Architecture Guide

## Current vs Recommended Architecture

### Current Setup (Works for 1-2 devices)
```
ESP32 → MQTT → Firebase RTDB ← Dashboard
```

**Limitations:**
- Not optimized for time-series sensor data
- Hard to scale to multiple devices
- No AI context management
- Limited querying capabilities

### Recommended for AI Home Automation

```
┌──────────────────────────────────────────────┐
│     Cloudflare Workers + OpenAI Edge        │
│     (ChatGPT API, Function Calling)          │
└──────────────────────────────────────────────┘
                    │
      ┌─────────────┼─────────────┐
      ▼             ▼             ▼
┌──────────┐  ┌──────────┐  ┌──────────┐
│ Firebase │  │ InfluxDB │  │Firestore │
│   RTDB   │  │    or    │  │  (Conv)  │
│ (State)  │  │ Supabase │  │ History  │
└──────────┘  └──────────┘  └──────────┘
      │             │             │
      └─────────────┴─────────────┘
                    │
              ┌─────┴─────┐
              │   MQTT    │
              │  Broker   │
              └─────┬─────┘
                    │
        ┌───────────┼────────────┐
        ▼           ▼            ▼
    ESP32-AC   ESP32-Light  Zigbee-Hub
```

## Phase 1: Quick ChatGPT Integration (Current Setup)

### Steps to Add AI Assistant:

1. **Get OpenAI API Key**
   - Go to https://platform.openai.com/api-keys
   - Create new secret key
   - Save it securely

2. **Add to Cloudflare Worker**
   ```bash
   cd cloudflare_dashboard
   wrangler secret put OPENAI_API_KEY
   # Paste your key when prompted
   ```

3. **Deploy**
   ```bash
   npx wrangler deploy
   ```

4. **Test the API**
   ```javascript
   // From browser console or Postman
   fetch('https://your-worker.workers.dev/api/chat', {
     method: 'POST',
     headers: { 'Content-Type': 'application/json' },
     body: JSON.stringify({
       message: 'Turn on the AC and set it to 22 degrees',
       sessionId: 'user123'
     })
   });
   ```

### Example Interactions:

```
User: "It's too hot"
AI: "I'll turn on the AC for you. Current room temp is 28°C, setting to 24°C in cooling mode."
→ Executes: power_on, set_temperature(24), set_mode(cool)

User: "Make it cooler"
AI: "Lowering temperature to 22°C"
→ Executes: set_temperature(22)

User: "Turn off all devices when I leave"
AI: "I'll turn off the AC when no motion is detected for 10 minutes."
→ Creates automation rule
```

## Phase 2: Time-Series Database for Sensors

### Option A: InfluxDB Cloud (Free Tier)

**Why:** Purpose-built for time-series data, efficient querying

```javascript
// Install
npm install @influxdata/influxdb-client

// ESP32 → MQTT → Worker → InfluxDB
const {InfluxDB, Point} = require('@influxdata/influxdb-client');

const influxDB = new InfluxDB({
  url: 'https://us-west-2-1.aws.cloud2.influxdata.com',
  token: env.INFLUX_TOKEN
});

// Write sensor data
const writeApi = influxDB.getWriteApi(org, bucket);
writeApi.writePoint(
  new Point('sensors')
    .tag('device', 'ESP32_AC_001')
    .tag('room', 'living_room')
    .floatField('temperature', 25.5)
    .floatField('humidity', 60)
    .timestamp(new Date())
);
await writeApi.close();

// Query for AI
const queryApi = influxDB.getQueryApi(org);
const data = await queryApi.collectRows(`
  from(bucket: "sensors")
    |> range(start: -24h)
    |> filter(fn: (r) => r.device == "ESP32_AC_001")
    |> aggregateWindow(every: 1h, fn: mean)
`);
```

### Option B: Supabase (PostgreSQL + Realtime)

**Why:** SQL for complex queries, good for multi-device

```sql
-- Create table
CREATE TABLE sensor_readings (
  id BIGSERIAL PRIMARY KEY,
  device_id TEXT NOT NULL,
  timestamp TIMESTAMPTZ DEFAULT NOW(),
  temperature FLOAT,
  humidity FLOAT,
  light FLOAT,
  motion BOOLEAN,
  room TEXT
);

CREATE INDEX idx_sensors_device_time 
ON sensor_readings(device_id, timestamp DESC);

-- Hypertable for time-series optimization (if using TimescaleDB)
SELECT create_hypertable('sensor_readings', 'timestamp');
```

```javascript
// Query from Worker
const { data } = await supabase
  .from('sensor_readings')
  .select('*')
  .eq('device_id', 'ESP32_AC_001')
  .gte('timestamp', new Date(Date.now() - 86400000)) // Last 24h
  .order('timestamp', { ascending: false });
```

## Phase 3: Multi-Device Architecture

### Device Registry Structure

```javascript
// In Firebase or Firestore
const DEVICE_REGISTRY = {
  devices: {
    'ac_living_room': {
      id: 'ESP32_AC_001',
      type: 'air_conditioner',
      protocol: 'ir',
      brand: 'daikin',
      location: {
        room: 'living_room',
        floor: 1
      },
      capabilities: [
        { name: 'power', type: 'boolean' },
        { name: 'temperature', type: 'number', min: 16, max: 30 },
        { name: 'mode', type: 'enum', values: ['cool', 'heat', 'dry', 'fan'] },
        { name: 'fan_speed', type: 'enum', values: ['auto', 'low', 'med', 'high'] }
      ],
      sensors: ['temperature', 'humidity', 'motion', 'light']
    },
    'light_bedroom': {
      id: 'ZIGBEE_LIGHT_001',
      type: 'smart_light',
      protocol: 'zigbee',
      location: {
        room: 'bedroom',
        floor: 2
      },
      capabilities: [
        { name: 'power', type: 'boolean' },
        { name: 'brightness', type: 'number', min: 0, max: 100 },
        { name: 'color_temp', type: 'number', min: 2700, max: 6500 }
      ]
    }
  }
};
```

### Unified Command Interface

```javascript
// Generic device control
async function controlDevice(deviceId, command, params) {
  const device = DEVICE_REGISTRY.devices[deviceId];
  
  switch(device.protocol) {
    case 'ir':
      return publishIRCommand(device.id, command, params);
    case 'zigbee':
      return publishZigbeeCommand(device.id, command, params);
    case 'wifi':
      return sendHTTPCommand(device.ip, command, params);
  }
}

// AI can call this generically
await controlDevice('ac_living_room', 'set_temperature', { value: 22 });
await controlDevice('light_bedroom', 'set_brightness', { value: 50 });
```

## Phase 4: Advanced AI Features

### 1. Contextual Awareness

```javascript
// Build rich context for ChatGPT
function buildAIContext(sessionId) {
  return {
    devices: await fetchAllDeviceStates(),
    sensors: await fetchLatestSensors(),
    userPreferences: await getUserPrefs(sessionId),
    weatherData: await getWeatherAPI(),
    timeOfDay: getTimeContext(),
    previousConversations: await getConversationHistory(sessionId, 5)
  };
}
```

### 2. Automation Rules

```javascript
// AI can create automation rules
const automations = {
  'auto_cool_evening': {
    trigger: { time: '18:00' },
    conditions: [
      { sensor: 'temperature', operator: '>', value: 26 },
      { sensor: 'motion', value: true }
    ],
    actions: [
      { device: 'ac_living_room', command: 'power_on' },
      { device: 'ac_living_room', command: 'set_temperature', value: 24 }
    ]
  }
};
```

### 3. Learning User Preferences

```javascript
// Track user patterns
const userPatterns = {
  userId: 'user123',
  preferences: {
    sleepTime: '22:00',
    wakeTime: '07:00',
    preferredTemp: {
      day: 24,
      night: 22
    },
    autoMode: true
  },
  patterns: [
    // AI learns: "User always sets AC to 22°C after 10pm"
    { time: '22:00-23:00', device: 'ac', action: 'set_temp', value: 22, frequency: 0.85 }
  ]
};
```

### 4. Proactive Suggestions

```javascript
// AI proactively suggests actions
async function generateSuggestions() {
  const context = await buildAIContext();
  
  // Example: High humidity detected
  if (context.sensors.humidity > 70) {
    return {
      suggestion: "Humidity is high (73%). Would you like me to run the AC in dry mode?",
      action: { device: 'ac', command: 'set_mode', value: 'dry' }
    };
  }
}
```

## Implementation Roadmap

### Week 1-2: Basic ChatGPT Integration
- ✅ Add AI handler (already created)
- Set up OpenAI API key
- Test basic commands
- Add chat UI to dashboard

### Week 3-4: Improve Data Structure
- Move sensor data to InfluxDB or Supabase
- Keep device state in Firebase RTDB
- Optimize queries

### Week 5-6: Multi-Device Support
- Create device registry
- Add Zigbee hub integration
- Implement unified command interface

### Week 7-8: Advanced AI Features
- Conversation history
- Automation rules
- User preference learning
- Proactive suggestions

## Cost Estimates (Monthly)

**Current Setup:**
- Firebase RTDB: Free tier (sufficient for 1-2 devices)
- Cloudflare Workers: Free tier
- MQTT (EMQX): Free tier
**Total: $0/month**

**With AI (Small Home):**
- Firebase: $0 (free tier)
- Cloudflare Workers: $5 (paid plan for AI)
- OpenAI API: ~$10-30 (depends on usage)
- InfluxDB Cloud: $0 (free tier: 10k writes/min)
**Total: $15-40/month**

**Scalable Setup (Smart Home):**
- Supabase Pro: $25
- Cloudflare Workers: $5
- OpenAI API: $30-100
- InfluxDB: $0-50
**Total: $60-180/month**

## Next Steps

1. **Deploy current ChatGPT integration**
2. **Test basic voice commands**
3. **Decide on database migration**
4. **Plan multi-device expansion**

Would you like me to implement any specific part of this architecture?
