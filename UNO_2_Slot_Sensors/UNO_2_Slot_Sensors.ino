// UNO #2 - FOUR PARKING SLOT SENSORS
// Sensor outputs are assumed ACTIVE LOW. Change readOccupied() if your sensors differ.
#define SLOT1_SENSOR 2
#define SLOT2_SENSOR 3
#define SLOT3_SENSOR 4
#define SLOT4_SENSOR 5
const byte sensorPins[4] = {SLOT1_SENSOR,SLOT2_SENSOR,SLOT3_SENSOR,SLOT4_SENSOR};
bool stableState[4] = {false,false,false,false};
bool lastRaw[4] = {false,false,false,false};
unsigned long rawChangedAt[4] = {0,0,0,0};
const unsigned long DEBOUNCE_MS = 40;
unsigned long lastReport = 0;

bool readOccupied(byte pin) { return digitalRead(pin) == LOW; }
void sendSlot(byte index, bool occupied) {
  Serial.print("SLOT="); Serial.print(index+1); Serial.print(",");
  Serial.println(occupied ? "OCCUPIED" : "FREE");
}
void setup() {
  Serial.begin(9600);
  for (byte i=0; i<4; i++) {
    pinMode(sensorPins[i], INPUT_PULLUP);
    bool initial = readOccupied(sensorPins[i]);
    lastRaw[i] = initial;
    stableState[i] = initial;
    rawChangedAt[i] = millis();
  }
  delay(300);
  Serial.println("UNO2_READY");
  // Always send initial states; the original sketch missed slots already occupied at boot.
  for (byte i=0; i<4; i++) sendSlot(i, stableState[i]);
}
void loop() {
  unsigned long now = millis();
  for (byte i=0; i<4; i++) {
    bool raw = readOccupied(sensorPins[i]);
    if (raw != lastRaw[i]) {
      lastRaw[i] = raw;
      rawChangedAt[i] = now;
    }
    if (now - rawChangedAt[i] >= DEBOUNCE_MS && stableState[i] != raw) {
      stableState[i] = raw;
      sendSlot(i, stableState[i]);
    }
  }
  // Periodic refresh prevents stale state after a missed serial message.
  if (now - lastReport >= 5000) {
    lastReport = now;
    for (byte i=0; i<4; i++) sendSlot(i, stableState[i]);
  }
  delay(5);
}
