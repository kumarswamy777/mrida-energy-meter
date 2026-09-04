#include <PZEM004Tv30.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

#include "config.h"
#include "web_dashboard.h"

// =========================================================================
// 📶 1. WIFI CONFIGURATION (Mee Hotspot / WiFi Details Ikkada Ivvandi)
// =========================================================================
// ⚠️ IMPORTANT: Ikkada mee Mobile Hotspot Name & Password ivvandi!
// ESP32 internet ki connect aithene Render Cloud nundi balance & relay receive cheskuntundhi!
const char* WIFI_SSID     = "YOUR_WIFI_NAME";     // <-- Mee Hotspot / WiFi Name (Exact Case!)
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"; // <-- Mee Hotspot / WiFi Password

// =========================================================================
// 🌐 2. RENDER CLOUD SERVER URL
// =========================================================================
const char* RENDER_SERVER_URL = "https://mrida-energy-meter.onrender.com";

// --- Hardware Objects & Server ---
WebServer server(80);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);

// --- Telemetry Data Structure ---
struct PzemData {
    float voltage;
    float current;
    float power;
    float energy;
    bool isValid;
};

// --- Global State Variables ---
PzemData readings = {230.2, 1.250, 287.75, 1.450, true};

unsigned long lastReadTime = 0;
unsigned long screenRefreshTime = 0;
unsigned long balanceUpdateTime = 0;
unsigned long lastWifiCheckTime = 0;
unsigned long lastCloudSyncTime = 0;
float lastEnergyValue = 0.0;

bool pzemConnected = false;
bool relayState = true;         // true = Load ON, false = Load OFF
bool faultDetected = false;     // Over-voltage / Over-current flag
bool theftDetected = false;     // Anti-theft trigger flag
bool manualOverride = false;    // Manual user override flag

float balance = DEFAULT_BALANCE;
float overVoltageThreshold = DEFAULT_OVER_VOLTAGE_THRESHOLD;
float overCurrentThreshold = DEFAULT_OVER_CURRENT_THRESHOLD;
float theftCurrentThreshold = DEFAULT_THEFT_CURRENT_THRESHOLD;
float minimumBalance = DEFAULT_MINIMUM_BALANCE;
float costPerKWh = DEFAULT_COST_PER_KWH;

int displayPage = 0;            // 0 = Live Telemetry, 1 = Network & Limits
unsigned long notificationEndTime = 0;
String currentNotification = "";
String activeIPString = "192.168.4.1";

// Button state variables
bool lastButtonState = HIGH;
unsigned long buttonPressTime = 0;

// --- Function Prototypes ---
void readPzemData();
void updateBalance();
void checkFaultsAndTheft();
void controlRelay();
void updateDisplay();
void drawMainPage();
void drawAlertsPage();
void showNotification(const String& message, unsigned long duration = 4000);
void handleButton();
void configureWiFi();
void checkWiFiConnection();
void syncWithRenderCloud();

void loadEEPROMData();
void saveBalance();
void saveThresholds();
void resetEEPROMToDefaults();
void sendJsonResponse(int code, const String& jsonStr);

// --- Local Web Server Route Handlers (Instant direct access on http://192.168.1.40) ---
void handleRoot() {
    server.send_P(200, "text/html", index_html);
}

void sendJsonResponse(int code, const String& jsonStr) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(code, "application/json", jsonStr);
}

void handleData() {
    StaticJsonDocument<512> doc;
    doc["voltage"] = readings.voltage;
    doc["current"] = readings.current;
    doc["power"] = readings.power;
    doc["energy"] = readings.energy;
    doc["balance"] = balance;
    doc["relayState"] = relayState;
    doc["faultDetected"] = faultDetected;
    doc["theftDetected"] = theftDetected;
    doc["pzemConnected"] = pzemConnected;
    doc["overVoltage"] = overVoltageThreshold;
    doc["overCurrent"] = overCurrentThreshold;
    doc["theftCurrent"] = theftCurrentThreshold;
    doc["minBalance"] = minimumBalance;
    doc["costPerKWh"] = costPerKWh;

    String jsonStr;
    serializeJson(doc, jsonStr);
    sendJsonResponse(200, jsonStr);
}

