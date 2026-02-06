/**
 * SmartHome ESP-NOW Hub (ESP32)
 * Универсальная версия с JSON структурой
 * ВЕРСИЯ 2.3: Исправления интерфейса
 */
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <esp_now.h>
#include <ArduinoJson.h>

// ---- 1. КОНФИГУРАЦИЯ ----
const char* AP_SSID = "SmartHome-Hub";
const char* AP_PASSWORD = "12345678";

// MAC нашего основного узла (ESP32-C3)
uint8_t nodeMacAddress[] = {0xAC, 0xEB, 0xE6, 0x49, 0x10, 0x28};
// MAC устройства "Теплица"
uint8_t greenhouseMac[] = {0xE8, 0x9F, 0x6D, 0x87, 0x34, 0x8A};

// ---- 2. УНИВЕРСАЛЬНАЯ СТРУКТУРА ESP-NOW ----
typedef struct esp_now_message {
    char json[192];
    uint8_t sender_id;
} esp_now_message;

// ---- 3. СТРУКТУРА ДАННЫХ ТЕПЛИЦЫ ----
#pragma pack(push, 1)
typedef struct greenhouse_packet {
    char temp_in[4];
    uint8_t reserved1[28];
    char temp_out[4];
    uint8_t reserved2[28];
    uint32_t relay2_state;
    uint32_t hum_in;
    uint32_t broken_sensor1;
    uint32_t broken_sensor2;
    uint32_t relay1_state;
} greenhouse_packet;
#pragma pack(pop)

// ---- 4. ГЛОБАЛЬНЫЕ ОБЪЕКТЫ ----
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
esp_now_message outgoingMessage;
esp_now_message incomingMessage;

unsigned long lastGreenhouseUpdate = 0;
const unsigned long GREENHOUSE_UPDATE_INTERVAL = 30000;

// ---- 5. ПРОТОТИПЫ ----
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len);
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void sendToNode(String cmd);
void broadcastWs(String type, String text, String state = "");
void processGreenhouseData(const uint8_t *data);
void processNodeData(const uint8_t *data, int len);
String relayStateToString(uint32_t state);

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== SmartHome ESP-NOW Hub (Версия 2.3) ===");
    Serial.println("=== Исправления интерфейса ===");

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        Serial.println("❌ Ошибка создания точки доступа!");
        while(1) delay(1000);
    }
    Serial.print("IP адрес: ");
    Serial.println(WiFi.softAPIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Умный дом ESP-NOW + Теплица</title>
    <style>
        body {font-family: Arial; text-align: center; margin-top: 20px; max-width: 800px; margin-left: auto; margin-right: auto;}
        h1 {color: #333;}
        
        /* КНОПКА ОБНОВИТЬ ДАННЫЕ */
        #refreshBtn {
            font-size: 14px;
            padding: 10px 25px;
            background: #3498db;
            color: white;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            margin: 20px auto;
            display: block;
            width: 250px;
            font-weight: bold;
            transition: all 0.3s;
        }
        #refreshBtn:hover {
            background: #2980b9;
            transform: translateY(-2px);
            box-shadow: 0 4px 8px rgba(0,0,0,0.1);
        }
        
        .section {
            background: #f9f9f9;
            border-radius: 12px;
            padding: 20px;
            margin: 25px 0;
            box-shadow: 0 4px 6px rgba(0,0,0,0.05);
            text-align: left;
        }
        .section-title {
            font-size: 1.5em;
            margin-bottom: 15px;
            color: #2c3e50;
            border-bottom: 2px solid #3498db;
            padding-bottom: 8px;
        }
        .sensor-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-top: 10px;
        }
        .sensor-item {
            background: white;
            padding: 15px;
            border-radius: 8px;
            border-left: 5px solid #3498db;
        }
        .sensor-label {
            font-weight: bold;
            color: #555;
            display: block;
            margin-bottom: 5px;
        }
        .sensor-value {
            font-size: 1.8em;
            font-family: 'Courier New', monospace;
            color: #2c3e50;
        }
        .sensor-unit {
            font-size: 0.9em;
            color: #7f8c8d;
            margin-left: 3px;
        }
        .relay-status {
            display: inline-block;
            padding: 5px 12px;
            border-radius: 20px;
            font-weight: bold;
            margin-top: 5px;
        }
        .relay-on {
            background-color: #27ae60;
            color: white;
        }
        .relay-off {
            background-color: #e74c3c;
            color: white;
        }
        
        /* КНОПКА LED (уменьшена и смещена влево) */
        #ledToggleBtn {
            font-size: 14px; /* Такой же размер как у "Обновить данные" */
            padding: 10px 25px;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            color: white;
            font-weight: bold;
            transition: all 0.3s;
            width: 250px; /* Такая же ширина */
            margin: 15px 0; /* Смещение влево */
            float: left; /* Выравнивание по левому краю */
            display: block;
        }
        #ledToggleBtn.led-on {
            background: linear-gradient(135deg, #e74c3c, #c0392b);
        }
        #ledToggleBtn.led-off {
            background: linear-gradient(135deg, #2ecc71, #27ae60);
        }
        #ledToggleBtn.led-unknown {
            background: #95a5a6;
            cursor: not-allowed;
        }
        #ledToggleBtn:disabled {
            opacity: 0.7;
            cursor: not-allowed;
        }
        #ledToggleBtn:not(:disabled):hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 8px rgba(0,0,0,0.1);
        }
        
        .control-buttons {
            margin-top: 20px;
            clear: both; /* Очистка обтекания */
        }
        
        #lastUpdate {
            font-size: 0.85em;
            color: #95a5a6;
            text-align: right;
            margin-top: 15px;
            font-style: italic;
        }
        .node-info {
            color: #7f8c8d;
            font-size: 0.9em;
            margin-bottom: 10px;
            clear: both; /* Чтобы текст не наезжал на кнопку */
        }
        #nodeSensorData { 
            min-height: 100px; 
            clear: both; /* Чтобы данные датчиков были под кнопкой */
            margin-top: 10px;
        }
    </style>
