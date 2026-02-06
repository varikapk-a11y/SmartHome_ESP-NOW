/**
 * SmartHome ESP-NOW Hub (ESP32)
 * Универсальная версия с JSON структурой
 * ВЕРСИЯ: Автообновление данных с датчиков
 */
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <esp_now.h>
#include <ArduinoJson.h>

// ---- 1. КОНФИГУРАЦИЯ ----
const char* AP_SSID = "SmartHome-Hub";
const char* AP_PASSWORD = "12345678";

// MAC вашего узла (ESP32-C3)
uint8_t nodeMacAddress[] = {0xAC, 0xEB, 0xE6, 0x49, 0x10, 0x28};

// ---- 2. УНИВЕРСАЛЬНАЯ СТРУКТУРА ESP-NOW ----
typedef struct esp_now_message {
    char json[192];      // {"type":"sensor", "data":{...}} или {"type":"command", "command":"LED_ON"}
    uint8_t sender_id;   // ID отправителя
} esp_now_message;

esp_now_message outgoingMessage;
esp_now_message incomingMessage;

// ---- 3. ГЛОБАЛЬНЫЕ ОБЪЕКТЫ ----
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ---- 4. ПРОТОТИПЫ ФУНКЦИЙ ----
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len);
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void sendToNode(String cmd);
void broadcastWs(String type, String text, String state = "");

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== SmartHome ESP-NOW Hub (JSON версия) ===");
    Serial.println("=== РЕЖИМ: АВТООБНОВЛЕНИЕ ДАННЫХ ===");

    // 1. WI-FI ТОЧКА ДОСТУПА
    Serial.print("Запуск точки доступа: ");
    Serial.println(AP_SSID);
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        Serial.println("❌ Ошибка создания точки доступа!");
        while(1) delay(1000);
    }
    Serial.print("IP адрес: ");
    Serial.println(WiFi.softAPIP());

    // 2. WEB-СЕРВЕР И WEB SOCKET
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Умный дом ESP-NOW</title>
    <style>
        body {font-family: Arial; text-align: center; margin-top: 30px;}
        button {font-size: 20px; padding: 15px 30px; margin: 10px; border: none; border-radius: 8px; cursor: pointer;}
        #btnOn {background: #4CAF50; color: white;}
        #btnOff {background: #f44336; color: white;}
        #btnStatus {background: #2196F3; color: white;}
        #status {margin-top: 30px; font-size: 18px; padding: 15px; background: #f5f5f5; border-radius: 8px; min-height: 60px; text-align: left;}
        .on {color: #4CAF50; font-weight: bold;}
        .off {color: #f44336; font-weight: bold;}
        .data {color: #FF9800;}
        .sensor-row { display: flex; justify-content: space-between; margin: 5px 0; }
        .sensor-label { font-weight: bold; }
        .sensor-value { font-family: monospace; }
        .gpio-status { color: #9C27B0; }
        #lastUpdate { font-size: 0.9em; color: #666; margin-top: 10px; text-align: right; }
    </style>
</head>
<body>
    <h1>🏠 Умный дом ESP-NOW</h1>
    <p>MAC узла: AC:EB:E6:49:10:28 | ID: 101 | <span id="connectionStatus">✅ Связь активна</span></p>
    <button id="btnOn" onclick="sendCommand('LED_ON')">▶ ВКЛЮЧИТЬ LED</button>
    <button id="btnOff" onclick="sendCommand('LED_OFF')">⏸ ВЫКЛЮЧИТЬ LED</button>
    <button id="btnStatus" onclick="sendCommand('GET_STATUS')">🔄 ОБНОВИТЬ СЕЙЧАС</button>
    
    <div id="status">
        <div>📊 <span class="data">Данные датчиков:</span></div>
        <div id="sensorData">Ожидание данных от узла... Данные появятся здесь автоматически.</div>
        <div id="lastUpdate">—</div>
    </div>

    <script>
        const ws = new WebSocket('ws://' + window.location.hostname + '/ws');
        let lastUpdateTime = null;
        
        ws.onopen = function() {
            console.log('✅ WebSocket подключён');
            document.getElementById('connectionStatus').textContent = '✅ Связь активна';
            document.getElementById('sensorData').innerHTML = '<em>Ожидание первого пакета данных...</em>';
        };
        
        ws.onmessage = function(event) {
            const msg = JSON.parse(event.data);
            console.log('Получено сообщение:', msg);
            
            if(msg.type === 'node_status') {
                // Обновляем статус кнопок при ответе на команду
                if(msg.state === 'on') {
                    document.getElementById('btnOn').style.opacity = '0.6';
                    document.getElementById('btnOff').style.opacity = '1';
                } else {
                    document.getElementById('btnOn').style.opacity = '1';
                    document.getElementById('btnOff').style.opacity = '0.6';
                }
                // Можно показать всплывающее уведомление, но не обязательно
                console.log('Статус узла:', msg.text);
            }
            else if(msg.type === 'sensor_data') {
                // ОСНОВНОЙ БЛОК: ОБНОВЛЯЕМ ДАННЫЕ ДАТЧИКОВ
                let html = '';
                if(msg.aht20) {
                    html += '<div class="sensor-row"><span class="sensor-label">AHT20 (t):</span><span class="sensor-value">' + msg.aht20.temp + '°C</span></div>';
                    html += '<div class="sensor-row"><span class="sensor-label">AHT20 (h):</span><span class="sensor-value">' + msg.aht20.hum + '%</span></div>';
                }
                if(msg.bmp280) {
                    html += '<div class="sensor-row"><span class="sensor-label">BMP280 (t):</span><span class="sensor-value">' + msg.bmp280.temp + '°C</span></div>';
                    html += '<div class="sensor-row"><span class="sensor-label">BMP280 (p):</span><span class="sensor-value">' + msg.bmp280.press + ' mmHg</span></div>';
                }
                document.getElementById('sensorData').innerHTML = html;
                
                // Обновляем время последнего обновления
                lastUpdateTime = new Date();
                document.getElementById('lastUpdate').textContent = 'Обновлено: ' + lastUpdateTime.toLocaleTimeString();
            }
            else if(msg.type === 'gpio_status') {
                // Обновляем статус светодиода, если нужно
                let html = '🔌 <span class="gpio-status">Состояние GPIO:</span><br>';
                if(msg.gpio8 !== undefined) {
                    html += '<div class="sensor-row"><span class="sensor-label">GPIO8 (LED):</span><span class="sensor-value">' + (msg.gpio8 ? 'ВКЛ' : 'ВЫКЛ') + '</span></div>';
                    document.getElementById('sensorData').innerHTML += html;
                }
            }
            else if(msg.type === 'hub_log') {
                console.log('Хаб:', msg.text);
            }
        };
        
        ws.onclose = function() {
            document.getElementById('connectionStatus').textContent = '❌ Связь потеряна';
            document.getElementById('sensorData').innerHTML = '<em style="color: red;">Соединение с хабом разорвано. Перезагрузите страницу.</em>';
        };
        
        function sendCommand(cmd) {
            if(ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({command: cmd}));
                console.log('Отправлена команда:', cmd);
            }
        }
        
        // Функция для обновления времени "сколько секунд назад"
        function updateTimeAgo() {
            if(lastUpdateTime) {
                const secondsAgo = Math.floor((new Date() - lastUpdateTime) / 1000);
                const elem = document.getElementById('lastUpdate');
                if(secondsAgo < 60) {
                    elem.textContent = `Обновлено: ${secondsAgo} сек. назад`;
                } else {
                    elem.textContent = `Обновлено: ${lastUpdateTime.toLocaleTimeString()}`;
                }
            }
        }
        
        // Запускаем таймер для обновления времени
        setInterval(updateTimeAgo, 1000);
    </script>
</body>
</html>
        )rawliteral";
        request->send(200, "text/html", html);
    });

    ws.onEvent(onWebSocketEvent);
    server.addHandler(&ws);
    server.begin();
    Serial.println("✅ Веб-сервер и WebSocket запущены.");

    // 3. ИНИЦИАЛИЗАЦИЯ ESP-NOW
    WiFi.mode(WIFI_AP_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ Ошибка инициализации ESP-NOW!");
        while(1) delay(1000);
    }

    // Регистрация колбэков
    esp_now_register_send_cb(onEspNowDataSent);
    esp_now_register_recv_cb(onEspNowDataRecv);

    // Добавление узла как пира
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, nodeMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("❌ Не удалось добавить узел в ESP-NOW!");
    } else {
        Serial.println("✅ Узел добавлен в ESP-NOW.");
    }

    Serial.println("\n=== ХАБ ГОТОВ К РАБОТЕ ===");
    Serial.println("1. Подключитесь к Wi-Fi: " + String(AP_SSID));
    Serial.println("2. Откройте в браузере: http://" + WiFi.softAPIP().toString());
    Serial.println("3. Данные с датчиков будут обновляться автоматически каждые 30 сек.\n");
}