void handleRecharge() {
    if (!server.hasArg("amount")) {
        sendJsonResponse(400, "{\"success\":false,\"message\":\"Missing amount parameter\"}");
        return;
    }

    float amount = server.arg("amount").toFloat();
    if (amount <= 0) {
        sendJsonResponse(400, "{\"success\":false,\"message\":\"Invalid recharge amount\"}");
        return;
    }

    balance += amount;
    saveBalance();

    if (balance > minimumBalance && !faultDetected) {
        relayState = true;
        manualOverride = false;
        controlRelay();
    }

    showNotification("RECHARGED: +" + String(amount, 0), 4000);
    Serial.println("[LOCAL] Recharge received: +Rs. " + String(amount, 2) + " | New Balance: Rs. " + String(balance, 2));

    StaticJsonDocument<256> doc;
    doc["success"] = true;
    doc["balance"] = balance;
    doc["message"] = "Recharge successful";

    String jsonStr;
    serializeJson(doc, jsonStr);
    sendJsonResponse(200, jsonStr);
}

void handleSetRelay() {
    if (!server.hasArg("state")) {
        sendJsonResponse(400, "{\"success\":false,\"message\":\"Missing state parameter\"}");
        return;
    }

    int state = server.arg("state").toInt();
    int overrideVal = server.hasArg("override") ? server.arg("override").toInt() : 0;

    relayState = (state == 1);
    manualOverride = (overrideVal == 1);
    controlRelay();

    showNotification(relayState ? "LOCAL: RELAY ON" : "LOCAL: RELAY OFF", 3000);
    Serial.println("[LOCAL] Relay switch command: " + String(relayState ? "ON" : "OFF"));

    StaticJsonDocument<256> doc;
    doc["success"] = true;
    doc["relayState"] = relayState;
    doc["manualOverride"] = manualOverride;

    String jsonStr;
    serializeJson(doc, jsonStr);
    sendJsonResponse(200, jsonStr);
}

void handleSetThresholds() {
    if (server.hasArg("overVoltage")) overVoltageThreshold = server.arg("overVoltage").toFloat();
    if (server.hasArg("overCurrent")) overCurrentThreshold = server.arg("overCurrent").toFloat();
    if (server.hasArg("theftCurrent")) theftCurrentThreshold = server.arg("theftCurrent").toFloat();
    if (server.hasArg("minBalance")) minimumBalance = server.arg("minBalance").toFloat();
    if (server.hasArg("costPerKWh")) costPerKWh = server.arg("costPerKWh").toFloat();

    saveThresholds();
    showNotification("SETTINGS SAVED", 3000);
    sendJsonResponse(200, "{\"success\":true,\"message\":\"Thresholds updated\"}");
}

void handleFactoryReset() {
    resetEEPROMToDefaults();
    showNotification("FACTORY RESET", 4000);
    sendJsonResponse(200, "{\"success\":true,\"message\":\"Factory reset performed\"}");
}

void handleNotFound() {
    server.send(404, "text/plain", "404: Page Not Found on ESP32");
}

// --- Relay Control (Active-LOW: LOW activates coil, HIGH de-activates) ---
void controlRelay() {
    pinMode(RELAY_PIN, OUTPUT);
    int pinLevel = (RELAY_ACTIVE_LOW) ? (relayState ? LOW : HIGH) : (relayState ? HIGH : LOW);
    digitalWrite(RELAY_PIN, pinLevel);
    Serial.print("[RELAY] State: ");
    Serial.print(relayState ? "ON (LOAD CONNECTED)" : "OFF (LOAD ISOLATED)");
    Serial.print(" | Pin GPIO ");
    Serial.print(RELAY_PIN);
    Serial.print(" Level: ");
    Serial.println(pinLevel == LOW ? "LOW (0V)" : "HIGH (3.3V)");
}

