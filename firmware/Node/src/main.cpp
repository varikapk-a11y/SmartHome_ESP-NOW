/**
 * SmartHome ESP-NOW Узел (ESP32-C3) с охраной
 * ВЕРСИЯ 2.6: ИСПРАВЛЕНО СНЯТИЕ ТРЕВОГИ
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
#define CONTACT1_PIN 3
#define CONTACT2_PIN 4
#define SENSOR_READ_INTERVAL 30000
#define SECURITY_CHECK_INTERVAL 2000
#define EMULATION_CHECK_INTERVAL 1000

// ---- ПАРАМЕТРЫ ЭМУЛЯЦИИ ----
#define EMULATE_CONTACT1 true
#define EMULATE_CONTACT2 true
#define MIN_BREAK_INTERVAL 60000
#define MAX_BREAK_INTERVAL 120000
#define MIN_BREAK_DURATION 5000
#define MAX_BREAK_DURATION 15000

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
unsigned long lastEmulationCheck = 0;

// Реальное состояние концевиков (с пинов)
bool realContact1Alarm = false;  // true = тревога (разомкнут)
bool realContact2Alarm = false;

// Эмулируемое состояние (которое отправляем на хаб)
bool emulatedContact1Alarm = false;
bool emulatedContact2Alarm = false;

// Для отслеживания изменений
bool lastSentContact1 = false;
bool lastSentContact2 = false;

// Таймеры для эмуляции разрывов
unsigned long nextBreakTime1 = 0;
unsigned long breakEndTime1 = 0;
bool isBroken1 = false;

unsigned long nextBreakTime2 = 0;
unsigned long breakEndTime2 = 0;
bool isBroken2 = false;

// MAC адрес хаба
uint8_t hubMacAddress[] = {0x9C, 0x9C, 0x1F, 0xC7, 0x2D, 0x94};

// ---- ПРОТОТИПЫ ----
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void sendJsonToHub(const char* json_string);
void readAndSendSensorData();
void sendGpioStatus();
void checkSecuritySensors();
void sendSecurityStatus(bool contact1Alarm, bool contact2Alarm);
void initEmulationTimers();
void updateEmulation();

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.print("\n=== УЗЕЛ ESP-NOW #");
    Serial.print(NODE_ID);
    Serial.println(" (ИСПРАВЛЕНО СНЯТИЕ ТРЕВОГИ) ===");
    
    Serial.print("MAC: ");
    Serial.print(NODE_MAC_STR);
    Serial.print(" | ID: ");
    Serial.println(NODE_ID);

    // Настройка пинов
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    pinMode(CONTACT1_PIN, INPUT_PULLUP);
    pinMode(CONTACT2_PIN, INPUT_PULLUP);
    
    // Читаем начальное состояние
    realContact1Alarm = (digitalRead(CONTACT1_PIN) == HIGH);
    realContact2Alarm = (digitalRead(CONTACT2_PIN) == HIGH);
    
    emulatedContact1Alarm = realContact1Alarm;
    emulatedContact2Alarm = realContact2Alarm;
    
    lastSentContact1 = emulatedContact1Alarm;
    lastSentContact2 = emulatedContact2Alarm;
    
    Serial.print("[1] Концевики: ");
    Serial.print("1=");
    Serial.print(realContact1Alarm ? "ТРЕВОГА" : "НОРМА");
    Serial.print(", 2=");
    Serial.println(realContact2Alarm ? "ТРЕВОГА" : "НОРМА");

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

    initEmulationTimers();

    delay(1000);
    sendGpioStatus();
    sendSecurityStatus(emulatedContact1Alarm, emulatedContact2Alarm);
    readAndSendSensorData();

    Serial.print("\n=== УЗЕЛ #");
    Serial.print(NODE_ID);
    Serial.println(" ГОТОВ ===\n");
    
    lastSensorReadTime = millis();
    lastSecurityCheck = millis();
    lastEmulationCheck = millis();
}

// ===================== LOOP =====================
void loop() {
    unsigned long now = millis();
    
    if (now - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        readAndSendSensorData();
        lastSensorReadTime = now;
    }
    
    if (now - lastSecurityCheck >= SECURITY_CHECK_INTERVAL) {
        realContact1Alarm = (digitalRead(CONTACT1_PIN) == HIGH);
        realContact2Alarm = (digitalRead(CONTACT2_PIN) == HIGH);
        
        // Обновляем эмулированное состояние (если не в режиме разрыва)
        if (!isBroken1) {
            emulatedContact1Alarm = realContact1Alarm;
        }
        if (!isBroken2) {
            emulatedContact2Alarm = realContact2Alarm;
        }
        
        checkSecuritySensors();
        lastSecurityCheck = now;
    }
    
    if (now - lastEmulationCheck >= EMULATION_CHECK_INTERVAL) {
        updateEmulation();
        lastEmulationCheck = now;
    }
    
    delay(100);
}

// ===================== ИНИЦИАЛИЗАЦИЯ ЭМУЛЯЦИИ =====================
void initEmulationTimers() {
    unsigned long now = millis();
    
    if (EMULATE_CONTACT1) {
        nextBreakTime1 = now + random(30000, 60000);
        Serial.println("[ЭМУЛЯЦИЯ] Таймер разрыва 1 инициализирован");
    }
    
    if (EMULATE_CONTACT2) {
        nextBreakTime2 = now + random(30000, 60000);
        Serial.println("[ЭМУЛЯЦИЯ] Таймер разрыва 2 инициализирован");
    }
}

// ===================== ОБНОВЛЕНИЕ ЭМУЛЯЦИИ =====================
void updateEmulation() {
    unsigned long now = millis();
    bool needUpdate = false;
    
    // Концевик 1
    if (EMULATE_CONTACT1) {
        if (isBroken1) {
            // Сейчас разомкнут - проверяем восстановление
            if (now >= breakEndTime1) {
                isBroken1 = false;
                emulatedContact1Alarm = realContact1Alarm;  // Возвращаем реальное состояние
                nextBreakTime1 = now + random(MIN_BREAK_INTERVAL, MAX_BREAK_INTERVAL);
                Serial.println("[ЭМУЛЯЦИЯ] Концевик 1 ВОССТАНОВЛЕН");
                needUpdate = true;
            }
        } else {
            // Сейчас норма - проверяем разрыв
            if (now >= nextBreakTime1) {
                isBroken1 = true;
                emulatedContact1Alarm = true;  // Принудительная тревога
                breakEndTime1 = now + random(MIN_BREAK_DURATION, MAX_BREAK_DURATION);
                Serial.println("[ЭМУЛЯЦИЯ] Концевик 1 РАЗОМКНУТ");
                needUpdate = true;
            }
        }
    }
    
    // Концевик 2
    if (EMULATE_CONTACT2) {
        if (isBroken2) {
            if (now >= breakEndTime2) {
                isBroken2 = false;
                emulatedContact2Alarm = realContact2Alarm;
                nextBreakTime2 = now + random(MIN_BREAK_INTERVAL, MAX_BREAK_INTERVAL);
                Serial.println("[ЭМУЛЯЦИЯ] Концевик 2 ВОССТАНОВЛЕН");
                needUpdate = true;
            }
        } else {
            if (now >= nextBreakTime2) {
                isBroken2 = true;
                emulatedContact2Alarm = true;
                breakEndTime2 = now + random(MIN_BREAK_DURATION, MAX_BREAK_DURATION);
                Serial.println("[ЭМУЛЯЦИЯ] Концевик 2 РАЗОМКНУТ");
                needUpdate = true;
            }
        }
    }
    
    // Отправляем изменения на хаб
    if (needUpdate) {
        sendSecurityStatus(emulatedContact1Alarm, emulatedContact2Alarm);
        // Обновляем lastSent переменные
        lastSentContact1 = emulatedContact1Alarm;
        lastSentContact2 = emulatedContact2Alarm;
    }
}

// ===================== ПРОВЕРКА КОНЦЕВИКОВ =====================
void checkSecuritySensors() {
    bool needSend = false;
    
    // Проверяем концевик 1
    if (emulatedContact1Alarm != lastSentContact1) {
        lastSentContact1 = emulatedContact1Alarm;
        needSend = true;
        Serial.print("[КОНЦЕВИК 1] ");
        Serial.println(emulatedContact1Alarm ? "ТРЕВОГА" : "НОРМА");
    }
    
    // Проверяем концевик 2
    if (emulatedContact2Alarm != lastSentContact2) {
        lastSentContact2 = emulatedContact2Alarm;
        needSend = true;
        Serial.print("[КОНЦЕВИК 2] ");
        Serial.println(emulatedContact2Alarm ? "ТРЕВОГА" : "НОРМА");
    }
    
    // Если были изменения - отправляем
    if (needSend) {
        sendSecurityStatus(emulatedContact1Alarm, emulatedContact2Alarm);
    }
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
}

// ===================== ОТПРАВКА =====================
void sendJsonToHub(const char* json_string) {
    size_t json_len = strlen(json_string);
    if (json_len >= sizeof(outgoingMessage.json)) {
        return;
    }
    
    strncpy(outgoingMessage.json, json_string, sizeof(outgoingMessage.json)-1);
    outgoingMessage.json[sizeof(outgoingMessage.json)-1] = '\0';
    outgoingMessage.sender_id = NODE_ID;
    
    esp_now_send(hubMacAddress, (uint8_t *) &outgoingMessage, sizeof(outgoingMessage));
}

void sendGpioStatus() {
    char json[64];
    int ledState = (digitalRead(LED_PIN) == LOW) ? 1 : 0;
    
    snprintf(json, sizeof(json),
        "{\"type\":\"gpio\",\"pin\":8,\"state\":%d}",
        ledState);
    
    Serial.print("[GPIO] LED=");
    Serial.println(ledState ? "ON" : "OFF");
    sendJsonToHub(json);
}

void sendSecurityStatus(bool contact1Alarm, bool contact2Alarm) {
    char json[128];
    bool alarm = contact1Alarm || contact2Alarm;
    
    snprintf(json, sizeof(json),
        "{\"type\":\"security\",\"alarm\":%s,\"contact1\":%s,\"contact2\":%s}",
        alarm ? "true" : "false",
        contact1Alarm ? "true" : "false",
        contact2Alarm ? "true" : "false");
    
    Serial.print("[ОХРАНА] alarm=");
    Serial.print(alarm ? "YES" : "NO");
    Serial.print(", c1=");
    Serial.print(contact1Alarm ? "ТРЕВОГА" : "НОРМА");
    Serial.print(", c2=");
    Serial.println(contact2Alarm ? "ТРЕВОГА" : "НОРМА");
    
    sendJsonToHub(json);
}

// ===================== ОБРАБОТКА КОМАНД =====================
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
            digitalWrite(LED_PIN, LOW);
            Serial.println("  -> 💡 LED ВКЛЮЧЁН");
            sendJsonToHub("{\"type\":\"ack\",\"command\":\"LED_ON\",\"status\":\"success\"}");
            sendGpioStatus();
        }
        else if (cmd && strcmp(cmd, "LED_OFF") == 0) {
            digitalWrite(LED_PIN, HIGH);
            Serial.println("  -> 💡 LED ВЫКЛЮЧЕН");
            sendJsonToHub("{\"type\":\"ack\",\"command\":\"LED_OFF\",\"status\":\"success\"}");
            sendGpioStatus();
        }
        else if (cmd && strcmp(cmd, "GET_STATUS") == 0) {
            Serial.println("  -> 📡 ЗАПРОС СТАТУСА");
            sendGpioStatus();
            sendSecurityStatus(emulatedContact1Alarm, emulatedContact2Alarm);
            readAndSendSensorData();
        }
    }
}

void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("⚠️ Ошибка отправки");
    }
}