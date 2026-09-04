# MRIDA ENERGY SOLUTIONS - ESP32 Smart Prepaid Energy Meter (OLED & PZEM-004T v3.0)

A comprehensive, production-ready ESP32 firmware for a **Prepaid Energy Meter system** equipped with a 0.96" SSD1306 OLED display, PZEM-004T AC Energy Sensor, EEPROM balance storage, automatic load cutoff relay, anti-theft detection, and a mobile-responsive Web Dashboard.

---

## 🛠️ Hardware Wiring Diagram

| Component | Component Pin | ESP32 GPIO Pin | Details |
| :--- | :--- | :--- | :--- |
| **SSD1306 OLED Display (I2C)** | SDA | **GPIO 21** | I2C Data |
| | SCL | **GPIO 22** | I2C Clock |
| | VCC | 3.3V / 5V | Power |
| | GND | GND | Ground |
| **PZEM-004T v3.0 Sensor** | RX | **GPIO 17 (TX2)** | Serial2 TX to PZEM RX |
| | TX | **GPIO 16 (RX2)** | Serial2 RX to PZEM TX |
| | VCC | 5V | Power |
| | GND | GND | Ground |
| **Relay Module** | IN / Signal | **GPIO 4** | Active HIGH load control |
| | VCC | 5V | Power |
| | GND | GND | Ground |
| **Push Button** | Terminal 1 | **GPIO 32** | Active LOW (Internal Pullup) |
| | Terminal 2 | GND | Ground |

---

## 📚 Required Arduino IDE Libraries

Before compiling, install the following libraries via the Arduino IDE Library Manager (**Tools -> Manage Libraries...**):

1. **Adafruit SSD1306** (`#include <Adafruit_SSD1306.h>`)
2. **Adafruit GFX Library** (`#include <Adafruit_GFX.h>`)
3. **PZEM-004T-v30** by Jakub Mandula (`#include <PZEM004Tv30.h>`)
4. **ArduinoJson** by Benoit Blanchon (v6.x) (`#include <ArduinoJson.h>`)

---

## 🚀 Getting Started & Compilation Instructions

1. Open `prepaid_Energy_Meter.ino` in Arduino IDE or PlatformIO.
2. Ensure `config.h` and `web_dashboard.h` are located in the same directory.
3. Select board: **ESP32 Dev Module**.
4. Set Partition Scheme to **Huge APP (3MB No OTA)** or standard **Default 4MB**.
5. Upload sketch to your ESP32.

---

## 🌐 Web Dashboard Access

1. Power on the ESP32. It will automatically host an Access Point:
   - **SSID**: `ESP32_Prepaid_Meter`
   - **Password**: `password123`
2. Connect your smartphone/PC to the WiFi network.
3. Open browser and navigate to: `http://192.168.4.1/`

---

## 🔘 Button Controls

- **Short Press (< 2 seconds)**: Switches between Screen 1 (Live Measurements & Balance) and Screen 2 (Safety Thresholds & Protection Limits).
- **Long Press (> 2 seconds)**: Toggles manual load override on the power relay.

---

## ⚙️ EEPROM Features & Security Protection

- **EEPROM Storage**: Account balance, total energy consumption, pricing rate, and threshold settings persist across power restarts.
- **Over-Voltage / Over-Current Disconnect**: Trips load relay automatically if AC line voltage or load current exceeds user-configured limits.
- **Anti-Theft Detection**: Logs and triggers alert if current draw (> 20mA) is detected while load relay is in OFF state.
- **Low-Balance Cutoff**: Disconnects load relay when balance drops to or below minimum threshold (default ₹0.00).
