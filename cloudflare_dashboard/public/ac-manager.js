// AC Management UI
class ACManager {
  constructor() {
    this.acs = {};
    this.currentAc = null;
    this.init();
  }

  async init() {
    this.setupUI();
    this.setupEventListeners();
    await this.loadAirConditioners();
  }

  async loadAirConditioners() {
    try {
      console.log('Loading ACs from API...');
      const response = await fetch('/api/acs');
      const data = await response.json();
      console.log('Loaded ACs:', data);
      this.acs = data || {};
      this.renderACList();
    } catch (error) {
      console.error('Failed to load ACs:', error);
      this.acs = {};
      this.renderACList(); // Render empty state
    }
  }

  setupUI() {
    // Get the existing AC Manager container from HTML
    const container = document.getElementById('ac-manager');
    if (!container) {
      console.error('AC Manager container not found');
      return;
    }
    
    container.innerHTML = `
      <div class="ac-manager-panel">
        <div class="ac-list-section">
          <div class="section-header">
            <h2>Air Conditioners</h2>
            <button id="add-ac-btn" class="btn-primary">
              <span>+</span> Add AC
            </button>
          </div>
          <div id="ac-list" class="ac-list"></div>
        </div>
        
        <div class="ac-control-section">
          <div id="ac-controls">
            <p class="placeholder">Select an AC to control</p>
          </div>
        </div>
      </div>
      
      <!-- Add/Edit AC Modal -->
      <div id="ac-modal" class="modal">
        <div class="modal-content">
          <span class="close">&times;</span>
          <h2 id="modal-title">Add Air Conditioner</h2>
          <form id="ac-form">
            <div class="form-group">
              <label for="ac-location">Location *</label>
              <input type="text" id="ac-location" required 
                     placeholder="e.g., Living Room, Bedroom">
            </div>
            
            <div class="form-group">
              <label for="ac-name">Display Name</label>
              <input type="text" id="ac-name" 
                     placeholder="Auto-generated from location">
            </div>
            
            <div class="form-group">
              <label for="ac-brand">Brand</label>
              <select id="ac-brand">
                <option value="Panasonic">Panasonic</option>
                <option value="Daikin">Daikin</option>
                <option value="Mitsubishi">Mitsubishi</option>
                <option value="LG">LG</option>
                <option value="Samsung">Samsung</option>
                <option value="Generic">Generic</option>
              </select>
            </div>
            
            <div class="form-row">
              <div class="form-group">
                <label for="ac-temperature">Default Temperature (°C)</label>
                <input type="number" id="ac-temperature" 
                       min="16" max="30" value="24">
              </div>
              
              <div class="form-group">
                <label for="ac-mode">Default Mode</label>
                <select id="ac-mode">
                  <option value="auto">Auto</option>
                  <option value="cool">Cool</option>
                  <option value="heat">Heat</option>
                  <option value="dry">Dry</option>
                  <option value="fan">Fan</option>
                </select>
              </div>
            </div>
            
            <div class="form-row">
              <div class="form-group">
                <label for="ac-fan">Default Fan Speed</label>
                <select id="ac-fan">
                  <option value="auto">Auto</option>
                  <option value="low">Low</option>
                  <option value="med">Medium</option>
                  <option value="high">High</option>
                </select>
              </div>
            </div>
            
            <h3>Automation Thresholds</h3>
            
            <div class="form-row">
              <div class="form-group">
                <label for="ac-humidity">Humidity Threshold (%)</label>
                <input type="number" id="ac-humidity" 
                       min="0" max="100" value="65">
              </div>
              
              <div class="form-group">
                <label for="ac-light">Light Threshold (lux)</label>
                <input type="number" id="ac-light" 
                       min="0" max="1000" value="30">
              </div>
            </div>
            
            <div class="form-actions">
              <button type="button" class="btn-secondary" id="cancel-btn">Cancel</button>
              <button type="submit" class="btn-primary">Save</button>
            </div>
          </form>
        </div>
      </div>
    `;
  }

  setupEventListeners() {
    // Add AC button
    document.getElementById('add-ac-btn').addEventListener('click', () => {
      this.openModal();
    });
    
    // Modal close
    const modal = document.getElementById('ac-modal');
    const closeBtn = modal.querySelector('.close');
    closeBtn.addEventListener('click', () => this.closeModal());
    
    // Cancel button
    document.getElementById('cancel-btn').addEventListener('click', () => {
      this.closeModal();
    });
    
    // Form submit
    document.getElementById('ac-form').addEventListener('submit', (e) => {
      e.preventDefault();
      this.saveAC();
    });
    
    // Close modal on outside click
    window.addEventListener('click', (e) => {
      if (e.target === modal) {
        this.closeModal();
      }
    });
  }

