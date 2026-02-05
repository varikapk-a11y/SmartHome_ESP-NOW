#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>

// ========== ВЫБОР И НАСТРОЙКА ДАТЧИКОВ ==========
// Раскомментируйте строку с вашим датчиком (только один!)
// #define USE_BME280
// #define USE_BMP280
#define USE_AHT20

#if defined(USE_BME280)
    #include <Adafruit_BME280.h>
    Adafruit_BME280 bme;
    #define SENSOR_TYPE "BME280"
#elif defined(USE_BMP280)
    #include <Adafruit_BMP280.h>
    Adafruit_BMP280 bmp;
    #define SENSOR_TYPE "BMP280"
#elif defined(USE_AHT20)
    #include <Adafruit_AHTX0.h>
    Adafruit_AHTX0 aht;
    #define SENSOR_TYPE "AHT20"
#endif

// ========== КОНСТАНТЫ И НАСТРОЙКИ ==========
#define NODE_ID 101
#define LED_PIN 8
#define SENSOR_READ_INTERVAL 60000  // Интервал чтения датчика (60 сек)

[env:esp32c3]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
...
build_flags =
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=1
...
monitor_speed = 115200


// Структура для ESP-NOW сообщений (увеличен размер для JSON)
typedef struct esp_now_message {
    char payload[256];    // JSON данные с показаниями
    uint8_t sender_id;
    char msg_type[16];    // "command", "sensor_data", "ack"
} esp_now_message;

// ========== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ==========
esp_now_message incomingMessage;
esp_now_message outgoingMessage;
unsigned long lastSensorReadTime = 0;

// ЗАМЕНИТЕ НА РЕАЛЬНЫЙ MAC-АДРЕС ВАШЕГО ХАБА!
uint8_t hubMacAddress[] = {0x9C, 0x9C, 0x1F, 0xC7, 0x2D, 0x94};

// ========== ПРОТОТИПЫ ФУНКЦИЙ ==========
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void sendToHub(const char* msg_type, const char* payload);
void readAndSendSensorData();
float hPaToMmHg(float hPa);
bool initSensor();

// ========== SETUP ==========
void setup() {
    Serial.begin(9600);
    delay(2000);
    
    Serial.println("\n=== ESP-NOW УЗЕЛ С ДАТЧИКАМИ ===");
    Serial.print("Тип датчика: ");
    Serial.println(SENSOR_TYPE);
    
    // Инициализация пинов
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);  // LED выключен (активный LOW)
    
    // Инициализация I2C для датчиков
    Wire.begin(6, 7);  // SDA=GPIO6, SCL=GPIO7 для ESP32-C3
    
    // Инициализация датчика
    if (!initSensor()) {
        Serial.println("ОШИБКА: Датчик не найден!");
        while(1);  // Остановка если датчик не инициализирован
    }
    
    // Вывод MAC-адреса узла
    Serial.print("MAC узла: ");
    Serial.println(WiFi.macAddress());
    
    // Настройка WiFi и ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("ОШИБКА инициализации ESP-NOW");
        while(1);
    }
    
    // Регистрация callback-функций
    esp_now_register_recv_cb(onEspNowDataRecv);
    esp_now_register_send_cb(onEspNowDataSent);
    
    // Добавление хаба как пира
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, hubMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("ОШИБКА добавления пира (хаба)");
        while(1);
    }
    
    Serial.println("Узел готов. Ожидание команд и отправка данных...");
    
    // Первое чтение данных с датчика
    readAndSendSensorData();
    lastSensorReadTime = millis();
}

// ========== LOOP ==========
void loop() {
    unsigned long currentTime = millis();
    
    // Периодическое чтение данных с датчика (раз в 60 сек)
    if (currentTime - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        readAndSendSensorData();
        lastSensorReadTime = currentTime;
    }
    
    delay(1000);
}

// ========== ИНИЦИАЛИЗАЦИЯ ДАТЧИКА ==========
bool initSensor() {
    bool success = false;
    
    #if defined(USE_BME280)
        success = bme.begin(0x76);  // Адрес BME280 (0x76 или 0x77)
        if (success) {
            bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                Adafruit_BME280::SAMPLING_X2,
                Adafruit_BME280::SAMPLING_X16,
                Adafruit_BME280::SAMPLING_X1,
                Adafruit_BME280::FILTER_X16,
                Adafruit_BME280::STANDBY_MS_0_5);
        }
    #elif defined(USE_BMP280)
        success = bmp.begin(0x76);  // Адрес BMP280
        if (success) {
            bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                Adafruit_BMP280::SAMPLING_X2,
                Adafruit_BMP280::SAMPLING_X16,
                Adafruit_BMP280::FILTER_X16,
                Adafruit_BMP280::STANDBY_MS_0_5);
        }
    #elif defined(USE_AHT20)
        success = aht.begin();  // AHT20 использует фиксированный адрес
    #endif
    
    return success;
}

