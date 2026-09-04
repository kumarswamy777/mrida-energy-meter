#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- Hardware Pin Definitions ---
// PZEM-004T v3.0 AC Energy Sensor Pins (Serial2)
#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17

// I2C OLED Display Pins (SSD1306 0.96" 128x64)
#define OLED_SDA 21
#define OLED_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1   
#define OLED_ADDRESS 0x3C 

// Control Pins
#define RELAY_PIN 4            // Relay module pin (GPIO 4)
#define RELAY_ACTIVE_LOW true  // true for standard Active-LOW relay modules (LOW=ON, HIGH=OFF)
#define BUTTON_PIN 32          // Push button (Active LOW with internal pullup)

// --- EEPROM Configuration ---
#define EEPROM_SIZE 128

// Memory Address Map
#define BALANCE_ADDR                  0   // float (4 bytes)
#define ENERGY_ADDR                   4   // float (4 bytes)
#define OVER_VOLTAGE_THRESHOLD_ADDR   8   // float (4 bytes)
#define OVER_CURRENT_THRESHOLD_ADDR   12  // float (4 bytes)
#define THEFT_CURRENT_THRESHOLD_ADDR  16  // float (4 bytes)
#define MINIMUM_BALANCE_ADDR          20  // float (4 bytes)
#define COST_PER_KWH_ADDR             24  // float (4 bytes)

// --- Default Values & Safety Thresholds ---
#define DEFAULT_OVER_VOLTAGE_THRESHOLD  260.0  // Volts
#define DEFAULT_OVER_CURRENT_THRESHOLD  10.0   // Amperes
#define DEFAULT_THEFT_CURRENT_THRESHOLD 0.02   // 20mA theft threshold when relay is OFF
#define DEFAULT_MINIMUM_BALANCE         0.0    // Minimum required balance in Rupees
#define DEFAULT_COST_PER_KWH            0.20   // Cost per kWh (e.g. ₹0.20)
#define DEFAULT_BALANCE                 250.00 // Default initial balance ₹250.00
#define DEFAULT_ENERGY                  1.450  // Default energy counter (kWh)

// --- System Timings ---
const unsigned long PZEM_READ_INTERVAL       = 1000;  // Sensor read interval (1 sec)
const unsigned long SCREEN_REFRESH_INTERVAL  = 1000;  // OLED screen refresh rate (1 sec)
const unsigned long BALANCE_UPDATE_INTERVAL  = 2000;  // Deduct energy cost from balance (2 sec)
const unsigned long WIFI_RECHECK_INTERVAL    = 30000; // Check and reconnect WiFi (30 sec)
const unsigned long DEBOUNCE_DELAY           = 50;    // Button debounce time (50 ms)
const unsigned long LONG_PRESS_TIME          = 2000;  // Long-press threshold for manual override (2 sec)

#endif // CONFIG_H
