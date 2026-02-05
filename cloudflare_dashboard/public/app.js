const DEFAULT_DEVICE_ID = "ESP32_AC_Remote_001";
const REFRESH_INTERVAL_MS = 15000;
const ALARM_REFRESH_INTERVAL_MS = 15000;
const MAX_ALARMS = 5;
const activity = [];
const MQTT_WS_URL = "wss://broker.emqx.io:8084/mqtt";
const MQTT_TOPIC_COMMAND = "ac/command";
const MQTT_CLIENT_ID_PREFIX = "ac-dashboard-";
const MQTT_TIMEOUT_MS = 4000;
let firebaseStatePath = "/devices/{device}/state.json";
let firebaseHistoryPath = "/devices/{device}/status_history.json";
let firebaseAlarmsPath = "/devices/{device}/alarms.json";
let firebaseHistoryOrderBy = "timestamp";
const ALLOWED_COMMANDS = new Set([
  "power_on",
  "power_off",
  "power_toggle",
  "temp_up",
  "temp_down",
  "set_temperature",
  "set_mode",
  "set_fan",
  "mode_cycle",
  "fan_cycle",
  "switch_brand",
  "set_brand",
  "set_humidity_threshold",
  "set_light_threshold",
  "get_status",
  "custom",
  "alarm_add",
  "alarm_update",
  "alarm_delete"
]);

const ALLOWED_MODES = new Set(["auto", "cool", "heat", "dry", "fan"]);
const ALLOWED_FANS = new Set(["auto", "low", "medium", "high"]);
const ALLOWED_BRANDS = new Set([
  "daikin",
  "mitsubishi",
  "panasonic",
  "gree",
  "midea",
  "haier",
  "samsung",
  "lg",
  "fujitsu",
  "hitachi"
]);

const statusPill = document.querySelector(".status-pill");
const statusText = document.getElementById("statusText");
const lastUpdated = document.getElementById("lastUpdated");
const powerState = document.getElementById("powerState");
const currentTemp = document.getElementById("currentTemp");
const currentMode = document.getElementById("currentMode");
const currentFan = document.getElementById("currentFan");
const currentBrand = document.getElementById("currentBrand");
const motionState = document.getElementById("motionState");
const dhtTemp = document.getElementById("dhtTemp");
const dhtHumidity = document.getElementById("dhtHumidity");
const lightLux = document.getElementById("lightLux");
const statusTimestamp = document.getElementById("statusTimestamp");
const uptime = document.getElementById("uptime");
const chartMeta = document.getElementById("chartMeta");
const chartTabs = document.getElementById("chartTabs");
const lightChartMeta = document.getElementById("lightChartMeta");
const lightChartTabs = document.getElementById("lightChartTabs");
const activityList = document.getElementById("activityList");
const alarmList = document.getElementById("alarmList");
const alarmCount = document.getElementById("alarmCount");
const alarmTimeInput = document.getElementById("alarmTime");
const alarmNameInput = document.getElementById("alarmName");
const alarmAddBtn = document.getElementById("alarmAddBtn");
const dryThresholdRange = document.getElementById("dryThresholdRange");
const dryThresholdValue = document.getElementById("dryThresholdValue");
const dryThresholdBtn = document.getElementById("dryThresholdBtn");
const dryThresholdDisplay = document.getElementById("dryThresholdDisplay");
const sleepLightRange = document.getElementById("sleepLightRange");
const sleepLightValue = document.getElementById("sleepLightValue");
const sleepLightBtn = document.getElementById("sleepLightBtn");
const sleepLightDisplay = document.getElementById("sleepLightDisplay");
const deviceIdValue = document.getElementById("deviceIdValue");
const powerOnBtn = document.getElementById("powerOnBtn");
const powerOffBtn = document.getElementById("powerOffBtn");
const setTempBtn = document.getElementById("setTempBtn");
const setTempInput = document.getElementById("setTemp");
const brandSelect = document.getElementById("brandSelect");
const setBrandBtn = document.getElementById("setBrandBtn");
let lastStatusSignature = "";
let lastAlarmSignature = "";
const CHART_RANGES = {
  realtime: { label: "Realtime", windowMs: null, bucketMs: null },
  hour: { label: "Hour", windowMs: 60 * 60 * 1000, bucketMs: 60 * 1000 },
  day: { label: "Day", windowMs: 24 * 60 * 60 * 1000, bucketMs: 30 * 60 * 1000 },
  week: { label: "Week", windowMs: 7 * 24 * 60 * 60 * 1000, bucketMs: 4 * 60 * 60 * 1000 }
};
const DEFAULT_CHART_RANGE = "hour";
const PAGE_OPENED_AT_MS = Date.now();
let roomChart;
let chartRange = DEFAULT_CHART_RANGE;
let chartRawPoints = [];
let realtimePoints = [];
let chartRequestId = 0;
let chartLoading = false;
let lightChart;
let lightChartRange = DEFAULT_CHART_RANGE;
let lightChartRawPoints = [];
let lightRealtimePoints = [];
let lightChartRequestId = 0;
let lightChartLoading = false;
const powerControls = Array.from(document.querySelectorAll("[data-requires-power]"));
let statusStream = null;
let alarmsStream = null;
let statusStreamReady = false;
let alarmsStreamReady = false;
const firebaseConfig = {
  baseUrl: "",
  auth: "",
  deviceId: DEFAULT_DEVICE_ID
};

function getDeviceId() {
  return firebaseConfig.deviceId || DEFAULT_DEVICE_ID;
}

function getFirebaseConfig() {
  return { baseUrl: firebaseConfig.baseUrl, auth: firebaseConfig.auth };
}

function buildFirebaseUrl(path, params = {}) {
  const { baseUrl, auth } = getFirebaseConfig();
  if (!baseUrl) {
    throw new Error("Firebase DB URL missing");
  }
  let urlBase = baseUrl;
  if (urlBase.endsWith("/")) {
    urlBase = urlBase.slice(0, -1);
  }
  const url = new URL(urlBase + path);
  if (auth) {
    url.searchParams.set("auth", auth);
  }
  Object.entries(params).forEach(([key, value]) => {
    if (value != null) {
      url.searchParams.set(key, value);
    }
  });
  return url.toString();
}

function normalizeFirebasePath(pathTemplate, deviceId) {
  let path = pathTemplate.replace("{device}", encodeURIComponent(deviceId));
  if (!path.startsWith("/")) {
    path = `/${path}`;
  }
  if (!path.endsWith(".json")) {
    path = `${path}.json`;
  }
  return path;
}

async function loadConfigFromWorker() {
  try {
    const response = await fetch("/config.json", { cache: "no-store" });
    if (!response.ok) {
      throw new Error("Config request failed");
    }
    const data = await response.json();
    applyWorkerConfig(data);
  } catch (error) {
    setStatus("warn", "Worker config unavailable");
  }
}

function applyWorkerConfig(config) {
  if (!config || typeof config !== "object") {
    return;
  }
  const firebase = config.firebase || {};
  if (firebase.url) {
    firebaseConfig.baseUrl = firebase.url;
  }
  if (firebase.auth) {
    firebaseConfig.auth = firebase.auth;
  }
  if (firebase.deviceId) {
    firebaseConfig.deviceId = firebase.deviceId;
  }
  if (deviceIdValue) {
    deviceIdValue.textContent = firebaseConfig.deviceId || "--";
  }
  if (firebase.statePath) {
    firebaseStatePath = firebase.statePath;
  }
  if (firebase.historyPath) {
    firebaseHistoryPath = firebase.historyPath;
  }
  if (firebase.alarmsPath) {
    firebaseAlarmsPath = firebase.alarmsPath;
  }
  if (firebase.historyOrderBy) {
    firebaseHistoryOrderBy = firebase.historyOrderBy;
  }
}

function setStatus(state, text) {
  statusPill.classList.remove("ok", "warn");
  if (state === "ok") {
    statusPill.classList.add("ok");
  } else if (state === "warn") {
    statusPill.classList.add("warn");
  }
  statusText.textContent = text;
}

function logActivity(text) {
  const stamp = new Date().toLocaleTimeString();
  activity.unshift(`${stamp} - ${text}`);
  activity.splice(8);
  activityList.innerHTML = activity.map((item) => `<li>${item}</li>`).join("");
}

