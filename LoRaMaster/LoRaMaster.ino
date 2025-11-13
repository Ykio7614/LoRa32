#include <WiFi.h>
#include <LoRa.h>
#include "LoRaBoards.h" 
#include <Arduino.h>

const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASS";

const char* SERVER_IP = "192.168.1.100"; 
const uint16_t SERVER_PORT = 12345;     

WiFiClient tcpClient;

#define UART_TX_PIN 17
#define UART_RX_PIN 16
HardwareSerial SerialU = Serial1; // используем Serial1

#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ 868.0
#endif

// По умолчанию какие-нибудь параметры (будут перезаписаны при INIT)
int currentSF = 12;
float currentBW_khz = 125.0;
int currentPWR = 14;

// Вспомогательные флаги
bool tcpConnected = false;

// Вспомогательные функции
void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect failed.");
  }
}

void ensureTCP() {
  if (tcpClient && tcpClient.connected()) return;
  if (WiFi.status() != WL_CONNECTED) {
    ensureWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
  }
  Serial.printf("Connecting to server %s:%u ...\n", SERVER_IP, SERVER_PORT);
  if (tcpClient.connect(SERVER_IP, SERVER_PORT)) {
    Serial.println("TCP connected.");
    tcpConnected = true;
  } else {
    Serial.println("TCP connect failed.");
    tcpConnected = false;
  }
}

// Применение параметров LoRa для приёма
void applyLoRaParams(int sf, float bw_khz, int pwr) {
  currentSF = sf;
  currentBW_khz = bw_khz;
  currentPWR = pwr;

  Serial.printf("Applying LoRa params: SF=%d BW=%.1f kHz PWR=%d dBm\n",
                currentSF, currentBW_khz, currentPWR);

  LoRa.setSpreadingFactor(currentSF);
  LoRa.setSignalBandwidth((long)(currentBW_khz * 1000.0)); // Hz
  LoRa.setTxPower(currentPWR); // tx power (хотя мы принимаем, это не критично)
}

