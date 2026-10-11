
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Update.h>
#include <string.h>

// =====================================================
// SMARTPARK - ESP32 #1 MASTER
// Dashboard + ESP-NOW + SELF FIRMWARE UPDATE
// Firmware update applies ONLY to ESP32 #1.
// =====================================================

// ---------------- WIFI SETTINGS ----------------

const char* AP_SSID = "SmartCarPark";
const char* AP_PASSWORD = "12345678";

const uint8_t WIFI_CHANNEL = 6;

// ---------------- ESP32 #2 MAC ADDRESS ----------------
// Confirm this matches the MAC printed by ESP32 #2.

uint8_t slaveMAC[] = {
  0x8C, 0x94, 0xDF, 0x4D, 0xA9, 0x18
};

// ---------------- SERVERS ----------------

WebServer server(80);
WebSocketsServer webSocket(81);

// ---------------- SYSTEM STATE ----------------

bool autoMode = true;
bool entryOpen = false;
bool exitOpen = false;
bool occupied[4] = {false, false, false, false};

String lastMessage = "System starting";
String updateError = "";

unsigned long lastStatusBroadcast = 0;
const unsigned long STATUS_INTERVAL = 500;

// Firmware upload state
bool updateStarted = false;
bool updateSucceeded = false;
size_t firmwareBytesWritten = 0;

// =====================================================
// ESP-NOW COMMAND SENDING
// =====================================================

void sendCommand(const String& command) {
  esp_err_t result = esp_now_send(
    slaveMAC,
    (const uint8_t*)command.c_str(),
    command.length() + 1
  );

  Serial.print("[ESP-NOW] Command: ");
  Serial.print(command);
  Serial.print(" | Result: ");
  Serial.println(result == ESP_OK ? "queued" : "send error");
}

// =====================================================
// STATUS JSON
// =====================================================

String makeStatus() {
  String json = "{";

  json += "\"mode\":\"";
  json += autoMode ? "AUTO" : "MANUAL";
  json += "\",";

  json += "\"entry\":\"";
  json += entryOpen ? "OPEN" : "CLOSED";
  json += "\",";

  json += "\"exit\":\"";
  json += exitOpen ? "OPEN" : "CLOSED";
  json += "\",";

  json += "\"slots\":[";

  for (int i = 0; i < 4; i++) {
    json += "\"";
    json += occupied[i] ? "FULL" : "FREE";
    json += "\"";

    if (i < 3) {
      json += ",";
    }
  }

  json += "],";

  json += "\"message\":\"";
  String safeMessage = lastMessage;
  safeMessage.replace("\\", "\\\\");
  safeMessage.replace("\"", "\\\"");
  safeMessage.replace("\n", " ");
  safeMessage.replace("\r", " ");
  json += safeMessage;
  json += "\"";

  json += "}";

  return json;
}

void broadcastStatus() {
  String status = makeStatus();
  webSocket.broadcastTXT(status);
}

// =====================================================
// ESP-NOW RECEIVE
// Expected messages from ESP32 #2 include:
// ENTRY=OPEN / ENTRY=CLOSED
// EXIT=OPEN / EXIT=CLOSED
// MODE=AUTO / MODE=MANUAL
// SLOT1=FULL / SLOT1=FREE ... SLOT4
// PARKING=FULL
// =====================================================