</head>
<body>
    <h1>🏠 Умный дом ESP-NOW + 🌿 Теплица</h1>
    
    <!-- КНОПКА ОБНОВИТЬ ДАННЫЕ -->
    <button id="refreshBtn" onclick="sendCommand('GET_STATUS')">🔄 ОБНОВИТЬ ДАННЫЕ</button>

    <!-- Секция основного узла -->
    <div class="section">
        <div class="section-title">📟 Основной узел (ID: 101)</div>
        <div class="node-info">MAC: AC:EB:E6:49:10:28</div>
        
        <!-- КНОПКА LED (смещена влево, уменьшена) -->
        <button id="ledToggleBtn" class="led-unknown" onclick="toggleLED()">--</button>
        <div style="clear: both;"></div> <!-- Очистка обтекания -->
        
        <!-- БЛОК ДЛЯ ДАННЫХ ДАТЧИКОВ -->
        <div id="nodeSensorData">
            <p>Нажмите "Обновить данные" для получения показаний</p>
        </div>
    </div>

    <!-- Секция теплицы -->
    <div class="section">
        <div class="section-title">🌿 Теплица (ID: 102)</div>
        <div class="node-info">MAC: E8:9F:6D:87:34:8A | Данные обновляются каждые 30 сек.</div>
        <div class="sensor-grid" id="greenhouseData">
            <div class="sensor-item">
                <span class="sensor-label">Температура (внутри):</span>
                <span class="sensor-value">--</span><span class="sensor-unit">°C</span>
            </div>
            <div class="sensor-item">
                <span class="sensor-label">Температура (улица):</span>
                <span class="sensor-value">--</span><span class="sensor-unit">°C</span>
            </div>
            <div class="sensor-item">
                <span class="sensor-label">Влажность (внутри):</span>
                <span class="sensor-value">--</span><span class="sensor-unit">%</span>
            </div>
            <div class="sensor-item">
                <span class="sensor-label">Реле 1 (основное):</span><br>
                <span id="relay1State" class="relay-status relay-off">--</span>
            </div>
            <div class="sensor-item">
                <span class="sensor-label">Реле 2 (доп.):</span><br>
                <span id="relay2State" class="relay-status relay-off">--</span>
            </div>
        </div>
        <div id="lastUpdate">Ожидание данных от теплицы...</div>
    </div>

    <script>
        const ws = new WebSocket('ws://' + window.location.hostname + '/ws');
        let lastGreenhouseUpdate = null;
        let ledState = 'unknown';
        let buttonLocked = false;

        ws.onopen = function() {
            console.log('✅ WebSocket подключён');
            updateLEDButton();
            setTimeout(() => {
                if (ledState === 'unknown') {
                    sendCommand('GET_STATUS');
                }
            }, 1000);
        };

        function updateLEDButton() {
            const btn = document.getElementById('ledToggleBtn');
            
            if (ledState === 'on') {
                btn.textContent = '⏸ ВЫКЛЮЧИТЬ LED';
                btn.className = 'led-on';
                btn.disabled = false;
            } else if (ledState === 'off') {
                btn.textContent = '▶ ВКЛЮЧИТЬ LED';
                btn.className = 'led-off';
                btn.disabled = false;
            } else {
                btn.textContent = '-- (статус неизвестен)';
                btn.className = 'led-unknown';
                btn.disabled = true;
            }
        }

        function toggleLED() {
            if (buttonLocked || ws.readyState !== WebSocket.OPEN) {
                console.log('Кнопка заблокирована или нет связи');
                return;
            }
            
            const btn = document.getElementById('ledToggleBtn');
            const newCmd = (ledState === 'on') ? 'LED_OFF' : 'LED_ON';
            
            console.log('Отправка команды:', newCmd, 'Текущее состояние:', ledState);
            
            buttonLocked = true;
            btn.disabled = true;
            btn.style.opacity = '0.7';
            
            setTimeout(() => {
                if (buttonLocked) {
                    console.log('Таймаут: подтверждение не получено');
                    buttonLocked = false;
                    updateLEDButton();
                }
            }, 5000);
            
            ws.send(JSON.stringify({command: newCmd}));
        }

        function sendCommand(cmd) {
            if (ws.readyState !== WebSocket.OPEN) {
                console.log('Нет связи с WebSocket');
                return;
            }
            ws.send(JSON.stringify({command: cmd}));
            console.log('Отправлена команда:', cmd);
        }

        ws.onmessage = function(event) {
            const msg = JSON.parse(event.data);
            console.log('Получено:', msg.type);

            if (msg.type === 'node_status') {
                console.log('Получен статус LED:', msg.state);
                ledState = msg.state;
                buttonLocked = false;
                updateLEDButton();
            }
            else if (msg.type === 'sensor_data') {
                // ТОЛЬКО данные датчиков, БЕЗ информации о GPIO
                let html = '<div class="sensor-grid">';
                if (msg.aht20) {
                    html += `<div class="sensor-item"><span class="sensor-label">AHT20 (t):</span><span class="sensor-value">${msg.aht20.temp}</span><span class="sensor-unit">°C</span></div>`;
                    html += `<div class="sensor-item"><span class="sensor-label">AHT20 (h):</span><span class="sensor-value">${msg.aht20.hum}</span><span class="sensor-unit">%</span></div>`;
                }
                if (msg.bmp280) {
                    html += `<div class="sensor-item"><span class="sensor-label">BMP280 (t):</span><span class="sensor-value">${msg.bmp280.temp}</span><span class="sensor-unit">°C</span></div>`;
                    html += `<div class="sensor-item"><span class="sensor-label">BMP280 (p):</span><span class="sensor-value">${msg.bmp280.press}</span><span class="sensor-unit">mmHg</span></div>`;
                }
                html += '</div>';
                document.getElementById('nodeSensorData').innerHTML = html;
                
                if (ledState === 'unknown') {
                    sendCommand('GET_STATUS');
                }
            }
            else if (msg.type === 'gpio_status') {
                // ТОЛЬКО обновляем состояние LED, НЕ выводим блок GPIO
                if (msg.gpio8 !== undefined) {
                    ledState = msg.gpio8 ? 'on' : 'off';
                    updateLEDButton();
                    // НЕ добавляем HTML с информацией о GPIO
                }
            }
            else if (msg.type === 'greenhouse_data') {
                lastGreenhouseUpdate = new Date();
                
                const greenhouseData = document.querySelectorAll('#greenhouseData .sensor-value');
                if (greenhouseData.length >= 3) {
                    greenhouseData[0].textContent = msg.temp_in;
                    greenhouseData[1].textContent = msg.temp_out;
                    greenhouseData[2].textContent = msg.hum_in;
                }
                
                // ОБНОВЛЯЕМ статусы реле КАЖДЫЙ раз при получении данных
                updateRelayDisplay('relay1State', msg.relay1_state);
                updateRelayDisplay('relay2State', msg.relay2_state);
                
                document.getElementById('lastUpdate').textContent = `Обновлено: ${lastGreenhouseUpdate.toLocaleTimeString()}`;
            }
        };

        function updateRelayDisplay(elementId, state) {
            const element = document.getElementById(elementId);
            if (state === 1 || state === '1') {
                element.textContent = 'ВКЛЮЧЕНО';
                element.className = 'relay-status relay-on';
            } else {
                element.textContent = 'ВЫКЛЮЧЕНО';
                element.className = 'relay-status relay-off';
            }
        }

        function updateTimeAgo() {
            if (lastGreenhouseUpdate) {
                const secondsAgo = Math.floor((new Date() - lastGreenhouseUpdate) / 1000);
                const elem = document.getElementById('lastUpdate');
                if (secondsAgo < 60) {
                    elem.textContent = `Обновлено: ${secondsAgo} сек. назад`;
                } else {
                    elem.textContent = `Обновлено: ${lastGreenhouseUpdate.toLocaleTimeString()}`;
                }
            }
        }
        
        setInterval(updateTimeAgo, 1000);
        updateLEDButton();

        ws.onclose = function() {
            console.log('WebSocket отключен');
            ledState = 'unknown';
            updateLEDButton();
        };
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

    WiFi.mode(WIFI_AP_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ Ошибка инициализации ESP-NOW!");
        while(1) delay(1000);
    }

    esp_now_register_send_cb(onEspNowDataSent);
    esp_now_register_recv_cb(onEspNowDataRecv);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, nodeMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("❌ Не удалось добавить основной узел!");
    } else {
        Serial.println("✅ Основной узел добавлен.");
    }

    esp_now_peer_info_t greenhousePeerInfo = {};
    memcpy(greenhousePeerInfo.peer_addr, greenhouseMac, 6);
    greenhousePeerInfo.channel = 0;
    greenhousePeerInfo.encrypt = false;
    if (esp_now_add_peer(&greenhousePeerInfo) != ESP_OK) {
        Serial.println("❌ Не удалось добавить теплицу!");
    } else {
        Serial.println("✅ Теплица добавлена.");
    }

    Serial.println("\n=== ХАБ ГОТОВ К РАБОТЕ ===");
    Serial.println("1. Подключитесь к Wi-Fi: " + String(AP_SSID));
    Serial.println("2. Откройте: http://" + WiFi.softAPIP().toString());
    Serial.println("3. Кнопка LED: цвет меняется после подтверждения\n");
}