void loop() {
    ws.cleanupClients();
    delay(10);
}

// ===================== WEB SOCKET ОБРАБОТЧИК =====================
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, data, len);
        if (!error && doc.containsKey("command")) {
            String cmd = doc["command"].as<String>();
            Serial.print("📨 Веб-команда: ");
            Serial.println(cmd);
            sendToNode(cmd);
        }
    }
}

// ===================== ОТПРАВКА НА УЗЕЛ =====================
void sendToNode(String cmd) {
    // Формируем JSON команды
    char json_cmd[64];
    snprintf(json_cmd, sizeof(json_cmd), "{\"type\":\"command\",\"command\":\"%s\"}", cmd.c_str());
    
    strncpy(outgoingMessage.json, json_cmd, sizeof(outgoingMessage.json)-1);
    outgoingMessage.json[sizeof(outgoingMessage.json)-1] = '\0';
    outgoingMessage.sender_id = 1; // ID хаба
    
    esp_err_t result = esp_now_send(nodeMacAddress, (uint8_t *) &outgoingMessage, sizeof(outgoingMessage));
    
    if (result == ESP_OK) {
        Serial.print("📡 Отправлена команда: ");
        Serial.println(cmd);
        broadcastWs("hub_log", "Команда '" + cmd + "' отправлена на узел", "");
    } else {
        Serial.print("❌ Ошибка отправки: ");
        Serial.println(result);
    }
}

