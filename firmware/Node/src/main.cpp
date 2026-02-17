/**
 * SmartHome ESP-NOW Узел (ESP32-C3) с охраной
 * Универсальная версия с JSON структурой и концевиками
 * ВЕРСИЯ 2.2: Эмуляция датчиков (без реального I2C)
 * 
 * ========== ДЛЯ ВЫБОРА НУЖНОГО УЗЛА ==========
 * Раскомментируйте ОДИН из блоков ниже и закомментируйте остальные
 */

// ===== БЛОК ДЛЯ УЗЛА #103 =====
// #define NODE_ID 103
// #define NODE_MAC_STR "88:56:A6:7D:09:64"
// ===============================

// ===== БЛОК ДЛЯ УЗЛА #104 =====
#define NODE_ID 104
#define NODE_MAC_STR "10:00:3B:B1:A6:9C"
// ===============================

// ===== БЛОК ДЛЯ УЗЛА #105 =====
// #define NODE_ID 105
// #define NODE_MAC_STR "88:56:A6:7C:F2:A8"
// ===============================

#include <Arduino.h>          // Основная библиотека Arduino
#include <WiFi.h>             // Wi-Fi библиотека
#include <esp_now.h>          // ESP-NOW библиотека
#include <ArduinoJson.h>      // Библиотека для работы с JSON

// ---- КОНСТАНТЫ ----
#define LED_PIN 8             // GPIO для встроенного LED (активный LOW)
#define CONTACT1_PIN 3        // GPIO для концевика 1 (НОРМАЛЬНО ЗАМКНУТ)
#define CONTACT2_PIN 4        // GPIO для концевика 2 (НОРМАЛЬНО ЗАМКНУТ)
#define SENSOR_READ_INTERVAL 30000     // Интервал чтения датчиков (30 сек)
#define SECURITY_CHECK_INTERVAL 2000   // Интервал проверки концевиков (2 сек)

// ---- УНИВЕРСАЛЬНАЯ СТРУКТУРА ESP-NOW ----
typedef struct esp_now_message {
    char json[192];           // JSON строка с данными (макс 192 байта)
    uint8_t sender_id;        // ID отправителя (номер узла)
} esp_now_message;

// ---- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ----
esp_now_message incomingMessage;    // Буфер для входящих сообщений
esp_now_message outgoingMessage;    // Буфер для исходящих сообщений

unsigned long lastSensorReadTime = 0;     // Время последнего чтения датчиков
unsigned long lastSecurityCheck = 0;      // Время последней проверки концевиков

bool lastContact1Alarm = false;   // Предыдущее состояние концевика 1
bool lastContact2Alarm = false;   // Предыдущее состояние концевика 2
// false = норма (замкнут), true = тревога (разомкнут)

// MAC адрес хаба (нужно заменить на реальный MAC вашего хаба)
uint8_t hubMacAddress[] = {0x9C, 0x9C, 0x1F, 0xC7, 0x2D, 0x94};

// ---- ПРОТОТИПЫ ФУНКЦИЙ ----
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void sendJsonToHub(const char* json_string);
void readAndSendSensorData();
void sendGpioStatus();
void checkSecuritySensors();
void sendSecurityStatus(bool contact1Alarm, bool contact2Alarm);

