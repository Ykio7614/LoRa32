// Only supports SX1276/SX1278
// 仅支持 SX1276/SX1278 无线电模块,SX1280,SX1262等其他无线电模块请使用RadioLibExamples目录的示例
#include <LoRa.h>
#include "LoRaBoards.h"
#include <Arduino.h>

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
#error "LoRa example is only allowed to run SX1276/78. For other RF models, please run examples/RadioLibExamples"
#endif

// Константы
#define BUTTON_PIN 25
#define TX_PIN 12
#define RX_PIN 13
#define RESPONSE_TIMEOUT 5000       // Таймаут ожидания ответа в мс
#define MESSAGE_DELAY 2000          // Задержка между отправками в мс
#define DEBOUNCE_DELAY 50           // Задержка для антидребезга
#define LORA_PACKET_DELAY 1000      // Задержка между пакетами LoRa в мс
#define LORA_PACKET_COUNT 10        // Количество отправляемых пакетов LoRa
#define TIMEOUT_RESET_DELAY 3000    // Задержка перед сбросом после таймаута в мс

// Настройки LoRa (используем переменные, чтобы можно было менять)
int currentSF = 10;                 // Spreading Factor
float currentBW = CONFIG_RADIO_BW;  // в kHz
int currentTX = CONFIG_RADIO_OUTPUT_POWER; // Мощность передатчика

// Состояния системы
enum SystemState {
  STATE_READY,
  STATE_WAITING_FOR_SOK,
  STATE_SENDING_LORA_PACKETS,
  STATE_TIMEOUT,
  STATE_TIMEOUT_WAITING_RESET
};

// Переменные состояния
SystemState systemState = STATE_READY;
bool buttonPressed = false;
unsigned long lastDebounceTime = 0;
String transmissionID = "";        // ID текущей передачи
String lastMessage = "";
String responseStatus = "Ready";
int messageCounter = 0;            // Счетчик отправленных сообщений
int loraPacketCounter = 0;         // Счетчик отправленных пакетов LoRa
unsigned long loraPacketStartTime = 0;
unsigned long lastPacketTime = 0;
unsigned long timeoutStartTime = 0; // Время перехода в состояние таймаута
bool sokReceived = false;           // Флаг получения SOK

// Хэш-функция для генерации ID передачи
String generateTransmissionID() {
  messageCounter++;  // Увеличиваем счетчик для каждой передачи
  
  // Используем комбинацию micros(), счетчика и случайных значений
  uint32_t seed = micros();
  seed ^= messageCounter << 16;
  
  // Добавляем случайные значения из аналоговых пинов, если они доступны
  #ifdef A0
  seed ^= analogRead(A0) << 8;
  #endif
  
  // Для ESP32 используем уникальный MAC
  #ifdef ESP32
  uint64_t mac = ESP.getEfuseMac();
  seed ^= (uint32_t)(mac & 0xFFFF);
  seed ^= (uint32_t)(mac >> 16) & 0xFFFF;
  #endif
  
  // Добавляем время (секунды с момента запуска)
  seed ^= (millis() / 1000) & 0xFFFF;
  
  // Простая хэш-функция для генерации 7-значного числа
  seed = (seed * 1103515245 + 12345) % 10000000;
  
  char idBuffer[8];
  sprintf(idBuffer, "%07lu", seed);
  return String(idBuffer);
}

void resetToReadyState() {
  systemState = STATE_READY;
  responseStatus = "Ready";
  loraPacketCounter = 0;
  sokReceived = false;
  updateDisplay();
  Serial.println("System reset to READY state");
}

// Функция для парсинга команды SSET
bool parseSSETCommand(String command) {
  // Проверяем, начинается ли команда с SSET
  if (!command.startsWith("SSET")) {
    return false;
  }
  
  Serial.print("Parsing SSET command: ");
  Serial.println(command);
  
  // Удаляем "SSET " из начала строки
  String params = command.substring(5);
  params.trim();
  
  // Разбиваем строку по запятым
  int paramCount = 0;
  int startPos = 0;
  int commaPos = params.indexOf(',');
  
  while (commaPos != -1 && paramCount < 3) {
    String param = params.substring(startPos, commaPos);
    param.trim();
    processSSETParam(param);
    
    startPos = commaPos + 1;
    commaPos = params.indexOf(',', startPos);
    paramCount++;
  }
  
  // Обрабатываем последний параметр
  if (startPos < params.length()) {
    String param = params.substring(startPos);
    param.trim();
    processSSETParam(param);
  }
  
  return true;
}