void onDataReceive(
  const esp_now_recv_info_t* info,
  const uint8_t* data,
  int len
) {
  if (data == nullptr || len <= 0) {
    return;
  }

  char buffer[160];

  int copyLength = len;
  if (copyLength >= (int)sizeof(buffer)) {
    copyLength = sizeof(buffer) - 1;
  }

  memcpy(buffer, data, copyLength);
  buffer[copyLength] = '\0';

  String message(buffer);
  message.trim();

  Serial.print("[ESP-NOW] Received: ");
  Serial.println(message);

  lastMessage = message;

  if (message == "ENTRY=OPEN") {
    entryOpen = true;
  } else if (message == "ENTRY=CLOSED") {
    entryOpen = false;
  } else if (message == "EXIT=OPEN") {
    exitOpen = true;
  } else if (message == "EXIT=CLOSED") {
    exitOpen = false;
  } else if (message == "MODE=AUTO") {
    autoMode = true;
  } else if (message == "MODE=MANUAL") {
    autoMode = false;
  } else if (message.startsWith("SLOT")) {
    int equalsPos = message.indexOf('=');

    if (equalsPos > 4) {
      int slotNumber = message.substring(4, equalsPos).toInt();
      String slotStatus = message.substring(equalsPos + 1);
      slotStatus.trim();

      if (slotNumber >= 1 && slotNumber <= 4) {
        if (slotStatus == "FULL") {
          occupied[slotNumber - 1] = true;
        } else if (slotStatus == "FREE") {
          occupied[slotNumber - 1] = false;
        }
      }
    }
  }

  // Do not call WebSocket broadcast from the ESP-NOW callback.
  // The main loop broadcasts the latest state.
}

// =====================================================
// ESP-NOW SEND CALLBACK - ESP32 CORE 3.x
// =====================================================

void onDataSent(
  const wifi_tx_info_t* info,
  esp_now_send_status_t status
) {
  Serial.print("[ESP-NOW] Send status: ");
  Serial.println(
    status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILED"
  );
}

// =====================================================
// DASHBOARD COMMAND HANDLER
// Commands are forwarded to ESP32 #2.
// =====================================================

void handleCommand(String command) {
  command.trim();

  Serial.print("[DASHBOARD] ");
  Serial.println(command);

  if (command == "AUTO") {
    autoMode = true;
    sendCommand("AUTO");
  } else if (command == "MANUAL") {
    autoMode = false;
    sendCommand("MANUAL");
  } else if (command == "OPEN_ENTRY") {
    sendCommand("OPEN_ENTRY");
  } else if (command == "CLOSE_ENTRY") {
    sendCommand("CLOSE_ENTRY");
  } else if (command == "OPEN_EXIT") {
    sendCommand("OPEN_EXIT");
  } else if (command == "CLOSE_EXIT") {
    sendCommand("CLOSE_EXIT");
  } else if (command == "ALL_ON") {
    sendCommand("ALL_ON");
  } else if (command == "ALL_OFF") {
    sendCommand("ALL_OFF");
  } else if (command.startsWith("GUIDE=")) {
    sendCommand(command);
  } else if (command.startsWith("PARKED=")) {
    sendCommand(command);
  } else if (command.startsWith("EXIT=")) {
    sendCommand(command);
  } else {
    Serial.println("[DASHBOARD] Unknown command");
    return;
  }

  broadcastStatus();
}

// =====================================================
// MAIN DASHBOARD HTML
// =====================================================

const char DASHBOARD_HTML[] PROGMEM = R"SMARTPARK(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SmartPark Control Center</title>

<style>
:root {
  --bg: #0b1120;
  --panel: #141e30;
  --panel2: #19263b;
  --border: #2a3951;
  --text: #eaf0fa;
  --muted: #94a3b8;
  --blue: #4f8cff;
  --green: #20c997;
  --red: #ff667a;
  --amber: #f6bd60;
}

* { box-sizing: border-box; }

body {
  margin: 0;
  font-family: Arial, Helvetica, sans-serif;
  color: var(--text);
  background:
    radial-gradient(circle at top right, #1a2a47 0, transparent 35%),
    var(--bg);
}

header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 16px;
  padding: 22px 5%;
  border-bottom: 1px solid var(--border);
  background: rgba(11,17,32,.9);
}

.brand {
  font-size: 22px;
  font-weight: 800;
  letter-spacing: .4px;
}

.brand span { color: var(--blue); }

.header-right {
  display: flex;
  gap: 10px;
  align-items: center;
  flex-wrap: wrap;
}

.badge {
  display: inline-flex;
  gap: 8px;
  align-items: center;
  border: 1px solid var(--border);
  background: var(--panel);
  color: var(--muted);
  border-radius: 20px;
  padding: 9px 12px;
  font-size: 12px;
}

