# AI Chatbot Implementation - Step by Step Guide

## Current State ✅
```
AC_IR_Remote/
├── ESP32 Code (Arduino)
│   ├── AC_IR_Remote.ino
│   ├── firebase_client.cpp/h
│   ├── mqtt_broker.cpp/h
│   ├── sensors.cpp/h
│   └── Other modules
├── cloudflare_dashboard/
│   ├── public/ (HTML/CSS/JS)
│   └── src/ (Cloudflare Workers)
└── Firebase RTDB (Database)
```

**Status:**
- ✅ ESP32 sends data to Firebase
- ✅ MQTT working
- ✅ Dashboard displays data
- ❌ No AI integration yet
- ❌ Sensors not showing (needs ESP32 re-upload)

---

## Phase 1: Fix Current Issues (Do This First!)
**Goal:** Get everything working before adding AI

### Step 1.1: Upload ESP32 Code with Sensor Support
**Time:** 10 minutes

```bash
# Actions:
1. Open Arduino IDE
2. Open AC_IR_Remote.ino
3. Click Upload (→)
4. Wait 15 seconds after upload
5. Open Serial Monitor (115200 baud)
```

**Expected Output:**
```
[Firebase] state+sensors -> HTTP 200
[Firebase] Connected (recent write)
```

**Verify:** Check Firebase Console → `/devices/ESP32_AC_Remote_001/state` has `sensors` object

### Step 1.2: Deploy Dashboard with Debug Logs
**Time:** 5 minutes

```bash
cd cloudflare_dashboard
npx wrangler deploy
```

**Verify:** Open dashboard, F12 console shows:
```
[Debug] Sensors found in status, calling updateSensorDisplay
```

### Step 1.3: Checkpoint ✓
- [ ] ESP32 uploading sensors to Firebase
- [ ] Dashboard displaying AC state
- [ ] Dashboard displaying sensor data (temp, humidity, light, motion)
- [ ] MQTT commands working (power on/off)

**If NOT all checked, STOP and fix before Phase 2!**

---

## Phase 2: Restructure for AI (Backend)
**Goal:** Prepare database and API structure

### Step 2.1: Update Firebase Structure
**Time:** 15 minutes

**Current Structure (messy):**
```
/devices/ESP32_AC_Remote_001/
  ├── state (AC + sensors mixed)
  ├── status_history (duplicated data)
  └── alarms
```

**New Structure (clean):**
```
/devices/
  └── ESP32_AC_Remote_001/
      ├── info (device metadata)
      ├── state (AC state only)
      ├── sensors (latest sensor readings)
      └── capabilities (what device can do)

/sensor_history/
  └── ESP32_AC_Remote_001/
      └── {timestamp} (historical data)

/conversations/
  └── {userId}/
      └── {sessionId}/
          ├── messages[]
          └── context

/commands/
  └── ESP32_AC_Remote_001/
      └── {timestamp} (command log)
```

**Action:** Update Firebase Rules

```json
{
  "rules": {
    ".read": true,
    ".write": true,
    "devices": {
      ".indexOn": ["type", "location"]
    },
    "sensor_history": {
      ".indexOn": ["timestamp"]
    }
  }
}
```

### Step 2.2: Create Device Registry
**Time:** 10 minutes

**Create:** New file in Firebase `/devices/ESP32_AC_Remote_001/info`

```json
{
  "device_id": "ESP32_AC_Remote_001",
  "name": "Living Room AC",
  "type": "air_conditioner",
  "protocol": "ir",
  "brand": "daikin",
  "location": {
    "room": "living_room",
    "floor": 1,
    "zone": "main"
  },
  "capabilities": [
    {
      "name": "power",
      "type": "boolean",
      "commands": ["power_on", "power_off", "power_toggle"]
    },
    {
      "name": "temperature",
      "type": "number",
      "min": 16,
      "max": 30,
      "unit": "celsius",
      "commands": ["set_temperature"]
    },
    {
      "name": "mode",
      "type": "enum",
      "values": ["auto", "cool", "heat", "dry", "fan"],
      "commands": ["set_mode"]
    },
    {
      "name": "fan_speed",
      "type": "enum",
      "values": ["auto", "low", "medium", "high"],
      "commands": ["set_fan"]
    }
  ],
  "sensors": ["temperature", "humidity", "light", "motion"],
  "online": true,
  "last_seen": 1738732800
}
```