// ========== ЧТЕНИЕ И ОТПРАВКА ДАННЫХ ДАТЧИКА ==========
void readAndSendSensorData() {
    float temperature = NAN;
    float humidity = NAN;
    float pressure_hPa = NAN;
    float pressure_mmHg = NAN;
    
    // Чтение данных в зависимости от типа датчика
    #if defined(USE_BME280)
        temperature = bme.readTemperature();
        humidity = bme.readHumidity();
        pressure_hPa = bme.readPressure() / 100.0F;
        pressure_mmHg = hPaToMmHg(pressure_hPa);
        
    #elif defined(USE_BMP280)
        temperature = bmp.readTemperature();
        pressure_hPa = bmp.readPressure() / 100.0F;
        pressure_mmHg = hPaToMmHg(pressure_hPa);
        // BMP280 не измеряет влажность
        
    #elif defined(USE_AHT20)
        sensors_event_t humidity_event, temp_event;
        aht.getEvent(&humidity_event, &temp_event);
        temperature = temp_event.temperature;
        humidity = humidity_event.relative_humidity;
        // AHT20 не измеряет давление
    #endif
    
    // Формирование JSON строки с показаниями
    char jsonPayload[256];
    
    #if defined(USE_BMP280)
        // Для BMP280 (нет влажности)
        snprintf(jsonPayload, sizeof(jsonPayload),
            "{\"node\":%d,\"sensor\":\"%s\",\"temp\":%.1f,\"press_hPa\":%.1f,\"press_mmHg\":%.1f}",
            NODE_ID, SENSOR_TYPE, temperature, pressure_hPa, pressure_mmHg);
    #elif defined(USE_AHT20)
        // Для AHT20 (нет давления)
        snprintf(jsonPayload, sizeof(jsonPayload),
            "{\"node\":%d,\"sensor\":\"%s\",\"temp\":%.1f,\"hum\":%.1f}",
            NODE_ID, SENSOR_TYPE, temperature, humidity);
    #else
        // Для BME280 (полный набор)
        snprintf(jsonPayload, sizeof(jsonPayload),
            "{\"node\":%d,\"sensor\":\"%s\",\"temp\":%.1f,\"hum\":%.1f,\"press_hPa\":%.1f,\"press_mmHg\":%.1f}",
            NODE_ID, SENSOR_TYPE, temperature, humidity, pressure_hPa, pressure_mmHg);
    #endif
    
    // Отправка данных на хаб
    sendToHub("sensor_data", jsonPayload);
    
    // Вывод в Serial для отладки
    Serial.print("Данные отправлены: ");
    Serial.println(jsonPayload);
}

// ========== КОНВЕРТАЦИЯ hPa В мм рт. ст. ==========
float hPaToMmHg(float hPa) {
    return hPa * 0.750062;  // 1 hPa = 0.750062 мм рт. ст.
}

// ========== ОБРАБОТКА ВХОДЯЩИХ СООБЩЕНИЙ ==========
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));
    
    Serial.print("Получено от хаба: ");
    Serial.print(incomingMessage.msg_type);
    Serial.print(" - ");
    Serial.println(incomingMessage.payload);
    
    // Обработка команд управления LED
    if (strcmp(incomingMessage.msg_type, "command") == 0) {
        if (strcmp(incomingMessage.payload, "LED_ON") == 0) {
            digitalWrite(LED_PIN, LOW);
            Serial.println("LED ВКЛЮЧЕН");
            sendToHub("ack", "ACK_ON");
        } 
        else if (strcmp(incomingMessage.payload, "LED_OFF") == 0) {
            digitalWrite(LED_PIN, HIGH);
            Serial.println("LED ВЫКЛЮЧЕН");
            sendToHub("ack", "ACK_OFF");
        }
        else if (strcmp(incomingMessage.payload, "GET_STATUS") == 0) {
            readAndSendSensorData();  // Немедленная отправка данных
        }
    }
}

// ========== ОТПРАВКА СООБЩЕНИЙ НА ХАБ ==========
void sendToHub(const char* msg_type, const char* payload) {
    // Очистка структуры
    memset(&outgoingMessage, 0, sizeof(outgoingMessage));
    
    // Заполнение данных
    strncpy(outgoingMessage.msg_type, msg_type, sizeof(outgoingMessage.msg_type)-1);
    strncpy(outgoingMessage.payload, payload, sizeof(outgoingMessage.payload)-1);
    outgoingMessage.sender_id = NODE_ID;
    
    // Отправка
    esp_err_t result = esp_now_send(hubMacAddress, 
        (uint8_t *)&outgoingMessage, sizeof(outgoingMessage));
    
    if (result != ESP_OK) {
        Serial.print("ОШИБКА отправки: ");
        Serial.println(result);
    }
}

// ========== CALLBACK ОТПРАВКИ ==========
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("Доставка не удалась, повторная попытка...");
    }
}