.dot {
  width: 8px;
  height: 8px;
  background: var(--amber);
  border-radius: 50%;
}

.dot.online { background: var(--green); }

main {
  width: min(1150px, 92%);
  margin: 30px auto 60px;
}

.intro { margin-bottom: 24px; }

h1 {
  font-size: clamp(26px, 4vw, 36px);
  margin: 0 0 8px;
}

p {
  color: var(--muted);
  line-height: 1.55;
}

.grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 14px;
}

.card {
  background: linear-gradient(150deg, var(--panel2), var(--panel));
  border: 1px solid var(--border);
  border-radius: 16px;
  padding: 20px;
  min-width: 0;
}

.metric-label {
  font-size: 12px;
  color: var(--muted);
  text-transform: uppercase;
  letter-spacing: .8px;
}

.metric {
  font-size: 26px;
  font-weight: 800;
  margin: 12px 0 5px;
}

.small {
  font-size: 12px;
  color: var(--muted);
}

.section-title {
  margin: 30px 0 14px;
  font-size: 18px;
}

.slots {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 14px;
}

.slot {
  border-radius: 14px;
  padding: 18px;
  border: 1px solid var(--border);
  background: var(--panel);
}

.slot h3 {
  margin: 0 0 12px;
  font-size: 15px;
}

.slot-state {
  font-weight: 700;
  font-size: 13px;
}

.free { color: var(--green); }
.full { color: var(--red); }

.controls {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 14px;
}

.control-card h3 { margin-top: 0; }

.button-row {
  display: flex;
  flex-wrap: wrap;
  gap: 9px;
  margin-top: 14px;
}

button, .link-button {
  border: 1px solid var(--border);
  border-radius: 9px;
  padding: 11px 14px;
  background: #25344b;
  color: var(--text);
  font-weight: 700;
  cursor: pointer;
  text-decoration: none;
  font-size: 13px;
}

button:hover, .link-button:hover {
  filter: brightness(1.15);
}

button.primary, .link-button.primary {
  background: var(--blue);
  border-color: var(--blue);
  color: white;
}

