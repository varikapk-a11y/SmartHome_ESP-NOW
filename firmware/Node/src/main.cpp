/**
 * SmartHome ESP-NOW Узел (ESP32-C3) с охраной и энкодером
 * Универсальная версия с JSON структурой и концевиками
 * ВЕРСИЯ 2.3: Концевики раз в секунду, статус раз в минуту
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
#define SECURITY_CHECK_INTERVAL 1000    // 1 сек - проверка концевиков
#define SECURITY_STATUS_INTERVAL 60000  // 60 сек - отправка статуса "всё ОК"
#define ENCODER_READ_INTERVAL 1000      // 1 сек - чтение энкодера

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
bool hasAS5600 = false;  // Флаг наличия энкодера

esp_now_message incomingMessage;
esp_now_message outgoingMessage;

unsigned long lastSensorReadTime = 0;
unsigned long lastSecurityCheck = 0;
unsigned long lastSecurityStatusTime = 0;
unsigned long lastEncoderReadTime = 0;

bool currentContact1 = false;   // false = норма (замкнут), true = тревога (разомкнут)
bool currentContact2 = false;
bool lastSentContact1 = false;
bool lastSentContact2 = false;

// Буфер для чтения AS5600
uint8_t angle_data[2];
uint16_t lastRawAngle = 0;        // Последний прочитанный угол
float lastAngleDeg = 0.0;         // Последний угол в градусах
bool magnetDetected = false;      // Флаг наличия магнита

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
void sendSecurityStatus(bool contact1Alarm, bool contact2Alarm);
void initAS5600();
uint16_t readRawAngle();
void readAndSendEncoderData();

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n=== УЗЕЛ ESP-NOW (JSON версия с охраной + AS5600) ===");
    Serial.println("MAC: AC:EB:E6:49:10:28 | ID: 101");
    Serial.println("Концевики: GPIO3 и GPIO4 (тревога при РАЗРЫВЕ цепи)");
    Serial.println("Энкодер: AS5600 на I2C (SDA=1, SCL=0), шаг 5 градусов");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // Инициализация концевиков (INPUT_PULLUP - нормально замкнутая цепь)
    pinMode(CONTACT1_PIN, INPUT_PULLUP);
    pinMode(CONTACT2_PIN, INPUT_PULLUP);
    Serial.println("[0] Концевики инициализированы (INPUT_PULLUP, нормально-замкнутые)");

    // I2C
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);
    Serial.println("[1] I2C инициализирован (100 кГц).");

    // Датчики
    Serial.println("[2] Инициализация датчиков...");
    initSensors();

    // Wi-Fi и ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    Serial.print("[3] MAC узла: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ОШИБКА] Инициализация ESP-NOW!");
        while(1);
    }
    Serial.println("[4] ESP-NOW инициализирован.");

    // Регистрация колбэков
    esp_now_register_recv_cb(onEspNowDataRecv);
    esp_now_register_send_cb(onEspNowDataSent);

    // Добавление хаба как пира
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, hubMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ОШИБКА] Не удалось добавить хаб!");
    } else {
        Serial.println("[5] Хаб добавлен как пир.");
    }

    // Чтение начального состояния концевиков
    currentContact1 = (digitalRead(CONTACT1_PIN) == HIGH);
    currentContact2 = (digitalRead(CONTACT2_PIN) == HIGH);
    lastSentContact1 = currentContact1;
    lastSentContact2 = currentContact2;
    
    Serial.print("[ОХРАНА] Начальное состояние: ");
    Serial.print("Концевик1=");
    Serial.print(currentContact1 ? "ТРЕВОГА (разомкнут)" : "НОРМА (замкнут)");
    Serial.print(", Концевик2=");
    Serial.println(currentContact2 ? "ТРЕВОГА (разомкнут)" : "НОРМА (замкнут)");

    // Инициализация AS5600
    initAS5600();

    // Отправка начального статуса на хаб
    sendSecurityStatus(currentContact1, currentContact2);
    
    // Первое чтение энкодера
    if (hasAS5600) {
        readAndSendEncoderData();
        lastEncoderReadTime = millis();
    }

    Serial.println("\n=== УЗЕЛ ГОТОВ К РАБОТЕ ===\n");
    readAndSendSensorData();
    lastSensorReadTime = millis();
    lastSecurityCheck = millis();
    lastSecurityStatusTime = millis();
}

// ===================== LOOP =====================
void loop() {
    unsigned long now = millis();
    
    // Проверка датчиков каждые 30 секунд
    if (now - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        readAndSendSensorData();
        lastSensorReadTime = now;
    }
    
    // Проверка концевиков раз в секунду
    if (now - lastSecurityCheck >= SECURITY_CHECK_INTERVAL) {
        checkSecuritySensors();
        lastSecurityCheck = now;
    }
    
    // Отправка статуса "всё ОК" раз в минуту (если ничего не менялось)
    if (now - lastSecurityStatusTime >= SECURITY_STATUS_INTERVAL) {
        // Отправляем текущее состояние (оно может быть и тревожным, если висит тревога)
        sendSecurityStatus(currentContact1, currentContact2);
        lastSecurityStatusTime = now;
        Serial.println("[ОХРАНА] Плановый статус (раз в минуту)");
    }
    
    // Чтение энкодера раз в секунду (с фильтром по углу)
    if (hasAS5600 && (now - lastEncoderReadTime >= ENCODER_READ_INTERVAL)) {
        readAndSendEncoderData();
        lastEncoderReadTime = now;
    }
    
    delay(10);
}

// ===================== ФУНКЦИИ ДАТЧИКОВ =====================
bool initSensors() {
    Serial.println("[DEBUG] Инициализация датчиков...");
    bool ok = false;
    
    // BMP280
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
    
    // AHT20
    if (aht.begin()) {
        hasAHT = true;
        Serial.println("  -> ✅ AHT20 найден");
        ok = true;
    } else {
        Serial.println("  -> ❌ AHT20 не найден");
    }
    
    return ok;
}

// ===================== ФУНКЦИИ AS5600 =====================
void initAS5600() {
    Serial.println("[DEBUG] Инициализация AS5600...");
    Serial.print("[AS5600] Проверка... ");
    
    Wire.beginTransmission(AS5600_ADDR);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
        hasAS5600 = true;
        Serial.println("✅ Датчик обнаружен по адресу 0x36");
        
        // Проверка магнита
        Wire.beginTransmission(AS5600_ADDR);
        Wire.write(STATUS_REG);
        Wire.endTransmission(false);
        Wire.requestFrom(AS5600_ADDR, 1);
        
        if (Wire.available()) {
            byte status = Wire.read();
            Serial.print("   Статус: 0x");
            Serial.print(status, HEX);
            
            if (status & 0x20) {
                magnetDetected = true;
                Serial.println(" | ✅ Магнит обнаружен");
            } else {
                magnetDetected = false;
                Serial.println(" | ❌ Магнит НЕ обнаружен");
            }
        }
        
        // Первое чтение угла
        lastRawAngle = readRawAngle();
        lastAngleDeg = (lastRawAngle * 360.0) / 4096.0;
        Serial.printf("   Начальный угол: %.1f° (%d)\n", lastAngleDeg, lastRawAngle);
        
    } else {
        hasAS5600 = false;
        Serial.printf("❌ Датчик НЕ найден (ошибка: %d)\n", error);
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

void readAndSendEncoderData() {
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
    
    // Отправляем только если изменилось состояние магнита ИЛИ угол изменился более чем на порог
    bool angleChanged = (fabs(angle_deg - lastAngleDeg) >= ENCODER_CHANGE_THRESHOLD);
    bool magnetChanged = (magnet_now != magnetDetected);
    
    if (angleChanged || magnetChanged) {
        char json[128];
        snprintf(json, sizeof(json),
            "{\"type\":\"encoder\",\"angle\":%.1f,\"raw\":%d,\"magnet\":%s}",
            angle_deg,
            raw_angle,
            magnet_now ? "true" : "false");
        
        sendJsonToHub(json);
        
        lastRawAngle = raw_angle;
        lastAngleDeg = angle_deg;
        magnetDetected = magnet_now;
    }
}

// ===================== ФУНКЦИИ ОТПРАВКИ =====================
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

void readAndSendSensorData() {
    float temp_bmp = NAN, press_hPa = NAN, press_mmHg = NAN;
    float temp_aht = NAN, hum_aht = NAN;

    if (hasBMP) {
        temp_bmp = bmp.readTemperature();
        press_hPa = bmp.readPressure() / 100.0F;
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
    char json[64];
    snprintf(json, sizeof(json),
        "{\"type\":\"gpio\",\"pin\":8,\"state\":%d}",
        digitalRead(LED_PIN) == LOW ? 1 : 0);
    
    sendJsonToHub(json);
}

// ===================== ФУНКЦИИ ОХРАНЫ =====================
void checkSecuritySensors() {
    // Читаем текущее состояние концевиков
    bool newContact1 = (digitalRead(CONTACT1_PIN) == HIGH);
    bool newContact2 = (digitalRead(CONTACT2_PIN) == HIGH);
    
    // Обновляем текущее состояние
    currentContact1 = newContact1;
    currentContact2 = newContact2;
    
    // Если состояние изменилось относительно последней отправки - отправляем
    if (newContact1 != lastSentContact1 || newContact2 != lastSentContact2) {
        Serial.print("[ОХРАНА] Изменение состояния: ");
        Serial.print("C1=");
        Serial.print(newContact1 ? "ТРЕВОГА" : "НОРМА");
        Serial.print(", C2=");
        Serial.println(newContact2 ? "ТРЕВОГА" : "НОРМА");
        
        sendSecurityStatus(newContact1, newContact2);
        
        // Запоминаем отправленное состояние
        lastSentContact1 = newContact1;
        lastSentContact2 = newContact2;
        
        // Сбрасываем таймер плановой отправки
        lastSecurityStatusTime = millis();
    }
}

void sendSecurityStatus(bool contact1Alarm, bool contact2Alarm) {
    char json[128];
    snprintf(json, sizeof(json),
        "{\"type\":\"security\",\"alarm\":%s,\"contact1\":%s,\"contact2\":%s}",
        (contact1Alarm || contact2Alarm) ? "true" : "false",
        contact1Alarm ? "true" : "false",
        contact2Alarm ? "true" : "false");
    
    sendJsonToHub(json);
}

// ===================== ОБРАБОТКА КОМАНД =====================
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    uint8_t hubMac[] = {0x9C, 0x9C, 0x1F, 0xC7, 0x2D, 0x94};
    if (memcmp(mac_addr, hubMac, 6) != 0) {
        return;
    }

    memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));
    
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, incomingMessage.json);
    
    if (error) {
        return;
    }
    
    const char* type = doc["type"];
    if (strcmp(type, "command") == 0) {
        const char* cmd = doc["command"];
        if (strcmp(cmd, "LED_ON") == 0) {
            digitalWrite(LED_PIN, LOW);
            sendJsonToHub("{\"type\":\"ack\",\"command\":\"LED_ON\",\"status\":\"success\"}");
            sendGpioStatus();
        }
        else if (strcmp(cmd, "LED_OFF") == 0) {
            digitalWrite(LED_PIN, HIGH);
            sendJsonToHub("{\"type\":\"ack\",\"command\":\"LED_OFF\",\"status\":\"success\"}");
            sendGpioStatus();
        }
        else if (strcmp(cmd, "GET_STATUS") == 0) {
            readAndSendSensorData();
            sendGpioStatus();
            sendSecurityStatus(currentContact1, currentContact2);
            
            if (hasAS5600) {
                readAndSendEncoderData();
            }
        }
    }
}

void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}