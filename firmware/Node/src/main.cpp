/**
 * SmartHome ESP-NOW Узел (ESP32-C3) с охраной и энкодером
 * Универсальная версия с JSON структурой и концевиками
 * ВЕРСИЯ 2.6: Энкодер - тишина при отсутствии магнита
 */
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <ArduinoJson.h>

// ---- КОНСТАНТЫ ----
#define NODE_ID 101
#define LED_PIN 8
#define CONTACT1_PIN 3    // GPIO для концевика 1 (НОРМАЛЬНО ЗАМКНУТ)
#define CONTACT2_PIN 4    // GPIO для концевика 2 (НОРМАЛЬНО ЗАМКНУТ)
#define SENSOR_READ_INTERVAL 30000      // 30 сек
#define SECURITY_CHECK_INTERVAL 200     // 200 мс - для быстрой реакции
#define STATUS_REPORT_INTERVAL 60000    // 60 сек - плановая отправка статуса
#define ENCODER_READ_INTERVAL 200       // 200 мс - частая проверка энкодера

// I2C пины для ESP32-C3
const int SDA_PIN = 1;
const int SCL_PIN = 0;

// ---- AS5600 КОНСТАНТЫ ----
#define AS5600_ADDR 0x36
#define ANGLE_H_REG 0x0E
#define ANGLE_L_REG 0x0F
#define STATUS_REG 0x0B
#define ENCODER_CHANGE_THRESHOLD 5.0  // Порог изменения угла в градусах

// ---- УНИВЕРСАЛЬНАЯ СТРУКТУРА ESP-NOW ----
typedef struct esp_now_message {
    char json[192];      // JSON строка с данными
    uint8_t sender_id;   // ID отправителя
} esp_now_message;

// ---- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ----
Adafruit_BMP280 bmp;
Adafruit_AHTX0 aht;
bool hasBMP = false;
bool hasAHT = false;
bool hasAS5600 = false;

esp_now_message incomingMessage;
esp_now_message outgoingMessage;

unsigned long lastSensorReadTime = 0;
unsigned long lastSecurityCheck = 0;
unsigned long lastStatusReportTime = 0;
unsigned long lastEncoderCheckTime = 0;

// Концевики
bool currentContact1 = false;
bool currentContact2 = false;
bool lastSentContact1 = false;
bool lastSentContact2 = false;

// Энкодер
uint8_t angle_data[2];
uint16_t lastRawAngle = 0;
float lastAngleDeg = 0.0;
float lastSentAngleDeg = 0.0;
bool magnetDetected = false;
bool lastSentMagnet = false;
unsigned long lastEncoderReportTime = 0;

// MAC хаба
uint8_t hubMacAddress[] = {0x9C, 0x9C, 0x1F, 0xC7, 0x2D, 0x94};