// --- Arduino Setup ---
void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("\n=======================================================");
    Serial.println("  MRIDA ENERGY SOLUTIONS - ESP32 SMART PREPAID METER   ");
    Serial.println("=======================================================");

    pinMode(RELAY_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    controlRelay();

    Serial2.begin(9600, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);

    Wire.begin(OLED_SDA, OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("[ERROR] SSD1306 OLED initialization failed!");
    } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 5);
        display.println("MRIDA ENERGY SOLUTIONS");
        display.drawLine(0, 15, 128, 15, SSD1306_WHITE);
        display.setCursor(0, 24);
        display.println("Smart Prepaid Meter");
        display.setCursor(0, 42);
        display.println("Connecting WiFi...");
        display.display();
    }

    if (!EEPROM.begin(EEPROM_SIZE)) {
        Serial.println("[ERROR] EEPROM init failed!");
    } else {
        loadEEPROMData();
    }

    if (balance <= 0.0) {
        balance = DEFAULT_BALANCE;
        saveBalance();
    }

    configureWiFi();

    // Start Local Web Server
    server.on("/", HTTP_GET, handleRoot);
    server.on("/data", HTTP_GET, handleData);
    server.on("/recharge", HTTP_POST, handleRecharge);
    server.on("/setRelay", HTTP_POST, handleSetRelay);
    server.on("/setThresholds", HTTP_POST, handleSetThresholds);
    server.on("/factoryReset", HTTP_POST, handleFactoryReset);
    server.onNotFound(handleNotFound);
    server.begin();

    Serial.println("\n[OK] System Ready!");
    Serial.print("Access locally in browser at: http://"); Serial.println(activeIPString);
}

// --- Main Loop ---
void loop() {
    server.handleClient();
    handleButton();

    unsigned long currentMillis = millis();

    // 1. Read PZEM Sensor Data (Hardware sensor or live simulation)
    if (currentMillis - lastReadTime >= PZEM_READ_INTERVAL) {
        lastReadTime = currentMillis;
        readPzemData();
        checkFaultsAndTheft();
    }

    // 2. Deduct Balance based on active energy consumption
    if (currentMillis - balanceUpdateTime >= BALANCE_UPDATE_INTERVAL) {
        balanceUpdateTime = currentMillis;
        updateBalance();
    }

    // 3. Update OLED Display
    if (currentMillis - screenRefreshTime >= SCREEN_REFRESH_INTERVAL) {
        screenRefreshTime = currentMillis;
        updateDisplay();
    }

    // 4. Cloud Sync with Render (Every 2 seconds when WiFi is connected)
    if (currentMillis - lastCloudSyncTime >= 2000) {
        lastCloudSyncTime = currentMillis;
        syncWithRenderCloud();
    }

    // 5. WiFi Reconnection Monitoring
    if (currentMillis - lastWifiCheckTime >= WIFI_RECHECK_INTERVAL) {
        lastWifiCheckTime = currentMillis;
        checkWiFiConnection();
    }
}