button.success { background: #116b57; }
button.danger { background: #792d40; }
button:disabled { opacity: .5; cursor: not-allowed; }

.status-line {
  margin-top: 18px;
  background: #0d1728;
  border: 1px solid var(--border);
  padding: 13px;
  border-radius: 10px;
  font-size: 13px;
  color: var(--muted);
  overflow-wrap: anywhere;
}

footer {
  color: var(--muted);
  text-align: center;
  padding: 25px 10px;
  font-size: 12px;
  border-top: 1px solid var(--border);
}

@media (max-width: 850px) {
  .grid, .slots { grid-template-columns: repeat(2, minmax(0, 1fr)); }
}

@media (max-width: 600px) {
  header { padding: 18px 4%; }
  main { width: 92%; margin-top: 22px; }
  .controls { grid-template-columns: 1fr; }
  .card { padding: 16px; }
}
</style>
</head>

<body>
<header>
  <div class="brand">SMART<span>PARK</span> <small>CONTROL</small></div>

  <div class="header-right">
    <div class="badge">
      <span class="dot" id="connectionDot"></span>
      <span id="connectionText">Connecting...</span>
    </div>
    <a class="link-button primary" href="/firmware">ESP32 #1 Firmware Update</a>
  </div>
</header>

<main>
  <div class="intro">
    <h1>Control Center</h1>
    <p>Monitor parking availability, control the gates and lighting, and manage the master firmware.</p>
  </div>

  <div class="grid">
    <div class="card">
      <div class="metric-label">Operating Mode</div>
      <div class="metric" id="modeValue">AUTO</div>
      <div class="small">Automatic or manual control</div>
    </div>

    <div class="card">
      <div class="metric-label">Available Slots</div>
      <div class="metric" id="freeValue">4 / 4</div>
      <div class="small">Parking capacity</div>
    </div>

    <div class="card">
      <div class="metric-label">Entrance Gate</div>
      <div class="metric" id="entryValue">CLOSED</div>
      <div class="small">Gate controller status</div>
    </div>

    <div class="card">
      <div class="metric-label">Exit Gate</div>
      <div class="metric" id="exitValue">CLOSED</div>
      <div class="small">Gate controller status</div>
    </div>
  </div>

  <h2 class="section-title">Parking Slots</h2>

  <div class="slots">
    <div class="slot">
      <h3>Slot 01</h3>
      <div class="slot-state free" id="slot0">FREE</div>
    </div>
    <div class="slot">
      <h3>Slot 02</h3>
      <div class="slot-state free" id="slot1">FREE</div>
    </div>
    <div class="slot">
      <h3>Slot 03</h3>
      <div class="slot-state free" id="slot2">FREE</div>
    </div>
    <div class="slot">
      <h3>Slot 04</h3>
      <div class="slot-state free" id="slot3">FREE</div>
    </div>
  </div>

  <h2 class="section-title">System Controls</h2>

  <div class="controls">
    <div class="card control-card">
      <h3>Operating Mode</h3>
      <p>Choose how the parking gates are managed.</p>
      <div class="button-row">
        <button class="primary" onclick="sendCommand('AUTO')">Automatic Mode</button>
        <button onclick="sendCommand('MANUAL')">Manual Mode</button>
      </div>
    </div>

    <div class="card control-card">
      <h3>Entrance Gate</h3>
      <p>Send an open or close command to the gate controller.</p>
      <div class="button-row">
        <button class="success" onclick="sendCommand('OPEN_ENTRY')">Open Entrance</button>
        <button class="danger" onclick="sendCommand('CLOSE_ENTRY')">Close Entrance</button>
      </div>
    </div>

    <div class="card control-card">
      <h3>Exit Gate</h3>
      <p>Control the exit gate independently.</p>
      <div class="button-row">
        <button class="success" onclick="sendCommand('OPEN_EXIT')">Open Exit</button>
        <button class="danger" onclick="sendCommand('CLOSE_EXIT')">Close Exit</button>
      </div>
    </div>

    <div class="card control-card">
      <h3>Lighting Control</h3>
      <p>Manage the common and parking lights. Automatic LDR operation remains with the lighting controller.</p>
      <div class="button-row">
        <button class="success" onclick="sendCommand('ALL_ON')">All Lights ON</button>
        <button class="danger" onclick="sendCommand('ALL_OFF')">All Lights OFF</button>
      </div>
      <div class="button-row">
        <button onclick="sendCommand('GUIDE=1')">Guide Slot 1</button>
        <button onclick="sendCommand('GUIDE=2')">Guide Slot 2</button>
        <button onclick="sendCommand('GUIDE=3')">Guide Slot 3</button>
        <button onclick="sendCommand('GUIDE=4')">Guide Slot 4</button>
      </div>
    </div>
  </div>

  <div class="status-line">
    <strong>Latest message:</strong>
    <span id="lastMessage">Waiting for controller...</span>
  </div>
</main>

<footer>
  SmartPark Control Center · Master ESP32 · Local Wi-Fi dashboard
</footer>

<script>
let ws;
let reconnectTimer;

function connectWebSocket() {
  const protocol = location.protocol === 'https:' ? 'wss://' : 'ws://';
  ws = new WebSocket(protocol + location.hostname + ':81/');

  ws.onopen = () => {
    document.getElementById('connectionText').textContent = 'Connected';
    document.getElementById('connectionDot').classList.add('online');
  };

  ws.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);
      updateDashboard(data);
    } catch (e) {
      console.log('Invalid status message', e);
    }
  };

  ws.onclose = () => {
    document.getElementById('connectionText').textContent = 'Disconnected';
    document.getElementById('connectionDot').classList.remove('online');
    clearTimeout(reconnectTimer);
    reconnectTimer = setTimeout(connectWebSocket, 2000);
  };

  ws.onerror = () => ws.close();
}