// ===================== ESP-NOW КОЛБЭКИ =====================
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("✉️ Доставка ESP-NOW: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ Успех" : "❌ Ошибка");
}

void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    // --- ФИЛЬТР: Принимаем данные ТОЛЬКО от нашего узла ---
    uint8_t allowedNodeMac[] = {0xAC, 0xEB, 0xE6, 0x49, 0x10, 0x28};
    if (memcmp(mac_addr, allowedNodeMac, 6) != 0) {
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        Serial.print("[ХАБ] Игнорирую чужой узел: ");
        Serial.println(macStr);
        return;
    }
    // --- КОНЕЦ ФИЛЬТРА ---

    memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));
    
    Serial.print("📥 JSON от узла: ");
    Serial.println(incomingMessage.json);

    // Парсим JSON от узла
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, incomingMessage.json);
    
    if (error) {
        Serial.print("❌ Ошибка парсинга JSON: ");
        Serial.println(error.c_str());
        return;
    }
    
    const char* type = doc["type"];
    
    if (strcmp(type, "sensor") == 0) {
        // ✅ ОСНОВНОЕ ИЗМЕНЕНИЕ: ВСЕГДА отправляем данные в веб-интерфейс
        // Данные датчиков пришли (автоматически или по запросу)
        JsonObject data = doc["data"];
        StaticJsonDocument<300> response;
        response["type"] = "sensor_data";
        
        if (data.containsKey("AHT20")) {
            response["aht20"]["temp"] = data["AHT20"]["temp"].as<String>();
            response["aht20"]["hum"] = data["AHT20"]["hum"].as<String>();
        }
        if (data.containsKey("BMP280")) {
            response["bmp280"]["temp"] = data["BMP280"]["temp"].as<String>();
            response["bmp280"]["press"] = data["BMP280"]["press_mmHg"].as<String>();
        }
        
        String jsonResponse;
        serializeJson(response, jsonResponse);
        ws.textAll(jsonResponse);
        Serial.println("📊 Данные с датчиков отправлены в веб-интерфейс.");
    }
    else if (strcmp(type, "ack") == 0) {
        // Подтверждение команд
        const char* cmd = doc["command"];
        if (strcmp(cmd, "LED_ON") == 0) {
            broadcastWs("node_status", "LED на узле ВКЛЮЧЁН", "on");
        }
        else if (strcmp(cmd, "LED_OFF") == 0) {
            broadcastWs("node_status", "LED на узле ВЫКЛЮЧЕН", "off");
        }
    }
    else if (strcmp(type, "gpio") == 0) {
        // Состояние GPIO
        StaticJsonDocument<200> response;
        response["type"] = "gpio_status";
        
        if (doc.containsKey("pin") && doc.containsKey("state")) {
            int pin = doc["pin"];
            int state = doc["state"];
            if (pin == 8) {
                response["gpio8"] = state;
            }
        }
        
        String jsonResponse;
        serializeJson(response, jsonResponse);
        ws.textAll(jsonResponse);
    }
}

// ===================== ОТПРАВКА В WEB SOCKET =====================
void broadcastWs(String type, String text, String state) {
    StaticJsonDocument<200> doc;
    doc["type"] = type;
    doc["text"] = text;
    if (state.length() > 0) doc["state"] = state;
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    ws.textAll(jsonResponse);
}