function formatNumber(value, unit, decimals = 1) {
  const numberValue = Number(value);
  if (!Number.isFinite(numberValue)) {
    return "--";
  }
  const multiplier = Math.pow(10, decimals);
  const formatted = Math.round(numberValue * multiplier) / multiplier;
  return unit ? `${formatted} ${unit}` : `${formatted}`;
}

function formatTimestamp(seconds) {
  const numberValue = Number(seconds);
  if (!Number.isFinite(numberValue) || numberValue <= 0) {
    return "--";
  }
  const date = new Date(numberValue * 1000);
  return date.toISOString().replace("T", " ").replace("Z", " UTC");
}

function formatUptime(ms) {
  const numberValue = Number(ms);
  if (!Number.isFinite(numberValue)) {
    return "--";
  }
  const totalSeconds = Math.floor(numberValue / 1000);
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  if (hours > 0) {
    return `${hours}h ${minutes}m`;
  }
  if (minutes > 0) {
    return `${minutes}m ${seconds}s`;
  }
  return `${seconds}s`;
}

async function fetchSensorData() {
  const deviceId = getDeviceId();
  try {
    const { baseUrl } = getFirebaseConfig();
    if (!baseUrl) {
      return;
    }
    const path = normalizeFirebasePath(firebaseHistoryPath, deviceId);
    // Simplified query - just get the last entry without ordering
    const url = buildFirebaseUrl(path, {
      limitToLast: "1"
    });
    console.log('[Debug] Fetching sensors from:', url);
    const response = await fetch(url, {
      headers: { "content-type": "application/json" }
    });
    if (!response.ok) {
      console.error('[Debug] Sensor fetch failed:', response.status);
      return;
    }
    const data = await response.json();
    console.log('[Debug] Sensor data received:', data);
    const latestEntry = extractLatestStatus(data);
    console.log('[Debug] Latest entry:', latestEntry);
    if (latestEntry && latestEntry.sensors) {
      console.log('[Debug] Updating sensors:', latestEntry.sensors);
      updateSensorDisplay(latestEntry.sensors, latestEntry.timestamp);
    } else {
      console.warn('[Debug] No sensor data found in latest entry');
    }
  } catch (error) {
    console.error('[Debug] Sensor fetch error:', error);
  }
}

async function fetchStatus() {
  const deviceId = getDeviceId();
  if (!statusStream) {
    setStatus("warn", "Loading status");
  }
  try {
    if (statusStreamReady) {
      return;
    }
    const { baseUrl } = getFirebaseConfig();
    if (!baseUrl) {
      setStatus("warn", "Firebase DB URL required");
      return;
    }
    const path = normalizeFirebasePath(firebaseStatePath, deviceId);
    const url = buildFirebaseUrl(path);
    console.log('[Debug] Fetching status from:', url);
    const response = await fetch(url, {
      headers: { "content-type": "application/json" }
    });
    if (!response.ok) {
      const text = await response.text();
      console.error('[Debug] Fetch failed:', response.status, text);
      throw new Error(text || "Status request failed");
    }
    const data = await response.json();
    console.log('[Debug] Received data:', data);
    const status = extractLatestStatus(data);
    console.log('[Debug] Extracted status:', status);
    if (!status) {
      if (!statusStream) {
        setStatus("warn", "No status yet");
      }
      console.warn('[Debug] No valid status in data');
      return;
    }
    updateStatus(status);
    if (!statusStream) {
      setStatus("ok", "Firebase live");
    }
    lastUpdated.textContent = `Last update: ${new Date().toLocaleTimeString()}`;
    logStatusChange(status);
  } catch (error) {
    console.error('[Debug] Status error:', error);
    if (!statusStream) {
      setStatus("warn", "Status unavailable");
    }
    logActivity("Status error");
  }
}

async function fetchHistorySince(startMs) {
  const deviceId = getDeviceId();
  try {
    const { baseUrl } = getFirebaseConfig();
    if (!baseUrl) {
      return [];
    }
    const path = normalizeFirebasePath(firebaseHistoryPath, deviceId);
    // Fetch all history data without orderBy to avoid index requirement
    const url = buildFirebaseUrl(path);
    const response = await fetch(url, {
      headers: { "content-type": "application/json" }
    });
    if (!response.ok) {
      const text = await response.text();
      throw new Error(text || "History request failed");
    }
    const data = await response.json();
    const allHistory = normalizeHistory(data);
    // Filter client-side by timestamp
    return allHistory.filter(point => point.t >= startMs);
  } catch (error) {
    return [];
  }
}

async function fetchLightHistorySince(startMs) {
  const deviceId = getDeviceId();
  try {
    const { baseUrl } = getFirebaseConfig();
    if (!baseUrl) {
      return [];
    }
    const path = normalizeFirebasePath(firebaseHistoryPath, deviceId);
    // Fetch all light history data without orderBy to avoid index requirement
    const url = buildFirebaseUrl(path);
    const response = await fetch(url, {
      headers: { "content-type": "application/json" }
    });
    if (!response.ok) {
      const text = await response.text();
      throw new Error(text || "Light history request failed");
    }
    const data = await response.json();
    const allHistory = normalizeLightHistory(data);
    // Filter client-side by timestamp
    return allHistory.filter(point => point.t >= startMs);
  } catch (error) {
    return [];
  }
}

async function fetchAlarms() {
  if (!alarmList) {
    return;
  }
  const deviceId = getDeviceId();
  try {
    if (alarmsStreamReady) {
      return;
    }
    const { baseUrl } = getFirebaseConfig();
    if (!baseUrl) {
      return;
    }
    const path = normalizeFirebasePath(firebaseAlarmsPath, deviceId);
    const url = buildFirebaseUrl(path);
    const response = await fetch(url, {
      headers: { "content-type": "application/json" }
    });
    if (!response.ok) {
      const text = await response.text();
      throw new Error(text || "Alarms request failed");
    }
    const data = await response.json();
    const alarms = normalizeAlarmsPayload(data);
    renderAlarms(alarms);
  } catch (error) {
    renderAlarms([]);
    logActivity("Alarm sync failed");
  }
}

function startFirebaseStreams() {
  stopFirebaseStreams();
  const { baseUrl } = getFirebaseConfig();
  if (!baseUrl) {
    setStatus("warn", "Firebase DB URL required");
    return;
  }
  loadChartForRange(chartRange);
  loadLightChartForRange(lightChartRange);
  fetchStatus();
  fetchSensorData();
  fetchAlarms();
  startStatusStream();
  startAlarmsStream();
}

function stopFirebaseStreams() {
  if (statusStream) {
    statusStream.close();
    statusStream = null;
  }
  if (alarmsStream) {
    alarmsStream.close();
    alarmsStream = null;
  }
  statusStreamReady = false;
  alarmsStreamReady = false;
}

function startStatusStream() {
  const deviceId = getDeviceId();
  try {
    const path = normalizeFirebasePath(firebaseStatePath, deviceId);
    const url = buildFirebaseUrl(path);
    statusStream = new EventSource(url);
    statusStreamReady = false;
    statusStream.onopen = () => {
      statusStreamReady = true;
      setStatus("ok", "Live updates");
      logActivity("Status stream connected");
    };
    statusStream.addEventListener("put", handleStatusStreamEvent);
    statusStream.addEventListener("patch", handleStatusStreamEvent);
    statusStream.onerror = () => {
      statusStreamReady = false;
      setStatus("warn", "Firebase stream disconnected");
    };
  } catch (error) {
    statusStreamReady = false;
    setStatus("warn", "Firebase stream error");
  }
}

function startAlarmsStream() {
  if (!alarmList) {
    return;
  }
  const deviceId = getDeviceId();
  try {
    const path = normalizeFirebasePath(firebaseAlarmsPath, deviceId);
    const url = buildFirebaseUrl(path);
    alarmsStream = new EventSource(url);
    alarmsStreamReady = false;
    alarmsStream.onopen = () => {
      alarmsStreamReady = true;
      logActivity("Alarm stream connected");
    };
    alarmsStream.addEventListener("put", handleAlarmsStreamEvent);
    alarmsStream.addEventListener("patch", handleAlarmsStreamEvent);
    alarmsStream.onerror = () => {
      alarmsStreamReady = false;
      logActivity("Alarm stream disconnected");
    };
  } catch (error) {
    alarmsStreamReady = false;
    logActivity("Alarm stream error");
  }
}

