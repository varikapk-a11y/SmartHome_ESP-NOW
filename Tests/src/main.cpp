/**
 * СКЕТЧ-ПЕРЕХВАТЧИК ДАННЫХ ESP-NOW
 * Цель: захват и анализ пакетов от устройства "Теплица" (MAC: E8:9F:6D:87:34:8A)
 * Подключите ESP32, откройте Serial Monitor (115200 baud).
 */

#include <WiFi.h>
#include <esp_now.h>

// MAC-адрес целевого устройства (Теплица)
uint8_t targetMac[] = {0xE8, 0x9F, 0x6D, 0x87, 0x34, 0x8A};

// Функция обратного вызова при получении данных ESP-NOW
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  // 1. СРАВНИВАЕМ MAC: нас интересует только устройство "Теплица"
  if (memcmp(mac_addr, targetMac, 6) != 0) {
    return; // Полностью игнорируем другие устройства
  }

  // 2. ВЫВОД ИНФОРМАЦИИ В SERIAL (АНАЛИЗ)
  Serial.println("\n" + String(60, '='));
  Serial.println("✅ ПАКЕТ ПОЛУЧЕН ОТ ТЕПЛИЦЫ");

  // 2.1. Вывод длины пакета (это критически важно)
  Serial.printf("Длина данных: %d байт\n", data_len);

  // 2.2. Вывод в HEX-формате (сырые байты)
  Serial.print("HEX: ");
  for (int i = 0; i < data_len; i++) {
    Serial.printf("%02X ", data[i]);
    if ((i + 1) % 16 == 0) Serial.println();
  }
  Serial.println();

  // 2.3. Попытка вывода как текстовой строки (ASCII)
  Serial.print("TEXT: \"");
  for (int i = 0; i < data_len; i++) {
    if (data[i] >= 32 && data[i] <= 126) {
      Serial.print((char)data[i]);
    } else {
      Serial.print('.');
    }
  }
  Serial.println("\"");

  // 2.4. Если данные похожи на JSON (начинаются с {)
  if (data_len > 0 && data[0] == '{') {
    Serial.println("⚠️ Возможно, это JSON (начинается с '{')");
    
    // Пробуем создать строку из данных
    String jsonString;
    for (int i = 0; i < data_len; i++) {
      jsonString += (char)data[i];
    }
    Serial.print("JSON как строка: ");
    Serial.println(jsonString);
  }

  Serial.println(String(60, '='));
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESP-NOW СНИФФЕР (Теплица) ===");
  Serial.println("Ожидание данных от MAC: E8:9F:6D:87:34:8A");
  Serial.println("Если устройство активно, данные появятся ниже...\n");

  // ВАЖНО: Для приёма ESP-NOW нужен режим STA
  WiFi.mode(WIFI_STA);
  
  // Отключаем Wi-Fi для уменьшения помех (опционально)
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Ошибка инициализации ESP-NOW!");
    while (1) delay(1000);
  }

  // Регистрация callback-функции приёма
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("✅ Сниффер запущен. Ожидание пакетов...");
}

void loop() {
  // Просто выводим точку каждые 10 секунд, чтобы знать, что устройство живо
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 10000) {
    Serial.print(".");
    lastPrint = millis();
  }
  delay(100);
}