// ---- ПРОТОТИПЫ ----
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void sendJsonToHub(const char* json_string);
void readAndSendSensorData();
void sendGpioStatus();
bool initSensors();
void checkSecuritySensors();
void sendSecurityStatus(bool contact1Alarm, bool contact2Alarm, bool force);
void initAS5600();
uint16_t readRawAngle();
void checkEncoder();
void sendEncoderData(float angle);

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n=== УЗЕЛ ESP-NOW (JSON версия) ===");
    Serial.println("MAC: AC:EB:E6:49:10:28 | ID: 101");
    Serial.println("Режим: статус раз в минуту, изменения мгновенно");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    pinMode(CONTACT1_PIN, INPUT_PULLUP);
    pinMode(CONTACT2_PIN, INPUT_PULLUP);
    Serial.println("[0] Концевики инициализированы");

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);
    Serial.println("[1] I2C инициализирован");

    Serial.println("[2] Инициализация датчиков...");
    initSensors();

    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    Serial.print("[3] MAC узла: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ОШИБКА] ESP-NOW!");
        while(1);
    }
    Serial.println("[4] ESP-NOW инициализирован");

    esp_now_register_recv_cb(onEspNowDataRecv);
    esp_now_register_send_cb(onEspNowDataSent);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, hubMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ОШИБКА] Добавление хаба");
    } else {
        Serial.println("[5] Хаб добавлен");
    }

    // Начальное состояние концевиков
    currentContact1 = (digitalRead(CONTACT1_PIN) == HIGH);
    currentContact2 = (digitalRead(CONTACT2_PIN) == HIGH);
    lastSentContact1 = currentContact1;
    lastSentContact2 = currentContact2;
    
    Serial.print("[КОНЦЕВИКИ] Начало: ");
    Serial.print("C1=");
    Serial.print(currentContact1 ? "ТРЕВОГА" : "НОРМА");
    Serial.print(", C2=");
    Serial.println(currentContact2 ? "ТРЕВОГА" : "НОРМА");

    // Инициализация AS5600
    initAS5600();

    // Отправка начального статуса
    sendSecurityStatus(currentContact1, currentContact2, true);
    if (hasAS5600 && magnetDetected) {
        sendEncoderData(lastAngleDeg);
        lastSentAngleDeg = lastAngleDeg;
        lastSentMagnet = true;
    }

    Serial.println("\n=== УЗЕЛ ГОТОВ ===\n");
    readAndSendSensorData();
    
    lastSensorReadTime = millis();
    lastSecurityCheck = millis();
    lastStatusReportTime = millis();
    lastEncoderCheckTime = millis();
    lastEncoderReportTime = millis();
}

// ===================== LOOP =====================
void loop() {
    unsigned long now = millis();
    
    // Датчики раз в 30 секунд
    if (now - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        readAndSendSensorData();
        lastSensorReadTime = now;
    }
    
    // Концевики - проверяем часто для быстрой реакции
    if (now - lastSecurityCheck >= SECURITY_CHECK_INTERVAL) {
        checkSecuritySensors();
        lastSecurityCheck = now;
    }
    
    // Энкодер - проверяем часто
    if (hasAS5600 && (now - lastEncoderCheckTime >= ENCODER_READ_INTERVAL)) {
        checkEncoder();
        lastEncoderCheckTime = now;
    }
    
    // Плановый отчет раз в минуту
    if (now - lastStatusReportTime >= STATUS_REPORT_INTERVAL) {
        Serial.println("[ПЛАНОВО] Отчет раз в минуту");
        
        // Отправляем статус концевиков
        sendSecurityStatus(currentContact1, currentContact2, true);
        
        // Отправляем данные энкодера только если есть магнит
        if (hasAS5600 && magnetDetected) {
            sendEncoderData(lastAngleDeg);
            lastSentAngleDeg = lastAngleDeg;
        }
        
        lastStatusReportTime = now;
    }
    
    delay(10);
}

// ===================== ФУНКЦИИ ДАТЧИКОВ =====================
bool initSensors() {
    bool ok = false;
    
    if (bmp.begin(0x76)) {
        hasBMP = true;
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                       Adafruit_BMP280::SAMPLING_X2,
                       Adafruit_BMP280::SAMPLING_X16,
                       Adafruit_BMP280::FILTER_X16,
                       Adafruit_BMP280::STANDBY_MS_500);
        Serial.println("  -> ✅ BMP280 найден");
        ok = true;
    } else {
        Serial.println("  -> ❌ BMP280 не найден");
    }
    
    if (aht.begin()) {
        hasAHT = true;
        Serial.println("  -> ✅ AHT20 найден");
        ok = true;
    } else {
        Serial.println("  -> ❌ AHT20 не найден");
    }
    
    return ok;
}

