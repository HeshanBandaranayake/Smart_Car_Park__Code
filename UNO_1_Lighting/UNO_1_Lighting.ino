
/*
=====================================================
 UNO #1 - SMART CAR PARK LIGHTING SYSTEM
 FIXED LDR NIGHT LIGHT LOGIC
=====================================================

PARKING SLOT LIGHTS
D2 = Slot 1
D3 = Slot 2
D4 = Slot 3
D5 = Slot 4

COMMON LIGHTS
D6 = Common Light 1
D7 = Common Light 2

LDR SENSORS
A0 = LDR 1
A1 = LDR 2

ESP32 #2 UART
ESP32 GPIO17 TX -> UNO D0/RX
ESP32 GPIO16 RX <- UNO D1/TX
Use a voltage divider on UNO TX -> ESP32 RX.

IMPORTANT:
Disconnect UART wires from D0/D1 while uploading.
=====================================================
*/

#include <Arduino.h>

// ---------------- PARKING SLOT LIGHTS ----------------

#define SLOT1_LIGHT 2
#define SLOT2_LIGHT 3
#define SLOT3_LIGHT 4
#define SLOT4_LIGHT 5

// ---------------- COMMON LIGHTS ----------------

#define COMMON_LIGHT1 6
#define COMMON_LIGHT2 7

// ---------------- LDR SENSORS ----------------

#define LDR1_PIN A0
#define LDR2_PIN A1

// Adjust after observing Serial Monitor readings.
#define DARK_THRESHOLD 400

// true: ADC readings increase in darkness.
// false: ADC readings decrease in darkness.
#define DARK_READING_IS_HIGH true

// ---------------- TIMING ----------------

const unsigned long LDR_INTERVAL_MS = 300;
const unsigned long PARKING_TIMEOUT_MS = 10000;

// ---------------- VARIABLES ----------------

String inputCommand = "";

unsigned long lastLDRCheck = 0;
unsigned long lastParkingActivity = 0;

bool parkingLightsOn = false;
bool slotLightState[4] = {false, false, false, false};

// =====================================================
// CONTROL ONE SLOT LIGHT
// =====================================================

void setSlotLight(int slot, bool state) {
  if (slot < 1 || slot > 4) return;

  const int pins[] = {
    SLOT1_LIGHT, SLOT2_LIGHT, SLOT3_LIGHT, SLOT4_LIGHT
  };

  slotLightState[slot - 1] = state;
  digitalWrite(pins[slot - 1], state ? HIGH : LOW);

  Serial.print("Slot ");
  Serial.print(slot);
  Serial.print(" light: ");
  Serial.println(state ? "ON" : "OFF");
}

// =====================================================
// CONTROL ALL PARKING SLOT LIGHTS
// =====================================================

void setAllParkingLights(bool state) {
  for (int slot = 1; slot <= 4; slot++) {
    setSlotLight(slot, state);
  }

  parkingLightsOn = state;

  if (state) {
    lastParkingActivity = millis();
  }
}

// =====================================================
// FIXED AUTOMATIC NIGHT LIGHT CONTROL
// =====================================================

void updateNightLights() {
  int ldr1Value = analogRead(LDR1_PIN);
  int ldr2Value = analogRead(LDR2_PIN);

  bool ldr1Dark;
  bool ldr2Dark;

  if (DARK_READING_IS_HIGH) {
    ldr1Dark = (ldr1Value > DARK_THRESHOLD);
    ldr2Dark = (ldr2Value > DARK_THRESHOLD);
  } else {
    ldr1Dark = (ldr1Value < DARK_THRESHOLD);
    ldr2Dark = (ldr2Value < DARK_THRESHOLD);
  }

  // Turn common lights ON if either sensor detects darkness.
  bool isDark = ldr1Dark || ldr2Dark;

  digitalWrite(COMMON_LIGHT1, isDark ? HIGH : LOW);
  digitalWrite(COMMON_LIGHT2, isDark ? HIGH : LOW);

  Serial.print("LDR1=");
  Serial.print(ldr1Value);
  Serial.print(" | LDR2=");
  Serial.print(ldr2Value);
  Serial.print(" | Environment=");
  Serial.print(isDark ? "DARK" : "BRIGHT");
  Serial.print(" | Common lights=");
  Serial.println(isDark ? "ON" : "OFF");
}

// =====================================================
// PROCESS ESP32 COMMANDS
// =====================================================

void processCommand(String command) {
  command.trim();
  command.toUpperCase();

  Serial.print("Received: ");
  Serial.println(command);

  if (command.startsWith("GUIDE=")) {
    int slot = command.substring(6).toInt();

    if (slot >= 1 && slot <= 4) {
      setSlotLight(slot, true);
      lastParkingActivity = millis();
    }
  }
  else if (command.startsWith("PARKED=")) {
    int slot = command.substring(7).toInt();

    if (slot >= 1 && slot <= 4) {
      setSlotLight(slot, true);
      lastParkingActivity = millis();
    }
  }
  else if (command.startsWith("EXIT=")) {
    int slot = command.substring(5).toInt();

    if (slot >= 1 && slot <= 4) {
      setSlotLight(slot, false);
      lastParkingActivity = millis();
    }
  }
  else if (command == "ALL_ON") {
    setAllParkingLights(true);
  }
  else if (command == "ALL_OFF") {
    setAllParkingLights(false);
  }
  else {
    Serial.println("Unknown command");
  }
}

// =====================================================
// READ UART COMMANDS
// =====================================================

void readSerialCommands() {
  while (Serial.available() > 0) {
    char incoming = Serial.read();

    if (incoming == '\n' || incoming == '\r') {
      if (inputCommand.length() > 0) {
        processCommand(inputCommand);
        inputCommand = "";
      }
    } else if (inputCommand.length() < 60) {
      inputCommand += incoming;
    } else {
      inputCommand = "";
    }
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(9600);

  pinMode(SLOT1_LIGHT, OUTPUT);
  pinMode(SLOT2_LIGHT, OUTPUT);
  pinMode(SLOT3_LIGHT, OUTPUT);
  pinMode(SLOT4_LIGHT, OUTPUT);

  pinMode(COMMON_LIGHT1, OUTPUT);
  pinMode(COMMON_LIGHT2, OUTPUT);

  pinMode(LDR1_PIN, INPUT);
  pinMode(LDR2_PIN, INPUT);

  setAllParkingLights(false);
  digitalWrite(COMMON_LIGHT1, LOW);
  digitalWrite(COMMON_LIGHT2, LOW);

  Serial.println();
  Serial.println("================================");
  Serial.println("UNO #1 LIGHTING SYSTEM READY");
  Serial.println("================================");

  lastLDRCheck = millis();
  lastParkingActivity = millis();
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  readSerialCommands();

  if (millis() - lastLDRCheck >= LDR_INTERVAL_MS) {
    lastLDRCheck = millis();
    updateNightLights();
  }

  // Turn off parking-slot lights after 10 seconds.
  // Automatic common night lights are controlled separately.
  if (parkingLightsOn &&
      millis() - lastParkingActivity >= PARKING_TIMEOUT_MS) {
    setAllParkingLights(false);
    Serial.println("Parking slot lights timed out.");
  }
}