// --- Render Cloud Sync Engine ---
void syncWithRenderCloud() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (String(RENDER_SERVER_URL) == "" || String(RENDER_SERVER_URL).indexOf("onrender.com") == -1) return;

    WiFiClientSecure client;
    client.setInsecure(); // Disable SSL certificate validation
    client.setTimeout(4000);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(4000);

    String url = String(RENDER_SERVER_URL) + "/update";
    url += "?voltage=" + String(readings.voltage, 1);
    url += "&current=" + String(readings.current, 3);
    url += "&power=" + String(readings.power, 1);
    url += "&energy=" + String(readings.energy, 4);
    url += "&balance=" + String(balance, 2);
    url += "&relayState=" + String(relayState ? 1 : 0);
    url += "&faultDetected=" + String(faultDetected ? 1 : 0);
    url += "&theftDetected=" + String(theftDetected ? 1 : 0);

    if (http.begin(client, url)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK || httpCode == 200) {
            String payload = http.getString();
            StaticJsonDocument<384> doc;
            DeserializationError err = deserializeJson(doc, payload);
            if (!err) {
                // 1. Process Pending Recharge from Cloud
                if (doc.containsKey("pendingRecharge")) {
                    float rechargeAmt = doc["pendingRecharge"];
                    if (rechargeAmt > 0.0) {
                        balance += rechargeAmt;
                        saveBalance();
                        if (balance > minimumBalance && !faultDetected) {
                            relayState = true;
                            manualOverride = false;
                            controlRelay();
                        }
                        showNotification("RECHARGED: +" + String(rechargeAmt, 0), 4000);
                        Serial.println("[CLOUD SYNC] Recharge applied: +Rs. " + String(rechargeAmt, 2) + " | Balance: Rs. " + String(balance, 2));
                    }
                }

                // 2. Process Cloud Relay Command
                if (doc.containsKey("relayCommand")) {
                    int cmd = doc["relayCommand"];
                    if (cmd == 1 || cmd == 0) {
                        bool targetRelay = (cmd == 1);
                        if (targetRelay != relayState) {
                            relayState = targetRelay;
                            manualOverride = (cmd == 0);
                            controlRelay();
                            showNotification(relayState ? "CLOUD: RELAY ON" : "CLOUD: RELAY OFF", 3000);
                            Serial.println("[CLOUD SYNC] Relay switched by Render -> " + String(relayState ? "ON (CLOSED)" : "OFF (OPEN)"));
                        }
                    }
                }
            }
        } else {
            Serial.print("[CLOUD SYNC] Render response code: ");
            Serial.println(httpCode);
        }
        http.end();
    }
}

// --- Sensor Reading Routine (Hardware + Auto Bench Simulation) ---
void readPzemData() {
    float v = pzem.voltage();
    float i = pzem.current();
    float p = pzem.power();
    float e = pzem.energy();

    if (!isnan(v) && v > 50.0) {
        pzemConnected = true;
        readings.voltage = v;
        readings.current = i;
        readings.power = p;
        readings.energy = e;
        readings.isValid = true;
    } else {
        pzemConnected = false;
        readings.isValid = true;

        if (relayState && balance > minimumBalance) {
            readings.voltage = 230.0 + ((float)random(-15, 15) / 10.0);
            readings.current = 1.250 + ((float)random(-40, 40) / 1000.0);
            readings.power = readings.voltage * readings.current;
            readings.energy += 0.0001;
        } else {
            readings.voltage = 230.0 + ((float)random(-8, 8) / 10.0);
            readings.current = 0.000;
            readings.power = 0.0;
        }
    }
}

// --- Energy Balance Billing Routine ---
void updateBalance() {
    if (!readings.isValid) return;

    if (relayState && balance > minimumBalance) {
        float energyIncrement = 0.0001;

        if (pzemConnected) {
            if (lastEnergyValue == 0.0 && readings.energy > 0.0) {
                lastEnergyValue = readings.energy;
                return;
            }
            float energyDiff = readings.energy - lastEnergyValue;
            if (energyDiff > 0.0) {
                energyIncrement = energyDiff;
                lastEnergyValue = readings.energy;
            }
        }

        float cost = energyIncrement * costPerKWh;
        if (balance > cost) {
            balance -= cost;
        } else {
            balance = 0.0;
            relayState = false;
            controlRelay();
            showNotification("LOW BALANCE CUTOFF", 5000);
        }
        saveBalance();
    }
}

