#include <LoRa.h>
#include "LoRaBoards.h"
#include <TinyGPSPlus.h>

// UART для GSMESP
#define TX_PIN 17
#define RX_PIN 16

// Кнопка
#define BUTTON_PIN 21

TinyGPSPlus gps;
#define gpsSerial Serial2

#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ 868.0
#endif

#if !defined(USING_SX1276) && !defined(USING_SX1278)
#error "LoRa example is only allowed to run SX1276/78."
#endif

int spreadingFactor = 12;     // spreading factor
float signalBandwidth = 125.0; // bandwidth (kHz)
int txPower = 14;             // tx power (dBm)

// GPS и ID
double lastLat = 0.0;
double lastLng = 0.0;
bool hasFix = false;
uint32_t currentID = 0;

// ---------------------- ХЭШ-ФУНКЦИЯ ----------------------
uint32_t fnv1a_hash(const char* str) {
    uint32_t hash = 2166136261UL;
    while (*str) {
        hash ^= (uint8_t)(*str++);
        hash *= 16777619UL;
    }
    return hash;
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN); // UART для GSMESP
    gpsSerial.begin(9600, SERIAL_8N1, 22, 23);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    Serial.println("LoRa Slave starting...");
    setupBoards();

#ifdef RADIO_TCXO_ENABLE
    pinMode(RADIO_TCXO_ENABLE, OUTPUT);
    digitalWrite(RADIO_TCXO_ENABLE, HIGH);
    delay(5);
#endif

    LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);

    if (!LoRa.begin(CONFIG_RADIO_FREQ * 1000000)) {
        Serial.println("ERROR: LoRa module not found!");
        while (true);
    } else {
        Serial.println("LoRa started successfully.");
        LoRa.setPreambleLength(16);
        LoRa.setSyncWord(0xAB);
        LoRa.enableCrc();
        LoRa.disableInvertIQ();
        LoRa.setCodingRate4(7);
        LoRa.setSpreadingFactor(spreadingFactor);
        LoRa.setSignalBandwidth(signalBandwidth * 1000);
        LoRa.setTxPower(txPower);
    }
}

void loop() {
    // GPS обработка
    while (gpsSerial.available() > 0) {
        if (gps.encode(gpsSerial.read())) {
            if (gps.location.isValid()) {
                lastLat = gps.location.lat();
                lastLng = gps.location.lng();
                hasFix = true;
            }
        }
    }

    //Генерация ID 
    if (hasFix && currentID == 0) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%lu-%.6f-%.6f", millis(), lastLat, lastLng);
        currentID = fnv1a_hash(buf);
        Serial.print("Generated ID: ");
        Serial.println(String(currentID, HEX));
    }

    // Приём новых параметров по UART
    if (Serial1.available()) {
        String cmd = Serial1.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0) {
            Serial.println("UART RX: " + cmd);

            int sf, pwr;
            float bw;
            if (sscanf(cmd.c_str(), "PARAMS SF:%d BW:%f PWR:%d", &sf, &bw, &pwr) == 3) {
                spreadingFactor = sf;
                signalBandwidth = bw;
                txPower = pwr;

                // применяем новые настройки
                LoRa.setSpreadingFactor(spreadingFactor);
                LoRa.setSignalBandwidth(signalBandwidth * 1000);
                LoRa.setTxPower(txPower);

                Serial.printf("[UART] Applied params: SF=%d BW=%.1f kHz PWR=%d dBm\n",
                              spreadingFactor, signalBandwidth, txPower);

                Serial1.println("OK");
                Serial.println("Sent OK via UART");
            }
        }
    }

    //Проверка кнопки 
    static bool buttonPressed = false;

    if (digitalRead(BUTTON_PIN) == LOW && !buttonPressed) {
        buttonPressed = true;
        Serial.println("Button pressed.");

        if (hasFix && currentID != 0) {
            // Формируем сообщение
            String msg = String("INIT ID:") + String(currentID, HEX) +
                         " Lat:" + String(lastLat, 6) +
                         " Lng:" + String(lastLng, 6) +
                         " SF:" + String(spreadingFactor) +
                         " BW:" + String(signalBandwidth, 1) +
                         " PWR:" + String(txPower);

            // Отправляем в Serial1
            Serial1.println(msg);
            Serial.println("Sent to Serial1: " + msg);

            // Ожидаем ответ "OK"
            unsigned long startTime = millis();
            bool okReceived = false;
            while (millis() - startTime < 10000) { // ждем 10 секунды
                if (Serial1.available()) {
                    String response = Serial1.readStringUntil('\n');
                    response.trim();
                    if (response.equalsIgnoreCase("OK")) {
                        okReceived = true;
                        Serial.println("Received OK from Serial1.");
                        break;
                    }
                }
            }

            // Если получен OK, отправляем 10 пакетов по LoRa
            if (okReceived) {
                Serial.println("Sending 10 LoRa packets...");

                // Формируем сообщение, одинаковое для всех пакетов
                String packetMsg = String("ID:") + String(currentID, HEX) + ",ADCDEF";

                for (int i = 0; i < 10; i++) {
                    LoRa.beginPacket();
                    LoRa.print(packetMsg);
                    LoRa.endPacket();
                    Serial.printf("LoRa packet %d sent: %s\n", i + 1, packetMsg.c_str());
                    delay(500);
                }

                Serial.println("All 10 packets sent.");
            }

                Serial.println("All 10 packets sent.");
            } else {
                Serial.println("No OK received. LoRa not transmitted.");
            }
        } else {
            Serial.println("No GPS fix or ID not generated yet!");
        }
    }

    // сброс состояния кнопки при отпускании
    if (digitalRead(BUTTON_PIN) == HIGH) {
        buttonPressed = false;
    }

    delay(50);
}
