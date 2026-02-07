// Air Conditioner Management API
// Handle CRUD operations for multiple AC units

export async function getAirConditioners(env) {
  try {
    const authParam = env.FIREBASE_AUTH ? `?auth=${env.FIREBASE_AUTH}` : '';
    const url = `${env.FIREBASE_DB_URL}/air_conditioners.json${authParam}`;
    console.log('Fetching ACs from:', url.replace(env.FIREBASE_AUTH || '', '[REDACTED]'));
    
    const response = await fetch(url);
    const data = await response.json();
    return data || {};
  } catch (error) {
    console.error('Error fetching ACs:', error);
    return {};
  }
}

export async function getAirConditioner(acId, env) {
  try {
    const authParam = env.FIREBASE_AUTH ? `?auth=${env.FIREBASE_AUTH}` : '';
    const url = `${env.FIREBASE_DB_URL}/air_conditioners/${acId}.json${authParam}`;
    const response = await fetch(url);
    return await response.json();
  } catch (error) {
    console.error('Error fetching AC:', error);
    return null;
  }
}

export async function createAirConditioner(acData, env) {
  console.log('Creating AC with data:', acData);
  console.log('Firebase URL:', env.FIREBASE_DB_URL);
  
  const acId = acData.id || generateAcId(acData.location);
  
  const newAc = {
    id: acId,
    name: acData.name || `${acData.location} AC`,
    brand: acData.brand || 'Generic',
    location: acData.location,
    enabled: true,
    state: {
      power: 'off',
      temperature: acData.temperature || 24,
      mode: acData.mode || 'auto',
      fan_speed: acData.fan_speed || 'auto'
    },
    automation: {
      humidity_threshold: acData.humidity_threshold || 65,
      light_threshold: acData.light_threshold || 30,
      auto_on: acData.auto_on || false,
      auto_off: acData.auto_off || false
    },
    created_at: Date.now(),
    last_updated: Date.now()
  };
  
  try {
    const authParam = env.FIREBASE_AUTH ? `?auth=${env.FIREBASE_AUTH}` : '';
    const url = `${env.FIREBASE_DB_URL}/air_conditioners/${acId}.json${authParam}`;
    console.log('Sending PUT to:', url.replace(env.FIREBASE_AUTH || '', '[REDACTED]'));
    console.log('Auth configured:', !!env.FIREBASE_AUTH);
    
    const response = await fetch(url, {
      method: 'PUT',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify(newAc)
    });
    
    console.log('Response status:', response.status);
    const responseData = await response.json();
    console.log('Response data:', responseData);
    
    if (response.ok) {
      return { success: true, data: newAc };
    } else {
      return { success: false, error: `Firebase error: ${response.status}`, details: responseData };
    }
  } catch (error) {
    console.error('Error creating AC:', error);
    return { success: false, error: error.message };
  }
}

export async function updateAirConditioner(acId, updates, env) {
  try {
    // Get existing AC
    const existing = await getAirConditioner(acId, env);
    if (!existing) {
      return { success: false, error: 'AC not found' };
    }
    
    // Merge updates
    const updated = {
      ...existing,
      ...updates,
      last_updated: Date.now()
    };
    
    // If state is being updated, merge it properly
    if (updates.state) {
      updated.state = {
        ...existing.state,
        ...updates.state
      };
    }
    
    // If automation is being updated, merge it properly
    if (updates.automation) {
      updated.automation = {
        ...existing.automation,
        ...updates.automation
      };
    }
    
    const authParam = env.FIREBASE_AUTH ? `?auth=${env.FIREBASE_AUTH}` : '';
    const url = `${env.FIREBASE_DB_URL}/air_conditioners/${acId}.json${authParam}`;
    const response = await fetch(url, {
      method: 'PUT',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify(updated)
    });
    
    if (response.ok) {
      return { success: true, data: updated };
    } else {
      return { success: false, error: `Failed to update AC: ${response.status}` };
    }
  } catch (error) {
    console.error('Error updating AC:', error);
    return { success: false, error: error.message };
  }
}

export async function deleteAirConditioner(acId, env) {
  try {
    const authParam = env.FIREBASE_AUTH ? `?auth=${env.FIREBASE_AUTH}` : '';
    const url = `${env.FIREBASE_DB_URL}/air_conditioners/${acId}.json${authParam}`;
    const response = await fetch(url, {
      method: 'DELETE'
    });
    
    if (response.ok) {
      return { success: true };
    } else {
      return { success: false, error: `Failed to delete AC: ${response.status}` };
    }
  } catch (error) {
    console.error('Error deleting AC:', error);
    return { success: false, error: error.message };
  }
}

export async function controlAirConditioner(acId, command, env) {
  try {
    const ac = await getAirConditioner(acId, env);
    if (!ac) {
      return { success: false, error: 'AC not found' };
    }
    
    // Update AC state based on command
    const newState = { ...ac.state };
    
    if (command.power !== undefined) {
      newState.power = command.power;
    }
    if (command.temperature !== undefined) {
      newState.temperature = Math.max(16, Math.min(30, command.temperature));
    }
    if (command.mode !== undefined) {
      newState.mode = command.mode;
    }
    if (command.fan_speed !== undefined) {
      newState.fan_speed = command.fan_speed;
    }
    
    // Update Firebase
    const result = await updateAirConditioner(acId, { state: newState }, env);
    
    if (result.success) {
      // Send MQTT command to ESP32
      const mqttCommand = {
        type: 'ac_control',
        ac_id: acId,
        ...newState,
        timestamp: Date.now()
      };
      
      // You'll need to publish this to MQTT broker
      // For now, just return the command
      return {
        success: true,
        data: result.data,
        mqtt_command: mqttCommand
      };
    }
    
    return result;
  } catch (error) {
    console.error('Error controlling AC:', error);
    return { success: false, error: error.message };
  }
}

function generateAcId(location) {
  return location
    .toLowerCase()
    .replace(/[^a-z0-9]/g, '_')
    .replace(/_+/g, '_')
    .replace(/^_|_$/g, '');
}