// Функция для обработки одного параметра SSET
void processSSETParam(String param) {
  if (param.startsWith("SF")) {
    String sfValue = param.substring(2);
    currentSF = sfValue.toInt();
    Serial.print("Set Spreading Factor to: ");
    Serial.println(currentSF);
  } 
  else if (param.startsWith("BW")) {
    String bwValue = param.substring(2);
    currentBW = bwValue.toFloat();
    Serial.print("Set Bandwidth to: ");
    Serial.println(currentBW);
  } 
  else if (param.startsWith("TX")) {
    String txValue = param.substring(2);
    currentTX = txValue.toInt();
    Serial.print("Set TX Power to: ");
    Serial.println(currentTX);
  }
}

// Функция для применения новых настроек LoRa
void applyLoRaSettings() {
  Serial.println("Applying new LoRa settings...");
  
  // Останавливаем текущую передачу, если она идет
  if (systemState == STATE_SENDING_LORA_PACKETS) {
    Serial.println("Interrupting current LoRa transmission to apply new settings");
    resetToReadyState();
  }
  
  // Применяем новые настройки
  LoRa.setTxPower(currentTX);
  LoRa.setSignalBandwidth(currentBW * 1000); // Конвертируем kHz в Hz
  LoRa.setSpreadingFactor(currentSF);
  
  // Возвращаем модуль в режим приема
  LoRa.receive();
  
  Serial.println("LoRa settings applied successfully");
  
  // Обновляем дисплей
  updateDisplay();
}

// Функция для обработки команд из Serial2
void processSerial2Commands() {
  while (Serial2.available()) {
    String command = Serial2.readStringUntil('\n');
    command.trim();
    
    if (command.length() > 0) {
      Serial.print("Received from Serial2: ");
      Serial.println(command);
      
      // Проверяем, является ли команда SSET
      if (command.startsWith("SSET")) {
        // Парсим и применяем настройки
        if (parseSSETCommand(command)) {
          // Применяем новые настройки LoRa
          applyLoRaSettings();
          
          // Отправляем подтверждение
          Serial2.println("SSOK");
          Serial.println("Sent SSOK to Serial2");
          
          responseStatus = "Settings updated";
          updateDisplay();
        }
      }
      // Проверяем, является ли команда SOK
      else if (command == "SOK") {
        sokReceived = true;
        Serial.println("SOK received and processed!");
        
        // Если мы в состоянии ожидания SOK, запускаем отправку пакетов
        if (systemState == STATE_WAITING_FOR_SOK) {
          responseStatus = "Success: SOK received!";
          Serial.println("Starting LoRa packets...");
          startLoRaPackets();
        }
      }
    }
  }
}

void setup() {
  setupBoards();
  
  // Задержка при включении
  delay(1500);

  Serial.begin(115200);
  Serial.println("Serial Sender with Button");
  
  // Настройка пина кнопки
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Настройка Serial2 (пины 12 и 13)
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.println("Serial2 initialized (RX:13, TX:12)");
  
  // Инициализация LoRa модуля
  Serial.println("Initializing LoRa module...");
  
#ifdef RADIO_TCXO_ENABLE
  pinMode(RADIO_TCXO_ENABLE, OUTPUT);
  digitalWrite(RADIO_TCXO_ENABLE, HIGH);
  Serial.println("TCXO enabled");
#endif

#ifdef RADIO_CTRL
  Serial.println("Setting LAN control...");
  digitalWrite(RADIO_CTRL, HIGH);
#endif /*RADIO_CTRL*/

  LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);
  if (!LoRa.begin(CONFIG_RADIO_FREQ * 1000000)) {
    Serial.println("Warning: LoRa initialization failed!");
    if (u8g2) {
      u8g2->clearBuffer();
      u8g2->drawStr(0, 12, "LoRa Init Failed!");
      u8g2->sendBuffer();
    }
    // Не останавливаем выполнение, так как LoRa не используется для отправки SINIT
  } else {
    Serial.println("LoRa module initialized successfully");
    
    // Настройка параметров LoRa с текущими значениями
    LoRa.setTxPower(currentTX);
    LoRa.setSignalBandwidth(currentBW * 1000);
    LoRa.setSpreadingFactor(currentSF);
    LoRa.setPreambleLength(16);
    LoRa.setSyncWord(0xAB);
    LoRa.disableCrc();
    LoRa.disableInvertIQ();
    LoRa.setCodingRate4(7);

    // Перевод в режим приема
    LoRa.receive();
    Serial.println("LoRa in receive mode");
  }
  
  // Вывод начальной информации на экран
  updateDisplay();
  
  Serial.println("System Ready. Press button on Pin25 to send.");
  Serial.println("Waiting for button press...");
}

