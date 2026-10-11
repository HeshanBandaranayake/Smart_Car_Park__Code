
// =====================================================
// ESP32 #2 - SMART CAR PARK CONTROLLER
// Separate entrance and exit servo angles
// ESP32 #1 <-> ESP32 #2 via ESP-NOW
// ESP32 #2 <-> UNO #1 and UNO #2 via UART
// =====================================================

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <ESP32Servo.h>
#include <string.h>

// =====================================================
// CONFIGURATION
// =====================================================

#define WIFI_CHANNEL 6

// Servo signal pins
#define ENTRY_SERVO_PIN 25
#define EXIT_SERVO_PIN  26

// Obstacle sensor pins
#define ENTRY_SENSOR_PIN 27
#define EXIT_SENSOR_PIN  33

// UART pins
#define LIGHT_RX 16
#define LIGHT_TX 17

#define SENSOR_RX 19
#define SENSOR_TX 18

// =====================================================
// SEPARATE SERVO ANGLES - EDIT THESE VALUES
// =====================================================

// ENTRANCE GATE
#define ENTRY_OPEN_ANGLE  0
#define ENTRY_CLOSE_ANGLE 90

// EXIT GATE
#define EXIT_OPEN_ANGLE   180
#define EXIT_CLOSE_ANGLE  90

// =====================================================

#define GATE_TIME 10000UL
#define EXIT_DEBOUNCE_MS 50

// LOW means obstacle detected.
// Change to HIGH if your sensor has opposite polarity.
#define ENTRY_SENSOR_ACTIVE LOW
#define EXIT_SENSOR_ACTIVE LOW

// =====================================================
// HARDWARE OBJECTS
// =====================================================

HardwareSerial LightSerial(1);
HardwareSerial SensorSerial(2);

Servo entryServo;
Servo exitServo;

uint8_t broadcastAddress[] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// =====================================================
// SHARED STATE
// =====================================================

volatile bool entryInterruptFlag = false;

TaskHandle_t gateTaskHandle = nullptr;
QueueHandle_t commandQueue = nullptr;

portMUX_TYPE slotMux = portMUX_INITIALIZER_UNLOCKED;

bool occupied[4] = {false, false, false, false};

volatile bool autoMode = true;
volatile bool entryIsOpen = false;
volatile bool exitIsOpen = false;

unsigned long entryOpenedAt = 0;
unsigned long exitOpenedAt = 0;
unsigned long lastSlotReport = 0;

bool exitObstaclePreviouslyDetected = false;
bool exitSensorArmed = true;
unsigned long exitDetectionStartedAt = 0;

struct CommandMessage {
  char text[48];
};

// =====================================================
// ENTRY SENSOR INTERRUPT
// =====================================================

