// Only supports SX1276/SX1278
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <LoRa.h>
#include "LoRaBoards.h"


const char* ssid = "LilBoPeevo";  
const char* password = "123123124";


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

void setup()
{
    setupBoards();
    WiFi.begin(ssid, password);
    Serial.begin(115200);
    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nConnected to Wi-Fi!");
    Serial.print("ESP32 Local IP: ");
    Serial.println(WiFi.localIP());
    

    server.on("/", HTTP_GET, []() {
        String page = "<html>\
        <body>\
        <h2>LoRa Web Interface</h2>\
        <form action='/send' method='POST'>\
        <input type='text' name='message' maxlength='20'>\
        <input type='submit' value='Send'>\
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
            Serial.println("LoRa settings updated:");
            Serial.print("Spreading Factor: "); Serial.println(spreadingFactor);
            Serial.print("Tx Power: "); Serial.println(txPower);
            Serial.print("Signal Bandwidth: "); Serial.println(signalBandwidth);
        }
        server.sendHeader("Location", "/");
        server.send(303);
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

    LoRa.enableCrc();

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
        Serial.print(LoRa.packetRssi());
        Serial.print(" ");
        Serial.print(LoRa.packetSnr());
        Serial.print("\n");
        if (u8g2) {
            u8g2->clearBuffer();
            char buf[256];
            u8g2->drawStr(0, 12, "Received OK!");
            u8g2->drawStr(0, 26, recv.c_str());
            snprintf(buf, sizeof(buf), "RSSI:%i", LoRa.packetRssi());
            u8g2->drawStr(0, 40, buf);
            snprintf(buf, sizeof(buf), "SNR:%.1f", LoRa.packetSnr());
            u8g2->drawStr(0, 56, buf);
            u8g2->sendBuffer();
        }
    }
}