// ===================== ФУНКЦИЯ SETUP =====================
// Выполняется один раз при старте узла
void setup() {
    // Инициализация последовательного порта для отладки
    Serial.begin(115200);
    delay(3000);  // Ждём 3 секунды для стабилизации

    // Вывод информации о узле в монитор порта (ИСПРАВЛЕНО)
    Serial.print("\n=== УЗЕЛ ESP-NOW #");
    Serial.print(NODE_ID);
    Serial.println(" (JSON версия с охраной, эмуляция датчиков) ===");
    
    Serial.print("MAC: ");
    Serial.print(NODE_MAC_STR);
    Serial.print(" | ID: ");
    Serial.println(NODE_ID);
    
    Serial.println("Концевики: GPIO3 и GPIO4 (тревога при РАЗРЫВЕ цепи)");
    Serial.println("Режим: ЭМУЛЯЦИЯ ДАТЧИКОВ (без I2C)");

    // Настройка пинов
    pinMode(LED_PIN, OUTPUT);           // LED как выход
    digitalWrite(LED_PIN, HIGH);        // Выключаем LED (HIGH = выключен)
    
    // Инициализация концевиков (INPUT_PULLUP - нормально замкнутая цепь)
    // Когда цепь замкнута - читаем LOW, когда разомкнута - читаем HIGH
    pinMode(CONTACT1_PIN, INPUT_PULLUP);
    pinMode(CONTACT2_PIN, INPUT_PULLUP);
    Serial.println("[0] Концевики инициализированы (INPUT_PULLUP, нормально-замкнутые)");

    // Настройка Wi-Fi в режиме STA (станция)
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);  // Устанавливаем мощность передатчика
    Serial.print("[1] MAC узла: ");
    Serial.println(WiFi.macAddress());    // Выводим реальный MAC узла

    // Инициализация ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ОШИБКА] Инициализация ESP-NOW!");
        while(1);  // Останавливаем выполнение при ошибке
    }
    Serial.println("[2] ESP-NOW инициализирован.");

    // Регистрация callback-функций
    esp_now_register_recv_cb(onEspNowDataRecv);  // На получение данных
    esp_now_register_send_cb(onEspNowDataSent);  // На отправку данных

    // Добавление хаба как пира (собеседника)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, hubMacAddress, 6);  // Копируем MAC хаба
    peerInfo.channel = 0;                           // Используем текущий канал Wi-Fi
    peerInfo.encrypt = false;                       // Без шифрования
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ОШИБКА] Не удалось добавить хаб!");
    } else {
        Serial.println("[3] Хаб добавлен как пир.");
    }

    // Чтение начального состояния концевиков
    lastContact1Alarm = (digitalRead(CONTACT1_PIN) == HIGH);
    lastContact2Alarm = (digitalRead(CONTACT2_PIN) == HIGH);
    
    // Вывод начального состояния охраны
    Serial.print("[ОХРАНА] Начальное состояние: ");
    Serial.print("Концевик1=");
    Serial.print(lastContact1Alarm ? "ТРЕВОГА (разомкнут)" : "НОРМА (замкнут)");
    Serial.print(", Концевик2=");
    Serial.println(lastContact2Alarm ? "ТРЕВОГА (разомкнут)" : "НОРМА (замкнут)");

    // Отправка начального статуса охраны на хаб
    sendSecurityStatus(lastContact1Alarm, lastContact2Alarm);
    
    // ВАЖНО! Отправка начального статуса LED, чтобы кнопка в веб-интерфейсе стала активной
    sendGpioStatus();

    // Финальное сообщение о готовности (ИСПРАВЛЕНО)
    Serial.print("\n=== УЗЕЛ #");
    Serial.print(NODE_ID);
    Serial.println(" ГОТОВ К РАБОТЕ (ЭМУЛЯЦИЯ) ===\n");
    
    // Первое чтение эмулированных данных датчиков
    readAndSendSensorData();
    lastSensorReadTime = millis();    // Запоминаем время первого чтения
    lastSecurityCheck = millis();     // Запоминаем время первой проверки
}

// ===================== ФУНКЦИЯ LOOP =====================
// Выполняется бесконечно в цикле
void loop() {
    unsigned long now = millis();  // Текущее время в миллисекундах
    
    // Эмуляция датчиков каждые 30 секунд
    if (now - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        readAndSendSensorData();           // Читаем и отправляем данные
        lastSensorReadTime = now;          // Обновляем время последнего чтения
    }
    
    // Проверка концевиков каждые 2 секунды
    if (now - lastSecurityCheck >= SECURITY_CHECK_INTERVAL) {
        checkSecuritySensors();            // Проверяем состояние концевиков
        lastSecurityCheck = now;           // Обновляем время последней проверки
    }
    
    delay(100);  // Небольшая задержка для снижения нагрузки на процессор
}