void IRAM_ATTR entryISR() {
  BaseType_t higherPriorityTaskWoken = pdFALSE;

  entryInterruptFlag = true;

  if (gateTaskHandle != nullptr) {
    vTaskNotifyGiveFromISR(
      gateTaskHandle,
      &higherPriorityTaskWoken
    );
  }

  if (higherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

// =====================================================
// SEND STATUS TO ESP32 #1
// =====================================================

void sendToMaster(const char* text) {
  esp_err_t result = esp_now_send(
    broadcastAddress,
    reinterpret_cast<const uint8_t*>(text),
    strlen(text)
  );

  Serial.printf(
    "[ESP-NOW] %s | result=%d\n",
    text,
    result
  );
}

// =====================================================
// PARKING SLOT STATUS
// =====================================================

void reportSlots() {
  for (int i = 0; i < 4; i++) {
    bool isOccupied;

    portENTER_CRITICAL(&slotMux);
    isOccupied = occupied[i];
    portEXIT_CRITICAL(&slotMux);

    char msg[24];

    snprintf(
      msg,
      sizeof(msg),
      "SLOT%d=%s",
      i + 1,
      isOccupied ? "FULL" : "FREE"
    );

    sendToMaster(msg);
  }
}

int firstFreeSlot() {
  int found = -1;

  portENTER_CRITICAL(&slotMux);

  for (int i = 0; i < 4; i++) {
    if (!occupied[i]) {
      found = i + 1;
      break;
    }
  }

  portEXIT_CRITICAL(&slotMux);

  return found;
}

// =====================================================
// ENTRANCE GATE
// =====================================================

void openEntryForSlot(int slot) {
  if (slot < 1 || slot > 4 || entryIsOpen) {
    return;
  }

  Serial.printf(
    "[ENTRY] Opening to %d degrees\n",
    ENTRY_OPEN_ANGLE
  );

  entryServo.write(ENTRY_OPEN_ANGLE);

  entryIsOpen = true;
  entryOpenedAt = millis();

  char command[16];
  snprintf(command, sizeof(command), "GUIDE=%d", slot);
  LightSerial.println(command);

  sendToMaster("ENTRY=OPEN");

  Serial.printf(
    "[ENTRY] Opened; guiding to slot %d\n",
    slot
  );
}

void closeEntry() {
  Serial.printf(
    "[ENTRY] Closing to %d degrees\n",
    ENTRY_CLOSE_ANGLE
  );

  entryServo.write(ENTRY_CLOSE_ANGLE);

  entryIsOpen = false;

  // UNO #1 should apply its own LDR/night-light logic.
  LightSerial.println("ALL_OFF");

  sendToMaster("ENTRY=CLOSED");

  Serial.println("[ENTRY] Closed");
}

// =====================================================
// EXIT GATE
// =====================================================

void openExit() {
  Serial.printf(
    "[EXIT] Opening to %d degrees\n",
    EXIT_OPEN_ANGLE
  );

  // Always issue the servo command, even if the
  // software status may already indicate OPEN.
  exitServo.write(EXIT_OPEN_ANGLE);

  exitIsOpen = true;
  exitOpenedAt = millis();

  sendToMaster("EXIT=OPEN");

  Serial.println("[EXIT] Open command executed");
}

void closeExit() {
  Serial.printf(
    "[EXIT] Closing to %d degrees\n",
    EXIT_CLOSE_ANGLE
  );

  exitServo.write(EXIT_CLOSE_ANGLE);

  exitIsOpen = false;

  sendToMaster("EXIT=CLOSED");

  Serial.println("[EXIT] Close command executed");
}

// =====================================================
// EXIT OBSTACLE SENSOR
// =====================================================

bool isExitObstacleDetected() {
  return digitalRead(EXIT_SENSOR_PIN) ==
         EXIT_SENSOR_ACTIVE;
}

// =====================================================
// PROCESS DASHBOARD COMMANDS
// =====================================================

void processCommand(const char* text) {
  Serial.printf("[COMMAND] Received: %s\n", text);

  if (!strcmp(text, "AUTO")) {
    autoMode = true;
    sendToMaster("MODE=AUTO");
    Serial.println("[MODE] Automatic");
  }
  else if (!strcmp(text, "MANUAL")) {
    autoMode = false;
    sendToMaster("MODE=MANUAL");
    Serial.println("[MODE] Manual");
  }

  // ---------------- ENTRANCE GATE ----------------

  else if (!strcmp(text, "OPEN_ENTRY")) {
    if (!entryIsOpen) {
      int slot = firstFreeSlot();

      if (slot > 0) {
        openEntryForSlot(slot);
      } else {
        // Manual opening is allowed even if all slots
        // are full; the operator explicitly requested it.
        Serial.println(
          "[ENTRY] No free slots; manual open requested"
        );

        Serial.printf(
          "[ENTRY] Opening to %d degrees\n",
          ENTRY_OPEN_ANGLE
        );

        entryServo.write(ENTRY_OPEN_ANGLE);
        entryIsOpen = true;
        entryOpenedAt = millis();

        sendToMaster("ENTRY=OPEN");
      }
    } else {
      // Reissue the target angle for manual testing.
      entryServo.write(ENTRY_OPEN_ANGLE);
    }
  }
  else if (!strcmp(text, "CLOSE_ENTRY")) {
    closeEntry();
  }

  // ---------------- EXIT GATE ----------------

  else if (!strcmp(text, "OPEN_EXIT")) {
    openExit();
  }
  else if (!strcmp(text, "CLOSE_EXIT")) {
    closeExit();
  }

  // ---------------- LIGHT CONTROLS ----------------

  else if (!strcmp(text, "LIGHTS_ON")) {
    LightSerial.println("ALL_ON");
    Serial.println("[LIGHTS] ALL_ON sent to UNO #1");
  }
  else if (!strcmp(text, "LIGHTS_OFF")) {
    LightSerial.println("ALL_OFF");
    Serial.println("[LIGHTS] ALL_OFF sent to UNO #1");
  }
  else if (!strcmp(text, "GUIDE_1")) {
    LightSerial.println("GUIDE=1");
    Serial.println("[LIGHTS] GUIDE=1 sent to UNO #1");
  }
  else if (!strcmp(text, "GUIDE_2")) {
    LightSerial.println("GUIDE=2");
    Serial.println("[LIGHTS] GUIDE=2 sent to UNO #1");
  }
  else if (!strcmp(text, "GUIDE_3")) {
    LightSerial.println("GUIDE=3");
    Serial.println("[LIGHTS] GUIDE=3 sent to UNO #1");
  }
  else if (!strcmp(text, "GUIDE_4")) {
    LightSerial.println("GUIDE=4");
    Serial.println("[LIGHTS] GUIDE=4 sent to UNO #1");
  }
  else {
    Serial.println("[COMMAND] Unknown command");
  }
}

// =====================================================
// ESP-NOW RECEIVE CALLBACK
// =====================================================

void onDataReceive(
  const esp_now_recv_info_t* info,
  const uint8_t* data,
  int len
) {
  if (
    len <= 0 ||
    len >= (int)sizeof(CommandMessage) ||
    commandQueue == nullptr
  ) {
    return;
  }

  CommandMessage msg = {};

  memcpy(msg.text, data, len);
  msg.text[len] = '\0';

  if (xQueueSend(commandQueue, &msg, 0) != pdTRUE) {
    Serial.println("[WARNING] Command queue full");
  }
}

// =====================================================
// TASK 1: ENTRANCE GATE
// =====================================================

void gateControlTask(void* parameter) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25));

    bool triggered;

    noInterrupts();
    triggered = entryInterruptFlag;
    entryInterruptFlag = false;
    interrupts();

    if (triggered && autoMode && !entryIsOpen) {
      vTaskDelay(pdMS_TO_TICKS(50));

      if (
        digitalRead(ENTRY_SENSOR_PIN) ==
        ENTRY_SENSOR_ACTIVE
      ) {
        int slot = firstFreeSlot();

        if (slot > 0) {
          openEntryForSlot(slot);
        } else {
          Serial.println("[ENTRY] Parking full");
          sendToMaster("PARKING=FULL");
        }
      }
    }

    if (
      entryIsOpen &&
      millis() - entryOpenedAt >= GATE_TIME
    ) {
      closeEntry();
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// =====================================================
// TASK 2: RECEIVE SLOT STATUS FROM UNO #2
// =====================================================

void sensorCommunicationTask(void* parameter) {
  char line[64];
  size_t pos = 0;

  for (;;) {
    while (SensorSerial.available()) {
      char c = (char)SensorSerial.read();

      if (c == '\r') {
        continue;
      }

      if (c == '\n') {
        line[pos] = '\0';

        int slot = 0;
        char state[20] = {};

        if (
          sscanf(line, "SLOT=%d,%19s", &slot, state) == 2 &&
          slot >= 1 &&
          slot <= 4
        ) {
          bool value = !strcmp(state, "OCCUPIED");

          portENTER_CRITICAL(&slotMux);
          occupied[slot - 1] = value;
          portEXIT_CRITICAL(&slotMux);

          char msg[24];

          snprintf(
            msg,
            sizeof(msg),
            "SLOT%d=%s",
            slot,
            value ? "FULL" : "FREE"
          );

          sendToMaster(msg);

          Serial.printf("[UNO #2] %s\n", line);
        }

        pos = 0;
      }
      else if (pos < sizeof(line) - 1) {
        line[pos++] = c;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// =====================================================
// TASK 3: PROCESS MASTER COMMANDS
// =====================================================

void commandTask(void* parameter) {
  CommandMessage msg;

  for (;;) {
    if (
      xQueueReceive(
        commandQueue,
        &msg,
        portMAX_DELAY
      ) == pdTRUE
    ) {
      processCommand(msg.text);
    }
  }
}

// =====================================================
// TASK 4: EXIT SENSOR AND AUTOMATIC CLOSE
// =====================================================

void exitGateTask(void* parameter) {
  for (;;) {
    bool detected = isExitObstacleDetected();

    if (detected) {
      if (!exitObstaclePreviouslyDetected) {
        exitDetectionStartedAt = millis();
        exitObstaclePreviouslyDetected = true;
      }

      if (
        autoMode &&
        exitSensorArmed &&
        !exitIsOpen &&
        millis() - exitDetectionStartedAt >= EXIT_DEBOUNCE_MS
      ) {
        Serial.println("[EXIT SENSOR] Obstacle detected");

        openExit();
        exitSensorArmed = false;
      }
    }
    else {
      exitObstaclePreviouslyDetected = false;
      exitSensorArmed = true;
    }

    if (
      exitIsOpen &&
      millis() - exitOpenedAt >= GATE_TIME
    ) {
      closeExit();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(700);

  Serial.println();
  Serial.println("==================================");
  Serial.println("ESP32 #2 - SMART CAR PARK");
  Serial.println("SEPARATE SERVO ANGLES ENABLED");
  Serial.println("==================================");

  WiFi.mode(WIFI_STA);
  delay(200);

  Serial.print("ESP32 #2 MAC: ");
  Serial.println(WiFi.macAddress());

  esp_err_t channelResult = esp_wifi_set_channel(
    WIFI_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );

  Serial.printf(
    "WiFi channel setup result: %d\n",
    channelResult
  );

  // UART to UNO #1 and UNO #2
  LightSerial.begin(
    9600,
    SERIAL_8N1,
    LIGHT_RX,
    LIGHT_TX
  );

  SensorSerial.begin(
    9600,
    SERIAL_8N1,
    SENSOR_RX,
    SENSOR_TX
  );

  // Initialize servos
  entryServo.setPeriodHertz(50);
  exitServo.setPeriodHertz(50);

  entryServo.attach(ENTRY_SERVO_PIN, 500, 2400);
  exitServo.attach(EXIT_SERVO_PIN, 500, 2400);

  // Start both gates at their separate closed angles
  entryServo.write(ENTRY_CLOSE_ANGLE);
  exitServo.write(EXIT_CLOSE_ANGLE);

  Serial.printf(
    "[ENTRY] GPIO %d | OPEN=%d | CLOSE=%d\n",
    ENTRY_SERVO_PIN,
    ENTRY_OPEN_ANGLE,
    ENTRY_CLOSE_ANGLE
  );

  Serial.printf(
    "[EXIT] GPIO %d | OPEN=%d | CLOSE=%d\n",
    EXIT_SERVO_PIN,
    EXIT_OPEN_ANGLE,
    EXIT_CLOSE_ANGLE
  );

  // Sensors
  pinMode(ENTRY_SENSOR_PIN, INPUT_PULLUP);
  pinMode(EXIT_SENSOR_PIN, INPUT_PULLUP);

  attachInterrupt(
    digitalPinToInterrupt(ENTRY_SENSOR_PIN),
    entryISR,
    FALLING
  );

  // Create queue before registering ESP-NOW callback
  commandQueue = xQueueCreate(8, sizeof(CommandMessage));

  if (commandQueue == nullptr) {
    Serial.println("[ERROR] Command queue creation failed");

    while (true) {
      delay(1000);
    }
  }

  // ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW initialization failed");
  } else {
    esp_now_register_recv_cb(onDataReceive);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcastAddress, 6);

    peer.channel = WIFI_CHANNEL;
    peer.encrypt = false;
    peer.ifidx = WIFI_IF_STA;

    esp_err_t peerResult = esp_now_add_peer(&peer);

    if (
      peerResult != ESP_OK &&
      peerResult != ESP_ERR_ESPNOW_EXIST
    ) {
      Serial.printf(
        "[ERROR] Broadcast peer failed: %d\n",
        peerResult
      );
    } else {
      Serial.println("[ESP-NOW] Ready");
    }
  }

  // FreeRTOS tasks
  xTaskCreatePinnedToCore(
    gateControlTask,
    "GateControl",
    4096,
    nullptr,
    5,
    &gateTaskHandle,
    1
  );

  xTaskCreatePinnedToCore(
    sensorCommunicationTask,
    "SlotUART",
    4096,
    nullptr,
    4,
    nullptr,
    1
  );

  xTaskCreatePinnedToCore(
    exitGateTask,
    "ExitGate",
    4096,
    nullptr,
    3,
    nullptr,
    1
  );

  xTaskCreatePinnedToCore(
    commandTask,
    "Commands",
    4096,
    nullptr,
    2,
    nullptr,
    1
  );

  Serial.println("[READY] ESP32 #2 started");
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  if (millis() - lastSlotReport >= 3000) {
    lastSlotReport = millis();
    reportSlots();
  }

  vTaskDelay(pdMS_TO_TICKS(100));
}