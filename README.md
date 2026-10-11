SMART CAR PARK - REVISED STARTING CODE
======================================

Upload:
1) ESP32_1_Master.ino to ESP32 #1
2) ESP32_2_Gate_Controller.ino to ESP32 #2
3) UNO_1_Lighting.ino to UNO #1
4) UNO_2_Slot_Sensors.ino to UNO #2

IMPORTANT CHECKS BEFORE POWERING:
- ESP32 #1 slaveMAC must match the STA MAC printed by ESP32 #2.
  If the printed MAC differs from 8C:94:DF:4D:A9:18, update slaveMAC in ESP32_1_Master.ino.
- Both ESP32 boards use Wi-Fi channel 6.
- UNO #1 UART: ESP32 #2 GPIO17 TX -> UNO1 D0/RX; UNO1 D1/TX -> ESP32 #2 GPIO16 RX through a level shifter/divider.
- UNO #2 UART: ESP32 #2 GPIO18 TX -> UNO2 D0/RX; UNO2 D1/TX -> ESP32 #2 GPIO19 RX through a level shifter/divider.
- All grounds must be connected together.
- HC-SR04 ECHO is 5V: use a voltage divider before ESP32 GPIO33.
- Power servos from a suitable external regulated 5V supply, not from an ESP32 GPIO; share grounds.
- UNO D0/D1 are used for UART, so disconnect the ESP32 UART wires when uploading to each UNO if upload fails.
- Confirm sensor outputs are active LOW. If not, change readOccupied() in UNO_2_Slot_Sensors.ino and the ESP32 entry interrupt edge if needed.
- UNO1's LDR logic assumes low analog readings mean dark. If your divider produces higher readings in darkness, invert the threshold condition.

AUTOMATIC BEHAVIOR:
- ESP32 #2 keeps current slot occupancy from UNO #2.
- Entrance sensor interrupt wakes the highest-priority gate task.
- If a free slot exists, the gate opens and sends GUIDE=n to UNO #1.
- If all slots are occupied, automatic entry is blocked.
- Gate auto-close is 10 seconds. Adjust GATE_TIME if required.
- Dashboard is at http://192.168.4.1 after connecting to Wi-Fi SmartCarPark (password 12345678).

NOTES:
- This is a revised integration baseline, not a guarantee of compile success on every library/core version.
- ESP32 #1 uses the ESP32 Arduino Core 3.x ESP-NOW callback signature. If using an older ESP32 core, callback signatures may need adjustment.
- Manual OPEN_ENTRY intentionally overrides the full-car-park check. Use only when safe.
- Bench-test with gates mechanically disconnected first. Check servo direction/angles before attaching the barriers.