void sendMessage() {
  // Генерируем новый ID передачи
  transmissionID = generateTransmissionID();
  
  // Формирование строки с текущими настройками
  lastMessage = "SINIT ID";
  lastMessage += transmissionID;
  lastMessage += ",LAT55.123456,LON37.123456";
  lastMessage += ",SF";
  lastMessage += String(currentSF);
  lastMessage += ",BW";
  lastMessage += String(currentBW, 1);
  lastMessage += ",TX";
  lastMessage += String(currentTX);
  
  // Отправка через Serial2
  Serial2.println(lastMessage);
  Serial.print("Sent to Serial2: ");
  Serial.println(lastMessage);
  Serial.print("Transmission ID: ");
  Serial.println(transmissionID);
  Serial.print("Message counter: ");
  Serial.println(messageCounter);
  
  // Начало ожидания ответа
  systemState = STATE_WAITING_FOR_SOK;
  responseStatus = "Waiting for SOK...";
  loraPacketStartTime = millis();
  sokReceived = false; // Сбрасываем флаг получения SOK
  
  // Обновление дисплея
  updateDisplay();
}

void sendLoRaPacket(int packetNumber) {
  // Формирование payload для пакета
  // Формат: {ID передачи, payload}
  String payload = "Packet " + String(packetNumber) + "/" + String(LORA_PACKET_COUNT);
  payload += ", Time: " + String(millis() / 1000) + "s";
  payload += ", RSSI: -" + String(random(60, 120));  // Имитация уровня сигнала
  
  // Формирование полного пакета
  String packet = "{" + transmissionID + ", " + payload + "}";
  
  // Отправка через LoRa
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();
  
  Serial.print("LoRa packet ");
  Serial.print(packetNumber);
  Serial.print(" sent: ");
  Serial.println(packet);
  
  lastPacketTime = millis();
}

void startLoRaPackets() {
  Serial.println("Starting LoRa packet transmission...");
  systemState = STATE_SENDING_LORA_PACKETS;
  loraPacketCounter = 0;
  responseStatus = "Sending LoRa packets...";
  
  // Отправляем первый пакет сразу
  sendLoRaPacket(1);
  loraPacketCounter = 1;
  
  updateDisplay();
}

void updateDisplay() {
  if (!u8g2) return;
  
  u8g2->clearBuffer();
  
  // Первая строка - статус
  u8g2->drawStr(0, 12, responseStatus.c_str());
  
  // Вторая строка - ID текущей передачи
  u8g2->drawStr(0, 26, "Tx ID:");
  if (transmissionID.length() > 0) {
    u8g2->drawStr(40, 26, transmissionID.c_str());
  } else {
    u8g2->drawStr(40, 26, "None");
  }
  
  // Третья строка - счетчик сообщений и пакетов LoRa
  char counterStr[40];
  if (systemState == STATE_SENDING_LORA_PACKETS) {
    snprintf(counterStr, sizeof(counterStr), "LoRa: %d/%d", loraPacketCounter, LORA_PACKET_COUNT);
  } else {
    snprintf(counterStr, sizeof(counterStr), "Msg #%d", messageCounter);
  }
  u8g2->drawStr(90, 26, counterStr);
  
  // Четвертая строка - текущие настройки LoRa
  char settings[40];
  snprintf(settings, sizeof(settings), "SF%d BW%.1f TX%d", 
           currentSF,
           currentBW, 
           currentTX);
  u8g2->drawStr(0, 40, settings);
  
  // Пятая строка - информация о состоянии
  if (systemState == STATE_WAITING_FOR_SOK) {
    char timeMsg[20];
    unsigned long elapsed = millis() - loraPacketStartTime;
    unsigned long remaining = (elapsed > RESPONSE_TIMEOUT) ? 0 : (RESPONSE_TIMEOUT - elapsed);
    snprintf(timeMsg, sizeof(timeMsg), "Wait: %lu ms", remaining);
    u8g2->drawStr(80, 40, timeMsg);
  } else if (systemState == STATE_SENDING_LORA_PACKETS) {
    char progress[30];
    int percent = (loraPacketCounter * 100) / LORA_PACKET_COUNT;
    snprintf(progress, sizeof(progress), "Progress: %d%%", percent);
    u8g2->drawStr(0, 54, progress);
  } else if (systemState == STATE_TIMEOUT || systemState == STATE_TIMEOUT_WAITING_RESET) {
    char timeoutMsg[30];
    unsigned long elapsed = millis() - timeoutStartTime;
    unsigned long remaining = (elapsed > TIMEOUT_RESET_DELAY) ? 0 : (TIMEOUT_RESET_DELAY - elapsed);
    snprintf(timeoutMsg, sizeof(timeoutMsg), "Reset in: %lu ms", remaining);
    u8g2->drawStr(80, 40, timeoutMsg);
  }
  
  // Шестая строка - последнее сообщение или информация
  if (systemState == STATE_SENDING_LORA_PACKETS) {
    char packetInfo[40];
    snprintf(packetInfo, sizeof(packetInfo), "Packet %d of %d sent", 
             loraPacketCounter, LORA_PACKET_COUNT);
    u8g2->drawStr(0, 68, packetInfo);
  } else if (lastMessage.length() > 0) {
    u8g2->drawStr(0, 54, "Last msg:");
    // Используем безопасное получение подстроки
    int maxLen = 20;
    if (lastMessage.length() < maxLen) {
      maxLen = lastMessage.length();
    }
    String shortMsg = lastMessage.substring(0, maxLen);
    u8g2->drawStr(0, 68, shortMsg.c_str());
    
    // Если сообщение длиннее, показываем многоточие
    if (lastMessage.length() > 20) {
      u8g2->drawStr(120, 68, "...");
    }
  } else {
    u8g2->drawStr(0, 54, "Press button to start");
  }
  
  u8g2->sendBuffer();
}

