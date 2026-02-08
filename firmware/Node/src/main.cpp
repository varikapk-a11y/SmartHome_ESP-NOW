/**
 * SmartHome ESP-NOW Узел (ESP32-C3) с охраной
 * Универсальная версия с JSON структурой и концевиками
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
#define SENSOR_READ_INTERVAL 30000 // 30 сек
#define SECURITY_CHECK_INTERVAL 2000 // 2 сек - проверка концевиков

// I2C пины для ESP32-C3
const int SDA_PIN = 1;
const int SCL_PIN = 0;

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
esp_now_message incomingMessage;
esp_now_message outgoingMessage;
unsigned long lastSensorReadTime = 0;
unsigned long lastSecurityCheck = 0;
bool lastContact1Alarm = false;   // false = норма (замкнут), true = тревога (разомкнут)
bool lastContact2Alarm = false;   // false = норма (замкнут), true = тревога (разомкнут)

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

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n=== УЗЕЛ ESP-NOW (JSON версия с охраной) ===");
    Serial.println("MAC: AC:EB:E6:49:10:28 | ID: 101");
    Serial.println("Концевики: GPIO3 и GPIO4 (тревога при РАЗРЫВЕ цепи)");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // Инициализация концевиков (INPUT_PULLUP - нормально замкнутая цепь)
    // При замыкании на GND: пин = LOW = норма
    // При разрыве цепи: пин = HIGH = тревога (через внутреннюю подтяжку)
    pinMode(CONTACT1_PIN, INPUT_PULLUP);
    pinMode(CONTACT2_PIN, INPUT_PULLUP);
    Serial.println("[0] Концевики инициализированы (INPUT_PULLUP, нормально-замкнутые)");

    // I2C
    Wire.begin(SDA_PIN, SCL_PIN);
    Serial.println("[1] I2C инициализирован.");

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

    // Первоначальная проверка концевиков
    // При замыкании на GND: digitalRead() = LOW (0) = норма
    // При разрыве цепи: digitalRead() = HIGH (1) = тревога
    lastContact1Alarm = (digitalRead(CONTACT1_PIN) == HIGH);
    lastContact2Alarm = (digitalRead(CONTACT2_PIN) == HIGH);
    
    Serial.print("[ОХРАНА] Начальное состояние: ");
    Serial.print("Концевик1=");
    Serial.print(lastContact1Alarm ? "ТРЕВОГА (разомкнут)" : "НОРМА (замкнут)");
    Serial.print(", Концевик2=");
    Serial.println(lastContact2Alarm ? "ТРЕВОГА (разомкнут)" : "НОРМА (замкнут)");

    // Отправка начального статуса на хаб
    sendSecurityStatus(lastContact1Alarm, lastContact2Alarm);

    Serial.println("\n=== УЗЕЛ ГОТОВ К РАБОТЕ ===\n");
    readAndSendSensorData();
    lastSensorReadTime = millis();
    lastSecurityCheck = millis();
}

// ===================== LOOP =====================
void loop() {
    unsigned long now = millis();
    
    // Проверка датчиков каждые 30 секунд
    if (now - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        readAndSendSensorData();
        lastSensorReadTime = now;
    }
    
    // Проверка концевиков каждые 2 секунды
    if (now - lastSecurityCheck >= SECURITY_CHECK_INTERVAL) {
        checkSecuritySensors();
        lastSecurityCheck = now;
    }
    
    delay(100);
}

// ===================== ФУНКЦИИ =====================
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

// ОТПРАВКА ЛЮБОГО JSON НА ХАБ
void sendJsonToHub(const char* json_string) {
    size_t json_len = strlen(json_string);
    if (json_len >= sizeof(outgoingMessage.json)) {
        Serial.printf("[ОШИБКА] JSON слишком длинный (%d байт). Максимум: %d\n", 
                     json_len, sizeof(outgoingMessage.json)-1);
        return;
    }
    
    strncpy(outgoingMessage.json, json_string, sizeof(outgoingMessage.json)-1);
    outgoingMessage.json[sizeof(outgoingMessage.json)-1] = '\0';
    outgoingMessage.sender_id = NODE_ID;
    
    esp_err_t result = esp_now_send(hubMacAddress, (uint8_t *) &outgoingMessage, sizeof(outgoingMessage));
    if (result == ESP_OK) {
        Serial.println("[УСПЕХ] JSON отправлен на хаб.");
    } else {
        Serial.printf("[ОШИБКА] Отправки: %d\n", result);
    }
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

    Serial.print("[ДАННЫЕ] Отправка: ");
    Serial.println(json);
    sendJsonToHub(json);
}

// ОТПРАВКА СОСТОЯНИЯ GPIO (LED)
void sendGpioStatus() {
    char json[64];
    snprintf(json, sizeof(json),
        "{\"type\":\"gpio\",\"pin\":8,\"state\":%d}",
        digitalRead(LED_PIN) == LOW ? 1 : 0);
    
    Serial.print("[GPIO] Отправка: ");
    Serial.println(json);
    sendJsonToHub(json);
}

// ПРОВЕРКА СОСТОЯНИЯ КОНЦЕВИКОВ
void checkSecuritySensors() {
    // С PULLUP: LOW = цепь замкнута (норма), HIGH = цепь разорвана (тревога)
    bool currentContact1Alarm = (digitalRead(CONTACT1_PIN) == HIGH);
    bool currentContact2Alarm = (digitalRead(CONTACT2_PIN) == HIGH);
    
    // Если состояние изменилось - отправляем уведомление
    if (currentContact1Alarm != lastContact1Alarm || currentContact2Alarm != lastContact2Alarm) {
        Serial.print("[ОХРАНА] Изменение: ");
        Serial.print("Концевик1=");
        Serial.print(currentContact1Alarm ? "ТРЕВОГА (разомкнут)" : "НОРМА (замкнут)");
        Serial.print(", Концевик2=");
        Serial.print(currentContact2Alarm ? "ТРЕВОГА (разомкнут)" : "НОРМА (замкнут)");
        Serial.println(" | Отправка на хаб...");
        
        sendSecurityStatus(currentContact1Alarm, currentContact2Alarm);
        
        lastContact1Alarm = currentContact1Alarm;
        lastContact2Alarm = currentContact2Alarm;
    }
}

// ОТПРАВКА СТАТУСА ОХРАНЫ
void sendSecurityStatus(bool contact1Alarm, bool contact2Alarm) {
    char json[128];
    snprintf(json, sizeof(json),
        "{\"type\":\"security\",\"alarm\":%s,\"contact1\":%s,\"contact2\":%s}",
        (contact1Alarm || contact2Alarm) ? "true" : "false",
        contact1Alarm ? "true" : "false",
        contact2Alarm ? "true" : "false");
    
    Serial.print("[ОХРАНА] Отправка: ");
    Serial.println(json);
    sendJsonToHub(json);
}

// ОБРАБОТКА ВХОДЯЩИХ КОМАНД
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    // --- ФИЛЬТР: Принимаем данные ТОЛЬКО от нашего хаба ---
    uint8_t hubMac[] = {0x9C, 0x9C, 0x1F, 0xC7, 0x2D, 0x94};
    if (memcmp(mac_addr, hubMac, 6) != 0) {
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        Serial.print("[УЗЕЛ] Игнорирую постороннее: ");
        Serial.println(macStr);
        return;
    }
    // --- КОНЕЦ ФИЛЬТРА ---

    memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));
    
    Serial.print("📥 JSON от хаба: ");
    Serial.println(incomingMessage.json);
    
    // Парсим JSON
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, incomingMessage.json);
    
    if (error) {
        Serial.print("❌ Ошибка парсинга JSON: ");
        Serial.println(error.c_str());
        return;
    }
    
    const char* type = doc["type"];
    if (strcmp(type, "command") == 0) {
        const char* cmd = doc["command"];
        if (strcmp(cmd, "LED_ON") == 0) {
            digitalWrite(LED_PIN, LOW);
            Serial.println("  -> 💡 LED ВКЛЮЧЁН");
            sendJsonToHub("{\"type\":\"ack\",\"command\":\"LED_ON\",\"status\":\"success\"}");
            sendGpioStatus();
        }
        else if (strcmp(cmd, "LED_OFF") == 0) {
            digitalWrite(LED_PIN, HIGH);
            Serial.println("  -> 💡 LED ВЫКЛЮЧЕН");
            sendJsonToHub("{\"type\":\"ack\",\"command\":\"LED_OFF\",\"status\":\"success\"}");
            sendGpioStatus();
        }
        else if (strcmp(cmd, "GET_STATUS") == 0) {
            Serial.println("  -> 📡 Запрос данных...");
            readAndSendSensorData();
            sendGpioStatus();
            // При запросе статуса также отправляем состояние охраны
            bool contact1Alarm = (digitalRead(CONTACT1_PIN) == HIGH);
            bool contact2Alarm = (digitalRead(CONTACT2_PIN) == HIGH);
            sendSecurityStatus(contact1Alarm, contact2Alarm);
        }
    }
}

void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("⚠️ Подтверждение не доставлено.");
    }
}