**Action:** Manually add this to Firebase Console

### Step 2.3: Separate Sensors from State
**Time:** 20 minutes

**Update:** `firebase_client.cpp`

Add new function to write sensors separately:

```cpp
bool firebaseWriteSensors(const SensorData& sensors) {
  if (!isFirebaseConfigured()) {
    return false;
  }

  StaticJsonDocument<256> doc;
  doc["device"] = FIREBASE_DEVICE_ID;
  doc["timestamp"] = static_cast<long>(time(nullptr));
  doc["motion"] = sensors.motionDetected;

  JsonObject dhtObj = doc.createNestedObject("dht");
  dhtObj["valid"] = sensors.dht_valid;
  if (sensors.dht_valid) {
    dhtObj["temperature"] = sensors.dht_temperature;
    dhtObj["humidity"] = sensors.dht_humidity;
  }

  JsonObject lightObj = doc.createNestedObject("light");
  lightObj["valid"] = sensors.light_valid;
  if (sensors.light_valid) {
    lightObj["lux"] = sensors.light_lux;
  }

  String payload;
  serializeJson(doc, payload);

  String path = "/devices/" + String(FIREBASE_DEVICE_ID) + "/sensors.json";
  return sendJson(buildUrl(path), payload, true, "sensors");
}
```

**Update:** `AC_IR_Remote.ino` sensor reading section:

```cpp
// Read sensors periodically (every 15 seconds)
static unsigned long lastSensorRead = 0;
if (millis() - lastSensorRead > 15000) {
  SensorData data = readAllSensors();
  printSensorData(data);
  handleAutoDry(data);
  handleSleepMode(data);
  if (isFirebaseConfigured()) {
    // Write sensors to separate path
    firebaseWriteSensors(data);
    // Also log to history
    firebaseQueueStatus(getACState(), data, "periodic");
  }
  lastSensorRead = millis();
}
```

### Step 2.4: Checkpoint ✓
- [ ] New Firebase structure created
- [ ] Device info/capabilities defined
- [ ] Sensors written to separate path
- [ ] State and sensors are separated

---

## Phase 3: Add AI Backend (Gemini - FREE)
**Goal:** Integrate Google Gemini for AI chat

### Step 3.1: Get Gemini API Key (FREE)
**Time:** 5 minutes

1. Go to: https://aistudio.google.com/app/apikey
2. Click "Create API Key"
3. Copy the key
4. Save it securely

### Step 3.2: Create Gemini AI Handler
**Time:** 20 minutes

**Create:** `cloudflare_dashboard/src/gemini-handler.js`

```javascript
/**
 * Google Gemini AI Handler (FREE)
 * 60 requests/minute free tier
 */

export async function handleGeminiChat(request, env) {
  const { message, sessionId } = await request.json();
  
  // Get device state and sensors
  const deviceState = await fetchDeviceState(env);
  const sensors = await fetchSensors(env);
  const capabilities = await fetchCapabilities(env);
  
  // Build context for Gemini
  const systemContext = buildSystemPrompt(deviceState, sensors, capabilities);
  
  // Call Gemini API
  const response = await fetch(
    `https://generativelanguage.googleapis.com/v1beta/models/gemini-pro:generateContent?key=${env.GEMINI_API_KEY}`,
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        contents: [{
          parts: [
            { text: systemContext },
            { text: `User request: ${message}` }
          ]
        }],
        generationConfig: {
          temperature: 0.7,
          maxOutputTokens: 500
        }
      })
    }
  );
  
  const data = await response.json();
  
  if (!data.candidates || !data.candidates[0]) {
    return jsonResponse({ error: 'No response from AI' }, 500);
  }
  
  const aiResponse = data.candidates[0].content.parts[0].text;
  
  // Parse commands from AI response
  const commands = extractCommands(aiResponse);
  
  // Execute commands if found
  if (commands.length > 0) {
    await executeCommands(commands, env);
  }
  
  // Save conversation
  await saveConversation(sessionId, message, aiResponse, env);
  
  return jsonResponse({
    response: aiResponse,
    commands: commands,
    timestamp: Date.now()
  });
}