void checkResponse() {
  if (systemState != STATE_WAITING_FOR_SOK) return;
  
  // Сначала проверяем, не получили ли мы SOK
  if (sokReceived) {
    // SOK уже получен, запускаем отправку пакетов
    responseStatus = "Success: SOK received!";
    Serial.println("SOK already received, starting LoRa packets...");
    startLoRaPackets();
    return;
  }
  
  // Проверка таймаута
  if (millis() - loraPacketStartTime > RESPONSE_TIMEOUT) {
    systemState = STATE_TIMEOUT;
    timeoutStartTime = millis();
    responseStatus = "Timeout - No SOK";
    Serial.println("Response timeout! No SOK received.");
    Serial.println("System will reset in 3 seconds...");
    updateDisplay();
  }
}

void processLoRaPackets() {
  if (systemState != STATE_SENDING_LORA_PACKETS) return;
  
  // Проверяем, нужно ли отправлять следующий пакет
  if (loraPacketCounter < LORA_PACKET_COUNT) {
    if (millis() - lastPacketTime >= LORA_PACKET_DELAY) {
      // Отправляем следующий пакет
      loraPacketCounter++;
      sendLoRaPacket(loraPacketCounter);
      
      // Обновляем статус
      char status[30];
      snprintf(status, sizeof(status), "Sending LoRa %d/%d", 
               loraPacketCounter, LORA_PACKET_COUNT);
      responseStatus = String(status);
      
      updateDisplay();
    }
  } else {
    // Все пакеты отправлены
    resetToReadyState();
    Serial.println("All LoRa packets have been sent.");
  }
}

void processTimeoutState() {
  if (systemState == STATE_TIMEOUT) {
    // Ждем 3 секунды, затем переходим в состояние ожидания сброса
    if (millis() - timeoutStartTime > TIMEOUT_RESET_DELAY) {
      systemState = STATE_TIMEOUT_WAITING_RESET;
      responseStatus = "Press button to retry";
      Serial.println("Timeout period ended. Press button to retry.");
      updateDisplay();
    }
  }
}

void loop() {
  // Чтение состояния кнопки
  int buttonState = digitalRead(BUTTON_PIN);
  
  // Антидребезг
  if (buttonState == LOW && !buttonPressed) {
    if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
      buttonPressed = true;
      lastDebounceTime = millis();
      
      // Проверяем состояние системы
      if (systemState == STATE_READY) {
        // Готовы к отправке нового сообщения
        Serial.println("Button pressed - generating new ID and sending message");
        sendMessage();
      } 
      else if (systemState == STATE_TIMEOUT_WAITING_RESET || systemState == STATE_TIMEOUT) {
        // В состоянии таймаута - сбрасываем и начинаем заново
        Serial.println("Button pressed - resetting system after timeout");
        resetToReadyState();
        delay(100); // Небольшая задержка перед отправкой
        sendMessage();
      }
      else if (systemState == STATE_SENDING_LORA_PACKETS) {
        // Отправка пакетов LoRa в процессе - можно прервать
        Serial.println("Button pressed - aborting LoRa packet transmission");
        resetToReadyState();
      }
      else {
        // Система занята (ожидание SOK)
        Serial.println("Button pressed but system is busy...");
        if (u8g2) {
          u8g2->clearBuffer();
          u8g2->drawStr(0, 12, "System busy");
          u8g2->drawStr(0, 26, "Waiting for SOK");
          u8g2->drawStr(0, 40, "Please wait...");
          u8g2->sendBuffer();
        }
      }
    }
  }
  
  // Сброс флага нажатия кнопки
  if (buttonState == HIGH && buttonPressed) {
    buttonPressed = false;
  }
  
  // Обработка команд из Serial2 (SSET, SOK и др.)
  processSerial2Commands();
  
  // Проверка ответа от Serial2 (для SINIT)
  checkResponse();
  
  // Обработка отправки пакетов LoRa
  processLoRaPackets();
  
  // Обработка состояния таймаута
  processTimeoutState();
  
  // Небольшая задержка для стабильности
  delay(10);
}