/**
 * SmartHome ESP-NOW Узел (ESP32-C3) с охраной
 * ВЕРСИЯ 4.1: ИСПРАВЛЕНА ОТПРАВКА СТАТУСА LED
 * Концевик 1: 20 сек НОРМА, 5 сек ТРЕВОГА, и по кругу
 * Концевик 2: постоянно НОРМА (замкнут)
 * Статус LED отправляется при каждой передаче данных
 */

// ===== БЛОК ДЛЯ УЗЛА #104 =====
#define NODE_ID 104
#define NODE_MAC_STR "10:00:3B:B1:A6:9C"
// ===============================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <ArduinoJson.h>

// ---- КОНСТАНТЫ ----
#define LED_PIN 8
#define CONTACT1_PIN 3     // Только этот концевик реально работает
#define SENSOR_READ_INTERVAL 30000
#define SECURITY_CHECK_INTERVAL 100
#define SECURITY_RESEND_INTERVAL 5000

// ---- ЦИКЛИЧЕСКИЙ ТАЙМЕР ДЛЯ КОНЦЕВИКА 1 ----
#define NORMAL_DURATION 20000  // 20 секунд в норме (замкнут)
#define ALARM_DURATION 5000     // 5 секунд в тревоге (разомкнут)

// ---- СТРУКТУРА ESP-NOW ----
typedef struct esp_now_message {
    char json[192];
    uint8_t sender_id;
} esp_now_message;

// ---- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ----
esp_now_message incomingMessage;
esp_now_message outgoingMessage;

unsigned long lastSensorReadTime = 0;
unsigned long lastSecurityCheck = 0;
unsigned long lastSecurityResendTime = 0;
unsigned long lastCycleTime = 0;

// Состояние концевика 1
bool contact1Alarm = false;  // false = НОРМА (замкнут), true = ТРЕВОГА (разомкнут)
bool lastSentContact1 = false;

// Концевик 2 всегда в норме
const bool contact2Alarm = false;

// MAC адрес хаба
uint8_t hubMacAddress[] = {0x9C, 0x9C, 0x1F, 0xC7, 0x2D, 0x94};

// ---- ПРОТОТИПЫ ----
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void sendJsonToHub(const char* json_string);
void readAndSendSensorData();
void sendGpioStatus();
void sendSecurityToHub(bool c1, bool c2);
void updateContactState();
void debugStatus();

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.print("\n=== УЗЕЛ ESP-NOW #");
    Serial.print(NODE_ID);
    Serial.println(" (С ОТПРАВКОЙ СТАТУСА LED) ===");
    
    Serial.print("MAC: ");
    Serial.print(NODE_MAC_STR);
    Serial.print(" | ID: ");
    Serial.println(NODE_ID);

    // Настройка пинов
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);  // LED выключен (активный LOW)
    
    // Концевик 1 настроен как INPUT_PULLUP, но мы его НЕ читаем
    // Мы полностью эмулируем его состояние через таймер
    pinMode(CONTACT1_PIN, INPUT_PULLUP);

    Serial.println("[1] РЕЖИМ РАБОТЫ:");
    Serial.println("    Концевик 1: ПОЛНАЯ ЭМУЛЯЦИЯ через таймер");
    Serial.println("    Цикл: 20 сек НОРМА, 5 сек ТРЕВОГА");
    Serial.println("    Концевик 2: постоянно НОРМА (замкнут)");
    Serial.println("    Статус LED: отправляется при каждой передаче");

    // Wi-Fi и ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ОШИБКА] ESP-NOW!");
        while(1);
    }

    esp_now_register_recv_cb(onEspNowDataRecv);
    esp_now_register_send_cb(onEspNowDataSent);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, hubMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ОШИБКА] Хаб не добавлен!");
    } else {
        Serial.println("[2] Хаб добавлен");
    }

    delay(1000);
    
    // Начинаем с НОРМЫ (false)
    contact1Alarm = false;
    lastSentContact1 = false;
    lastCycleTime = millis();
    
    // Отправляем начальные статусы
    sendGpioStatus();  // Статус LED сразу при старте
    sendSecurityToHub(contact1Alarm, contact2Alarm);
    readAndSendSensorData();

    Serial.print("\n=== УЗЕЛ #");
    Serial.print(NODE_ID);
    Serial.println(" ГОТОВ ===\n");
    Serial.println("СТАРТ: НОРМА (20 сек)");
    
    lastSensorReadTime = millis();
    lastSecurityCheck = millis();
    lastSecurityResendTime = millis();
}

