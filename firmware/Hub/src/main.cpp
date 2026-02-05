#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <esp_now.h>
#include <ArduinoJson.h>

// Структура сообщений ESP-NOW (должна совпадать с узлом!)
typedef struct esp_now_message {
    char payload[256];
    uint8_t sender_id;
    char msg_type[16]; // "command", "sensor_data", "ack"
} esp_now_message;

// ========== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ==========
String hubStaMac = "";   // STA MAC (для ESP-NOW) - ОСНОВНОЙ!
String hubApMac = "";    // AP MAC (для веб-интерфейса)
String nodeMac = "";     // MAC узла, который подключится
String lastSensorJson = "{}";
String displayHtml = "";
unsigned long lastUpdateTime = 0;

AsyncWebServer server(80);
esp_now_message incomingMessage;

// Параметры точки доступа для веб-интерфейса
const char* ap_ssid = "SmartHome-Hub";
const char* ap_password = "12345678";

// HTML страница (остается как в вашем исходнике)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Умный дом ESP-NOW</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        .container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #2c3e50; text-align: center; }
        .btn { background: #3498db; color: white; border: none; padding: 12px 24px; margin: 5px; border-radius: 5px; cursor: pointer; font-size: 16px; }
        .btn:hover { background: #2980b9; }
        .btn-off { background: #e74c3c; }
        .btn-off:hover { background: #c0392b; }
        .sensor-box { background: #ecf0f1; border-radius: 8px; padding: 15px; margin: 15px 0; border-left: 5px solid #3498db; }
        .sensor-title { color: #2c3e50; font-size: 1.2em; margin-bottom: 10px; }
        .sensor-row { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px dashed #bdc3c7; }
        .sensor-label { font-weight: bold; color: #34495e; }
        .sensor-value { font-family: monospace; color: #16a085; font-weight: bold; }
        .unit { color: #7f8c8d; font-size: 0.9em; }
        #lastUpdate { text-align: center; color: #95a5a6; font-style: italic; margin-top: 20px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🏠 Хаб умного дома ESP-NOW</h1>
        <p style="text-align:center;">
            <strong>STA MAC (ESP-NOW):</strong> %HUB_STA_MAC%<br>
            <strong>AP MAC (Веб):</strong> %HUB_AP_MAC%<br>
            <strong>Узел:</strong> <span id="nodeMacField">%NODE_MAC%</span>
        </p>
        
        <div style="text-align:center;">
            <button class="btn" onclick="sendCmd('LED_ON')">Включить LED</button>
            <button class="btn btn-off" onclick="sendCmd('LED_OFF')">Выключить LED</button>
            <button class="btn" onclick="sendCmd('GET_STATUS')">Обновить данные</button>
        </div>

        <div id="sensorDisplay">
            %SENSOR_DATA%
        </div>

        <div id="lastUpdate">Ожидание данных от узла...</div>
    </div>

    <script>
        function sendCmd(cmd) {
            fetch('/cmd?cmd=' + cmd)
                .then(r => r.text())
                .then(txt => console.log('Ответ:', txt));
        }

        setInterval(() => {
            fetch('/data')
                .then(r => r.json())
                .then(data => {
                    if(data.html) {
                        document.getElementById('sensorDisplay').innerHTML = data.html;
                        document.getElementById('lastUpdate').textContent = 'Обновлено: ' + new Date().toLocaleTimeString();
                    }
                    if(data.nodeMac) {
                        document.getElementById('nodeMacField').textContent = data.nodeMac;
                    }
                });
        }, 10000);
    </script>
</body>
</html>
)rawliteral";

// ========== CALLBACK-ФУНКЦИИ ESP-NOW ==========
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("[ХАБ] Статус отправки: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ Успех" : "❌ Ошибка");
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    // Сохраняем MAC узла (который прислал данные)
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    nodeMac = String(macStr);
    Serial.print("[ХАБ] Данные от узла: ");
    Serial.print(nodeMac);
    Serial.print(" | ");

    if (len <= sizeof(incomingMessage)) {
        memcpy(&incomingMessage, incomingData, len);
        
        if (strcmp(incomingMessage.msg_type, "sensor_data") == 0) {
            lastSensorJson = String(incomingMessage.payload);
            lastUpdateTime = millis();
            Serial.println("Данные датчиков");
            
            // Парсинг JSON от узла
            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, lastSensorJson);
            
            if (!error) {
                displayHtml = "";
                
                // Данные AHT20
                if (doc.containsKey("AHT20")) {
                    displayHtml += "<div class='sensor-box'><div class='sensor-title'>🌡️ Датчик AHT20</div>";
                    displayHtml += "<div class='sensor-row'><span class='sensor-label'>Температура:</span><span class='sensor-value'>";
                    displayHtml += String(doc["AHT20"]["temp"].as<float>(), 1);
                    displayHtml += "<span class='unit'>°C</span></span></div>";
                    
                    displayHtml += "<div class='sensor-row'><span class='sensor-label'>Влажность:</span><span class='sensor-value'>";
                    displayHtml += String(doc["AHT20"]["hum"].as<float>(), 1);
                    displayHtml += "<span class='unit'>%</span></span></div>";
                    displayHtml += "</div>";
                }
                
                // Данные BMP280
                if (doc.containsKey("BMP280")) {
                    JsonObject bmp = doc["BMP280"];
                    displayHtml += "<div class='sensor-box'><div class='sensor-title'>📊 Датчик BMP280</div>";
                    displayHtml += "<div class='sensor-row'><span class='sensor-label'>Температура:</span><span class='sensor-value'>";
                    displayHtml += String(bmp["temp"].as<float>(), 1);
                    displayHtml += "<span class='unit'>°C</span></span></div>";
                    
                    displayHtml += "<div class='sensor-row'><span class='sensor-label'>Давление:</span><span class='sensor-value'>";
                    displayHtml += String(bmp["press_mmHg"].as<float>(), 1);
                    displayHtml += "<span class='unit'>мм рт. ст.</span></span></div>";
                    displayHtml += "</div>";
                }
                
                Serial.println("[ХАБ] HTML сформирован.");
            } else {
                Serial.println("[ХАБ] Ошибка парсинга JSON!");
                displayHtml = "<div class='no-data'>Ошибка формата данных</div>";
            }
        }
        else if (strcmp(incomingMessage.msg_type, "ack") == 0) {
            Serial.print("Подтверждение: ");
            Serial.println(incomingMessage.payload);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n" + String(60, '='));
    Serial.println("        ХАБ ESP32 (ИСПРАВЛЕННАЯ ВЕРСИЯ)");
    Serial.println(String(60, '='));

    // ========== КЛЮЧЕВОЕ ИСПРАВЛЕНИЕ: WIFI_AP_STA ==========
    WiFi.mode(WIFI_AP_STA); // Гибридный режим
    
    // 1. Инициализируем точку доступа для веб-интерфейса
    WiFi.softAP(ap_ssid, ap_password);
    hubApMac = WiFi.softAPmacAddress();
    
    // 2. Получаем STA MAC-адрес для ESP-NOW
    hubStaMac = WiFi.macAddress();
    
    Serial.println("[ХАБ] ДИАГНОСТИКА MAC-АДРЕСОВ:");
    Serial.print("  STA MAC (для ESP-NOW): ");
    Serial.println(hubStaMac);
    Serial.print("  AP  MAC (для веб-интерфейса): ");
    Serial.println(hubApMac);
    Serial.print("  IP веб-интерфейса: ");
    Serial.println(WiFi.softAPIP());
    Serial.println();

    // ========== ИНИЦИАЛИЗАЦИЯ ESP-NOW ==========
    // Важно: ESP-NOW будет использовать STA интерфейс!
    esp_err_t initResult = esp_now_init();
    if (initResult != ESP_OK) {
        Serial.printf("[ХАБ] Критическая ошибка инициализации ESP-NOW! Код: %d\n", initResult);
        while(1) { delay(1000); }
    }
    Serial.println("[ХАБ] ESP-NOW успешно инициализирован (на STA интерфейсе).");

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    Serial.println("[ХАБ] Callback-функции зарегистрированы.");

    // ========== ВЕБ-СЕРВЕР ==========
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = FPSTR(index_html);
        html.replace("%HUB_STA_MAC%", hubStaMac);
        html.replace("%HUB_AP_MAC%", hubApMac);
        html.replace("%NODE_MAC%", nodeMac.length() > 0 ? nodeMac : "не подключён");
        html.replace("%SENSOR_DATA%", displayHtml.length() > 0 ? displayHtml : 
                    "<div class='sensor-box'><div class='sensor-title'>📡 Ожидание данных</div><p style='text-align:center;color:#95a5a6;'>Подключите узел ESP32-C3</p></div>");
        request->send(200, "text/html", html);
    });
    
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<200> resp;
        resp["html"] = displayHtml;
        resp["nodeMac"] = nodeMac;
        String jsonResponse;
        serializeJson(resp, jsonResponse);
        request->send(200, "application/json", jsonResponse);
    });
    
    server.on("/cmd", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("cmd")) {
            String command = request->getParam("cmd")->value();
            Serial.printf("[ХАБ] Веб-команда: '%s'\n", command.c_str());
            
            if (nodeMac.length() == 0) {
                request->send(200, "text/plain", "❌ Узел не подключён.");
                return;
            }
            
            // Преобразуем MAC строку в массив для отправки
            uint8_t nodeMacAddr[6];
            sscanf(nodeMac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &nodeMacAddr[0], &nodeMacAddr[1], &nodeMacAddr[2],
                   &nodeMacAddr[3], &nodeMacAddr[4], &nodeMacAddr[5]);
            
            // Подготавливаем сообщение ESP-NOW
            esp_now_message outgoingMsg;
            memset(&outgoingMsg, 0, sizeof(outgoingMsg));
            strncpy(outgoingMsg.msg_type, "command", sizeof(outgoingMsg.msg_type)-1);
            strncpy(outgoingMsg.payload, command.c_str(), sizeof(outgoingMsg.payload)-1);
            outgoingMsg.sender_id = 200;
            
            // Отправляем
            esp_err_t result = esp_now_send(nodeMacAddr, (uint8_t *)&outgoingMsg, sizeof(outgoingMsg));
            
            if (result == ESP_OK) {
                request->send(200, "text/plain", "✅ Команда '" + command + "' отправлена на " + nodeMac);
            } else {
                request->send(200, "text/plain", "❌ Ошибка отправки. Код: " + String(result));
            }
        }
    });

    server.begin();
    Serial.println("[ХАБ] HTTP сервер запущен.");
    Serial.println("[ХАБ] Готов к работе. Ожидаю узел...");
    Serial.println(String(60, '='));
    Serial.println("ВАЖНО: Укажите в коде узла STA MAC хаба: " + hubStaMac);
    Serial.println(String(60, '='));
}

void loop() {
    delay(1000);
}
