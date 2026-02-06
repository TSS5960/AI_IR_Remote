/**
 * AI Assistant Handler for Home Automation
 * Integrates ChatGPT for natural language control
 */

export async function handleAIChat(request, env) {
  const { message, sessionId } = await request.json();
  
  // Get current device state from Firebase
  const stateUrl = `${env.FIREBASE_DB_URL}/devices/${env.FIREBASE_DEVICE_ID}/state.json`;
  const stateResponse = await fetch(stateUrl);
  const deviceState = await stateResponse.json();
  
  // Build system context
  const systemContext = buildSystemContext(deviceState);
  
  // Call ChatGPT
  const chatResponse = await fetch('https://api.openai.com/v1/chat/completions', {
    method: 'POST',
    headers: {
      'Authorization': `Bearer ${env.OPENAI_API_KEY}`,
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      model: 'gpt-4-turbo',
      messages: [
        { role: 'system', content: systemContext },
        { role: 'user', content: message }
      ],
      functions: getDeviceFunctions(),
      function_call: 'auto'
    })
  });
  
  const data = await chatResponse.json();
  
  // Execute function if called
  if (data.choices[0].message.function_call) {
    const result = await executeFunction(
      data.choices[0].message.function_call,
      env
    );
    return jsonResponse({ message: data.choices[0].message, result });
  }
  
  return jsonResponse({ message: data.choices[0].message.content });
}

function buildSystemContext(deviceState) {
  return `You are a smart home assistant controlling an AC unit.

Current Device State:
- Power: ${deviceState.power ? 'ON' : 'OFF'}
- Temperature: ${deviceState.temperature}°C
- Mode: ${deviceState.mode}
- Fan Speed: ${deviceState.fan_speed}
${deviceState.sensors ? `
Current Room Conditions:
- Temperature: ${deviceState.sensors.dht?.temperature}°C
- Humidity: ${deviceState.sensors.dht?.humidity}%
- Light: ${deviceState.sensors.light?.lux} lux
- Motion: ${deviceState.sensors.motion ? 'Detected' : 'None'}
` : ''}

You can control the AC using these functions. Be conversational and helpful.`;
}

function getDeviceFunctions() {
  return [
    {
      name: 'control_ac_power',
      description: 'Turn AC on or off',
      parameters: {
        type: 'object',
        properties: {
          action: {
            type: 'string',
            enum: ['on', 'off', 'toggle'],
            description: 'Power action'
          }
        },
        required: ['action']
      }
    },
    {
      name: 'set_ac_temperature',
      description: 'Set AC temperature',
      parameters: {
        type: 'object',
        properties: {
          temperature: {
            type: 'number',
            minimum: 16,
            maximum: 30,
            description: 'Target temperature in Celsius'
          }
        },
        required: ['temperature']
      }
    },
    {
      name: 'set_ac_mode',
      description: 'Change AC operating mode',
      parameters: {
        type: 'object',
        properties: {
          mode: {
            type: 'string',
            enum: ['auto', 'cool', 'heat', 'dry', 'fan'],
            description: 'AC operating mode'
          }
        },
        required: ['mode']
      }
    }
  ];
}

async function executeFunction(functionCall, env) {
  const { name, arguments: args } = functionCall;
  const params = JSON.parse(args);
  
  // Publish MQTT command
  const command = mapFunctionToCommand(name, params);
  
  // Here you would publish to MQTT or call your command API
  // For now, return the command that would be executed
  
  return { success: true, command };
}

function mapFunctionToCommand(functionName, params) {
  const commandMap = {
    'control_ac_power': () => ({
      command: params.action === 'toggle' ? 'power_toggle' : `power_${params.action}`
    }),
    'set_ac_temperature': () => ({
      command: 'set_temperature',
      value: params.temperature
    }),
    'set_ac_mode': () => ({
      command: 'set_mode',
      value: params.mode
    })
  };
  
  return commandMap[functionName]();
}

function jsonResponse(data) {
  return new Response(JSON.stringify(data), {
    headers: { 'Content-Type': 'application/json' }
  });
}