// ===================== LOOP =====================
void loop() {
    unsigned long now = millis();
    
    // Эмуляция датчиков
    if (now - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        readAndSendSensorData();
        lastSensorReadTime = now;
    }
    
    // Обновление состояния концевика по таймеру
    updateContactState();
    
    // Отправка изменений (если есть)
    if (contact1Alarm != lastSentContact1) {
        lastSentContact1 = contact1Alarm;
        Serial.print("[ИЗМЕНЕНИЕ] Концевик 1: ");
        Serial.println(contact1Alarm ? "ТРЕВОГА (5 сек)" : "НОРМА (20 сек)");
        sendSecurityToHub(contact1Alarm, contact2Alarm);
    }
    
    // Периодическая повторная отправка статуса
    if (now - lastSecurityResendTime >= SECURITY_RESEND_INTERVAL) {
        Serial.println("[ПЕРИОДИЧЕСКАЯ ОТПРАВКА]");
        sendSecurityToHub(contact1Alarm, contact2Alarm);
        lastSecurityResendTime = now;
    }
    
    // Отладка раз в 10 секунд
    static unsigned long lastDebug = 0;
    if (now - lastDebug >= 10000) {
        debugStatus();
        lastDebug = now;
    }
    
    delay(10);
}

// ===================== ОБНОВЛЕНИЕ СОСТОЯНИЯ КОНЦЕВИКА =====================
void updateContactState() {
    unsigned long now = millis();
    unsigned long elapsed = now - lastCycleTime;
    
    if (contact1Alarm == false) {
        // Сейчас НОРМА - проверяем, не пора ли в ТРЕВОГУ
        if (elapsed >= NORMAL_DURATION) {
            contact1Alarm = true;  // Переключаем в ТРЕВОГУ
            lastCycleTime = now;
            Serial.println("[ТАЙМЕР] 20 сек НОРМА прошло -> переключение в ТРЕВОГУ");
        }
    } else {
        // Сейчас ТРЕВОГА - проверяем, не пора ли в НОРМУ
        if (elapsed >= ALARM_DURATION) {
            contact1Alarm = false;  // Переключаем в НОРМУ
            lastCycleTime = now;
            Serial.println("[ТАЙМЕР] 5 сек ТРЕВОГА прошло -> переключение в НОРМУ");
        }
    }
}

// ===================== ОТЛАДКА =====================
void debugStatus() {
    unsigned long now = millis();
    unsigned long elapsed = now - lastCycleTime;
    unsigned long remaining;
    int ledPhysicalState = digitalRead(LED_PIN);
    
    Serial.println("\n--- СОСТОЯНИЕ ---");
    Serial.print("LED физически: ");
    Serial.println(ledPhysicalState == LOW ? "ВКЛЮЧЕН" : "ВЫКЛЮЧЕН");
    Serial.print("LED статус для хаба: ");
    Serial.println(ledPhysicalState == LOW ? "ON (1)" : "OFF (0)");
    
    Serial.print("Концевик 1: ");
    Serial.print(contact1Alarm ? "ТРЕВОГА" : "НОРМА");
    
    if (contact1Alarm == false) {
        remaining = NORMAL_DURATION - elapsed;
        Serial.print(" (осталось НОРМЫ: ");
        Serial.print(remaining / 1000);
        Serial.println(" сек)");
    } else {
        remaining = ALARM_DURATION - elapsed;
        Serial.print(" (осталось ТРЕВОГИ: ");
        Serial.print(remaining / 1000);
        Serial.println(" сек)");
    }
    
    Serial.print("Концевик 2: НОРМА (постоянно)");
    Serial.println("\n------------------\n");
}

// ===================== ОТПРАВКА СТАТУСА ОХРАНЫ =====================
void sendSecurityToHub(bool c1, bool c2) {
    char json[128];
    bool alarm = c1 || c2;  // c2 всегда false
    
    snprintf(json, sizeof(json),
        "{\"type\":\"security\",\"alarm\":%s,\"contact1\":%s,\"contact2\":%s}",
        alarm ? "true" : "false",
        c1 ? "true" : "false",
        c2 ? "true" : "false");
    
    Serial.print("[ОТПРАВКА ОХРАНЫ] alarm=");
    Serial.print(alarm ? "YES" : "NO");
    Serial.print(", c1=");
    Serial.print(c1 ? "ТРЕВОГА" : "НОРМА");
    Serial.println(", c2=НОРМА");
    
    sendJsonToHub(json);
    
    // ВАЖНО: после отправки охраны сразу отправляем статус LED
    sendGpioStatus();
}