function handleStatusStreamEvent(event) {
  const payload = parseStreamEvent(event);
  if (!payload) {
    return;
  }
  const status = extractLatestStatus(payload.data ?? payload);
  if (!status) {
    return;
  }
  updateStatus(status);
  setStatus("ok", "Live updates");
  lastUpdated.textContent = `Last update: ${new Date().toLocaleTimeString()}`;
  logStatusChange(status);
}

function handleAlarmsStreamEvent(event) {
  const payload = parseStreamEvent(event);
  if (!payload) {
    return;
  }
  const alarms = normalizeAlarmsPayload(payload.data ?? payload);
  renderAlarms(alarms);
}

function parseStreamEvent(event) {
  if (!event?.data) {
    return null;
  }
  try {
    return JSON.parse(event.data);
  } catch (error) {
    return null;
  }
}

function normalizeAlarmsPayload(payload) {
  if (!payload || typeof payload !== "object") {
    return [];
  }
  const list = Array.isArray(payload) ? payload : payload.alarms;
  const alarms = [];
  if (Array.isArray(list)) {
    list.forEach((alarm) => {
      const normalized = normalizeAlarmRecord(alarm);
      if (normalized) {
        alarms.push(normalized);
      }
    });
  } else if (typeof list === "object") {
    Object.values(list).forEach((alarm) => {
      const normalized = normalizeAlarmRecord(alarm);
      if (normalized) {
        alarms.push(normalized);
      }
    });
  }
  return alarms.slice(0, MAX_ALARMS);
}

function normalizeAlarmRecord(record) {
  if (!record || typeof record !== "object") {
    return null;
  }
  const hour = Number(record.hour);
  const minute = Number(record.minute);
  if (!Number.isInteger(hour) || !Number.isInteger(minute)) {
    return null;
  }
  const name = record.name != null ? String(record.name) : "";
  const enabled = record.enabled === false ? false : Boolean(record.enabled ?? true);
  return { hour, minute, name, enabled };
}

function renderAlarms(alarms) {
  if (!alarmList || !alarmCount) {
    return;
  }
  const count = alarms.length;
  if (count === 0) {
    alarmCount.textContent = "0 saved";
    if (lastAlarmSignature !== "empty") {
      alarmList.innerHTML = `<div class="alarm-empty">No alarms saved yet.</div>`;
      lastAlarmSignature = "empty";
    }
    return;
  }

  const signature = alarms
    .map((alarm) => `${alarm.hour}:${alarm.minute}:${alarm.enabled}:${alarm.name}`)
    .join("|");
  if (signature === lastAlarmSignature) {
    return;
  }
  lastAlarmSignature = signature;
  logActivity("Alarm list updated");
  alarmCount.textContent = `${count} saved`;

  alarmList.innerHTML = alarms
    .map((alarm, index) => {
      const timeValue = formatAlarmTime(alarm.hour, alarm.minute);
      const nameValue = alarm.name || `alarm clock ${index + 1}`;
      const safeName = escapeHtml(nameValue);
      const statusLabel = alarm.enabled ? "ON" : "OFF";
      return `
        <div class="alarm-item" data-index="${index + 1}">
          <div class="alarm-slot">
            <span class="alarm-index">Alarm ${index + 1}</span>
            <span class="alarm-status">${statusLabel}</span>
          </div>
          <input type="time" class="alarm-time-input" value="${timeValue}" />
          <input type="text" class="alarm-name-input" value="${safeName}" maxlength="31" />
          <div class="alarm-actions">
            <button class="btn ghost alarm-update">Save</button>
            <button class="btn alarm-delete">Delete</button>
          </div>
        </div>
      `;
    })
    .join("");
}

function sanitizeAlarmName(value) {
  if (!value) {
    return "";
  }
  return String(value).trim().slice(0, 31);
}

function formatAlarmTime(hour, minute) {
  const safeHour = Math.min(Math.max(hour, 0), 23);
  const safeMinute = Math.min(Math.max(minute, 0), 59);
  return `${String(safeHour).padStart(2, "0")}:${String(safeMinute).padStart(2, "0")}`;
}

function parseAlarmTime(value) {
  if (!value || typeof value !== "string") {
    return null;
  }
  const parts = value.split(":");
  if (parts.length < 2) {
    return null;
  }
  const hour = Number(parts[0]);
  const minute = Number(parts[1]);
  if (!Number.isInteger(hour) || !Number.isInteger(minute)) {
    return null;
  }
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return null;
  }
  return { hour, minute };
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}
function updateSensorDisplay(sensors, timestamp) {
  console.log('[Debug] updateSensorDisplay called with:', sensors, timestamp);
  if (!sensors) {
    console.warn('[Debug] No sensors object provided');
    return;
  }

  motionState.textContent = `Motion: ${sensors.motion ? "Yes" : "No"}`;

  if (sensors.dht && sensors.dht.valid) {
    console.log('[Debug] DHT data valid:', sensors.dht);
    dhtTemp.textContent = formatNumber(sensors.dht.temperature, "℃", 2);
    dhtHumidity.textContent = formatNumber(sensors.dht.humidity, "%");
    ingestRoomReading(sensors.dht.temperature, sensors.dht.humidity, timestamp);
  } else {
    console.warn('[Debug] DHT data invalid or missing:', sensors.dht);
    dhtTemp.textContent = "--";
    dhtHumidity.textContent = "--";
  }

  if (sensors.light && sensors.light.valid) {
    console.log('[Debug] Light data valid:', sensors.light);
    if (lightLux) {
      lightLux.textContent = formatNumber(sensors.light.lux, "lux");
    }
    ingestLightReading(sensors.light.lux, timestamp);
  } else {
    console.warn('[Debug] Light data invalid or missing:', sensors.light);
    if (lightLux) {
      lightLux.textContent = "--";
    }
  }
}

function updateStatus(status) {
  if (!status) {
    return;
  }
  const isOn = Boolean(status.power);
  setPowerControls(isOn);
  updatePowerButtons(isOn);
  powerState.textContent = `Power: ${status.power ? "On" : "Off"}`;
  currentTemp.textContent = formatNumber(status.temperature, null, 2);
  syncTempInput(status.temperature);
  currentMode.textContent = status.mode || "--";
  currentFan.textContent = status.fan_speed || "--";
  updateModeButtons(status.mode);
  updateFanButtons(status.fan_speed);
  currentBrand.textContent = status.brand || "--";
  updateDryThresholdFromStatus(status);
  updateSleepLightFromStatus(status);
  statusTimestamp.textContent = formatTimestamp(status.timestamp);
  uptime.textContent = formatUptime(status.uptime_ms);

  // Update sensors if present in status
  if (status.sensors) {
    updateSensorDisplay(status.sensors, status.timestamp);
  }

  if (status.brand && brandSelect) {
    const lower = status.brand.toLowerCase();
    if ([...brandSelect.options].some((opt) => opt.value === lower)) {
      brandSelect.value = lower;
    }
  }
}

function setPowerControls(isOn) {
  powerControls.forEach((control) => {
    control.disabled = !isOn;
    control.classList.toggle("is-disabled", !isOn);
  });
}

function updateModeButtons(mode) {
  const modeValue = mode ? String(mode).toLowerCase() : "";
  document.querySelectorAll('[data-command="set_mode"]').forEach((button) => {
    const value = button.getAttribute("data-value");
    button.classList.toggle("is-active", value === modeValue);
  });
}

function updateFanButtons(fan) {
  const fanValue = fan ? String(fan).toLowerCase() : "";
  document.querySelectorAll('[data-command="set_fan"]').forEach((button) => {
    const value = button.getAttribute("data-value");
    button.classList.toggle("is-active", value === fanValue);
  });
}

function updatePowerButtons(isOn) {
  if (!powerOnBtn || !powerOffBtn) {
    return;
  }
  powerOnBtn.classList.toggle("is-active", isOn);
  powerOffBtn.classList.toggle("is-active", !isOn);
  powerOnBtn.disabled = isOn;
  powerOffBtn.disabled = !isOn;
}

function syncTempInput(tempValue) {
  if (!setTempInput) {
    return;
  }
  const numericValue = Number(tempValue);
  if (!Number.isFinite(numericValue)) {
    return;
  }
  setTempInput.value = String(clampTemp(numericValue));
}