function sendCommand(command) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    alert('Dashboard is not connected to the master ESP32.');
    return;
  }

  ws.send(command);
}

function updateDashboard(data) {
  document.getElementById('modeValue').textContent = data.mode || 'UNKNOWN';
  document.getElementById('entryValue').textContent = data.entry || 'UNKNOWN';
  document.getElementById('exitValue').textContent = data.exit || 'UNKNOWN';
  document.getElementById('lastMessage').textContent = data.message || 'No message';

  if (Array.isArray(data.slots)) {
    let free = 0;

    data.slots.forEach((value, index) => {
      const element = document.getElementById('slot' + index);
      if (!element) return;

      element.textContent = value;
      element.className = 'slot-state ' + (value === 'FREE' ? 'free' : 'full');

      if (value === 'FREE') free++;
    });

    document.getElementById('freeValue').textContent = free + ' / 4';
  }
}

connectWebSocket();
</script>
</body>
</html>
)SMARTPARK";

// =====================================================
// FIRMWARE UPDATE PAGE
// Upload the compiled ESP32 #1 application .bin.
// Do NOT upload bootloader or partition-table binaries.
// =====================================================

const char UPDATE_HTML[] PROGMEM = R"SMARTUPDATE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SmartPark Firmware Update</title>
<style>
* { box-sizing: border-box; }
body {
  margin: 0;
  min-height: 100vh;
  display: grid;
  place-items: center;
  padding: 20px;
  background: #0b1120;
  color: #eaf0fa;
  font-family: Arial, Helvetica, sans-serif;
}
.card {
  width: min(550px, 100%);
  padding: 28px;
  border: 1px solid #2a3951;
  border-radius: 18px;
  background: #141e30;
}
h1 { margin-top: 0; font-size: 26px; }
p, .hint { color: #a8b5c9; line-height: 1.6; }
input[type=file] {
  width: 100%;
  margin: 15px 0;
  padding: 12px;
  background: #0b1120;
  border: 1px solid #2a3951;
  border-radius: 8px;
  color: #eaf0fa;
}
button, a {
  display: inline-block;
  border: 0;
  border-radius: 8px;
  padding: 12px 16px;
  background: #4f8cff;
  color: white;
  font-weight: bold;
  text-decoration: none;
  cursor: pointer;
}
button:disabled { opacity: .55; cursor: not-allowed; }
.warning {
  padding: 12px;
  border-radius: 8px;
  background: #382b1d;
  color: #ffd59a;
  font-size: 13px;
}
#progressWrap {
  display: none;
  margin-top: 18px;
  height: 12px;
  border-radius: 8px;
  overflow: hidden;
  background: #26354a;
}
#progress {
  height: 100%;
  width: 0;
  background: #20c997;
}
#status { overflow-wrap: anywhere; }
.back { margin-top: 18px; }
</style>
</head>
<body>
<div class="card">
  <h1>ESP32 #1 Firmware Update</h1>
  <p>Install a compiled application firmware file on the SmartPark master ESP32.</p>

  <div class="warning">
    Upload only the matching ESP32 application .bin file.
    Do not disconnect power while the firmware is being written.
    The dashboard will become temporarily unavailable while the master restarts.
  </div>

  <form id="uploadForm">
    <input type="file" id="firmware" name="firmware"
           accept=".bin,application/octet-stream" required>
    <button id="uploadButton" type="submit">Upload Firmware</button>
  </form>

  <div id="progressWrap"><div id="progress"></div></div>
  <p id="status">Ready for firmware file.</p>
  <div class="back"><a href="/">Back to Dashboard</a></div>
</div>

<script>
const form = document.getElementById('uploadForm');
const fileInput = document.getElementById('firmware');
const button = document.getElementById('uploadButton');
const statusText = document.getElementById('status');
const progressWrap = document.getElementById('progressWrap');
const progressBar = document.getElementById('progress');

