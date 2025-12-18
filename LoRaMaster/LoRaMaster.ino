#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
#include "LoRaBoards.h"

// Конфигурация WiFi AP
const char* AP_SSID = "LoRaConfig";
const char* AP_PASSWORD = "12345678";
IPAddress apIP(192, 168, 4, 1);
IPAddress netMask(255, 255, 255, 0);

// Конфигурация UART
const char* RESPONSE_MESSAGE = "SOK";
const int RX_PIN = 12;
const int TX_PIN = 13;
const long UART_BAUD = 9600;

// Конфигурация LoRa по умолчанию
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ 868.0
#endif

// Текущие настройки
String currentSF = "12";
String currentBW = "125.0";
String currentTX = "17";
String currentFreq = "868.0";

// Параметры из SINIT
String deviceID = "";
String deviceLat = "";
String deviceLon = "";

// Буферы
char uartBuffer[256];
char loraBuffer[256];
char displayBuffer[256];
unsigned int bufferIndex = 0;
bool settingsApplied = false;

// Веб-сервер
WebServer server(80);

void setup() {
    setupBoards();
    delay(1500);
    
    Serial.begin(115200);
    Serial1.begin(UART_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);
    
    Serial.println("UART+LoRa Gateway with WiFi AP");
    
    // Инициализация дисплея
    if (u8g2) {
        u8g2->setFont(u8g2_font_ncenB08_tr);
        displayScreen("Starting...", "WiFi AP: LoRaConfig", "IP: 192.168.4.1", "");
    }
    
    // Запуск WiFi точки доступа
    setupWiFiAP();
    
    // Настройка веб-сервера
    setupWebServer();
    
    Serial.print("WiFi AP started: ");
    Serial.println(AP_SSID);
    Serial.print("IP address: ");
    Serial.println(WiFi.softAPIP());
    
    // Инициализация LoRa
    initLoRa();
}

void loop() {
    // Обработка веб-запросов
    server.handleClient();
    
    // Прием UART
    readUART();
    
    // Прием LoRa
    readLoRa();
}

void setupWiFiAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, netMask);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    
    Serial.println("WiFi AP Configuration:");
    Serial.print("SSID: ");
    Serial.println(AP_SSID);
    Serial.print("Password: ");
    Serial.println(AP_PASSWORD);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
}