function buildSystemPrompt(deviceState, sensors, capabilities) {
  return `You are a home automation assistant controlling an air conditioner.

CURRENT DEVICE STATE:
- Power: ${deviceState.power ? 'ON' : 'OFF'}
- Temperature Setting: ${deviceState.temperature}°C
- Mode: ${deviceState.mode}
- Fan Speed: ${deviceState.fan_speed}
- Brand: ${deviceState.brand}

CURRENT ROOM CONDITIONS:
- Temperature: ${sensors.dht?.temperature}°C
- Humidity: ${sensors.dht?.humidity}%
- Light Level: ${sensors.light?.lux} lux
- Motion: ${sensors.motion ? 'Detected' : 'None'}

AVAILABLE COMMANDS:
${JSON.stringify(capabilities, null, 2)}

INSTRUCTIONS:
1. Be helpful and conversational
2. When user wants to control device, respond with command in this format:
   [COMMAND:command_name:param1=value1,param2=value2]
   
   Examples:
   - "Turn on AC" → [COMMAND:power_on]
   - "Set to 22 degrees" → [COMMAND:set_temperature:value=22]
   - "Cool mode" → [COMMAND:set_mode:value=cool]
   
3. Consider current room conditions when making suggestions
4. Explain what you're doing

Respond naturally to the user's request.`;
}

function extractCommands(aiResponse) {
  const commandRegex = /\[COMMAND:([^:]+)(?::([^\]]+))?\]/g;
  const commands = [];
  let match;
  
  while ((match = commandRegex.exec(aiResponse)) !== null) {
    const command = { name: match[1], params: {} };
    
    if (match[2]) {
      const params = match[2].split(',');
      params.forEach(param => {
        const [key, value] = param.split('=');
        command.params[key.trim()] = value.trim();
      });
    }
    
    commands.push(command);
  }
  
  return commands;
}

async function executeCommands(commands, env) {
  // Publish to MQTT or write to Firebase
  for (const cmd of commands) {
    const payload = {
      command: cmd.name,
      ...cmd.params,
      source: 'ai_assistant',
      timestamp: Date.now()
    };
    
    // Log to Firebase commands history
    const commandUrl = `${env.FIREBASE_DB_URL}/commands/${env.FIREBASE_DEVICE_ID}/${Date.now()}.json`;
    await fetch(commandUrl, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
  }
}

async function fetchDeviceState(env) {
  const url = `${env.FIREBASE_DB_URL}/devices/${env.FIREBASE_DEVICE_ID}/state.json`;
  const response = await fetch(url);
  return await response.json();
}

async function fetchSensors(env) {
  const url = `${env.FIREBASE_DB_URL}/devices/${env.FIREBASE_DEVICE_ID}/sensors.json`;
  const response = await fetch(url);
  return await response.json();
}

async function fetchCapabilities(env) {
  const url = `${env.FIREBASE_DB_URL}/devices/${env.FIREBASE_DEVICE_ID}/info.json`;
  const response = await fetch(url);
  const info = await response.json();
  return info?.capabilities || [];
}

async function saveConversation(sessionId, userMsg, aiMsg, env) {
  const conversationUrl = `${env.FIREBASE_DB_URL}/conversations/${sessionId}/${Date.now()}.json`;
  await fetch(conversationUrl, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      user: userMsg,
      assistant: aiMsg,
      timestamp: Date.now()
    })
  });
}