void loop() {
    ws.cleanupClients();
    delay(10);
}

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

void sendToNode(String cmd) {
    char json_cmd[64];
    snprintf(json_cmd, sizeof(json_cmd), "{\"type\":\"command\",\"command\":\"%s\"}", cmd.c_str());

    strncpy(outgoingMessage.json, json_cmd, sizeof(outgoingMessage.json)-1);
    outgoingMessage.json[sizeof(outgoingMessage.json)-1] = '\0';
    outgoingMessage.sender_id = 1;

    esp_err_t result = esp_now_send(nodeMacAddress, (uint8_t *) &outgoingMessage, sizeof(outgoingMessage));

    if (result == ESP_OK) {
        Serial.print("📡 Отправлена команда на узел: ");
        Serial.println(cmd);
    } else {
        Serial.print("❌ Ошибка отправки: ");
        Serial.println(result);
    }
}

void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    Serial.printf("✉️ Доставка для %s: ", macStr);
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ Успех" : "❌ Ошибка");
}

void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    uint8_t nodeMac[] = {0xAC, 0xEB, 0xE6, 0x49, 0x10, 0x28};
    if (memcmp(mac_addr, nodeMac, 6) == 0) {
        Serial.printf("\n📥 Пакет от основного узла (%s), длина: %d байт\n", macStr, len);
        processNodeData(incomingData, len);
        return;
    }

    uint8_t greenhouseMac[] = {0xE8, 0x9F, 0x6D, 0x87, 0x34, 0x8A};
    if (memcmp(mac_addr, greenhouseMac, 6) == 0) {
        Serial.printf("\n🌿 Пакет от теплицы (%s), длина: %d байт\n", macStr, len);
        
        if (len == sizeof(greenhouse_packet)) {
            processGreenhouseData(incomingData);
        } else {
            Serial.printf("❌ Неожиданная длина пакета! Ожидалось %d, получено %d\n", 
                         sizeof(greenhouse_packet), len);
        }
        return;
    }

    Serial.printf("[ХАБ] Игнорирую неизвестное устройство: %s\n", macStr);
}