function adjustTempInput(delta) {
  if (!setTempInput) {
    return null;
  }
  const currentValue = Number(setTempInput.value);
  const baseValue = Number.isFinite(currentValue) ? currentValue : 24;
  const nextValue = clampTemp(baseValue + delta);
  setTempInput.value = String(nextValue);
  currentTemp.textContent = formatNumber(nextValue, null, 2);
  return nextValue;
}

function clampTemp(value) {
  const min = Number(setTempInput.min) || 16;
  const max = Number(setTempInput.max) || 30;
  return Math.min(Math.max(value, min), max);
}

function updateDryThresholdFromStatus(status) {
  if (!status) {
    return;
  }
  const rawValue =
    status.auto_dry_threshold ?? status.humidity_threshold ?? status.dry_threshold;
  const numericValue = Number(rawValue);
  if (!Number.isFinite(numericValue)) {
    return;
  }
  syncDryThresholdInputs(numericValue);
}

function updateSleepLightFromStatus(status) {
  if (!status) {
    return;
  }
  const rawValue =
    status.sleep_light_threshold ?? status.light_threshold ?? status.auto_sleep_light_threshold;
  const numericValue = Number(rawValue);
  if (!Number.isFinite(numericValue)) {
    return;
  }
  syncSleepLightInputs(numericValue);
}

function syncDryThresholdInputs(value) {
  const clamped = clampDryThreshold(value);
  if (dryThresholdRange) {
    dryThresholdRange.value = String(clamped);
  }
  if (dryThresholdValue) {
    dryThresholdValue.value = String(clamped);
  }
  if (dryThresholdDisplay) {
    dryThresholdDisplay.textContent = `${clamped}%`;
  }
}

function clampDryThreshold(value) {
  const min = dryThresholdRange ? Number(dryThresholdRange.min) : 40;
  const max = dryThresholdRange ? Number(dryThresholdRange.max) : 85;
  const numericValue = Number(value);
  if (!Number.isFinite(numericValue)) {
    return min;
  }
  return Math.min(Math.max(numericValue, min), max);
}

function syncSleepLightInputs(value) {
  const clamped = clampSleepLight(value);
  if (sleepLightRange) {
    sleepLightRange.value = String(clamped);
  }
  if (sleepLightValue) {
    sleepLightValue.value = String(clamped);
  }
  if (sleepLightDisplay) {
    sleepLightDisplay.textContent = `${clamped} lux`;
  }
}

function clampSleepLight(value) {
  const min = sleepLightRange ? Number(sleepLightRange.min) : 1;
  const max = sleepLightRange ? Number(sleepLightRange.max) : 1000;
  const numericValue = Number(value);
  if (!Number.isFinite(numericValue)) {
    return min;
  }
  return Math.min(Math.max(numericValue, min), max);
}

function logStatusChange(status) {
  if (!status) {
    return;
  }
  const signature = [
    status.power,
    status.temperature,
    status.mode,
    status.fan_speed,
    status.brand
  ].join("|");
  if (signature !== lastStatusSignature) {
    lastStatusSignature = signature;
    logActivity("Status changed");
  }
}

function normalizeHistory(raw) {
  if (!raw) {
    return [];
  }
  const points = [];
  if (Array.isArray(raw)) {
    raw.forEach((entry, index) => {
      const point = normalizeHistoryEntry(entry, index);
      if (point) {
        points.push(point);
      }
    });
  } else if (typeof raw === "object") {
    Object.entries(raw).forEach(([key, entry]) => {
      const point = normalizeHistoryEntry(entry, key);
      if (point) {
        points.push(point);
      }
    });
  }
  points.sort((a, b) => a.t - b.t);
  return points;
}

function normalizeLightHistory(raw) {
  if (!raw) {
    return [];
  }
  const points = [];
  if (Array.isArray(raw)) {
    raw.forEach((entry, index) => {
      const point = normalizeLightHistoryEntry(entry, index);
      if (point) {
        points.push(point);
      }
    });
  } else if (typeof raw === "object") {
    Object.entries(raw).forEach(([key, entry]) => {
      const point = normalizeLightHistoryEntry(entry, key);
      if (point) {
        points.push(point);
      }
    });
  }
  points.sort((a, b) => a.t - b.t);
  return points;
}

function extractLatestStatus(data) {
  if (!data) {
    return null;
  }
  if (looksLikeStatus(data)) {
    return data;
  }
  if (Array.isArray(data)) {
    for (let i = data.length - 1; i >= 0; i -= 1) {
      if (data[i]) {
        return data[i];
      }
    }
    return null;
  }
  if (typeof data === "object") {
    const entries = Object.values(data).filter((entry) => entry && typeof entry === "object");
    if (entries.length === 0) {
      return null;
    }
    if (entries.length === 1) {
      return entries[0];
    }
    return entries.reduce((latest, entry) => {
      const latestTime = extractTimestampMs(latest);
      const entryTime = extractTimestampMs(entry);
      if (entryTime === null && latestTime === null) {
        return latest;
      }
      if (entryTime === null) {
        return latest;
      }
      if (latestTime === null) {
        return entry;
      }
      return entryTime > latestTime ? entry : latest;
    }, entries[0]);
  }
  return null;
}

function looksLikeStatus(entry) {
  return (
    entry &&
    typeof entry === "object" &&
    ("power" in entry || "temperature" in entry || "sensors" in entry)
  );
}

function extractTimestampMs(entry) {
  if (!entry || typeof entry !== "object") {
    return null;
  }
  const value = entry.timestamp ?? entry.time ?? entry.t;
  return toTimeMs(value);
}

function normalizeHistoryEntry(entry, key) {
  const reading = extractDhtReading(entry);
  if (!reading) {
    return null;
  }
  const timeMs = resolveHistoryTimestamp(entry, key);
  if (!Number.isFinite(timeMs)) {
    return null;
  }
  return { t: timeMs, temp: reading.temp, humidity: reading.humidity };
}

function normalizeLightHistoryEntry(entry, key) {
  const lux = extractLightLux(entry);
  if (!Number.isFinite(lux)) {
    return null;
  }
  const timeMs = resolveHistoryTimestamp(entry, key);
  if (!Number.isFinite(timeMs)) {
    return null;
  }
  return { t: timeMs, lux };
}

function extractDhtReading(entry) {
  if (!entry || typeof entry !== "object") {
    return null;
  }
  if (entry.sensors && entry.sensors.dht) {
    const temp = Number(entry.sensors.dht.temperature);
    const humidity = Number(entry.sensors.dht.humidity);
    if (Number.isFinite(temp) && Number.isFinite(humidity)) {
      return { temp, humidity };
    }
  }
  if (entry.dht) {
    const temp = Number(entry.dht.temperature);
    const humidity = Number(entry.dht.humidity);
    if (Number.isFinite(temp) && Number.isFinite(humidity)) {
      return { temp, humidity };
    }
  }
  if (Number.isFinite(Number(entry.dht_temperature)) && Number.isFinite(Number(entry.dht_humidity))) {
    return { temp: Number(entry.dht_temperature), humidity: Number(entry.dht_humidity) };
  }
  if (Number.isFinite(Number(entry.temperature)) && Number.isFinite(Number(entry.humidity))) {
    return { temp: Number(entry.temperature), humidity: Number(entry.humidity) };
  }
  return null;
}

function extractLightLux(entry) {
  if (!entry || typeof entry !== "object") {
    return null;
  }
  const candidate =
    entry?.sensors?.light?.lux ??
    entry?.light?.lux ??
    entry?.light_lux ??
    entry?.lux ??
    null;
  const value = Number(candidate);
  if (!Number.isFinite(value)) {
    return null;
  }
  return value;
}

function resolveHistoryTimestamp(entry, key) {
  const candidate = entry?.timestamp ?? entry?.time ?? entry?.t;
  const fromCandidate = toTimeMs(candidate);
  if (Number.isFinite(fromCandidate)) {
    return fromCandidate;
  }

  if (key) {
    const numericKey = Number(key);
    const fromKey = toTimeMs(numericKey);
    if (Number.isFinite(fromKey)) {
      return fromKey;
    }
    const parsed = Date.parse(key);
    if (!Number.isNaN(parsed)) {
      return parsed;
    }
  }

  return null;
}