// ===================== ЭМУЛЯЦИЯ ДАТЧИКОВ =====================
// Функция генерирует случайные значения датчиков и отправляет их на хаб
void readAndSendSensorData() {
    // Генерируем случайные, но правдоподобные значения
    // random(0, 200) даёт число от 0 до 199, делим на 10 для получения десятых долей
    float temp_aht = 20.0 + (random(0, 200) / 10.0);      // 20.0 - 40.0 °C
    float hum_aht = 40.0 + (random(0, 400) / 10.0);       // 40.0 - 80.0 %
    float temp_bmp = temp_aht - 1.0 + (random(-50, 50) / 10.0); // чуть холоднее основного
    float press_mmHg = 740.0 + (random(-50, 50) / 10.0);   // 735 - 745 мм рт. ст.

    // Формируем JSON строку с данными
    char json[192];
    snprintf(json, sizeof(json),
        "{\"type\":\"sensor\",\"data\":{\"AHT20\":{\"temp\":%.1f,\"hum\":%.1f},\"BMP280\":{\"temp\":%.1f,\"press_mmHg\":%.1f}}}",
        temp_aht, hum_aht, temp_bmp, press_mmHg);

    // Выводим в монитор порта для отладки
    Serial.print("[ЭМУЛЯЦИЯ] Отправка: ");
    Serial.println(json);
    
    // Отправляем JSON на хаб
    sendJsonToHub(json);
}

// ===================== ФУНКЦИИ ОТПРАВКИ =====================

// Универсальная функция отправки JSON на хаб
void sendJsonToHub(const char* json_string) {
    // Проверяем длину JSON (не должна превышать размер буфера)
    size_t json_len = strlen(json_string);
    if (json_len >= sizeof(outgoingMessage.json)) {
        Serial.printf("[ОШИБКА] JSON слишком длинный (%d байт). Максимум: %d\n", 
                     json_len, sizeof(outgoingMessage.json)-1);
        return;
    }
    
    // Копируем JSON в структуру для отправки
    strncpy(outgoingMessage.json, json_string, sizeof(outgoingMessage.json)-1);
    outgoingMessage.json[sizeof(outgoingMessage.json)-1] = '\0';  // Гарантируем завершающий ноль
    outgoingMessage.sender_id = NODE_ID;  // Указываем ID отправителя
    
    // Отправляем данные через ESP-NOW
    esp_err_t result = esp_now_send(hubMacAddress, (uint8_t *) &outgoingMessage, sizeof(outgoingMessage));
    
    // Проверяем результат отправки
    if (result == ESP_OK) {
        Serial.println("[УСПЕХ] JSON отправлен на хаб.");
    } else {
        Serial.printf("[ОШИБКА] Отправки: %d\n", result);
    }
}

// Функция отправки статуса GPIO (состояние LED)
void sendGpioStatus() {
    char json[64];
    // Формируем JSON с состоянием пина 8
    // digitalRead(LED_PIN) == LOW ? 1 : 0 - LED горит при LOW, поэтому инвертируем для логики
    snprintf(json, sizeof(json),
        "{\"type\":\"gpio\",\"pin\":8,\"state\":%d}",
        digitalRead(LED_PIN) == LOW ? 1 : 0);
    
    Serial.print("[GPIO] Отправка: ");
    Serial.println(json);
    sendJsonToHub(json);
}

// ===================== ФУНКЦИИ ОХРАНЫ =====================