// --- Protection & Fault Check Logic ---
void checkFaultsAndTheft() {
    faultDetected = false;
    theftDetected = false;

    if (readings.isValid) {
        if (readings.voltage > overVoltageThreshold) {
            faultDetected = true;
            if (relayState && !manualOverride) {
                relayState = false;
                controlRelay();
                showNotification("OVER VOLTAGE!", 5000);
            }
        }

        if (readings.current > overCurrentThreshold) {
            faultDetected = true;
            if (relayState && !manualOverride) {
                relayState = false;
                controlRelay();
                showNotification("OVER CURRENT!", 5000);
            }
        }

        if (!relayState && readings.current > theftCurrentThreshold) {
            theftDetected = true;
            showNotification("THEFT ALARM!", 5000);
        }
    }

    if (balance <= minimumBalance && relayState && !manualOverride) {
        relayState = false;
        controlRelay();
        showNotification("LOW BALANCE CUTOFF", 5000);
    }
}

// --- Display Rendering Routines (128x64 OLED) ---
void updateDisplay() {
    display.clearDisplay();

    if (currentNotification != "" && millis() < notificationEndTime) {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 5);
        display.println("=== NOTIFICATION ===");
        display.setTextSize(2);
        display.setCursor(0, 25);
        display.println(currentNotification);
        display.display();
        return;
    } else {
        currentNotification = "";
    }

    if (displayPage == 0) {
        drawMainPage();
    } else {
        drawAlertsPage();
    }

    display.display();
}

void drawMainPage() {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("MRIDA ");
    display.setCursor(95, 0);
    display.print(WiFi.status() == WL_CONNECTED ? " [WIFI]" : "  [AP]");

    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // Line 1: Voltage & Current
    display.setCursor(0, 14);
    display.print("V: ");
    display.print(readings.voltage, 1); display.print("V ");
    display.print("I:"); display.print(readings.current, 2); display.print("A");

    // Line 2: Power & Energy
    display.setCursor(0, 27);
    display.print("P: ");
    display.print(readings.power, 1); display.print("W ");
    display.print("E:"); display.print(readings.energy, 3); display.print("k");

    // Line 3: Balance
    display.setCursor(0, 40);
    display.print("BAL: Rs.");
    display.print(balance, 2);

    // Line 4: Relay & System Status
    display.setCursor(0, 53);
    display.print("RELAY:");
    display.print(relayState ? "ON " : "OFF");
    display.print(" | ");
    if (faultDetected) display.print("FAULT");
    else if (theftDetected) display.print("THEFT");
    else display.print("OK");
}

void drawAlertsPage() {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("NETWORK & SAFETY");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    display.setCursor(0, 14);
    display.print("IP: "); display.println(activeIPString);

    display.setCursor(0, 27);
    display.print("OV Limit: "); display.print(overVoltageThreshold, 0); display.print("V");

    display.setCursor(0, 40);
    display.print("OC Limit: "); display.print(overCurrentThreshold, 1); display.print("A");

    display.setCursor(0, 53);
    display.print("MinBal:Rs."); display.print(minimumBalance, 0);
    display.print(" | ");
    display.print(WiFi.status() == WL_CONNECTED ? "WiFi" : "SoftAP");
}

void showNotification(const String& message, unsigned long duration) {
    currentNotification = message;
    notificationEndTime = millis() + duration;
}

void handleButton() {
    bool currentButtonState = digitalRead(BUTTON_PIN);

    if (lastButtonState == HIGH && currentButtonState == LOW) {
        buttonPressTime = millis();
    } 
    else if (lastButtonState == LOW && currentButtonState == HIGH) {
        unsigned long pressDuration = millis() - buttonPressTime;
        if (pressDuration >= LONG_PRESS_TIME) {
            manualOverride = !manualOverride;
            relayState = !relayState;
            controlRelay();
            showNotification(relayState ? "OVERRIDE: ON" : "OVERRIDE: OFF", 3000);
        } else if (pressDuration >= DEBOUNCE_DELAY) {
            displayPage = (displayPage == 0) ? 1 : 0;
        }
    }

    lastButtonState = currentButtonState;
}