void processNodeData(const uint8_t *data, int len) {
    if (len <= sizeof(incomingMessage)) {
        memcpy(&incomingMessage, data, len);
    } else {
        Serial.println("❌ Пакет от узла слишком большой!");
        return;
    }
    
    Serial.print("📥 JSON от узла: ");
    Serial.println(incomingMessage.json);

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, incomingMessage.json);
    
    if (error) {
        Serial.print("❌ Ошибка парсинга JSON: ");
        Serial.println(error.c_str());
        return;
    }
    
    const char* type = doc["type"];
    
    if (strcmp(type, "sensor") == 0) {
        JsonObject dataObj = doc["data"];
        StaticJsonDocument<300> response;
        response["type"] = "sensor_data";
        
        if (dataObj.containsKey("AHT20")) {
            response["aht20"]["temp"] = dataObj["AHT20"]["temp"].as<String>();
            response["aht20"]["hum"] = dataObj["AHT20"]["hum"].as<String>();
        }
        if (dataObj.containsKey("BMP280")) {
            response["bmp280"]["temp"] = dataObj["BMP280"]["temp"].as<String>();
            response["bmp280"]["press"] = dataObj["BMP280"]["press_mmHg"].as<String>();
        }
        
        String jsonResponse;
        serializeJson(response, jsonResponse);
        ws.textAll(jsonResponse);
        Serial.println("📊 Данные с датчиков отправлены в веб-интерфейс.");
    }
    else if (strcmp(type, "ack") == 0) {
        const char* cmd = doc["command"];
        const char* status = doc["status"];
        
        Serial.printf("✅ Подтверждение от узла: команда '%s', статус '%s'\n", cmd, status);
        
        if (strcmp(cmd, "LED_ON") == 0) {
            broadcastWs("node_status", "LED на узле ВКЛЮЧЁН", "on");
        }
        else if (strcmp(cmd, "LED_OFF") == 0) {
            broadcastWs("node_status", "LED на узле ВЫКЛЮЧЕН", "off");
        }
    }
    else if (strcmp(type, "gpio") == 0) {
        // Отправляем только данные о состоянии GPIO для обновления кнопки
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

void processGreenhouseData(const uint8_t *data) {
    greenhouse_packet packet;
    memcpy(&packet, data, sizeof(greenhouse_packet));

    unsigned long now = millis();
    if (now - lastGreenhouseUpdate < GREENHOUSE_UPDATE_INTERVAL) {
        Serial.println("⚠️  Данные теплицы не отправлены в веб (частое обновление)");
        return;
    }
    lastGreenhouseUpdate = now;

    char temp_in_str[5] = {0};
    char temp_out_str[5] = {0};
    strncpy(temp_in_str, packet.temp_in, 4);
    strncpy(temp_out_str, packet.temp_out, 4);

    StaticJsonDocument<300> response;
    response["type"] = "greenhouse_data";
    response["temp_in"] = temp_in_str;
    response["temp_out"] = temp_out_str;
    response["hum_in"] = packet.hum_in;
    response["relay1_state"] = packet.relay1_state;
    response["relay2_state"] = packet.relay2_state;

    String jsonResponse;
    serializeJson(response, jsonResponse);
    ws.textAll(jsonResponse);

    Serial.println("✅ Данные теплицы обработаны и отправлены в веб:");
    Serial.printf("   Температура внутри: %s °C\n", temp_in_str);
    Serial.printf("   Температура улица:  %s °C\n", temp_out_str);
    Serial.printf("   Влажность внутри:   %u %%\n", packet.hum_in);
    Serial.printf("   Реле 1:             %s\n", relayStateToString(packet.relay1_state).c_str());
    Serial.printf("   Реле 2:             %s\n", relayStateToString(packet.relay2_state).c_str());
}

String relayStateToString(uint32_t state) {
    return (state == 1) ? "ВКЛЮЧЕНО" : "ВЫКЛЮЧЕНО";
}

void broadcastWs(String type, String text, String state) {
    StaticJsonDocument<200> doc;
    doc["type"] = type;
    doc["text"] = text;
    if (state.length() > 0) doc["state"] = state;
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    ws.textAll(jsonResponse);
}