function jsonResponse(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: {
      'Content-Type': 'application/json',
      'Access-Control-Allow-Origin': '*'
    }
  });
}
```

### Step 3.3: Update Worker Entry Point
**Time:** 5 minutes

**Update:** `cloudflare_dashboard/src/index.js`

```javascript
import { handleGeminiChat } from './gemini-handler.js';

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    
    // Handle CORS preflight
    if (request.method === 'OPTIONS') {
      return new Response(null, {
        headers: {
          'Access-Control-Allow-Origin': '*',
          'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
          'Access-Control-Allow-Headers': 'Content-Type'
        }
      });
    }
    
    // AI Chat endpoint
    if (url.pathname === "/api/chat" && request.method === "POST") {
      return handleGeminiChat(request, env);
    }
    
    // Config endpoint
    if (url.pathname === "/config.json") {
      return jsonResponse({
        firebase: {
          url: env.FIREBASE_DB_URL || "",
          auth: env.FIREBASE_AUTH || "",
          deviceId: env.FIREBASE_DEVICE_ID || ""
        }
      });
    }

    // Serve static files
    if (env.ASSETS) {
      return env.ASSETS.fetch(request);
    }

    return new Response("Not Found", { status: 404 });
  }
};

function jsonResponse(payload) {
  return new Response(JSON.stringify(payload, null, 2), {
    status: 200,
    headers: {
      "content-type": "application/json",
      "cache-control": "no-store",
      "Access-Control-Allow-Origin": "*"
    }
  });
}
```

### Step 3.4: Add Gemini API Key to Cloudflare
**Time:** 2 minutes

```bash
cd cloudflare_dashboard
wrangler secret put GEMINI_API_KEY
# Paste your Gemini API key when prompted
```

### Step 3.5: Deploy Backend
**Time:** 3 minutes

```bash
npx wrangler deploy
```

### Step 3.6: Test API Endpoint
**Time:** 5 minutes

```bash
# Test from terminal
curl -X POST https://your-worker.workers.dev/api/chat \
  -H "Content-Type: application/json" \
  -d '{
    "message": "Turn on the AC and set it to 22 degrees",
    "sessionId": "test123"
  }'
```

**Expected Response:**
```json
{
  "response": "I'll turn on the AC and set it to 22°C for you. [COMMAND:power_on] [COMMAND:set_temperature:value=22]",
  "commands": [
    { "name": "power_on", "params": {} },
    { "name": "set_temperature", "params": { "value": "22" } }
  ],
  "timestamp": 1738732800000
}
```

### Step 3.7: Checkpoint ✓
- [ ] Gemini API key obtained
- [ ] AI handler created
- [ ] Worker updated
- [ ] API deployed
- [ ] API test successful

---

## Phase 4: Add AI Frontend (Chat UI)
**Goal:** Create chat interface on dashboard

### Step 4.1: Add Chat UI to Dashboard
**Time:** 30 minutes

**Update:** `cloudflare_dashboard/public/index.html`

Add before `</body>`:

```html
<!-- AI Chat Assistant -->
<section class="section">
  <div class="container">
    <h2 class="title">🤖 AI Assistant</h2>
    <div class="box" style="max-width: 800px; margin: 0 auto;">
      <div id="chatMessages" style="height: 400px; overflow-y: auto; margin-bottom: 1rem; padding: 1rem; border: 1px solid #ddd; border-radius: 4px;">
        <div class="chat-message assistant">
          <strong>AI:</strong> Hi! I can help you control your AC. Try saying "Turn on the AC" or "What's the temperature?"
        </div>
      </div>
      <div class="field has-addons">
        <div class="control is-expanded">
          <input 
            type="text" 
            id="chatInput" 
            class="input" 
            placeholder="Ask me anything... (e.g., 'Make it cooler')"
            autocomplete="off"
          />
        </div>
        <div class="control">
          <button id="chatSend" class="button is-primary">
            <span>Send</span>
          </button>
        </div>
      </div>
      <div id="chatStatus" style="margin-top: 0.5rem; font-size: 0.875rem; color: #666;"></div>
    </div>
  </div>