// ===================== AS5600 =====================
void initAS5600() {
    Serial.print("[AS5600] Проверка... ");
    
    Wire.beginTransmission(AS5600_ADDR);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
        hasAS5600 = true;
        Serial.println("✅ Датчик найден");
        
        Wire.beginTransmission(AS5600_ADDR);
        Wire.write(STATUS_REG);
        Wire.endTransmission(false);
        Wire.requestFrom(AS5600_ADDR, 1);
        
        if (Wire.available()) {
            byte status = Wire.read();
            magnetDetected = (status & 0x20);
            lastSentMagnet = magnetDetected;
            Serial.print("   Магнит: ");
            Serial.println(magnetDetected ? "✅ есть" : "❌ нет");
        }
        
        lastRawAngle = readRawAngle();
        lastAngleDeg = (lastRawAngle * 360.0) / 4096.0;
        lastSentAngleDeg = lastAngleDeg;
        Serial.printf("   Угол: %.1f°\n", lastAngleDeg);
        
    } else {
        hasAS5600 = false;
        Serial.println("❌ Датчик не найден");
    }
}

uint16_t readRawAngle() {
    Wire.beginTransmission(AS5600_ADDR);
    Wire.write(ANGLE_H_REG);
    Wire.endTransmission(false);
    
    Wire.requestFrom(AS5600_ADDR, 2);
    if (Wire.available() >= 2) {
        angle_data[0] = Wire.read();
        angle_data[1] = Wire.read();
        return (angle_data[0] << 8) | angle_data[1];
    }
    return 0;
}

void checkEncoder() {
    if (!hasAS5600) return;
    
    uint16_t raw_angle = readRawAngle();
    float angle_deg = (raw_angle * 360.0) / 4096.0;
    
    // Проверка магнита
    Wire.beginTransmission(AS5600_ADDR);
    Wire.write(STATUS_REG);
    Wire.endTransmission(false);
    Wire.requestFrom(AS5600_ADDR, 1);
    
    bool magnet_now = false;
    if (Wire.available()) {
        byte status = Wire.read();
        magnet_now = (status & 0x20);
    }
    
    // Обновляем текущие значения
    lastRawAngle = raw_angle;
    lastAngleDeg = angle_deg;
    
    // === Если магнита нет ===
    if (!magnet_now) {
        // Если магнит только что пропал - отправляем спецсообщение
        if (lastSentMagnet) {
            Serial.println("[ЭНКОДЕР] Магнит пропал");
            char json[64];
            snprintf(json, sizeof(json),
                "{\"type\":\"encoder\",\"magnet\":false}");
            sendJsonToHub(json);
            lastSentMagnet = false;
        }
        // Больше ничего не отправляем
        return;
    }
    
    // === Если магнит есть ===
    
    // Магнит только что появился - отправляем угол
    if (!lastSentMagnet) {
        Serial.printf("[ЭНКОДЕР] Магнит появился, угол: %.1f°\n", angle_deg);
        sendEncoderData(angle_deg);
        lastSentMagnet = true;
        lastSentAngleDeg = angle_deg;
        return;
    }
    
    // Магнит был и есть - проверяем изменение угла
    if (fabs(angle_deg - lastSentAngleDeg) >= ENCODER_CHANGE_THRESHOLD) {
        Serial.printf("[ЭНКОДЕР] Угол изменился: %.1f° -> %.1f°\n", lastSentAngleDeg, angle_deg);
        sendEncoderData(angle_deg);
        lastSentAngleDeg = angle_deg;
    }
}

void sendEncoderData(float angle) {
    char json[128];
    snprintf(json, sizeof(json),
        "{\"type\":\"encoder\",\"angle\":%.1f,\"magnet\":true}",
        angle);
    sendJsonToHub(json);
}

// ===================== ОТПРАВКА =====================
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
    if (result == ESP_OK) {
        Serial.printf("[ESP-NOW] Отправлено: %s\n", json_string);
    } else {
        Serial.printf("[ESP-NOW] Ошибка: %d\n", result);
    }
}

