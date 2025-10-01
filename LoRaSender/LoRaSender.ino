#include <LoRa.h>               // #include <LoRa.h>
#include "LoRaBoards.h"         // #include "LoRaBoards.h"
#include <TinyGPSPlus.h>


TinyGPSPlus gps;
#define gpsSerial Serial2   

#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           868.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   17
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             125.0
#endif

#if !defined(USING_SX1276) && !defined(USING_SX1278)
#error "LoRa example is only allowed to run SX1276/78."
#endif

int spreadingFactor = 12;
int txPower = CONFIG_RADIO_OUTPUT_POWER;
float signalBandwidth = CONFIG_RADIO_BW;

int counter = 0;
unsigned long lastSend = 0;
const unsigned long sendInterval = 10000; // 10 секунд

uint32_t currentID = 0;
uint16_t seqNum = 0;
uint16_t packetsPerID = 10; // сколько пакетов отправлять под одним ID
uint16_t packetsSent = 0;

double lastLat = 0.0;
double lastLng = 0.0;
bool hasFix = false;

uint32_t fnv1a_hash(const char* str) {
    uint32_t hash = 2166136261UL;
    while (*str) {
        hash ^= (uint8_t)(*str++);
        hash *= 16777619UL;
    }
    return hash;
}


void applyLoRaSettings() {
  LoRa.setSpreadingFactor(spreadingFactor);
  LoRa.setTxPower(txPower);
  LoRa.setSignalBandwidth(signalBandwidth * 1000);
}


void setup() {
  Serial.begin(115200);

  
  Serial.println("Waiting for GPS fix...");

  Serial.println("=== LoRa Initialization ===");
  setupBoards(true);
  gpsSerial.begin(9600, SERIAL_8N1, 22, 23);

#ifdef RADIO_TCXO_ENABLE
  Serial.print("TCXO enable pin: ");
  Serial.println(RADIO_TCXO_ENABLE);
  pinMode(RADIO_TCXO_ENABLE, OUTPUT);
  digitalWrite(RADIO_TCXO_ENABLE, HIGH);
  delay(5); 
#endif

  Serial.print("LoRa Pins: CS=");
  Serial.print(RADIO_CS_PIN);
  Serial.print(", RST=");
  Serial.print(RADIO_RST_PIN);
  Serial.print(", DIO0=");
  Serial.println(RADIO_DIO0_PIN);

  LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);

  Serial.print("Starting LoRa at ");
  Serial.print(CONFIG_RADIO_FREQ);
  Serial.println(" MHz ...");

  if (!LoRa.begin(CONFIG_RADIO_FREQ * 1000000)) {
    Serial.println("ERROR: LoRa module not found or not powered!");
    Serial.println("Check CS/RST/DIO0 pins and 3.3V supply.");
  } else {
    Serial.println("LoRa module started successfully!");

    applyLoRaSettings();
    LoRa.setPreambleLength(16);
    LoRa.setSyncWord(0xAB);
    LoRa.enableCrc();
    LoRa.disableInvertIQ();
    LoRa.setCodingRate4(7);

    Serial.println("LoRa settings applied.");
  }
}

void loop() {
    while (gpsSerial.available() > 0) {
        if (gps.encode(gpsSerial.read())) {
            if (gps.location.isValid()) {
                lastLat = gps.location.lat();
                lastLng = gps.location.lng();
                hasFix = true;
                Serial.print("GPS Fix: ");
                Serial.print(lastLat, 6);
                Serial.print(", ");
                Serial.println(lastLng, 6);
            }
        }
    }

    if (millis() > 5000 && gps.charsProcessed() < 10) {
        Serial.println(F("Warning: No GPS data detected yet."));
    }

    if (packetsSent == 0 && hasFix) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%lu-%.6f-%.6f", millis(), lastLat, lastLng);
        currentID = fnv1a_hash(buf);
        seqNum = 0;
        packetsSent = 0;
        Serial.print("New Packet ID: ");
        Serial.println(currentID, HEX);
    }

    unsigned long now = millis();
    if (now - lastSend >= sendInterval && hasFix) {
        lastSend = now;

        if (!LoRa.beginPacket()) {
            Serial.println("LoRa not ready, skipping packet");
        } else {
            LoRa.print("ID:");
            LoRa.print(currentID, HEX);
            LoRa.print(" SEQ:");
            LoRa.print(seqNum);
            LoRa.print(" Lat:");
            LoRa.print(lastLat, 6);
            LoRa.print(" Lng:");
            LoRa.print(lastLng, 6);
            LoRa.endPacket();

            Serial.print("Packet sent: ID=");
            Serial.print(currentID, HEX);
            Serial.print(" SEQ=");
            Serial.println(seqNum);

            seqNum++;
            packetsSent++;

            if (packetsSent >= packetsPerID) {
                packetsSent = 0; 
            }
        }
    }

    if (u8g2) {
        u8g2->clearBuffer();
        if (hasFix) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Lat: %.6f", lastLat);
            u8g2->drawStr(0, 12, buf);
            snprintf(buf, sizeof(buf), "Lng: %.6f", lastLng);
            u8g2->drawStr(0, 26, buf);
            snprintf(buf, sizeof(buf), "ID: %08X SEQ: %d", currentID, seqNum-1);
            u8g2->drawStr(0, 40, buf);
        } else {
            u8g2->drawStr(0, 12, "No GPS Fix");
        }
        u8g2->sendBuffer();
    }

    delay(100);
}






void displayLocationInfo() {
  Serial.println(F("-------------------------------------"));
  Serial.println("\n Location Info:");

  if (gps.location.isValid()) {
    Serial.print("Latitude:  ");
    Serial.print(gps.location.lat(), 6);
    Serial.print(" ");
    Serial.println(gps.location.rawLat().negative ? "S" : "N");

    Serial.print("Longitude: ");
    Serial.print(gps.location.lng(), 6);
    Serial.print(" ");
    Serial.println(gps.location.rawLng().negative ? "W" : "E");

    Serial.print("Fix Quality: ");
    Serial.println(gps.location.isValid() ? "Valid" : "Invalid");

    Serial.print("Satellites: ");
    Serial.println(gps.satellites.value());

    Serial.print("Altitude:   ");
    Serial.print(gps.altitude.meters());
    Serial.println(" m");

    Serial.print("Speed:      ");
    Serial.print(gps.speed.kmph());
    Serial.println(" km/h");

    Serial.print("Course:     ");
    Serial.print(gps.course.deg());
    Serial.println("°");

    Serial.print("Date:       ");
    if (gps.date.isValid()) {
      Serial.printf("%02d/%02d/%04d\n", gps.date.day(), gps.date.month(), gps.date.year());
    } else {
      Serial.println("Invalid");
    }

    Serial.print("Time (UTC): ");
    if (gps.time.isValid()) {
      Serial.printf("%02d:%02d:%02d\n", gps.time.hour(), gps.time.minute(), gps.time.second());
    } else {
      Serial.println("Invalid");
    }
  }
  
  Serial.println(F("-------------------------------------"));
}