function toTimeMs(value) {
  const numberValue = Number(value);
  if (!Number.isFinite(numberValue)) {
    return null;
  }
  if (numberValue > 1000000000000) {
    return numberValue;
  }
  if (numberValue > 100000) {
    return numberValue * 1000;
  }
  return null;
}

function buildChart() {
  const canvas = document.getElementById("roomChart");
  if (!canvas || typeof Chart === "undefined") {
    return;
  }

  roomChart = new Chart(canvas, {
    type: "line",
    data: {
      labels: [],
      datasets: [
        {
          label: "Temperature (℃)",
          data: [],
          yAxisID: "yTemp",
          borderColor: "#ff7b3a",
          backgroundColor: "rgba(255, 123, 58, 0.2)",
          borderWidth: 2,
          tension: 0.3,
          fill: true,
          spanGaps: false
        },
        {
          label: "Humidity (%)",
          data: [],
          yAxisID: "yHumidity",
          borderColor: "#2f8f83",
          backgroundColor: "rgba(47, 143, 131, 0.15)",
          borderWidth: 2,
          tension: 0.3,
          fill: true,
          spanGaps: false
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: "index", intersect: false },
      scales: {
        x: {
          grid: { display: false },
          ticks: { maxTicksLimit: 6, color: "#6b7280" }
        },
        yTemp: {
          position: "left",
          grid: { color: "rgba(31, 42, 51, 0.08)" },
          ticks: {
            color: "#6b7280",
            callback: (value) => `${value}℃`
          },
          title: {
            display: true,
            text: "Temperature (℃)",
            color: "#6b7280",
            font: { family: "IBM Plex Mono", size: 11 }
          }
        },
        yHumidity: {
          position: "right",
          grid: { drawOnChartArea: false },
          ticks: {
            color: "#6b7280",
            callback: (value) => `${value}%`
          },
          title: {
            display: true,
            text: "Humidity (%)",
            color: "#6b7280",
            font: { family: "IBM Plex Mono", size: 11 }
          }
        }
      },
      plugins: {
        legend: {
          display: true,
          labels: {
            color: "#1f2a33",
            font: { family: "IBM Plex Mono", size: 11 }
          }
        },
        tooltip: {
          callbacks: {
            label: (context) => {
              const label = context.dataset.label || "";
              const value = context.parsed?.y;
              if (!Number.isFinite(value)) {
                return label;
              }
              if (context.dataset.yAxisID === "yTemp") {
                const rounded = Math.round(value * 100) / 100;
                return `${label}: ${rounded} ℃`;
              }
              if (context.dataset.yAxisID === "yHumidity") {
                const rounded = Math.round(value * 10) / 10;
                return `${label}: ${rounded} %`;
              }
              const rounded = Math.round(value * 10) / 10;
              return `${label}: ${rounded}`;
            }
          }
        }
      }
    }
  });
}

function buildLightChart() {
  const canvas = document.getElementById("lightChart");
  if (!canvas || typeof Chart === "undefined") {
    return;
  }

  lightChart = new Chart(canvas, {
    type: "line",
    data: {
      labels: [],
      datasets: [
        {
          label: "Light (lux)",
          data: [],
          yAxisID: "yLux",
          borderColor: "#f0b35b",
          backgroundColor: "rgba(240, 179, 91, 0.18)",
          borderWidth: 2,
          tension: 0.3,
          fill: true,
          spanGaps: false
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: "index", intersect: false },
      scales: {
        x: {
          grid: { display: false },
          ticks: { maxTicksLimit: 6, color: "#6b7280" }
        },
        yLux: {
          position: "left",
          grid: { color: "rgba(31, 42, 51, 0.08)" },
          ticks: {
            color: "#6b7280",
            callback: (value) => `${value} lux`
          },
          title: {
            display: true,
            text: "Light (lux)",
            color: "#6b7280",
            font: { family: "IBM Plex Mono", size: 11 }
          }
        }
      },
      plugins: {
        legend: {
          display: true,
          labels: {
            color: "#1f2a33",
            font: { family: "IBM Plex Mono", size: 11 }
          }
        },
        tooltip: {
          callbacks: {
            label: (context) => {
              const label = context.dataset.label || "";
              const value = context.parsed?.y;
              if (!Number.isFinite(value)) {
                return label;
              }
              const rounded = Math.round(value * 10) / 10;
              return `${label}: ${rounded} lux`;
            }
          }
        }
      }
    }
  });
}

function initChartRangeTabs() {
  if (!chartTabs) {
    return;
  }
  chartTabs.querySelectorAll("button[data-range]").forEach((button) => {
    button.addEventListener("click", () => {
      const nextRange = button.getAttribute("data-range");
      if (nextRange) {
        loadChartForRange(nextRange);
      }
    });
  });
  setActiveChartTab(chartRange);
  setChartMeta(buildChartMetaText(chartRange));
}

function initLightChartRangeTabs() {
  if (!lightChartTabs) {
    return;
  }
  lightChartTabs.querySelectorAll("button[data-range]").forEach((button) => {
    button.addEventListener("click", () => {
      const nextRange = button.getAttribute("data-range");
      if (nextRange) {
        loadLightChartForRange(nextRange);
      }
    });
  });
  setActiveLightChartTab(lightChartRange);
  setLightChartMeta(buildChartMetaText(lightChartRange));
}

function setChartMeta(text) {
  if (chartMeta) {
    chartMeta.textContent = text;
  }
}

function setLightChartMeta(text) {
  if (lightChartMeta) {
    lightChartMeta.textContent = text;
  }
}

function setChartTabsDisabled(disabled) {
  if (!chartTabs) {
    return;
  }
  chartTabs.querySelectorAll("button[data-range]").forEach((button) => {
    button.disabled = disabled;
  });
}

function setLightChartTabsDisabled(disabled) {
  if (!lightChartTabs) {
    return;
  }
  lightChartTabs.querySelectorAll("button[data-range]").forEach((button) => {
    button.disabled = disabled;
  });
}

function setActiveChartTab(range) {
  if (!chartTabs) {
    return;
  }
  chartTabs.querySelectorAll("button[data-range]").forEach((button) => {
    const isActive = button.getAttribute("data-range") === range;
    button.classList.toggle("is-active", isActive);
    button.setAttribute("aria-pressed", isActive ? "true" : "false");
  });
}

function setActiveLightChartTab(range) {
  if (!lightChartTabs) {
    return;
  }
  lightChartTabs.querySelectorAll("button[data-range]").forEach((button) => {
    const isActive = button.getAttribute("data-range") === range;
    button.classList.toggle("is-active", isActive);
    button.setAttribute("aria-pressed", isActive ? "true" : "false");
  });
}

function buildChartMetaText(range) {
  if (range === "realtime") {
    return "Realtime: since page opened";
  }
  if (range === "hour") {
    return "Past 1 hour · 1-min avg";
  }
  if (range === "day") {
    return "Past 24 hours · 30-min avg";
  }
  if (range === "week") {
    return "Past 7 days · 4-hour avg";
  }
  return "--";
}

function resolveChartWindow(range) {
  const config = CHART_RANGES[range];
  if (!config || !Number.isFinite(config.windowMs) || !Number.isFinite(config.bucketMs)) {
    return null;
  }
  const nowMs = Date.now();
  const endMs = Math.ceil(nowMs / config.bucketMs) * config.bucketMs;
  const startMs = endMs - config.windowMs;
  return { startMs, endMs };
}

async function loadChartForRange(range) {
  if (!CHART_RANGES[range]) {
    return;
  }

  chartRange = range;
  setActiveChartTab(range);

  const { baseUrl } = getFirebaseConfig();
  if (!baseUrl) {
    setChartMeta("Firebase DB URL required");
    return;
  }

  const requestId = (chartRequestId += 1);

  if (range === "realtime") {
    setChartMeta(buildChartMetaText(range));
    const points = await fetchHistorySince(PAGE_OPENED_AT_MS);
    if (requestId !== chartRequestId) {
      return;
    }
    realtimePoints = mergeHistoryPoints(realtimePoints, points.filter((point) => point.t >= PAGE_OPENED_AT_MS));
    renderRealtimeChart();
    return;
  }

  chartLoading = true;
  setChartTabsDisabled(true);
  setChartMeta("Loading...");
  try {
    const window = resolveChartWindow(range);
    if (!window) {
      return;
    }
    const points = await fetchHistorySince(window.startMs);
    if (requestId !== chartRequestId) {
      return;
    }
    chartRawPoints = mergeHistoryPoints(
      chartRawPoints,
      points.filter((point) => point.t >= window.startMs && point.t <= window.endMs)
    ).filter((point) => point.t >= window.startMs && point.t <= window.endMs);
    renderAggregatedChart(range, chartRawPoints, window.startMs, window.endMs);
    setChartMeta(buildChartMetaText(range));
  } finally {
    if (requestId === chartRequestId) {
      chartLoading = false;
      setChartTabsDisabled(false);
    }
  }
}

async function loadLightChartForRange(range) {
  if (!CHART_RANGES[range]) {
    return;
  }

  lightChartRange = range;
  setActiveLightChartTab(range);

  const { baseUrl } = getFirebaseConfig();
  if (!baseUrl) {
    setLightChartMeta("Firebase DB URL required");
    return;
  }

  const requestId = (lightChartRequestId += 1);

  if (range === "realtime") {
    setLightChartMeta(buildChartMetaText(range));
    const points = await fetchLightHistorySince(PAGE_OPENED_AT_MS);
    if (requestId !== lightChartRequestId) {
      return;
    }
    lightRealtimePoints = mergeLightPoints(
      lightRealtimePoints,
      points.filter((point) => point.t >= PAGE_OPENED_AT_MS)
    );
    renderRealtimeLightChart();
    return;
  }

  lightChartLoading = true;
  setLightChartTabsDisabled(true);
  setLightChartMeta("Loading...");
  try {
    const window = resolveChartWindow(range);
    if (!window) {
      return;
    }
    const points = await fetchLightHistorySince(window.startMs);
    if (requestId !== lightChartRequestId) {
      return;
    }
    lightChartRawPoints = mergeLightPoints(
      lightChartRawPoints,
      points.filter((point) => point.t >= window.startMs && point.t <= window.endMs)
    ).filter((point) => point.t >= window.startMs && point.t <= window.endMs);
    renderAggregatedLightChart(range, lightChartRawPoints, window.startMs, window.endMs);
    setLightChartMeta(buildChartMetaText(range));
  } finally {
    if (requestId === lightChartRequestId) {
      lightChartLoading = false;
      setLightChartTabsDisabled(false);
    }
  }
}

function ingestRoomReading(temp, humidity, timestampSeconds) {
  const tempValue = Number(temp);
  const humidityValue = Number(humidity);
  if (!Number.isFinite(tempValue) || !Number.isFinite(humidityValue)) {
    return;
  }

  const timeMs = resolveTimestamp(timestampSeconds);
  const point = { t: timeMs, temp: tempValue, humidity: humidityValue };

  if (timeMs >= PAGE_OPENED_AT_MS) {
    realtimePoints = upsertHistoryPoint(realtimePoints, point);
    if (chartRange === "realtime") {
      renderRealtimeChart();
    }
  }

  const config = CHART_RANGES[chartRange];
  if (config && Number.isFinite(config.windowMs) && chartRange !== "realtime") {
    const window = resolveChartWindow(chartRange);
    if (!window) {
      return;
    }
    chartRawPoints = upsertHistoryPoint(chartRawPoints, point).filter(
      (p) => p.t >= window.startMs && p.t <= window.endMs
    );
    renderAggregatedChart(chartRange, chartRawPoints, window.startMs, window.endMs);
  }
}

function ingestLightReading(lux, timestampSeconds) {
  const luxValue = Number(lux);
  if (!Number.isFinite(luxValue)) {
    return;
  }

  const timeMs = resolveTimestamp(timestampSeconds);
  const point = { t: timeMs, lux: luxValue };

  if (timeMs >= PAGE_OPENED_AT_MS) {
    lightRealtimePoints = upsertLightPoint(lightRealtimePoints, point);
    if (lightChartRange === "realtime") {
      renderRealtimeLightChart();
    }
  }

  const config = CHART_RANGES[lightChartRange];
  if (config && Number.isFinite(config.windowMs) && lightChartRange !== "realtime") {
    const window = resolveChartWindow(lightChartRange);
    if (!window) {
      return;
    }
    lightChartRawPoints = upsertLightPoint(lightChartRawPoints, point).filter(
      (p) => p.t >= window.startMs && p.t <= window.endMs
    );
    renderAggregatedLightChart(lightChartRange, lightChartRawPoints, window.startMs, window.endMs);
  }
}

function resolveTimestamp(timestampSeconds) {
  const seconds = Number(timestampSeconds);
  if (Number.isFinite(seconds) && seconds > 100000) {
    return seconds * 1000;
  }
  return Date.now();
}

function upsertHistoryPoint(points, point) {
  if (points.length === 0) {
    return [point];
  }

  const nextPoints = points.slice();
  const lastPoint = nextPoints[nextPoints.length - 1];
  if (lastPoint && Math.abs(lastPoint.t - point.t) < 10000) {
    lastPoint.temp = point.temp;
    lastPoint.humidity = point.humidity;
    return nextPoints;
  }

  nextPoints.push(point);
  if (lastPoint && point.t >= lastPoint.t) {
    return nextPoints;
  }

  nextPoints.sort((a, b) => a.t - b.t);
  return dedupeHistoryPoints(nextPoints);
}

function mergeHistoryPoints(existing, incoming) {
  if (!incoming || incoming.length === 0) {
    return existing;
  }
  if (!existing || existing.length === 0) {
    return dedupeHistoryPoints(incoming.slice().sort((a, b) => a.t - b.t));
  }

  const combined = existing.concat(incoming);
  combined.sort((a, b) => a.t - b.t);
  return dedupeHistoryPoints(combined);
}

function dedupeHistoryPoints(points) {
  if (points.length <= 1) {
    return points;
  }

  const deduped = [points[0]];
  for (let i = 1; i < points.length; i += 1) {
    const current = points[i];
    const last = deduped[deduped.length - 1];
    if (last && Math.abs(current.t - last.t) < 10000) {
      last.temp = current.temp;
      last.humidity = current.humidity;
      continue;
    }
    deduped.push(current);
  }
  return deduped;
}

function upsertLightPoint(points, point) {
  if (points.length === 0) {
    return [point];
  }

  const nextPoints = points.slice();
  const lastPoint = nextPoints[nextPoints.length - 1];
  if (lastPoint && Math.abs(lastPoint.t - point.t) < 10000) {
    lastPoint.lux = point.lux;
    return nextPoints;
  }

  nextPoints.push(point);
  if (lastPoint && point.t >= lastPoint.t) {
    return nextPoints;
  }

  nextPoints.sort((a, b) => a.t - b.t);
  return dedupeLightPoints(nextPoints);
}

function mergeLightPoints(existing, incoming) {
  if (!incoming || incoming.length === 0) {
    return existing;
  }
  if (!existing || existing.length === 0) {
    return dedupeLightPoints(incoming.slice().sort((a, b) => a.t - b.t));
  }

  const combined = existing.concat(incoming);
  combined.sort((a, b) => a.t - b.t);
  return dedupeLightPoints(combined);
}

function dedupeLightPoints(points) {
  if (points.length <= 1) {
    return points;
  }

  const deduped = [points[0]];
  for (let i = 1; i < points.length; i += 1) {
    const current = points[i];
    const last = deduped[deduped.length - 1];
    if (last && Math.abs(current.t - last.t) < 10000) {
      last.lux = current.lux;
      continue;
    }
    deduped.push(current);
  }
  return deduped;
}

function renderRealtimeChart() {
  const points = realtimePoints.filter((point) => point.t >= PAGE_OPENED_AT_MS);
  updateRoomChart(
    points.map((point) => formatChartLabel(point.t, "realtime")),
    points.map((point) => point.temp),
    points.map((point) => point.humidity)
  );
}

function renderRealtimeLightChart() {
  const points = lightRealtimePoints.filter((point) => point.t >= PAGE_OPENED_AT_MS);
  updateLightChart(
    points.map((point) => formatChartLabel(point.t, "realtime")),
    points.map((point) => point.lux)
  );
}

function renderAggregatedChart(range, points, startMs, endMs) {
  const config = CHART_RANGES[range];
  if (!config || !Number.isFinite(config.bucketMs)) {
    return;
  }

  const series = aggregatePoints(points, startMs, endMs, config.bucketMs, range);
  updateRoomChart(series.labels, series.tempData, series.humidityData);
}

function renderAggregatedLightChart(range, points, startMs, endMs) {
  const config = CHART_RANGES[range];
  if (!config || !Number.isFinite(config.bucketMs)) {
    return;
  }

  const series = aggregateLightPoints(points, startMs, endMs, config.bucketMs, range);
  updateLightChart(series.labels, series.luxData);
}

function updateRoomChart(labels, tempData, humidityData) {
  if (!roomChart) {
    return;
  }
  roomChart.data.labels = labels;
  roomChart.data.datasets[0].data = tempData;
  roomChart.data.datasets[1].data = humidityData;
  roomChart.update("none");
}

function updateLightChart(labels, luxData) {
  if (!lightChart) {
    return;
  }
  lightChart.data.labels = labels;
  lightChart.data.datasets[0].data = luxData;
  lightChart.update("none");
}

function aggregatePoints(points, startMs, endMs, bucketMs, range) {
  const sortedPoints = points.slice().sort((a, b) => a.t - b.t);
  const labels = [];
  const tempData = [];
  const humidityData = [];

  let pointIndex = 0;
  while (pointIndex < sortedPoints.length && sortedPoints[pointIndex].t < startMs) {
    pointIndex += 1;
  }

  for (let bucketStart = startMs; bucketStart < endMs; bucketStart += bucketMs) {
    const bucketEnd = Math.min(bucketStart + bucketMs, endMs);
    labels.push(formatChartLabel(bucketStart, range));

    let tempSum = 0;
    let humiditySum = 0;
    let count = 0;

    while (pointIndex < sortedPoints.length && sortedPoints[pointIndex].t < bucketEnd) {
      const point = sortedPoints[pointIndex];
      if (point.t >= bucketStart && point.t < bucketEnd) {
        tempSum += point.temp;
        humiditySum += point.humidity;
        count += 1;
      }
      pointIndex += 1;
    }

    if (count === 0) {
      tempData.push(null);
      humidityData.push(null);
      continue;
    }

    tempData.push(Math.round((tempSum / count) * 100) / 100);
    humidityData.push(Math.round((humiditySum / count) * 10) / 10);
  }

  return { labels, tempData, humidityData };
}

function aggregateLightPoints(points, startMs, endMs, bucketMs, range) {
  const sortedPoints = points.slice().sort((a, b) => a.t - b.t);
  const labels = [];
  const luxData = [];

  let pointIndex = 0;
  while (pointIndex < sortedPoints.length && sortedPoints[pointIndex].t < startMs) {
    pointIndex += 1;
  }

  for (let bucketStart = startMs; bucketStart < endMs; bucketStart += bucketMs) {
    const bucketEnd = Math.min(bucketStart + bucketMs, endMs);
    labels.push(formatChartLabel(bucketStart, range));

    let luxSum = 0;
    let count = 0;

    while (pointIndex < sortedPoints.length && sortedPoints[pointIndex].t < bucketEnd) {
      const point = sortedPoints[pointIndex];
      if (point.t >= bucketStart && point.t < bucketEnd) {
        luxSum += point.lux;
        count += 1;
      }
      pointIndex += 1;
    }

    if (count === 0) {
      luxData.push(null);
      continue;
    }

    luxData.push(Math.round((luxSum / count) * 10) / 10);
  }

  return { labels, luxData };
}

function formatChartLabel(timeMs, range) {
  const date = new Date(timeMs);
  if (range === "realtime") {
    return date.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  }
  if (range === "week") {
    return date.toLocaleString([], { month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit" });
  }
  return date.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

function buildCommandPayload(command, payload) {
  if (!ALLOWED_COMMANDS.has(command)) {
    return { error: `Unsupported command: ${command}` };
  }

  const commandPayload = { command };

  if (command === "set_temperature") {
    const value = Number(payload.value);
    if (!Number.isFinite(value)) {
      return { error: "set_temperature requires numeric value" };
    }
    commandPayload.value = value;
  }

  if (command === "set_mode") {
    const value = String(payload.value || "").toLowerCase();
    if (!ALLOWED_MODES.has(value)) {
      return { error: "set_mode requires value: auto, cool, heat, dry, fan" };
    }
    commandPayload.value = value;
  }

  if (command === "set_fan") {
    const value = String(payload.value || "").toLowerCase();
    if (!ALLOWED_FANS.has(value)) {
      return { error: "set_fan requires value: auto, low, medium, high" };
    }
    commandPayload.value = value;
  }

  if (command === "set_brand") {
    const value = String(payload.value || "").toLowerCase();
    if (!ALLOWED_BRANDS.has(value)) {
      return { error: "set_brand requires a supported brand" };
    }
    commandPayload.value = value;
  }

  if (command === "set_humidity_threshold") {
    const value = Number(payload.value);
    if (!Number.isFinite(value)) {
      return { error: "set_humidity_threshold requires numeric value" };
    }
    commandPayload.value = clampDryThreshold(value);
  }

  if (command === "set_light_threshold") {
    const value = Number(payload.value);
    if (!Number.isFinite(value)) {
      return { error: "set_light_threshold requires numeric value" };
    }
    commandPayload.value = clampSleepLight(value);
  }

  if (command === "custom") {
    const id = Number(payload.id);
    if (!Number.isInteger(id) || id < 1 || id > 5) {
      return { error: "custom requires id 1-5" };
    }
    commandPayload.id = String(id);
  }

  if (command === "alarm_add") {
    const hour = Number(payload.hour);
    const minute = Number(payload.minute);
    if (!Number.isInteger(hour) || !Number.isInteger(minute) || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
      return { error: "alarm_add requires hour 0-23 and minute 0-59" };
    }
    commandPayload.hour = hour;
    commandPayload.minute = minute;
    const name = sanitizeAlarmName(payload.name);
    if (name) {
      commandPayload.name = name;
    }
  }

  if (command === "alarm_update") {
    const index = Number(payload.index);
    const hour = Number(payload.hour);
    const minute = Number(payload.minute);
    if (!Number.isInteger(index) || index < 1 || index > MAX_ALARMS) {
      return { error: `alarm_update requires index 1-${MAX_ALARMS}` };
    }
    if (!Number.isInteger(hour) || !Number.isInteger(minute) || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
      return { error: "alarm_update requires hour 0-23 and minute 0-59" };
    }
    commandPayload.index = index;
    commandPayload.hour = hour;
    commandPayload.minute = minute;
    const name = sanitizeAlarmName(payload.name);
    if (name) {
      commandPayload.name = name;
    }
  }

  if (command === "alarm_delete") {
    const index = Number(payload.index);
    if (!Number.isInteger(index) || index < 1 || index > MAX_ALARMS) {
      return { error: `alarm_delete requires index 1-${MAX_ALARMS}` };
    }
    commandPayload.index = index;
  }

  return commandPayload;
}

async function mqttPublish(topic, payload) {
  let settled = false;
  let socket;
  let timer;
  const clientId = `${MQTT_CLIENT_ID_PREFIX}${crypto.randomUUID?.() || Date.now()}`;

  const finalize = (result) => {
    if (settled) {
      return;
    }
    settled = true;
    if (timer) {
      clearTimeout(timer);
    }
    if (socket && socket.readyState === WebSocket.OPEN) {
      socket.close();
    }
    return result;
  };

  try {
    socket = new WebSocket(MQTT_WS_URL, "mqtt");
  } catch (error) {
    return { ok: false, error: "MQTT WebSocket unavailable" };
  }

  return new Promise((resolve) => {
    timer = setTimeout(() => {
      resolve(finalize({ ok: false, error: "MQTT timeout" }));
    }, MQTT_TIMEOUT_MS);

    socket.binaryType = "arraybuffer";

    socket.addEventListener("open", () => {
      socket.send(buildConnectPacket(clientId));
    });

    socket.addEventListener("message", (event) => {
      const data = toUint8Array(event.data);
      if (data.length < 2) {
        return;
      }
      const packetType = data[0] >> 4;
      if (packetType === 2) {
        const returnCode = data[3];
        if (returnCode !== 0) {
          resolve(finalize({ ok: false, error: "MQTT CONNACK error" }));
          return;
        }
        socket.send(buildPublishPacket(topic, payload));
        socket.send(new Uint8Array([0xe0, 0x00]));
        resolve(finalize({ ok: true }));
      }
    });

    socket.addEventListener("error", () => {
      resolve(finalize({ ok: false, error: "MQTT websocket error" }));
    });

    socket.addEventListener("close", () => {
      resolve(finalize({ ok: false, error: "MQTT connection closed" }));
    });
  });
}

function buildConnectPacket(clientId) {
  const protocolName = encodeString("MQTT");
  const protocolLevel = new Uint8Array([0x04]);
  const connectFlags = new Uint8Array([0x02]);
  const keepAlive = new Uint8Array([0x00, 0x3c]);
  const clientIdBytes = encodeString(clientId);

  const variableHeader = concatBuffers(protocolName, protocolLevel, connectFlags, keepAlive);
  const remainingLength = variableHeader.length + clientIdBytes.length;
  const fixedHeader = new Uint8Array([0x10, ...encodeRemainingLength(remainingLength)]);

  return concatBuffers(fixedHeader, variableHeader, clientIdBytes);
}

function buildPublishPacket(topic, message) {
  const topicBytes = encodeString(topic);
  const payload = new TextEncoder().encode(message);
  const remainingLength = topicBytes.length + payload.length;
  const fixedHeader = new Uint8Array([0x30, ...encodeRemainingLength(remainingLength)]);
  return concatBuffers(fixedHeader, topicBytes, payload);
}

function encodeString(value) {
  const encoder = new TextEncoder();
  const bytes = encoder.encode(value);
  const length = bytes.length;
  return concatBuffers(new Uint8Array([length >> 8, length & 0xff]), bytes);
}

function encodeRemainingLength(length) {
  const bytes = [];
  let remaining = length;
  do {
    let encoded = remaining % 128;
    remaining = Math.floor(remaining / 128);
    if (remaining > 0) {
      encoded |= 0x80;
    }
    bytes.push(encoded);
  } while (remaining > 0);
  return bytes;
}

function concatBuffers(...buffers) {
  const totalLength = buffers.reduce((sum, buf) => sum + buf.length, 0);
  const output = new Uint8Array(totalLength);
  let offset = 0;
  buffers.forEach((buffer) => {
    output.set(buffer, offset);
    offset += buffer.length;
  });
  return output;
}

function toUint8Array(data) {
  if (data instanceof ArrayBuffer) {
    return new Uint8Array(data);
  }
  if (ArrayBuffer.isView(data)) {
    return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  }
  if (typeof data === "string") {
    return new TextEncoder().encode(data);
  }
  return new Uint8Array();
}

async function sendCommand(command, extra = {}) {
  try {
    const commandPayload = buildCommandPayload(command, extra);
    if (commandPayload.error) {
      throw new Error(commandPayload.error);
    }

    setStatus("warn", `Sending ${command}`);
    const result = await mqttPublish(MQTT_TOPIC_COMMAND, JSON.stringify(commandPayload));
    if (!result.ok) {
      throw new Error(result.error || "MQTT publish failed");
    }

    logActivity(`Command sent: ${command}`);
    setTimeout(fetchStatus, 2000);
  } catch (error) {
    setStatus("warn", "Command failed");
    logActivity(`Command error: ${command}`);
  }
}

async function sendAlarmCommand(command, payload) {
  await sendCommand(command, payload);
  setTimeout(fetchAlarms, 2000);
}

document.querySelectorAll("[data-command]").forEach((button) => {
  button.addEventListener("click", () => {
    const command = button.getAttribute("data-command");
    const value = button.getAttribute("data-value");
    const id = button.getAttribute("data-id");
    const payload = {};
    if (value) {
      payload.value = value;
    }
    if (id) {
      payload.id = Number(id);
    }
    if (command === "temp_up") {
      adjustTempInput(1);
    }
    if (command === "temp_down") {
      adjustTempInput(-1);
    }
    sendCommand(command, payload);
  });
});

if (alarmAddBtn) {
  alarmAddBtn.addEventListener("click", async () => {
    const timeValue = alarmTimeInput?.value;
    const parsed = parseAlarmTime(timeValue);
    if (!parsed) {
      setStatus("warn", "Invalid alarm time");
      logActivity("Alarm add failed");
      return;
    }
    const name = sanitizeAlarmName(alarmNameInput?.value || "");
    await sendAlarmCommand("alarm_add", { hour: parsed.hour, minute: parsed.minute, name });
    if (alarmNameInput) {
      alarmNameInput.value = "";
    }
  });
}

if (alarmList) {
  alarmList.addEventListener("click", async (event) => {
    const target = event.target;
    if (!(target instanceof HTMLElement)) {
      return;
    }
    const row = target.closest(".alarm-item");
    if (!row) {
      return;
    }
    const index = Number(row.dataset.index);
    if (!Number.isInteger(index)) {
      return;
    }
    if (target.classList.contains("alarm-delete")) {
      await sendAlarmCommand("alarm_delete", { index });
      return;
    }
    if (target.classList.contains("alarm-update")) {
      const timeValue = row.querySelector(".alarm-time-input")?.value;
      const parsed = parseAlarmTime(timeValue);
      if (!parsed) {
        setStatus("warn", "Invalid alarm time");
        logActivity("Alarm update failed");
        return;
      }
      const nameValue = row.querySelector(".alarm-name-input")?.value;
      const name = sanitizeAlarmName(nameValue || "");
      await sendAlarmCommand("alarm_update", {
        index,
        hour: parsed.hour,
        minute: parsed.minute,
        name
      });
    }
  });
}

setTempBtn.addEventListener("click", () => {
  const value = Number(setTempInput.value);
  if (Number.isFinite(value)) {
    currentTemp.textContent = formatNumber(value, null, 2);
    sendCommand("set_temperature", { value });
  }
});

setBrandBtn.addEventListener("click", () => {
  sendCommand("set_brand", { value: brandSelect.value });
});

if (dryThresholdRange && dryThresholdValue) {
  dryThresholdRange.addEventListener("input", () => {
    const value = clampDryThreshold(dryThresholdRange.value);
    dryThresholdValue.value = String(value);
    if (dryThresholdDisplay) {
      dryThresholdDisplay.textContent = `${value}%`;
    }
  });

  dryThresholdValue.addEventListener("input", () => {
    const value = clampDryThreshold(dryThresholdValue.value);
    dryThresholdRange.value = String(value);
    if (dryThresholdDisplay) {
      dryThresholdDisplay.textContent = `${value}%`;
    }
  });
}

if (dryThresholdBtn) {
  dryThresholdBtn.addEventListener("click", () => {
    if (!dryThresholdValue) {
      return;
    }
    const value = clampDryThreshold(dryThresholdValue.value);
    sendCommand("set_humidity_threshold", { value });
  });
}

if (sleepLightRange && sleepLightValue) {
  sleepLightRange.addEventListener("input", () => {
    const value = clampSleepLight(sleepLightRange.value);
    sleepLightValue.value = String(value);
    if (sleepLightDisplay) {
      sleepLightDisplay.textContent = `${value} lux`;
    }
  });

  sleepLightValue.addEventListener("input", () => {
    const value = clampSleepLight(sleepLightValue.value);
    sleepLightRange.value = String(value);
    if (sleepLightDisplay) {
      sleepLightDisplay.textContent = `${value} lux`;
    }
  });
}

if (sleepLightBtn) {
  sleepLightBtn.addEventListener("click", () => {
    if (!sleepLightValue) {
      return;
    }
    const value = clampSleepLight(sleepLightValue.value);
    sendCommand("set_light_threshold", { value });
  });
}

buildChart();
buildLightChart();
initChartRangeTabs();
initLightChartRangeTabs();
if (dryThresholdRange) {
  syncDryThresholdInputs(Number(dryThresholdRange.value));
}
if (sleepLightRange) {
  syncSleepLightInputs(Number(sleepLightRange.value));
}
loadConfigFromWorker().then(startFirebaseStreams);
setPowerControls(false);
updatePowerButtons(false);
setInterval(fetchStatus, REFRESH_INTERVAL_MS);
setInterval(fetchSensorData, REFRESH_INTERVAL_MS);
setInterval(fetchAlarms, ALARM_REFRESH_INTERVAL_MS);