form.addEventListener('submit', function(e) {
  e.preventDefault();

  const file = fileInput.files[0];
  if (!file) {
    statusText.textContent = 'Please select a firmware .bin file.';
    return;
  }

  if (!file.name.toLowerCase().endsWith('.bin')) {
    statusText.textContent = 'Invalid file. Select an ESP32 .bin file.';
    return;
  }

  if (!confirm('Update ESP32 #1 firmware and restart the master?')) return;

  button.disabled = true;
  fileInput.disabled = true;
  progressWrap.style.display = 'block';
  statusText.textContent = 'Uploading firmware... Keep the board powered.';

  const xhr = new XMLHttpRequest();
  const formData = new FormData();
  formData.append('firmware', file, file.name);

  xhr.open('POST', '/update', true);
  xhr.setRequestHeader('X-Firmware-Name', file.name);

  xhr.upload.onprogress = function(event) {
    if (event.lengthComputable) {
      const percent = Math.round((event.loaded / event.total) * 100);
      progressBar.style.width = percent + '%';
      statusText.textContent = 'Uploading: ' + percent + '%';
    }
  };

  xhr.onload = function() {
    if (xhr.status === 200) {
      statusText.textContent =
        'Firmware written successfully. The master is restarting. Reconnect to SmartCarPark and open the dashboard again.';
      progressBar.style.width = '100%';
    } else {
      statusText.textContent = 'Update failed: ' + xhr.responseText;
      button.disabled = false;
      fileInput.disabled = false;
    }
  };

  xhr.onerror = function() {
    statusText.textContent =
      'Connection lost. The board may be restarting, or the update failed. Reconnect and check the dashboard.';
    button.disabled = false;
    fileInput.disabled = false;
  };

  xhr.send(formData);
});
</script>
</body>
</html>
)SMARTUPDATE";

// =====================================================
// FIRMWARE UPDATE HANDLERS
// =====================================================

void handleFirmwarePage() {
  server.send_P(200, "text/html", UPDATE_HTML);
}

void handleUpdateFinished() {
  if (Update.hasError() || !updateSucceeded) {
    String errorText = "Firmware update failed.";

    if (updateError.length() > 0) {
      errorText += " ";
      errorText += updateError;
    }

    server.send(500, "text/plain", errorText);
    Serial.println("[UPDATE] Failed. Existing firmware should remain available if the flash operation was unsuccessful.");
    updateStarted = false;
    updateSucceeded = false;
    updateError = "";
    return;
  }

  server.send(
    200,
    "text/plain",
    "Firmware upload successful. ESP32 #1 is restarting."
  );

  Serial.printf(
    "[UPDATE] Success. Bytes written: %u\n",
    (unsigned int)firmwareBytesWritten
  );

  delay(1000);
  ESP.restart();
}

void handleFirmwareUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    updateStarted = false;
    updateSucceeded = false;
    firmwareBytesWritten = 0;
    updateError = "";

    Serial.printf(
      "[UPDATE] Starting upload: %s\n",
      upload.filename.c_str()
    );

    if (!upload.filename.endsWith(".bin") &&
        !upload.filename.endsWith(".BIN")) {
      updateError = "File extension must be .bin.";
      Serial.println("[UPDATE] Rejected: not a .bin file.");
      return;
    }

    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      updateError = Update.errorString();
      Serial.print("[UPDATE] Begin failed: ");
      Serial.println(updateError);
      return;
    }

    updateStarted = true;
  }

  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!updateStarted) {
      return;
    }

    size_t written = Update.write(
      upload.buf,
      upload.currentSize
    );

    if (written != upload.currentSize) {
      updateError = Update.errorString();
      Serial.print("[UPDATE] Write failed: ");
      Serial.println(updateError);
      Update.abort();
      updateStarted = false;
      return;
    }

    firmwareBytesWritten += written;
  }

  else if (upload.status == UPLOAD_FILE_END) {
    if (!updateStarted) {
      updateSucceeded = false;
      return;
    }

    if (Update.end(true)) {
      updateSucceeded = true;
      updateStarted = false;

      Serial.printf(
        "[UPDATE] Finished: %u bytes\n",
        (unsigned int)upload.totalSize
      );
    } else {
      updateSucceeded = false;
      updateError = Update.errorString();

      Serial.print("[UPDATE] Finalization failed: ");
      Serial.println(updateError);

      Update.abort();
      updateStarted = false;
    }
  }

  else if (upload.status == UPLOAD_FILE_ABORTED) {
    Serial.println("[UPDATE] Upload aborted.");

    if (updateStarted) {
      Update.abort();
    }

    updateStarted = false;
    updateSucceeded = false;
    updateError = "Upload was aborted.";
  }
}