void configureWiFi() {
    WiFi.mode(WIFI_AP_STA);

    WiFi.softAP("ESP32_Prepaid_Meter", "password123");
    activeIPString = WiFi.softAPIP().toString();

    if (String(WIFI_SSID) != "YOUR_WIFI_NAME" && String(WIFI_SSID) != "") {
        Serial.print("[WiFi] Connecting to WiFi: ");
        Serial.println(WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            activeIPString = WiFi.localIP().toString();
            Serial.println("\n[WiFi] Connected to WiFi successfully!");
            Serial.print("[WiFi] Assigned IP: http://");
            Serial.println(activeIPString);
        } else {
            Serial.println("\n[WiFi] Could not connect to external WiFi. Operating in SoftAP mode (192.168.4.1)");
            activeIPString = "192.168.4.1";
        }
    }
}

void checkWiFiConnection() {
    if (String(WIFI_SSID) != "YOUR_WIFI_NAME" && String(WIFI_SSID) != "" && WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Reconnecting to WiFi...");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
}

void loadEEPROMData() {
    EEPROM.get(BALANCE_ADDR, balance);
    EEPROM.get(ENERGY_ADDR, lastEnergyValue);
    EEPROM.get(OVER_VOLTAGE_THRESHOLD_ADDR, overVoltageThreshold);
    EEPROM.get(OVER_CURRENT_THRESHOLD_ADDR, overCurrentThreshold);
    EEPROM.get(THEFT_CURRENT_THRESHOLD_ADDR, theftCurrentThreshold);
    EEPROM.get(MINIMUM_BALANCE_ADDR, minimumBalance);
    EEPROM.get(COST_PER_KWH_ADDR, costPerKWh);

    if (isnan(balance) || balance < 0) balance = DEFAULT_BALANCE;
    if (isnan(lastEnergyValue) || lastEnergyValue < 0) lastEnergyValue = DEFAULT_ENERGY;
    if (isnan(overVoltageThreshold) || overVoltageThreshold <= 0) overVoltageThreshold = DEFAULT_OVER_VOLTAGE_THRESHOLD;
    if (isnan(overCurrentThreshold) || overCurrentThreshold <= 0) overCurrentThreshold = DEFAULT_OVER_CURRENT_THRESHOLD;
    if (isnan(theftCurrentThreshold) || theftCurrentThreshold <= 0) theftCurrentThreshold = DEFAULT_THEFT_CURRENT_THRESHOLD;
    if (isnan(minimumBalance) || minimumBalance < 0) minimumBalance = DEFAULT_MINIMUM_BALANCE;
    if (isnan(costPerKWh) || costPerKWh <= 0) costPerKWh = DEFAULT_COST_PER_KWH;

    Serial.println("[EEPROM] Data loaded successfully.");
}

void saveBalance() {
    EEPROM.put(BALANCE_ADDR, balance);
    EEPROM.put(ENERGY_ADDR, readings.energy);
    EEPROM.commit();
}

void saveThresholds() {
    EEPROM.put(OVER_VOLTAGE_THRESHOLD_ADDR, overVoltageThreshold);
    EEPROM.put(OVER_CURRENT_THRESHOLD_ADDR, overCurrentThreshold);
    EEPROM.put(THEFT_CURRENT_THRESHOLD_ADDR, theftCurrentThreshold);
    EEPROM.put(MINIMUM_BALANCE_ADDR, minimumBalance);
    EEPROM.put(COST_PER_KWH_ADDR, costPerKWh);
    EEPROM.commit();
}

void resetEEPROMToDefaults() {
    balance = DEFAULT_BALANCE;
    lastEnergyValue = DEFAULT_ENERGY;
    overVoltageThreshold = DEFAULT_OVER_VOLTAGE_THRESHOLD;
    overCurrentThreshold = DEFAULT_OVER_CURRENT_THRESHOLD;
    theftCurrentThreshold = DEFAULT_THEFT_CURRENT_THRESHOLD;
    minimumBalance = DEFAULT_MINIMUM_BALANCE;
    costPerKWh = DEFAULT_COST_PER_KWH;

    saveBalance();
    saveThresholds();
    Serial.println("[EEPROM] Factory reset completed.");
}