// Отправка строки на сервер (по TCP), с автоматическим переподключением
void sendToServer(const String &s) {
  ensureTCP();
  if (tcpClient && tcpClient.connected()) {
    tcpClient.println(s);
    tcpClient.flush();
    Serial.println("Sent to server: " + s);
  } else {
    Serial.println("Cannot send to server — not connected.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  // UART для GSM/внешнего ESP32 (по которому придут INIT и через который шлём PARAMS)
  SerialU.begin(9600, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  Serial.println("Master starting...");

  // LoRa init
  LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);
  if (!LoRa.begin(CONFIG_RADIO_FREQ * 1000000)) {
    Serial.println("ERROR: LoRa module not found!");
    while (true) delay(1000);
  }
  LoRa.setPreambleLength(16);
  LoRa.setSyncWord(0xAB);
  LoRa.enableCrc();
  LoRa.disableInvertIQ();
  LoRa.setCodingRate4(7);

  applyLoRaParams(currentSF, currentBW_khz, currentPWR);

  // WiFi/TCP (подключим по требованию в loop)
  ensureWiFi();
  ensureTCP();
}

String tcpBuffer = "";
String uartBuffer = "";

void loop() {
  // ---- 1) Чтение от сервера по TCP: ожидаем строки PARAMS ...\n
  ensureTCP();
  if (tcpClient && tcpClient.connected() && tcpClient.available()) {
    String line = tcpClient.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      Serial.println("TCP RX: " + line);
      // Ожидаем формат: PARAMS SF:<int> BW:<float> PWR:<int>
      int sf, pwr;
      float bw;
      if (sscanf(line.c_str(), "PARAMS SF:%d BW:%f PWR:%d", &sf, &bw, &pwr) == 3) {
        // Пересылаем по UART на ESP32+GSM (который далее перешлёт на slaver)
        String out = String("PARAMS SF:") + String(sf) +
                     " BW:" + String(bw, 1) +
                     " PWR:" + String(pwr);
        SerialU.println(out);
        Serial.println("Forwarded PARAMS to Serial1: " + out);
        // В локальной конфигурации можно не применять — мастер будет применять параметры
        // после получения INIT от slaver (по заданию).
      } else {
        Serial.println("Unknown message from server (ignored).");
      }
    }
  }

  // 2) Прослушивание Serial1 (UART) на предмет INIT, и ответ OK
  if (SerialU.available()) {
    String s = SerialU.readStringUntil('\n'); // читаем строку
    s.trim();
    if (s.length() > 0) {
      Serial.println("UART RX: " + s);
      // Если это INIT строка
      // Формат ожидаемый: INIT ID:<hex> Lat:<lat> Lng:<lng> SF:<int> BW:<float> PWR:<int>
      unsigned long id_hex = 0;
      double lat = 0.0, lng = 0.0;
      int sf = 0, pwr = 0;
      float bw = 0.0;

      // Для безопасного разбора используем sscanf — %x читает hex (без 0x)
      int matched = sscanf(s.c_str(), "INIT ID:%lx Lat:%lf Lng:%lf SF:%d BW:%f PWR:%d",
                           &id_hex, &lat, &lng, &sf, &bw, &pwr);
      if (matched == 6) {
        Serial.println("Parsed INIT OK.");
        // Отправляем OK по UART
        SerialU.println("OK");
        Serial.println("Sent OK to Serial1.");

        // Применяем параметры LoRa для приёма
        applyLoRaParams(sf, bw, pwr);

        // -Слушаем до 10 пакетов по LoRa
        int expected = 10;
        int received = 0;
        long sumRssi = 0;
        double sumSnr = 0.0;
        unsigned long firstPacketMillis = 0;
        const unsigned long timeoutAfterFirstMs = 15000; // 15s 

        Serial.println("Waiting for LoRa packets...");
        unsigned long overallStart = millis();
        while (received < expected) {
          int packetSize = LoRa.parsePacket();
          if (packetSize) {
            // получили пакет
            String payload = "";
            while (LoRa.available()) {
              payload += (char)LoRa.read();
            }
            long rssi = LoRa.packetRssi();
            double snr = LoRa.packetSnr(); 

            received++;
            sumRssi += rssi;
            sumSnr += snr;
            Serial.printf("LoRa pkt %d: len=%d rssi=%ld snr=%.2f payload=\"%s\"\n",
                          received, packetSize, rssi, snr, payload.c_str());

            if (firstPacketMillis == 0) firstPacketMillis = millis();
          }

          // Если уже пришёл хотя бы 1 пакет — проверяем таймаут после первого
          if (firstPacketMillis != 0 && (millis() - firstPacketMillis) > timeoutAfterFirstMs) {
            Serial.println("Timeout after first packet reached.");
            break;
          }

          // Также ограничим максимальное ожидание (вдруг не пришёл ни 1 пакета)
          if (firstPacketMillis == 0 && (millis() - overallStart) > 20000) { // 20s без пакетов
            Serial.println("Global timeout (no packets) reached.");
            break;
          }

          delay(10);
        } 

        int missing = expected - received;
        if (missing < 0) missing = 0;

        double avgRssi = (received > 0) ? ((double)sumRssi / received) : 0.0;
        double avgSnr = (received > 0) ? (sumSnr / received) : 0.0;

        // Формируем результат и шлём на сервер
        // RESULT ID:<id> Lat:<lat> Lng:<lng> SNR:<avg_snr> RSSI:<avg_rssi> MISSING:<missing>
        char resultBuf[256];
        snprintf(resultBuf, sizeof(resultBuf),
                 "RESULT ID:%lX Lat:%.6f Lng:%.6f SNR:%.2f RSSI:%.2f MISSING:%d",
                 id_hex, lat, lng, avgSnr, avgRssi, missing);

        String resultStr = String(resultBuf);
        sendToServer(resultStr);

      } else {
        // Не INIT — можно игнорировать или ответить ошибкой
        Serial.println("Received non-INIT on UART (ignored).");
      }
    }
  }

  // Небольшая пауза
  delay(20);
}