void setupWebServer() {
    // Корневая страница с формой настроек
    server.on("/", HTTP_GET, []() {
        String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>LoRa Configuration</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; }
        .status { background: #e8f5e9; padding: 10px; border-radius: 5px; margin: 10px 0; }
        .form-group { margin: 15px 0; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        select, input { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 5px; font-size: 16px; }
        button { background: #4CAF50; color: white; border: none; padding: 15px 30px; border-radius: 5px; cursor: pointer; font-size: 16px; width: 100%; margin-top: 20px; }
        button:hover { background: #45a049; }
        .current { background: #e3f2fd; padding: 10px; border-radius: 5px; margin: 10px 0; }
        .device-info { background: #fff3cd; padding: 10px; border-radius: 5px; margin: 10px 0; border: 1px solid #ffeaa7; }
    </style>
</head>
<body>
    <div class="container">
        <h1>LoRa Configuration</h1>
        
        <div class="status">
            <p><strong>Device IP:</strong> 192.168.4.1</p>
            <p><strong>WiFi SSID:</strong> LoRaConfig</p>
            <p><strong>UART Port:</strong> 12(RX), 13(TX) - 9600 baud</p>
        </div>
        
        <div class="device-info">
            <h3>Device Information (from SINIT)</h3>
            <p><strong>Device ID:</strong> )rawliteral" + deviceID + R"rawliteral(</p>
            <p><strong>Latitude:</strong> )rawliteral" + deviceLat + R"rawliteral(</p>
            <p><strong>Longitude:</strong> )rawliteral" + deviceLon + R"rawliteral(</p>
        </div>
        
        <div class="current">
            <h3>Current LoRa Settings</h3>
            <p><strong>Spreading Factor:</strong> )rawliteral" + currentSF + R"rawliteral(</p>
            <p><strong>Bandwidth:</strong> )rawliteral" + currentBW + R"rawliteral( kHz</p>
            <p><strong>TX Power:</strong> )rawliteral" + currentTX + R"rawliteral( dBm</p>
            <p><strong>Frequency:</strong> )rawliteral" + currentFreq + R"rawliteral( MHz</p>
        </div>
        
        <form action="/set" method="GET">
            <div class="form-group">
                <label for="sf">Spreading Factor (SF):</label>
                <select id="sf" name="sf">
                    <option value="7" )rawliteral" + (currentSF=="7"?"selected":"") + R"rawliteral(>SF7 - Fastest, Lowest Range</option>
                    <option value="8" )rawliteral" + (currentSF=="8"?"selected":"") + R"rawliteral(>SF8</option>
                    <option value="9" )rawliteral" + (currentSF=="9"?"selected":"") + R"rawliteral(>SF9</option>
                    <option value="10" )rawliteral" + (currentSF=="10"?"selected":"") + R"rawliteral(>SF10</option>
                    <option value="11" )rawliteral" + (currentSF=="11"?"selected":"") + R"rawliteral(>SF11</option>
                    <option value="12" )rawliteral" + (currentSF=="12"?"selected":"") + R"rawliteral(>SF12 - Slowest, Highest Range</option>
                </select>
            </div>
            
            <div class="form-group">
                <label for="bw">Bandwidth (BW):</label>
                <select id="bw" name="bw">
                    <option value="125.0" )rawliteral" + (currentBW=="125.0"?"selected":"") + R"rawliteral(>125.0 kHz - Best Range</option>
                    <option value="250.0" )rawliteral" + (currentBW=="250.0"?"selected":"") + R"rawliteral(>250.0 kHz</option>
                    <option value="500.0" )rawliteral" + (currentBW=="500.0"?"selected":"") + R"rawliteral(>500.0 kHz - Highest Data Rate</option>
                </select>
            </div>
            
            <div class="form-group">
                <label for="tx">TX Power:</label>
                <select id="tx" name="tx">
                    <option value="2" )rawliteral" + (currentTX=="2"?"selected":"") + R"rawliteral(>2 dBm</option>
                    <option value="5" )rawliteral" + (currentTX=="5"?"selected":"") + R"rawliteral(>5 dBm</option>
                    <option value="8" )rawliteral" + (currentTX=="8"?"selected":"") + R"rawliteral(>8 dBm</option>
                    <option value="11" )rawliteral" + (currentTX=="11"?"selected":"") + R"rawliteral(>11 dBm</option>
                    <option value="14" )rawliteral" + (currentTX=="14"?"selected":"") + R"rawliteral(>14 dBm</option>
                    <option value="17" )rawliteral" + (currentTX=="17"?"selected":"") + R"rawliteral(>17 dBm</option>
                    <option value="20" )rawliteral" + (currentTX=="20"?"selected":"") + R"rawliteral(>20 dBm - Max Power</option>
                </select>
            </div>
            
            <div class="form-group">
                <label for="freq">Frequency (MHz):</label>
                <select id="freq" name="freq">
                    <option value="433.0" )rawliteral" + (currentFreq=="433.0"?"selected":"") + R"rawliteral(>433.0 MHz</option>
                    <option value="868.0" )rawliteral" + (currentFreq=="868.0"?"selected":"") + R"rawliteral(>868.0 MHz (Europe)</option>
                    <option value="915.0" )rawliteral" + (currentFreq=="915.0"?"selected":"") + R"rawliteral(>915.0 MHz (USA)</option>
                </select>
            </div>
            
            <button type="submit">Apply Settings</button>
        </form>
        
        <div style="margin-top: 20px; text-align: center;">
            <a href="/status" style="color: #2196F3; text-decoration: none;">View Device Status</a>
        </div>
    </div>
</body>
</html>
        )rawliteral";
        
        server.send(200, "text/html", html);
    });
    
    // Страница применения настроек
    server.on("/set", HTTP_GET, []() {
        if (server.hasArg("sf") && server.hasArg("bw") && server.hasArg("tx") && server.hasArg("freq")) {
            currentSF = server.arg("sf");
            currentBW = server.arg("bw");
            currentTX = server.arg("tx");
            currentFreq = server.arg("freq");
            
            // Формируем команду для отправки
            String command = "SSET SF" + currentSF + ",BW" + currentBW + ",TX" + currentTX + ",F" + currentFreq;
            
            // Отправляем через Serial1
            Serial1.println(command);
            
            Serial.print("Sent to Serial1: ");
            Serial.println(command);
            
            // Обновляем LoRa настройки локально
            updateLoRaSettings();
            
            // Устанавливаем флаг, что настройки применены
            settingsApplied = true;
            
            // Отображаем на дисплее
            if (u8g2) {
                displayScreen("Settings Updated", 
                             ("SF:" + currentSF).c_str(),
                             ("BW:" + currentBW).c_str(),
                             ("TX:" + currentTX).c_str());
            }
            
            // Редирект с сообщением об успехе
            String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta http-equiv="refresh" content="3;url=/">
    <title>Settings Applied</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; padding: 50px; }
        .success { color: #4CAF50; font-size: 24px; }
        .info { background: #e3f2fd; padding: 15px; border-radius: 5px; margin: 20px auto; max-width: 600px; }
    </style>
</head>
<body>
    <div class="success">✓ Settings Applied Successfully!</div>
    
    <div class="info">
        <p><strong>Command sent:</strong> )rawliteral" + command + R"rawliteral(</p>
        <p><strong>Next step:</strong> Device will apply settings and send SINIT when ready.</p>
    </div>
    
    <p>Redirecting in 3 seconds...</p>
</body>
</html>
            )rawliteral";
            
            server.send(200, "text/html", html);
        } else {
            server.send(400, "text/plain", "Missing parameters");
        }
    });
    
    // Страница статуса
    server.on("/status", HTTP_GET, []() {
        String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Device Status</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .container { max-width: 800px; margin: 0 auto; }
        .card { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.1); margin: 10px 0; }
        h2 { color: #333; }
        .status-item { margin: 10px 0; padding: 10px; background: #f5f5f5; border-radius: 5px; }
        .data-row { display: flex; justify-content: space-between; margin: 5px 0; }
        .data-label { font-weight: bold; }
        .data-value { font-family: monospace; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Device Status</h1>
        
        <div class="card">
            <h2>Connected Device Info</h2>
            <div class="data-row">
                <span class="data-label">Device ID:</span>
                <span class="data-value">)rawliteral" + deviceID + R"rawliteral(</span>
            </div>
            <div class="data-row">
                <span class="data-label">Latitude:</span>
                <span class="data-value">)rawliteral" + deviceLat + R"rawliteral(</span>
            </div>
            <div class="data-row">
                <span class="data-label">Longitude:</span>
                <span class="data-value">)rawliteral" + deviceLon + R"rawliteral(</span>
            </div>
            <div class="data-row">
                <span class="data-label">Last SINIT:</span>
                <span class="data-value">)rawliteral" + (deviceID.length() > 0 ? "Received" : "Waiting...") + R"rawliteral(</span>
            </div>
        </div>
        
        <div class="card">
            <h2>LoRa Settings</h2>
            <div class="data-row">
                <span class="data-label">Frequency:</span>
                <span class="data-value">)rawliteral" + currentFreq + R"rawliteral( MHz</span>
            </div>
            <div class="data-row">
                <span class="data-label">Spreading Factor:</span>
                <span class="data-value">SF)rawliteral" + currentSF + R"rawliteral(</span>
            </div>
            <div class="data-row">
                <span class="data-label">Bandwidth:</span>
                <span class="data-value">)rawliteral" + currentBW + R"rawliteral( kHz</span>
            </div>
            <div class="data-row">
                <span class="data-label">TX Power:</span>
                <span class="data-value">)rawliteral" + currentTX + R"rawliteral( dBm</span>
            </div>
        </div>
        
        <div class="card">
            <h2>UART Settings</h2>
            <div class="data-row">
                <span class="data-label">RX Pin:</span>
                <span class="data-value">)rawliteral" + String(RX_PIN) + R"rawliteral(</span>
            </div>
            <div class="data-row">
                <span class="data-label">TX Pin:</span>
                <span class="data-value">)rawliteral" + String(TX_PIN) + R"rawliteral(</span>
            </div>
            <div class="data-row">
                <span class="data-label">Baud Rate:</span>
                <span class="data-value">)rawliteral" + String(UART_BAUD) + R"rawliteral(</span>
            </div>
        </div>
        
        <div class="card">
            <h2>WiFi Status</h2>
            <div class="data-row">
                <span class="data-label">AP SSID:</span>
                <span class="data-value">)rawliteral" + String(AP_SSID) + R"rawliteral(</span>
            </div>
            <div class="data-row">
                <span class="data-label">Connected Clients:</span>
                <span class="data-value">)rawliteral" + String(WiFi.softAPgetStationNum()) + R"rawliteral(</span>
            </div>
            <div class="data-row">
                <span class="data-label">IP Address:</span>
                <span class="data-value">)rawliteral" + WiFi.softAPIP().toString() + R"rawliteral(</span>
            </div>
        </div>
        
        <div style="text-align: center; margin-top: 20px;">
            <a href="/" style="color: #2196F3; text-decoration: none;">← Back to Settings</a>
        </div>
    </div>
</body>
</html>
        )rawliteral";
        
        server.send(200, "text/html", html);
    });
    
    // Запуск сервера
    server.begin();
    Serial.println("HTTP server started");
}

void readUART() {
    while (Serial1.available() > 0) {
        char c = Serial1.read();
        
        if (c == '\n' || c == '\r') {
            if (bufferIndex > 0) {
                uartBuffer[bufferIndex] = '\0';
                
                Serial.print("[UART] Received: ");
                Serial.println(uartBuffer);
                
                // Проверяем, что пришло
                if (strncmp(uartBuffer, "SINIT", 5) == 0) {
                    // Получена команда SINIT - парсим параметры
                    parseSINITCommand(uartBuffer);
                    
                    // Немедленно отправляем SOK
                    Serial1.println(RESPONSE_MESSAGE);
                    Serial.print("[UART] Sent: ");
                    Serial.println(RESPONSE_MESSAGE);
                    
                    if (u8g2) {
                        displayScreen("SINIT Received", "SOK sent", deviceID.c_str(), "");
                    }
                } else if (strncmp(uartBuffer, "SSET", 4) == 0) {
                    // Получены настройки SSET - просто логируем
                    if (u8g2) {
                        String shortMsg = String(uartBuffer).substring(0, 20);
                        displayScreen("SSET Received", shortMsg.c_str(), "No response needed", "");
                    }
                } else if (strcmp(uartBuffer, "SSOK") == 0) {
                    // Подтверждение применения настроек
                    if (u8g2) {
                        displayScreen("Settings Confirmed", "SSOK received", "Settings applied", "on remote device");
                    }
                    settingsApplied = true;
                } else if (strncmp(uartBuffer, "STATUS", 6) == 0) {
                    // Запрос статуса - можно отправить обратно текущие настройки
                    String status = "STATUS SF" + currentSF + ",BW" + currentBW + ",TX" + currentTX;
                    Serial1.println(status);
                    Serial.print("[UART] Status sent: ");
                    Serial.println(status);
                } else {
                    // Другое сообщение
                    if (u8g2) {
                        String shortMsg = String(uartBuffer).substring(0, 20);
                        displayScreen("UART Message", shortMsg.c_str(), "", "");
                    }
                }
                
                bufferIndex = 0;
            }
        } else if (bufferIndex < sizeof(uartBuffer) - 1) {
            uartBuffer[bufferIndex++] = c;
        }
    }
}

void parseSINITCommand(const char* command) {
    // Пример: SINIT ID7523350,LAT55.123456,LON37.123456,SF9,BW250.0,TX17
    String cmdStr = String(command);
    
    // Извлекаем параметры
    int startPos = 6; // Пропускаем "SINIT "
    
    // Ищем ID
    int idStart = cmdStr.indexOf("ID", startPos);
    int idEnd = cmdStr.indexOf(",", idStart);
    if (idStart != -1 && idEnd != -1) {
        deviceID = cmdStr.substring(idStart + 2, idEnd);
    }
    
    // Ищем LAT
    int latStart = cmdStr.indexOf("LAT", startPos);
    int latEnd = cmdStr.indexOf(",", latStart);
    if (latStart != -1 && latEnd != -1) {
        deviceLat = cmdStr.substring(latStart + 3, latEnd);
    }
    
    // Ищем LON
    int lonStart = cmdStr.indexOf("LON", startPos);
    int lonEnd = cmdStr.indexOf(",", lonStart);
    if (lonStart != -1 && lonEnd != -1) {
        deviceLon = cmdStr.substring(lonStart + 3, lonEnd);
    }
    
    // Ищем SF (Spreading Factor)
    int sfStart = cmdStr.indexOf("SF", startPos);
    int sfEnd = cmdStr.indexOf(",", sfStart);
    if (sfStart != -1 && sfEnd != -1) {
        String newSF = cmdStr.substring(sfStart + 2, sfEnd);
        if (newSF.toInt() >= 7 && newSF.toInt() <= 12) {
            currentSF = newSF;
        }
    }
    
    // Ищем BW (Bandwidth)
    int bwStart = cmdStr.indexOf("BW", startPos);
    int bwEnd = cmdStr.indexOf(",", bwStart);
    if (bwStart != -1 && bwEnd != -1) {
        String newBW = cmdStr.substring(bwStart + 2, bwEnd);
        currentBW = newBW;
    }
    
    // Ищем TX (Transmit Power)
    int txStart = cmdStr.indexOf("TX", startPos);
    if (txStart != -1) {
        String newTX = cmdStr.substring(txStart + 2);
        if (newTX.toInt() >= 2 && newTX.toInt() <= 20) {
            currentTX = newTX;
        }
    }
    
    // Обновляем настройки LoRa на основе полученных параметров
    updateLoRaSettings();
    
    Serial.println("Parsed SINIT parameters:");
    Serial.print("  Transmission ID: "); Serial.println(deviceID);
    Serial.print("  Latitude: "); Serial.println(deviceLat);
    Serial.print("  Longitude: "); Serial.println(deviceLon);
    Serial.print("  SF: "); Serial.println(currentSF);
    Serial.print("  BW: "); Serial.println(currentBW);
    Serial.print("  TX: "); Serial.println(currentTX);
}

void readLoRa() {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        int i = 0;
        while (LoRa.available() && i < sizeof(loraBuffer) - 1) {
            loraBuffer[i++] = (char)LoRa.read();
        }
        loraBuffer[i] = '\0';
        
        int rssi = LoRa.packetRssi();
        float snr = LoRa.packetSnr();
        
        Serial.print("[LoRa] ");
        Serial.print(loraBuffer);
        Serial.print(" RSSI:");
        Serial.print(rssi);
        Serial.print(" SNR:");
        Serial.println(snr);
        
        if (u8g2) {
            String shortMsg = String(loraBuffer).substring(0, 20);
            snprintf(displayBuffer, sizeof(displayBuffer), "RSSI:%d SNR:%.1f", rssi, snr);
            displayScreen("LoRa Message", shortMsg.c_str(), displayBuffer, "");
        }
        
        LoRa.receive();
    }
}

void initLoRa() {
    #ifdef RADIO_TCXO_ENABLE
    pinMode(RADIO_TCXO_ENABLE, OUTPUT);
    digitalWrite(RADIO_TCXO_ENABLE, HIGH);
    delay(100);
    #endif

    #ifdef RADIO_CTRL
    digitalWrite(RADIO_CTRL, HIGH);
    #endif

    LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);
    
    if (!LoRa.begin(CONFIG_RADIO_FREQ * 1000000)) {
        Serial.println("LoRa init failed");
        if (u8g2) {
            displayScreen("LoRa: FAILED", "UART only mode", "", "");
        }
        return;
    }
    
    // Применяем текущие настройки
    updateLoRaSettings();
    
    LoRa.receive();
    Serial.println("LoRa ready");
}

void updateLoRaSettings() {
    // Устанавливаем частоту
    float freq = currentFreq.toFloat();
    LoRa.setFrequency(freq * 1000000);
    
    // Устанавливаем TX power
    int txPower = currentTX.toInt();
    LoRa.setTxPower(txPower);
    
    // Устанавливаем bandwidth
    float bw = currentBW.toFloat();
    LoRa.setSignalBandwidth(bw * 1000);
    
    // Устанавливаем spreading factor
    int sf = currentSF.toInt();
    LoRa.setSpreadingFactor(sf);
    
    Serial.print("LoRa settings updated: SF");
    Serial.print(sf);
    Serial.print(", BW");
    Serial.print(bw);
    Serial.print(" kHz, TX");
    Serial.print(txPower);
    Serial.print(" dBm, F");
    Serial.print(freq);
    Serial.println(" MHz");
}

void displayScreen(const char* line1, const char* line2, const char* line3, const char* line4) {
    if (!u8g2) return;
    
    u8g2->clearBuffer();
    u8g2->drawStr(0, 12, line1);
    u8g2->drawStr(0, 26, line2);
    u8g2->drawStr(0, 40, line3);
    u8g2->drawStr(0, 54, line4);
    u8g2->sendBuffer();
}