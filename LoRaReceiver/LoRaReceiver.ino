// Only supports SX1276/SX1278
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <LoRa.h>
#include "LoRaBoards.h"


const char* ssid = "A54";  
const char* password = "66666666";
// const IPAddress local_IP(192, 168, 203, 101);   // Желаемый статический IP ESP32
// const IPAddress gateway(192, 168, 203, 169);    // Gateway из вашего вывода 192.168.203.169
// const IPAddress subnet(255, 255, 255, 0);      // Маска подсети

char boardIP[16];

bool showInfo = false;

String webMessage = "Waiting for update...";

WebServer server(80);

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
#error "LoRa example is only allowed to run SX1276/78. For other RF models, please run examples/RadioLibExamples
#endif

int spreadingFactor = 12;
int txPower = CONFIG_RADIO_OUTPUT_POWER;
float signalBandwidth = CONFIG_RADIO_BW;


void applyLoRaSettings() {
    LoRa.setSpreadingFactor(spreadingFactor);
    LoRa.setTxPower(txPower);
    LoRa.setSignalBandwidth(signalBandwidth * 1000);
}

int countBitErrors(String received, String reference) {
    int errors = 0;
    int len = min(received.length(), reference.length());

    for (int i = 0; i < len; i++) {
        char recvChar = received[i];
        char refChar = reference[i];

        // Подсчет битовых ошибок через XOR
        for (int bit = 0; bit < 8; bit++) {
            if (((recvChar >> bit) & 1) != ((refChar >> bit) & 1)) {
                errors++;
            }
        }
    }
    return errors;
}



void setup()
{
    setupBoards();

    Serial.begin(115200);
    Serial.print("Connecting to WiFi...");

    // if (!WiFi.config(local_IP, gateway, subnet)) {
    //     Serial.println("Failed to configure static IP");
    // }

    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nConnected to Wi-Fi!");
    Serial.print("ESP32 Local IP: ");
    Serial.println(WiFi.localIP());

    strcpy(boardIP, WiFi.localIP().toString().c_str());

    server.on("/", HTTP_GET, []() {
        String page = "<html>\
        <body>\
        <h2>LoRa Web Interface LoRaReceiver</h2>\
        </form>\
        <h3>LoRa Settings</h3>\
        <form action='/update' method='POST'>\
        Spreading Factor (7-12): <input type='number' name='sf' min='7' max='12' value='" + String(spreadingFactor) + "'><br>\
        Tx Power (2-20 dBm): <input type='number' name='tx' min='2' max='20' value='" + String(txPower) + "'><br>\
        Signal Bandwidth (7.8-500 kHz): <input type='number' name='bw' step='0.1' min='7.8' max='500' value='" + String(signalBandwidth) + "'><br>\
        <input type='submit' value='Update'>\
        </form>\
        </body>\
        </html>";
        server.send(200, "text/html", page);
    });



    server.on("/update", HTTP_POST, []() {
        if (server.hasArg("sf") && server.hasArg("tx") && server.hasArg("bw")) {
            int newSF = server.arg("sf").toInt();
            int newTx = server.arg("tx").toInt();
            float newBW = server.arg("bw").toFloat();

            if (newSF >= 7 && newSF <= 12) spreadingFactor = newSF;
            if (newTx >= 2 && newTx <= 20) txPower = newTx;
            if (newBW >= 7.8 && newBW <= 500) signalBandwidth = newBW;

            applyLoRaSettings();
            Serial.print("SettingsUpdated{ ");
            Serial.print("SF: "); Serial.print(spreadingFactor);
            Serial.print(" TX: "); Serial.print(txPower);
            Serial.print(" BW: "); Serial.print(signalBandwidth);
            Serial.print(" }");
            Serial.print("\n");
        }
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
        server.sendHeader("Location", "/");
        server.send(200, "text/plain", "OK");
    });

    server.begin();
    Serial.println("HTTP Server Started");

    
    // When the power is turned on, a delay is required.
    delay(1500);

    Serial.println("LoRa Receiver");

#ifdef  RADIO_TCXO_ENABLE
    pinMode(RADIO_TCXO_ENABLE, OUTPUT);
    digitalWrite(RADIO_TCXO_ENABLE, HIGH);
#endif

    LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);
    if (!LoRa.begin(CONFIG_RADIO_FREQ * 1000000)) {
        Serial.println("Starting LoRa failed!");
        while (1);
    }

    applyLoRaSettings();

    LoRa.setPreambleLength(16);

    LoRa.setSyncWord(0xAB);

    LoRa.disableCrc();

    LoRa.disableInvertIQ();

    LoRa.setCodingRate4(7);

    LoRa.receive();

}

void loop()
{
    server.handleClient();
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        String recv = "";
        while (LoRa.available()) {
            recv += (char)LoRa.read();
        }

        String reference = "Lorem ipsum dolor sit amet";
        int bitErrors = countBitErrors(recv.substring(0, 26), reference);

        Serial.print("PacketInfo{ ");
        Serial.print("Rssi: ");
        Serial.print(LoRa.packetRssi());
        Serial.print(" Snr: ");
        Serial.print(LoRa.packetSnr());
        Serial.print(" Bit errors: ");
        Serial.print(bitErrors);
        Serial.print(" }");
        Serial.print("\n");

        if (u8g2) {
            u8g2->clearBuffer();
            char buf[256];
            u8g2->drawStr(0, 12, boardIP);
            u8g2->drawStr(0, 26, recv.c_str());
            snprintf(buf, sizeof(buf), "RSSI:%i", LoRa.packetRssi());
            u8g2->drawStr(0, 40, buf);
            snprintf(buf, sizeof(buf), "SNR:%.1f", LoRa.packetSnr());
            u8g2->drawStr(0, 56, buf);
            u8g2->sendBuffer();
            showInfo = true;
        }
    }
    if(showInfo == false){
        if (u8g2) {
            u8g2->clearBuffer();
            u8g2->drawStr(0, 40, boardIP);
            u8g2->drawStr(0, 56, "Receiver");
            u8g2->sendBuffer();

        }
    }
}