</section>
```

### Step 4.2: Add Chat JavaScript
**Time:** 20 minutes

**Update:** `cloudflare_dashboard/public/app.js`

Add at the end:

```javascript
// AI Chat Integration
const chatMessages = document.getElementById('chatMessages');
const chatInput = document.getElementById('chatInput');
const chatSend = document.getElementById('chatSend');
const chatStatus = document.getElementById('chatStatus');

let sessionId = 'user_' + Date.now();

async function sendChatMessage() {
  const message = chatInput.value.trim();
  if (!message) return;
  
  // Add user message to chat
  addChatMessage('user', message);
  chatInput.value = '';
  chatStatus.textContent = 'AI is thinking...';
  chatSend.disabled = true;
  
  try {
    const response = await fetch('/api/chat', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        message: message,
        sessionId: sessionId
      })
    });
    
    const data = await response.json();
    
    // Remove command markers from display
    const cleanResponse = data.response.replace(/\[COMMAND:[^\]]+\]/g, '').trim();
    
    // Add AI response to chat
    addChatMessage('assistant', cleanResponse);
    
    // Show commands executed
    if (data.commands && data.commands.length > 0) {
      const commandList = data.commands.map(c => c.name).join(', ');
      chatStatus.textContent = `Executed: ${commandList}`;
      setTimeout(() => chatStatus.textContent = '', 3000);
    } else {
      chatStatus.textContent = '';
    }
    
  } catch (error) {
    addChatMessage('assistant', 'Sorry, I encountered an error. Please try again.');
    chatStatus.textContent = 'Error: ' + error.message;
  } finally {
    chatSend.disabled = false;
    chatInput.focus();
  }
}

function addChatMessage(role, text) {
  const messageDiv = document.createElement('div');
  messageDiv.className = `chat-message ${role}`;
  messageDiv.style.marginBottom = '1rem';
  messageDiv.style.padding = '0.5rem';
  messageDiv.style.borderRadius = '4px';
  messageDiv.style.backgroundColor = role === 'user' ? '#e8f4f8' : '#f0f0f0';
  
  const label = role === 'user' ? 'You' : 'AI';
  messageDiv.innerHTML = `<strong>${label}:</strong> ${escapeHtml(text)}`;
  
  chatMessages.appendChild(messageDiv);
  chatMessages.scrollTop = chatMessages.scrollHeight;
}

function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