// Проверка состояния концевиков и отправка изменений
void checkSecuritySensors() {
    // Читаем текущее состояние концевиков
    bool currentContact1Alarm = (digitalRead(CONTACT1_PIN) == HIGH);
    bool currentContact2Alarm = (digitalRead(CONTACT2_PIN) == HIGH);
    
    // Если состояние изменилось с прошлого раза
    if (currentContact1Alarm != lastContact1Alarm || currentContact2Alarm != lastContact2Alarm) {
        Serial.print("[ОХРАНА] Изменение: ");
        Serial.print("Концевик1=");
        Serial.print(currentContact1Alarm ? "ТРЕВОГА" : "НОРМА");
        Serial.print(", Концевик2=");
        Serial.print(currentContact2Alarm ? "ТРЕВОГА" : "НОРМА");
        Serial.println(" | Отправка на хаб...");
        
        // Отправляем новый статус на хаб
        sendSecurityStatus(currentContact1Alarm, currentContact2Alarm);
        
        // Обновляем сохранённые значения
        lastContact1Alarm = currentContact1Alarm;
        lastContact2Alarm = currentContact2Alarm;
    }
}

// Функция отправки статуса охраны на хаб
void sendSecurityStatus(bool contact1Alarm, bool contact2Alarm) {
    char json[128];
    // Формируем JSON с состояниями концевиков
    // alarm = true если хотя бы один концевик в тревоге
    snprintf(json, sizeof(json),
        "{\"type\":\"security\",\"alarm\":%s,\"contact1\":%s,\"contact2\":%s}",
        (contact1Alarm || contact2Alarm) ? "true" : "false",
        contact1Alarm ? "true" : "false",
        contact2Alarm ? "true" : "false");
    
    Serial.print("[ОХРАНА] Отправка: ");
    Serial.println(json);
    sendJsonToHub(json);
}

// ===================== ОБРАБОТКА КОМАНД ОТ ХАБА =====================

// Callback-функция при получении данных
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    // Проверяем, что отправитель - наш хаб
    uint8_t hubMac[] = {0x9C, 0x9C, 0x1F, 0xC7, 0x2D, 0x94};
    if (memcmp(mac_addr, hubMac, 6) != 0) {
        // Если MAC не совпадает с хабом - игнорируем
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        Serial.print("[УЗЕЛ] Игнорирую постороннее: ");
        Serial.println(macStr);
        return;
    }

    // Копируем полученные данные в буфер
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
    
    // Получаем тип сообщения
    const char* type = doc["type"];
    
    // Если это команда
    if (strcmp(type, "command") == 0) {
        const char* cmd = doc["command"];
        
        // Команда включения LED
        if (strcmp(cmd, "LED_ON") == 0) {
            digitalWrite(LED_PIN, LOW);   // LOW = включено (для ESP32-C3)
            Serial.println("  -> 💡 LED ВКЛЮЧЁН");
            // Отправляем подтверждение
            sendJsonToHub("{\"type\":\"ack\",\"command\":\"LED_ON\",\"status\":\"success\"}");
            // Отправляем обновлённый статус GPIO
            sendGpioStatus();
        }
        // Команда выключения LED
        else if (strcmp(cmd, "LED_OFF") == 0) {
            digitalWrite(LED_PIN, HIGH);  // HIGH = выключено
            Serial.println("  -> 💡 LED ВЫКЛЮЧЕН");
            // Отправляем подтверждение
            sendJsonToHub("{\"type\":\"ack\",\"command\":\"LED_OFF\",\"status\":\"success\"}");
            // Отправляем обновлённый статус GPIO
            sendGpioStatus();
        }
        // Команда запроса статуса
        else if (strcmp(cmd, "GET_STATUS") == 0) {
            Serial.println("  -> 📡 Запрос данных...");
            // Отправляем все данные
            readAndSendSensorData();                    // Данные датчиков
            sendGpioStatus();                           // Статус LED
            
            // Отправляем состояние охраны
            bool contact1Alarm = (digitalRead(CONTACT1_PIN) == HIGH);
            bool contact2Alarm = (digitalRead(CONTACT2_PIN) == HIGH);
            sendSecurityStatus(contact1Alarm, contact2Alarm);
        }
    }
}

// Callback-функция при отправке данных (подтверждение доставки)
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("⚠️ Подтверждение не доставлено.");
    }
}