// ===================== ДАТЧИКИ =====================
void readAndSendSensorData() {
    float temp_aht = 20.0 + (random(0, 200) / 10.0);
    float hum_aht = 40.0 + (random(0, 400) / 10.0);
    float temp_bmp = temp_aht - 1.0 + (random(-50, 50) / 10.0);
    float press_mmHg = 740.0 + (random(-50, 50) / 10.0);

    char json[192];
    snprintf(json, sizeof(json),
        "{\"type\":\"sensor\",\"data\":{\"AHT20\":{\"temp\":%.1f,\"hum\":%.1f},\"BMP280\":{\"temp\":%.1f,\"press_mmHg\":%.1f}}}",
        temp_aht, hum_aht, temp_bmp, press_mmHg);

    Serial.print("[ДАТЧИКИ] ");
    Serial.println(json);
    sendJsonToHub(json);
    
    // ВАЖНО: после отправки датчиков сразу отправляем статус LED
    sendGpioStatus();
}

// ===================== ОТПРАВКА СТАТУСА LED =====================
void sendGpioStatus() {
    char json[64];
    // digitalRead(LED_PIN) == LOW означает что LED горит (активный LOW)
    int ledState = (digitalRead(LED_PIN) == LOW) ? 1 : 0;
    
    snprintf(json, sizeof(json),
        "{\"type\":\"gpio\",\"pin\":8,\"state\":%d}",
        ledState);
    
    Serial.print("[ОТПРАВКА LED] статус=");
    Serial.println(ledState ? "ON" : "OFF");
    
    sendJsonToHub(json);
}

// ===================== ОТПРАВКА JSON =====================
void sendJsonToHub(const char* json_string) {
    size_t json_len = strlen(json_string);
    if (json_len >= sizeof(outgoingMessage.json)) {
        Serial.println("[ОШИБКА] JSON слишком длинный");
        return;
    }
    
    strncpy(outgoingMessage.json, json_string, sizeof(outgoingMessage.json)-1);
    outgoingMessage.json[sizeof(outgoingMessage.json)-1] = '\0';
    outgoingMessage.sender_id = NODE_ID;
    
    esp_err_t result = esp_now_send(hubMacAddress, (uint8_t *) &outgoingMessage, sizeof(outgoingMessage));
    
    if (result != ESP_OK) {
        Serial.println("⚠️ Ошибка отправки");
    }
}

// ===================== ОБРАБОТКА ВХОДЯЩИХ КОМАНД =====================
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    if (memcmp(mac_addr, hubMacAddress, 6) != 0) {
        return;
    }

    memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));
    
    Serial.print("📥 ПОЛУЧЕНО: ");
    Serial.println(incomingMessage.json);
    
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, incomingMessage.json);
    
    if (error) {
        Serial.println("❌ Ошибка JSON");
        return;
    }
    
    const char* type = doc["type"];
    if (type && strcmp(type, "command") == 0) {
        const char* cmd = doc["command"];
        
        if (cmd && strcmp(cmd, "LED_ON") == 0) {
            digitalWrite(LED_PIN, LOW);  // Включаем LED (активный LOW)
            Serial.println("  -> 💡 LED ВКЛЮЧЁН");
            sendJsonToHub("{\"type\":\"ack\",\"command\":\"LED_ON\",\"status\":\"success\"}");
            sendGpioStatus();  // Отправляем новый статус LED
        }
        else if (cmd && strcmp(cmd, "LED_OFF") == 0) {
            digitalWrite(LED_PIN, HIGH);  // Выключаем LED
            Serial.println("  -> 💡 LED ВЫКЛЮЧЕН");
            sendJsonToHub("{\"type\":\"ack\",\"command\":\"LED_OFF\",\"status\":\"success\"}");
            sendGpioStatus();  // Отправляем новый статус LED
        }
        else if (cmd && strcmp(cmd, "GET_STATUS") == 0) {
            Serial.println("  -> 📡 ЗАПРОС СТАТУСА");
            sendGpioStatus();  // Отправляем статус LED
            sendSecurityToHub(contact1Alarm, contact2Alarm);  // Отправляем охрану
            readAndSendSensorData();  // Отправляем датчики
        }
    }
}

// ===================== ОБРАБОТКА ПОДТВЕРЖДЕНИЙ =====================
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("⚠️ ESP-NOW: пакет не доставлен");
    }
}