void readAndSendSensorData() {
    float temp_bmp = 0, press_mmHg = 0;
    float temp_aht = 0, hum_aht = 0;

    Serial.println("[ДАТЧИКИ] Чтение...");
    
    if (hasBMP) {
        temp_bmp = bmp.readTemperature();
        float press_hPa = bmp.readPressure() / 100.0F;
        press_mmHg = press_hPa * 0.750062;
    }
    
    if (hasAHT) {
        sensors_event_t humidity, temp;
        aht.getEvent(&humidity, &temp);
        temp_aht = temp.temperature;
        hum_aht = humidity.relative_humidity;
    }

    char json[192];
    snprintf(json, sizeof(json),
        "{\"type\":\"sensor\",\"data\":{\"AHT20\":{\"temp\":%.1f,\"hum\":%.1f},\"BMP280\":{\"temp\":%.1f,\"press_mmHg\":%.1f}}}",
        temp_aht, hum_aht, temp_bmp, press_mmHg);

    sendJsonToHub(json);
}

void sendGpioStatus() {
    int ledState = digitalRead(LED_PIN) == LOW ? 1 : 0;
    char json[64];
    snprintf(json, sizeof(json),
        "{\"type\":\"gpio\",\"pin\":8,\"state\":%d}", ledState);
    
    sendJsonToHub(json);
}

// ===================== КОНЦЕВИКИ =====================
void checkSecuritySensors() {
    bool newContact1 = (digitalRead(CONTACT1_PIN) == HIGH);
    bool newContact2 = (digitalRead(CONTACT2_PIN) == HIGH);
    
    currentContact1 = newContact1;
    currentContact2 = newContact2;
    
    // Если изменилось - отправляем немедленно
    if (newContact1 != lastSentContact1 || newContact2 != lastSentContact2) {
        Serial.printf("[КОНЦЕВИКИ] Изменение: C1=%d C2=%d\n", newContact1, newContact2);
        sendSecurityStatus(newContact1, newContact2, false);
        
        lastSentContact1 = newContact1;
        lastSentContact2 = newContact2;
    }
}

void sendSecurityStatus(bool contact1Alarm, bool contact2Alarm, bool force) {
    char json[128];
    snprintf(json, sizeof(json),
        "{\"type\":\"security\",\"alarm\":%s,\"contact1\":%s,\"contact2\":%s}",
        (contact1Alarm || contact2Alarm) ? "true" : "false",
        contact1Alarm ? "true" : "false",
        contact2Alarm ? "true" : "false");
    
    Serial.printf("[КОНЦЕВИКИ] %s: %s\n", force ? "ПЛАНОВО" : "СРОЧНО", json);
    sendJsonToHub(json);
}

// ===================== КОМАНДЫ =====================
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    uint8_t hubMac[] = {0x9C, 0x9C, 0x1F, 0xC7, 0x2D, 0x94};
    
    if (memcmp(mac_addr, hubMac, 6) != 0) {
        return;
    }

    memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));
    Serial.printf("[ПРИНЯТО] %s\n", incomingMessage.json);
    
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, incomingMessage.json);
    
    if (error) return;
    
    const char* type = doc["type"];
    if (strcmp(type, "command") == 0) {
        const char* cmd = doc["command"];
        
        if (strcmp(cmd, "LED_ON") == 0) {
            digitalWrite(LED_PIN, LOW);
            sendJsonToHub("{\"type\":\"ack\",\"command\":\"LED_ON\"}");
            sendGpioStatus();
        }
        else if (strcmp(cmd, "LED_OFF") == 0) {
            digitalWrite(LED_PIN, HIGH);
            sendJsonToHub("{\"type\":\"ack\",\"command\":\"LED_OFF\"}");
            sendGpioStatus();
        }
        else if (strcmp(cmd, "GET_STATUS") == 0) {
            readAndSendSensorData();
            sendGpioStatus();
            sendSecurityStatus(currentContact1, currentContact2, true);
            if (hasAS5600 && magnetDetected) {
                sendEncoderData(lastAngleDeg);
            }
        }
    }
}

void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    // Не используется
}