// =====================================================
// WEBSOCKET EVENTS
// =====================================================

void webSocketEvent(
  uint8_t client,
  WStype_t type,
  uint8_t* payload,
  size_t length
) {
  if (type == WStype_CONNECTED) {
    Serial.printf("[WS] Client %u connected\n", client);

    String status = makeStatus();
    webSocket.sendTXT(client, status);
  }

  else if (type == WStype_DISCONNECTED) {
    Serial.printf("[WS] Client %u disconnected\n", client);
  }

  else if (type == WStype_TEXT) {
    String command;

    for (size_t i = 0; i < length; i++) {
      command += (char)payload[i];
    }

    handleCommand(command);
  }
}

// =====================================================
// WIFI + ESP-NOW INITIALIZATION
// =====================================================

bool initializeEspNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Initialization failed.");
    return false;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceive);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, slaveMAC, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  if (esp_now_is_peer_exist(slaveMAC)) {
    esp_now_del_peer(slaveMAC);
  }

  esp_err_t result = esp_now_add_peer(&peerInfo);

  if (result != ESP_OK) {
    Serial.print("[ESP-NOW] Failed to add slave peer: ");
    Serial.println(result);
    return false;
  }

  Serial.println("[ESP-NOW] Ready.");
  return true;
}

// =====================================================
// HTTP ROUTES
// =====================================================

void initializeWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", DASHBOARD_HTML);
  });

  server.on("/firmware", HTTP_GET, handleFirmwarePage);

  server.on(
    "/update",
    HTTP_POST,
    handleUpdateFinished,
    handleFirmwareUpload
  );

  server.on("/status", HTTP_GET, []() {
    server.send(200, "application/json", makeStatus());
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "Page not found.");
  });

  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  Serial.println("[HTTP] Web server started on port 80.");
  Serial.println("[WS] WebSocket server started on port 81.");
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("==================================");
  Serial.println(" SMARTPARK - ESP32 #1 MASTER");
  Serial.println(" Dashboard + Self Firmware Update");
  Serial.println("==================================");

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);

  bool apStarted = WiFi.softAP(
    AP_SSID,
    AP_PASSWORD,
    WIFI_CHANNEL
  );

  if (!apStarted) {
    Serial.println("[WIFI] Failed to start access point.");
  } else {
    Serial.println("[WIFI] Access point started.");
    Serial.print("[WIFI] SSID: ");
    Serial.println(AP_SSID);
    Serial.print("[WIFI] Dashboard: http://");
    Serial.println(WiFi.softAPIP());
  }

  // Keep the ESP-NOW radio on the same channel as the AP.
  esp_err_t channelResult = esp_wifi_set_channel(
    WIFI_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );

  if (channelResult != ESP_OK) {
    Serial.print("[WIFI] Channel setting warning: ");
    Serial.println(channelResult);
  }

  if (!initializeEspNow()) {
    Serial.println(
      "[WARNING] ESP-NOW setup failed. Dashboard may load, "
      "but slave communication may not work."
    );
  }

  initializeWebServer();

  Serial.println("[SYSTEM] Ready.");
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  server.handleClient();
  webSocket.loop();

  unsigned long now = millis();

  if (now - lastStatusBroadcast >= STATUS_INTERVAL) {
    lastStatusBroadcast = now;
    broadcastStatus();
  }

  delay(2);
}