  renderACList() {
    const list = document.getElementById('ac-list');
    list.innerHTML = '';
    
    const acArray = Object.values(this.acs);
    
    if (acArray.length === 0) {
      list.innerHTML = '<p class="no-acs">No air conditioners added yet</p>';
      return;
    }
    
    acArray.forEach(ac => {
      const isSelected = this.currentAc && this.currentAc.id === ac.id;
      const card = document.createElement('div');
      card.className = `ac-card ${ac.state.power === 'on' ? 'active' : ''} ${isSelected ? 'selected' : ''}`;
      card.innerHTML = `
        <div class="ac-card-header">
          <h3>${ac.name}</h3>
          <span class="ac-brand">${ac.brand}</span>
        </div>
        <div class="ac-card-body">
          <div class="ac-status">
            <span class="status-indicator ${ac.state.power === 'on' ? 'on' : 'off'}"></span>
            <span>${ac.state.power === 'on' ? 'ON' : 'OFF'}</span>
          </div>
          <div class="ac-temp">${ac.state.temperature}°C</div>
          <div class="ac-mode">${ac.state.mode} / ${ac.state.fan_speed}</div>
        </div>
        <div class="ac-card-actions">
          <button class="btn-icon" onclick="acManager.selectAC('${ac.id}')">
            <span>⚙️</span> Control
          </button>
          <button class="btn-icon" onclick="acManager.editAC('${ac.id}')">
            <span>✏️</span> Edit
          </button>
          <button class="btn-icon danger" onclick="acManager.deleteAC('${ac.id}')">
            <span>🗑️</span> Delete
          </button>
        </div>
      `;
      
      list.appendChild(card);
    });
  }

  selectAC(acId) {
    this.currentAc = this.acs[acId];
    this.renderACList(); // Re-render to show selected state
    this.renderControls();
  }

  renderControls() {
    if (!this.currentAc) return;
    
    const controls = document.getElementById('ac-controls');
    const ac = this.currentAc;
    
    controls.innerHTML = `
      <div class="ac-control-panel">
        <div class="control-header">
          <div class="control-header-main">
            <h2 class="control-ac-name">${ac.name}</h2>
            <span class="control-ac-brand">${ac.brand}</span>
          </div>
          <p class="control-ac-location">📍 ${ac.location}</p>
          <div class="control-status-badge ${ac.state.power === 'on' ? 'status-on' : 'status-off'}">
            <span class="status-dot"></span>
            ${ac.state.power === 'on' ? 'Currently ON' : 'Currently OFF'}
          </div>
        </div>
        
        <div class="power-control">
          <button class="power-btn ${ac.state.power === 'on' ? 'on' : 'off'}"
                  onclick="acManager.togglePower()">
            <span class="power-icon">${ac.state.power === 'on' ? '⏻' : '⏻'}</span>
            ${ac.state.power === 'on' ? 'Turn OFF' : 'Turn ON'}
          </button>
        </div>
        
        <div class="temp-control">
          <label>Temperature: <strong>${ac.state.temperature}°C</strong></label>
          <div class="temp-controls">
            <button onclick="acManager.adjustTemp(-1)">-</button>
            <input type="range" min="16" max="30" value="${ac.state.temperature}" 
                   onchange="acManager.setTemp(this.value)">
            <button onclick="acManager.adjustTemp(1)">+</button>
          </div>
        </div>
        
        <div class="mode-control">
          <label>Mode</label>
          <div class="mode-buttons">
            ${['auto', 'cool', 'heat', 'dry', 'fan'].map(mode => `
              <button class="mode-btn ${ac.state.mode === mode ? 'active' : ''}"
                      onclick="acManager.setMode('${mode}')">
                ${mode.toUpperCase()}
              </button>
            `).join('')}
          </div>
        </div>
        
        <div class="fan-control">
          <label>Fan Speed</label>
          <div class="fan-buttons">
            ${['auto', 'low', 'med', 'high'].map(speed => `
              <button class="fan-btn ${ac.state.fan_speed === speed ? 'active' : ''}"
                      onclick="acManager.setFanSpeed('${speed}')">
                ${speed.toUpperCase()}
              </button>
            `).join('')}
          </div>
        </div>
        
        <div class="automation-info">
          <h3>Automation Settings</h3>
          <p>Humidity Threshold: ${ac.automation.humidity_threshold}%</p>
          <p>Light Threshold: ${ac.automation.light_threshold} lux</p>
        </div>
      </div>
    `;
  }

  async togglePower() {
    if (!this.currentAc) return;
    
    const newPower = this.currentAc.state.power === 'on' ? 'off' : 'on';
    await this.sendControl({ power: newPower });
  }

  async adjustTemp(delta) {
    if (!this.currentAc) return;
    
    const newTemp = Math.max(16, Math.min(30, this.currentAc.state.temperature + delta));
    await this.sendControl({ temperature: newTemp });
  }

  async setTemp(temp) {
    if (!this.currentAc) return;
    await this.sendControl({ temperature: parseInt(temp) });
  }

  async setMode(mode) {
    if (!this.currentAc) return;
    await this.sendControl({ mode });
  }

  async setFanSpeed(speed) {
    if (!this.currentAc) return;
    await this.sendControl({ fan_speed: speed });
  }

