/**
 * ТЕСТОВЫЙ СНИФФЕР ДЛЯ ТЕПЛИЦЫ
 * Анализ JSON-данных от устройства теплицы (MAC: E8:9F:6D:87:34:8A)
 */

#include <WiFi.h>
#include <esp_now.h>
#include <ArduinoJson.h> // Убедитесь, что библиотека установлена в platformio.ini

// MAC-адрес теплицы
uint8_t greenhouseMac[] = {0xE8, 0x9F, 0x6D, 0x87, 0x34, 0x8A};

// Структура, аналогичная основной системе (для совместимости)
typedef struct esp_now_message {
    char json[256];      // Буфер с запасом
    uint8_t sender_id;
} esp_now_message;

esp_now_message incomingMessage;

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len) {
    // 1. Проверяем, что это теплица
    if (memcmp(mac_addr, greenhouseMac, 6) != 0) {
        return; // Игнорируем другие устройства
    }
    
    // 2. Копируем данные
    if (len <= sizeof(incomingMessage)) {
        memcpy(&incomingMessage, data, len);
    } else {
        Serial.println("❌ Пакет слишком большой!");
        return;
    }
    
    // 3. Выводим разделитель для удобства
    Serial.println("\n" + String('=') * 60);
    Serial.println("✅ ДАННЫЕ ОТ ТЕПЛИЦЫ");
    Serial.printf("Длина пакета: %d байт\n", len);
    Serial.printf("Время: %lu мс\n", millis());
    
    // 4. Показываем сырые данные
    Serial.print("СЫРОЙ JSON: ");
    Serial.println(incomingMessage.json);
    
    // 5. Детальный анализ JSON
    StaticJsonDocument<512> doc; // Документ с запасом
    DeserializationError error = deserializeJson(doc, incomingMessage.json);
    
    if (error) {
        Serial.print("❌ Ошибка парсинга JSON: ");
        Serial.println(error.c_str());
        
        // Показываем HEX для отладки
        Serial.print("HEX: ");
        for (int i = 0; i < len && i < 50; i++) {
            Serial.printf("%02X ", data[i]);
        }
        Serial.println();
    } else {
        Serial.println("📊 СТРУКТУРА JSON:");
        
        // Проверяем все ожидаемые поля
        const char* fields[] = {"temp_in", "temp_out", "temp_soil", "hum_in", "hum_out", "tvoc", "co2"};
        for (const char* field : fields) {
            if (doc.containsKey(field)) {
                Serial.printf("  %-12s: %s\n", field, doc[field].as<const char*>());
            } else {
                Serial.printf("  %-12s: ❌ ОТСУТСТВУЕТ!\n", field);
            }
        }
        
        // Проверяем тип данных каждого поля
        Serial.println("\n🔍 ТИПЫ ДАННЫХ:");
        for (const char* field : fields) {
            if (doc.containsKey(field)) {
                JsonVariant value = doc[field];
                if (value.is<const char*>()) {
                    Serial.printf("  %s: строка\n", field);
                } else if (value.is<int>()) {
                    Serial.printf("  %s: целое число\n", field);
                } else if (value.is<float>()) {
                    Serial.printf("  %s: число с плавающей точкой\n", field);
                }
            }
        }
        
        // Проверяем наличие лишних полей
        int fieldCount = 0;
        for (JsonPair kv : doc.as<JsonObject>()) {
            fieldCount++;
        }
        Serial.printf("\n📈 Всего полей в JSON: %d\n", fieldCount);
    }
    
    Serial.println(String('=') * 60);
    
    // 6. Краткий вывод для быстрого мониторинга
    Serial.print("💎 КРАТКО: ");
    if (doc.containsKey("temp_in") && doc.containsKey("hum_in")) {
        Serial.printf("Внутри: %s°C, %s%% | ", 
                     doc["temp_in"].as<const char*>(), 
                     doc["hum_in"].as<const char*>());
    }
    if (doc.containsKey("temp_out")) {
        Serial.printf("Снаружи: %s°C", doc["temp_out"].as<const char*>());
    }
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(2000); // Даём время открыть Serial Monitor
    
    Serial.println("\n\n" + String('=') * 60);
    Serial.println("🎯 ТЕСТОВЫЙ СНИФФЕР ДЛЯ ТЕПЛИЦЫ");
    Serial.println("MAC: E8:9F:6D:87:34:8A");
    Serial.println("Ожидание данных...");
    Serial.println(String('=') * 60 + "\n");
    
    // Настройка Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(); // Отключаемся от сетей для чистоты эфира
    
    // Инициализация ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ Ошибка инициализации ESP-NOW!");
        while(1) delay(1000);
    }
    
    // Регистрируем callback
    esp_now_register_recv_cb(OnDataRecv);
    
    Serial.println("✅ Сниффер запущен. Данные появятся ниже:\n");
}

void loop() {
    // Можно добавить периодический пинг
    static unsigned long lastPing = 0;
    if (millis() - lastPing > 30000) { // Каждые 30 секунд
        Serial.printf("[%lu мс] Ожидание данных от теплицы...\n", millis());
        lastPing = millis();
    }
    delay(100);
}