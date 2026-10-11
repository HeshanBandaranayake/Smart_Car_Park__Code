# SmartPark – Smart Car Parking System

An IoT-based smart parking management system built using ESP32 microcontrollers, Arduino UNO boards, parking sensors, obstacle detection sensors, servo motors, and automatic lighting.

SmartPark helps monitor parking availability, control entrance and exit gates, manage parking lights, and provide a local web dashboard for monitoring and manual control.

---

## Table of Contents

- [Project Overview](#project-overview)
- [Features](#features)
- [System Architecture](#system-architecture)
- [Hardware Components](#hardware-components)
- [Software and Technologies](#software-and-technologies)
- [System Operation](#system-operation)
- [Circuit Connections](#circuit-connections)
- [Wi-Fi Configuration](#wi-fi-configuration)
- [Firmware Installation](#firmware-installation)
- [Dashboard](#dashboard)
- [Firmware Update](#firmware-update)
- [Safety Considerations](#safety-considerations)
- [Troubleshooting](#troubleshooting)
- [Future Improvements](#future-improvements)
- [License](#license)

---

## Project Overview

SmartPark is a distributed embedded system designed to automate important functions of a parking facility.

The system uses four microcontroller boards:

1. **ESP32 #1 – Master Controller:** Hosts the local Wi-Fi access point, web dashboard, and system monitoring interface.
2. **ESP32 #2 – Gate and Communication Controller:** Controls the entrance and exit servo motors and communicates with the Arduino UNO boards.
3. **Arduino UNO #1 – Lighting Controller:** Reads LDR sensors and controls parking and common-area lights.
4. **Arduino UNO #2 – Parking Sensor Controller:** Reads the four parking-slot sensors and reports their occupancy status.

The two ESP32 boards communicate using ESP-NOW. Serial communication connects ESP32 #2 with the two Arduino UNO boards.

The system is designed to operate locally without requiring an external internet connection.

## Features

- Real-time parking-slot status monitoring.
- Four parking spaces with individual occupancy detection.
- Automatic and manual operating modes.
- Independent entrance and exit gate controls.
- Servo-based gate operation.
- Obstacle detection for gate control.
- Automatic lighting based on ambient light levels.
- Four parking-slot lights and two common-area lights.
- Local Wi-Fi access point.
- Web-based monitoring and control dashboard.
- WebSocket communication for dashboard updates.
- ESP-NOW communication between ESP32 controllers.
- Serial communication between ESP32 #2 and Arduino UNO boards.
- Dashboard-based firmware upload for ESP32 #1.

> **Firmware update limitation:** The dashboard firmware uploader updates ESP32 #1 only. It does not automatically update ESP32 #2 or either Arduino UNO.

---

## System Architecture

```text
                  Phone / Computer
                         |
                      Wi-Fi
                         |
               +-------------------+
               | ESP32 #1          |
               | Master Controller |
               | Web Dashboard     |
               | HTTP + WebSocket  |
               +-------------------+
                         |
                      ESP-NOW
                         |
               +-------------------+
               | ESP32 #2          |
               | Gate Controller   |
               | Sensor Interface  |
               +-------------------+
                    /          \
              Serial            Serial
                /                  \
       +----------------+  +----------------+
       | Arduino UNO #1 |  | Arduino UNO #2 |
       | Lighting       |  | Slot Sensors   |
       | LDR Inputs     |  | 4 Slot Inputs  |
       +----------------+  +----------------+
                |                  |
          6 Light Outputs     4 Parking Sensors
```

### Communication interfaces

| Connection | Communication | Purpose |
|---|---|---|
| Phone/computer ↔ ESP32 #1 | Wi-Fi, HTTP, WebSocket | Dashboard access and control |
| ESP32 #1 ↔ ESP32 #2 | ESP-NOW | Commands and system status |
| ESP32 #2 ↔ UNO #1 | UART serial | Lighting commands |
| ESP32 #2 ↔ UNO #2 | UART serial | Parking-slot sensor data |

The ESP-NOW channel must match the channel used by the master access point and the slave controller.

---

## Hardware Components

| Component | Quantity | Purpose |
|---|---:|---|
| ESP32 development board | 2 | Master and gate controller |
| Arduino UNO R3 | 2 | Lighting and parking-sensor control |
| Servo motor | 2 | Entrance and exit gates |
| Obstacle detection sensor | 1 or as configured | Detects an approaching object or vehicle |
| Parking-slot sensors | 4 | Detects vehicle occupancy |
| LDR sensor | 2 | Measures ambient light |
| Parking-slot lights | 4 | Illuminates individual parking spaces |
| Common-area lights | 2 | Illuminates shared areas |
| Suitable resistors | As required | Voltage dividers and LDR circuits |
| External regulated power supply | As required | Powers servos and other loads |
| Jumper wires | As required | Circuit connections |
| Breadboard or terminal connectors | As required | Prototyping and connections |

**Sensor note:** The obstacle detector used in the final circuit should match the actual sensor module installed. Confirm its output voltage, detection range, and active HIGH/LOW behavior before connecting it.

---

## Software and Technologies

- Arduino IDE
- C/C++ for embedded firmware
- ESP32 Arduino Core
- Arduino UNO firmware
- Wi-Fi SoftAP
- ESP-NOW
- HTTP web server
- WebSocket communication
- HTML, CSS, and JavaScript
- ESP32 `Update` library for master firmware installation

---

## System Operation

### 1. System startup

When powered on:

1. ESP32 #1 initializes the Wi-Fi access point.
2. ESP32 #1 starts the HTTP and WebSocket servers.
3. ESP-NOW communication is initialized.
4. ESP32 #2 initializes its gate-control and serial interfaces.
5. UNO #1 initializes the lighting outputs and LDR inputs.
6. UNO #2 initializes the parking-slot sensors.
7. The dashboard becomes available through the local Wi-Fi network.

### 2. Parking-slot detection

Each parking space has an associated sensor connected to Arduino UNO #2.

1. The sensor detects whether a vehicle occupies its parking space.
2. UNO #2 reads the sensor state.
3. The Arduino sends the slot information to ESP32 #2 through serial communication.
4. ESP32 #2 forwards the status to ESP32 #1 through ESP-NOW.
5. ESP32 #1 updates the dashboard to display the slot as `FREE` or `FULL`.

The displayed state depends on the sensor's detection logic and the firmware's active HIGH/LOW configuration.

### 3. Vehicle entrance process

1. A vehicle approaches the entrance.
2. The obstacle detection sensor provides an input to ESP32 #2.
3. The controller evaluates the sensor input and current operating mode.
4. In automatic mode, the programmed gate-control logic determines whether the entrance gate can open.
5. The entrance servo moves to its configured open position.
6. After the vehicle enters, the gate-control logic determines when to close the gate.
7. Updated gate status can be reported to the master controller and dashboard.

The entrance gate should only admit a vehicle when a parking space is available. This condition must be enforced by the active gate-controller firmware.

### 4. Vehicle exit process

1. A vehicle approaches the exit.
2. The configured exit detection sensor provides input to the controller.
3. The exit gate opens according to the configured automatic or manual control logic.
4. The vehicle passes through the exit.
5. The gate closes according to the programmed safety and timing rules.
6. Parking-slot sensors update the occupancy information when the vehicle leaves its parking space.

### 5. Automatic lighting

UNO #1 reads the two LDR sensors to measure the surrounding light level.

- In bright conditions, the automatic lighting logic can turn the relevant lights off.
- In dark conditions, the automatic lighting logic can turn the relevant lights on.
- The four slot lights and two common-area lights are controlled by the configured output logic.
- Dashboard lighting commands can be used where supported by the installed firmware.

The threshold and detection polarity must be calibrated using actual LDR readings.

### 6. Manual dashboard control

The user connects to the SmartPark Wi-Fi network and opens the dashboard in a browser.

The dashboard provides controls for:

- Automatic mode.
- Manual mode.
- Opening and closing the entrance gate.
- Opening and closing the exit gate.
- Turning the lights on and off.
- Activating supported parking guidance functions.

Manual commands are sent from the dashboard to ESP32 #1, then forwarded to ESP32 #2 using ESP-NOW.

The gate controller must retain appropriate sensor and safety checks even when manual commands are enabled.

---

## Circuit Connections

The following pin assignments describe the intended project configuration. Check the final firmware and actual wiring before making connections.

### ESP32 #2 – Gate Controller

| Component | ESP32 #2 pin |
|---|---|
| Entrance servo signal | GPIO 25 |
| Entrance obstacle sensor output | GPIO 27 |
| Exit servo signal | GPIO 26 |
| Exit sensor trigger, if using HC-SR04 | GPIO 32 |
| Exit sensor echo, if using HC-SR04 | GPIO 33 |
| UART TX to UNO #1 | GPIO 17 |
| UART RX from UNO #1 | GPIO 16 |
| UART TX to UNO #2 | GPIO 18 |
| UART RX from UNO #2 | GPIO 19 |

The exit sensor entries above apply only if the installed firmware and hardware still use an HC-SR04 ultrasonic sensor. If an obstacle detector replaces it, update the pin table to match the actual sensor wiring and firmware.

### Arduino UNO #1 – Lighting

| Component | UNO #1 pin |
|---|---|
| Slot light 1 | D2 |
| Slot light 2 | D3 |
| Slot light 3 | D4 |
| Slot light 4 | D5 |
| Common light 1 | D6 |
| Common light 2 | D7 |
| LDR 1 analog output | A0 |
| LDR 2 analog output | A1 |
| Serial RX | D0 / RX |
| Serial TX | D1 / TX |

### Arduino UNO #2 – Parking Sensors

| Component | UNO #2 pin |
|---|---|
| Slot sensor 1 | D2 |
| Slot sensor 2 | D3 |
| Slot sensor 3 | D4 |
| Slot sensor 4 | D5 |
| Serial RX | D0 / RX |
| Serial TX | D1 / TX |

### ESP32 #1 – Master Controller

ESP32 #1 uses its Wi-Fi and ESP-NOW interfaces for communication. The firmware update page is provided through its HTTP server, so no additional external hardware pin is required for the update feature.

### Electrical considerations

- Connect the grounds of the boards and power supplies together.
- Do not power servo motors directly from an ESP32 GPIO pin.
- Use a suitable regulated supply for the servo motors.
- Protect ESP32 inputs from 5 V signals using appropriate level shifting or voltage dividers.
- UNO TX signals connected to ESP32 RX pins require 3.3 V-compatible level shifting.
- If the installed obstacle sensor produces a 5 V output, verify that the ESP32 input is protected.
- Check the polarity and voltage requirements of all sensor modules before powering the circuit.

---

## Wi-Fi Configuration

The default master access-point configuration is:

| Setting | Value |
|---|---|
| Wi-Fi network name | `SmartCarPark` |
| Wi-Fi password | `12345678` |
| Wi-Fi channel | 6 |
| Dashboard URL | `http://192.168.4.1` |
| HTTP port | 80 |
| WebSocket port | 81 |

### Connecting to the dashboard

1. Power on the SmartPark system.
2. Open the Wi-Fi settings on your phone or computer.
3. Connect to `SmartCarPark`.
4. Enter the password `12345678`.
5. Open `http://192.168.4.1` in a web browser.
6. Use the dashboard to view parking availability and access supported controls.

The dashboard is hosted locally by ESP32 #1. Internet access is not required for local operation.

---

## Firmware Installation

### ESP32 #1 – Master

1. Install the ESP32 board package in Arduino IDE.
2. Open `ESP32_1_Master.ino`.
3. Select the appropriate ESP32 board and partition scheme.
4. Connect the board through USB.
5. Select the correct serial port.
6. Compile and upload the firmware.
7. Open Serial Monitor to inspect startup messages.
8. Connect to the SmartPark Wi-Fi network and verify the dashboard.

### ESP32 #2 – Gate Controller

1. Open `ESP32_2_Gate_Controller.ino`.
2. Confirm the ESP-NOW channel and master communication settings.
3. Verify the slave MAC address reported by the actual board.
4. Upload the firmware using the appropriate method.
5. Check servo operation, obstacle detection, serial communication, and status messages.

### Arduino UNO #1 – Lighting Controller

1. Open `UNO_1_Lighting.ino`.
2. Select Arduino UNO R3.
3. Upload the sketch through USB.
4. Check LDR readings in Serial Monitor.
5. Test the lighting response under bright and dark conditions.
6. Verify that the serial command protocol matches ESP32 #2.

### Arduino UNO #2 – Parking Sensors

1. Open `UNO_2_Slot_Sensors.ino`.
2. Select Arduino UNO R3.
3. Upload the sketch through USB.
4. Test each sensor separately.
5. Confirm that each sensor reports the correct slot state.
6. Verify serial messages received by ESP32 #2.

**Important:** Disconnect devices from UNO pins D0 and D1 when required for USB uploading, then reconnect them after programming. Follow the wiring and upload instructions for your actual circuit.

---

## Firmware Update

SmartPark includes a dashboard-based firmware uploader for **ESP32 #1 only**.

### Updating the master ESP32

1. Compile the master firmware for the correct ESP32 board.
2. Export the compiled application `.bin` file from Arduino IDE.
3. Connect to the SmartCarPark Wi-Fi network.
4. Open `http://192.168.4.1`.
5. Select the firmware update page.
6. Choose the matching master application `.bin` file.
7. Start the update and wait for the board to restart.
8. Reconnect to the dashboard and verify normal operation.

Only upload firmware built for the correct board and flash configuration. Do not upload the bootloader or partition-table binary as the application firmware.

The dashboard uploader does not program ESP32 #2 or the Arduino UNO boards. Those boards must be updated using their supported programming methods.

---

## Safety Considerations

- Test gate movements without a vehicle before normal operation.
- Keep hands and objects clear of moving servo gates.
- Use suitable external power for servos and lighting loads.
- Protect all 3.3 V ESP32 inputs from 5 V signals.
- Confirm the gate controller's free-slot check before enabling automatic entrance operation.
- Avoid powering down the master during firmware installation.
- Keep a USB recovery method available.
- Do not expose firmware upload endpoints to untrusted users.
- Use the parking sensors and gate detection logic to reduce the risk of unsafe movement.

This prototype is intended for educational and demonstration purposes. A real parking installation would require additional safety mechanisms, fault detection, and testing.

---

## Troubleshooting

| Problem | Possible cause | Suggested check |
|---|---|---|
| SmartCarPark Wi-Fi does not appear | Access point startup failed | Check the ESP32 power supply and Serial Monitor |
| Dashboard does not open | Incorrect IP or Wi-Fi connection | Connect to SmartCarPark and use `http://192.168.4.1` |
| Parking slots do not update | Sensor wiring or serial communication issue | Test each sensor and inspect UNO #2 serial output |
| ESP32 boards do not communicate | Incorrect MAC address or channel mismatch | Confirm the slave MAC and channel 6 settings |
| Gate servo does not move | Power, signal, or servo-angle issue | Check servo power, common ground, and GPIO assignments |
| Lights operate in reverse | Incorrect LDR threshold or polarity | Read ADC values in bright and dark conditions |
| UNO does not upload over USB | Serial connection occupies D0/D1 | Disconnect the serial wires during uploading if necessary |
| Firmware upload fails | Wrong `.bin`, insufficient flash space, or interrupted upload | Rebuild for the correct board and inspect Serial Monitor |
| Dashboard controls do not work | Command mismatch between master and slave firmware | Compare the exact command strings supported by ESP32 #2 |

---

## Future Improvements

Potential improvements include:

- More robust gate safety and vehicle-presence detection.
- A history of parking occupancy events.
- Password-protected dashboard controls.
- Improved communication error reporting.
- Configurable sensor thresholds.
- Watchdog and fault-recovery mechanisms.
- Firmware updates for additional controllers through a properly supported programming interface.
- Secure firmware validation and rollback support.
- A mobile-friendly dashboard with additional monitoring features.

---

## License

Choose an appropriate open-source license before publishing this repository. For example, the MIT License allows others to use, modify, and distribute the project under its terms.

If no license has been added, the repository should not yet claim to be released under the MIT License.

---

## Author

**SmartPark Smart Car Parking System**

Developed as an embedded systems and Internet of Things project.

The project demonstrates the integration of microcontrollers, sensor-based monitoring, wireless communication, automatic lighting, servo control, and a local web-based user interface.