  async sendControl(command) {
    if (!this.currentAc) return;
    
    try {
      const response = await fetch(`/api/acs/${this.currentAc.id}/control`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(command)
      });
      
      const result = await response.json();
      
      if (result.success) {
        // Update local state
        this.currentAc.state = result.data.state;
        this.acs[this.currentAc.id] = this.currentAc;
        
        // Update UI
        this.renderControls();
        this.renderACList();
        
        console.log('Control sent:', result.mqtt_command);
        
        // Publish complete state command to MQTT broker with AC ID
        if (window.mqttPublish && result.mqtt_command) {
          // Send the full mqtt_command from backend which includes ac_id
          console.log('Publishing MQTT command with AC ID:', result.mqtt_command);
          const mqttResult = await window.mqttPublish('ac/command', JSON.stringify(result.mqtt_command));
          
          if (mqttResult.ok) {
            console.log('MQTT command published successfully for AC:', result.mqtt_command.ac_id);
          } else {
            console.warn('MQTT publish warning:', mqttResult.error);
          }
        } else {
          console.warn('MQTT publish function not available - make sure dashboard is loaded');
        }
      } else {
        alert('Failed to control AC: ' + result.error);
      }
    } catch (error) {
      console.error('Control error:', error);
      alert('Failed to send control command');
    }
  }

  openModal(ac = null) {
    const modal = document.getElementById('ac-modal');
    const form = document.getElementById('ac-form');
    
    if (ac) {
      // Edit mode
      document.getElementById('modal-title').textContent = 'Edit Air Conditioner';
      document.getElementById('ac-location').value = ac.location;
      document.getElementById('ac-name').value = ac.name;
      document.getElementById('ac-brand').value = ac.brand;
      document.getElementById('ac-temperature').value = ac.state.temperature;
      document.getElementById('ac-mode').value = ac.state.mode;
      document.getElementById('ac-fan').value = ac.state.fan_speed;
      document.getElementById('ac-humidity').value = ac.automation.humidity_threshold;
      document.getElementById('ac-light').value = ac.automation.light_threshold;
      form.dataset.acId = ac.id;
    } else {
      // Add mode
      document.getElementById('modal-title').textContent = 'Add Air Conditioner';
      form.reset();
      delete form.dataset.acId;
    }
    
    modal.style.display = 'block';
  }

  closeModal() {
    const modal = document.getElementById('ac-modal');
    modal.style.display = 'none';
  }

  async saveAC() {
    const form = document.getElementById('ac-form');
    const acId = form.dataset.acId;
    
    const data = {
      location: document.getElementById('ac-location').value,
      name: document.getElementById('ac-name').value || null,
      brand: document.getElementById('ac-brand').value,
      temperature: parseInt(document.getElementById('ac-temperature').value),
      mode: document.getElementById('ac-mode').value,
      fan_speed: document.getElementById('ac-fan').value,
      humidity_threshold: parseInt(document.getElementById('ac-humidity').value),
      light_threshold: parseInt(document.getElementById('ac-light').value)
    };
    
    console.log('Saving AC with data:', data);
    
    try {
      let response;
      if (acId) {
        // Update existing AC
        console.log('Updating AC:', acId);
        response = await fetch(`/api/acs/${acId}`, {
          method: 'PUT',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(data)
        });
      } else {
        // Create new AC
        console.log('Creating new AC');
        response = await fetch('/api/acs', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(data)
        });
      }
      
      console.log('Response status:', response.status);
      const result = await response.json();
      console.log('Result:', result);
      
      if (result.success) {
        await this.loadAirConditioners();
        this.closeModal();
        alert('AC saved successfully!');
      } else {
        const errorMsg = result.error + (result.details ? '\n\nDetails: ' + JSON.stringify(result.details) : '');
        alert('Failed to save AC:\n' + errorMsg);
        console.error('Save failed:', result);
      }
    } catch (error) {
      console.error('Save error:', error);
      alert('Failed to save AC configuration:\n' + error.message + '\n\nCheck browser console for details.');
    }
  }

  editAC(acId) {
    const ac = this.acs[acId];
    if (ac) {
      this.openModal(ac);
    }
  }

  async deleteAC(acId) {
    const ac = this.acs[acId];
    if (!ac) return;
    
    if (!confirm(`Delete ${ac.name}?`)) return;
    
    try {
      const response = await fetch(`/api/acs/${acId}`, {
        method: 'DELETE'
      });
      
      const result = await response.json();
      
      if (result.success) {
        await this.loadAirConditioners();
        if (this.currentAc && this.currentAc.id === acId) {
          this.currentAc = null;
          document.getElementById('ac-controls').innerHTML = '<p class="placeholder">Select an AC to control</p>';
        }
      } else {
        alert('Failed to delete AC: ' + result.error);
      }
    } catch (error) {
      console.error('Delete error:', error);
      alert('Failed to delete AC');
    }
  }
}

// Global instance
window.acManager = null;

// Initialize when page loads
document.addEventListener('DOMContentLoaded', () => {
  window.acManager = new ACManager();
});