// Event listeners
chatSend.addEventListener('click', sendChatMessage);
chatInput.addEventListener('keypress', (e) => {
  if (e.key === 'Enter') {
    sendChatMessage();
  }
});
```

### Step 4.3: Deploy Frontend
**Time:** 3 minutes

```bash
cd cloudflare_dashboard
npx wrangler deploy
```

### Step 4.4: Test Full Integration
**Time:** 10 minutes

1. Open dashboard in browser
2. Scroll to "🤖 AI Assistant" section
3. Try these commands:
   - "Turn on the AC"
   - "Set temperature to 22 degrees"
   - "What's the current temperature?"
   - "Make it cooler"
   - "Switch to cool mode"

### Step 4.5: Checkpoint ✓
- [ ] Chat UI visible on dashboard
- [ ] Can send messages to AI
- [ ] AI responds naturally
- [ ] Commands are executed
- [ ] Chat history shows

---

## Phase 5: Connect AI Commands to ESP32
**Goal:** Make AI commands actually control the AC

### Step 5.1: Update MQTT Subscription in ESP32
**Time:** 10 minutes

ESP32 already listens to `ac/command` topic through MQTT. The AI just needs to publish to it.

**Verify:** The MQTT broker connection is working and ESP32 receives commands.

### Step 5.2: Add MQTT Publishing to AI Handler
**Time:** 15 minutes

**Update:** `cloudflare_dashboard/src/gemini-handler.js`

Add MQTT publishing function:

```javascript
async function executeCommands(commands, env) {
  for (const cmd of commands) {
    const payload = {
      command: cmd.name,
      ...cmd.params,
      source: 'ai_assistant',
      timestamp: Date.now()
    };
    
    // Log to Firebase
    const commandUrl = `${env.FIREBASE_DB_URL}/commands/${env.FIREBASE_DEVICE_ID}/${Date.now()}.json`;
    await fetch(commandUrl, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    
    // Also write to Firebase state for ESP32 to pick up
    const stateUrl = `${env.FIREBASE_DB_URL}/devices/${env.FIREBASE_DEVICE_ID}/pending_command.json`;
    await fetch(stateUrl, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
  }
}
```

### Step 5.3: Update ESP32 to Watch for AI Commands
**Time:** 15 minutes

**Add to:** `firebase_client.cpp`

```cpp
void checkAICommands() {
  // Check for pending AI commands
  // This runs in loop(), checking Firebase for new commands
}
```

**Alternative:** ESP32 already listens via MQTT, so we can use existing MQTT flow.

### Step 5.4: Test End-to-End
**Time:** 10 minutes

1. Open dashboard
2. Say to AI: "Turn on the AC"
3. Check Serial Monitor - should see:
   ```
   [MQTT] Received: power_on
   [AC] Power: ON
   [Firebase] state -> HTTP 200
   ```

4. Dashboard should update showing AC is ON

### Step 5.5: Final Checkpoint ✓
- [ ] AI chat works on dashboard
- [ ] AI commands execute on ESP32
- [ ] ESP32 responds to commands
- [ ] Dashboard updates in real-time
- [ ] Conversation history saved

---

## Summary of Changes

### Files Created:
1. ✅ `cloudflare_dashboard/src/gemini-handler.js` - AI integration
2. ✅ `AI_ARCHITECTURE.md` - Architecture guide
3. ✅ `IMPLEMENTATION_STEPS.md` - This file

### Files Modified:
1. ✅ `cloudflare_dashboard/src/index.js` - Add AI endpoint
2. ✅ `cloudflare_dashboard/public/index.html` - Add chat UI
3. ✅ `cloudflare_dashboard/public/app.js` - Add chat JavaScript
4. ✅ `firebase_client.cpp/h` - Separate sensors
5. ✅ `AC_IR_Remote.ino` - Update sensor handling

### Configuration:
1. ✅ Gemini API Key added to Cloudflare
2. ✅ Firebase structure updated
3. ✅ Device capabilities defined

---

## Testing Checklist

Before considering done:
- [ ] ESP32 sends sensor data every 15 seconds
- [ ] Dashboard displays all data correctly
- [ ] AI chat responds to messages
- [ ] AI commands execute on ESP32
- [ ] Conversation history saved
- [ ] No errors in browser console
- [ ] No errors in Serial Monitor

---

## Estimated Total Time

- Phase 1 (Fix): ~30 minutes
- Phase 2 (Restructure): ~45 minutes
- Phase 3 (AI Backend): ~40 minutes
- Phase 4 (AI Frontend): ~60 minutes
- Phase 5 (Integration): ~40 minutes

**Total: ~3.5 hours of focused work**

---

## Next Steps After Completion

1. Add voice input (Web Speech API)
2. Add multi-device support
3. Implement automation rules
4. Add user preferences learning
5. Migrate to InfluxDB for better sensor history

---

## Need Help?

If stuck on any step:
1. Check Serial Monitor for ESP32 errors
2. Check browser console (F12) for frontend errors
3. Check Cloudflare Worker logs: `wrangler tail`
4. Verify Firebase data structure in console

**Ready to start Phase 1?**
