#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <esp_now.h>
#include <ArduinoJson.h>

String getHubMacAddress() {
    return WiFi.softAPmacAddress();
}

const char* ssid = "ESP32-Now-Hub";
const char* password = "12345678";
String slaveMac = "";
AsyncWebServer server(80);
esp_now_peer_info_t peerInfo;

// HTML страница с кнопками управления (ВАЖНО: добавлен тег <meta charset="UTF-8">)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>ESP-NOW Hub Controller</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; text-align: center; margin: 40px; background-color: #f4f4f4; }
        h1 { color: #333; }
        .btn {
            background-color: #4CAF50; border: none; color: white; padding: 15px 32px;
            text-align: center; text-decoration: none; display: inline-block;
            font-size: 18px; margin: 10px; cursor: pointer; border-radius: 8px;
            width: 220px; transition: background-color 0.3s;
        }
        .btn:hover { background-color: #45a049; }
        .btn-off { background-color: #f44336; }
        .btn-off:hover { background-color: #d32f2f; }
        .btn-status { background-color: #008CBA; }
        .btn-status:hover { background-color: #007B9A; }
        #responseContainer {
            margin-top: 30px; padding: 20px; background-color: white; border-radius: 10px;
            box-shadow: 0 4px 8px rgba(0,0,0,0.1); min-height: 100px;
            text-align: left; white-space: pre-wrap; font-family: monospace;
        }
        .info-box {
            background: #f8f9fa; border: 1px solid #dee2e6; border-radius: 8px;
            padding: 15px; margin: 20px 0; box-shadow: 0 2px 4px rgba(0,0,0,0.05);
        }
        .info-box h3 { margin-top: 0; color: #495057; font-size: 1.1em; }
        .mac-container { display: flex; flex-direction: column; gap: 10px; margin: 15px 0; }
        .mac-item {
            display: flex; align-items: center; background: white; padding: 8px 12px;
            border-radius: 6px; border: 1px solid #e9ecef;
        }
        .mac-label { font-weight: bold; color: #495057; min-width: 80px; }
        .mac-value {
            font-family: monospace; background: #e9ecef; padding: 4px 8px;
            border-radius: 4px; flex-grow: 1; margin: 0 10px; font-size: 0.9em;
        }
        .copy-btn {
            background: #6c757d; color: white; border: none; border-radius: 4px;
            padding: 4px 8px; cursor: pointer; font-size: 0.9em;
        }
        .copy-btn:hover { background: #5a6268; }
        .info-text { font-size: 0.85em; color: #6c757d; margin: 10px 0 0 0; font-style: italic; }
    </style>
</head>
<body>
    <h1>Контроллер умного дома ESP-NOW</h1>
    <p>Управление ESP32-C3 Узлом</p>
    <button class="btn" onclick="sendCommand('ON')">Включить светодиод</button>
    <button class="btn btn-off" onclick="sendCommand('OFF')">Выключить светодиод</button>
    <button class="btn btn-status" onclick="sendCommand('STATUS')">Запрос статуса</button>
    <div id="responseContainer">Ответы от узла появятся здесь...</div>
    <div class="info-box">
        <h3>🔧 Информация для настройки</h3>
        <div class="mac-container">
            <div class="mac-item">
                <span class="mac-label">MAC хаба:</span>
                <span class="mac-value" id="hubMac">%HUB_MAC%</span>
                <button class="copy-btn" onclick="copyToClipboard('hubMac')">📋</button>
            </div>
            <div class="mac-item">
                <span class="mac-label">MAC узла:</span>
                <span class="mac-value" id="nodeMac">Не получен</span>
                <button class="copy-btn" onclick="copyToClipboard('nodeMac')">📋</button>
            </div>
        </div>
        <p class="info-text">Используйте MAC хаба для настройки узла в коде</p>
    </div>
    <script>
        function sendCommand(cmd) {
            fetch('/control?command=' + cmd)
                .then(response => response.text())
                .then(data => {
                    document.getElementById('responseContainer').innerHTML = 
                        'Команда "' + cmd + '" отправлена\n' + data;
                })
                .catch(error => {
                    document.getElementById('responseContainer').innerHTML = 'Ошибка: ' + error;
                });
        }
        function copyToClipboard(elementId) {
            const macElement = document.getElementById(elementId);
            const macText = macElement.textContent;
            navigator.clipboard.writeText(macText).then(() => {
                const originalText = macElement.textContent;
                macElement.textContent = 'Скопировано!';
                macElement.style.color = '#28a745';
                setTimeout(() => {
                    macElement.textContent = originalText;
                    macElement.style.color = '';
                }, 1000);
            });
        }
    </script>
</body>
</html>
)rawliteral";

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("Статус отправки ESP-NOW: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Успех" : "Ошибка");
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    slaveMac = String(macStr);
    Serial.print("Получено данных от: ");
    Serial.println(slaveMac);
    Serial.print("Данные: ");
    Serial.write(incomingData, len);
    Serial.println();
}

void setup() {
    Serial.begin(9600);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    Serial.println("Access Point создан!");
    Serial.print("IP адрес: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("MAC адрес хаба: ");
    Serial.println(getHubMacAddress());
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("Ошибка инициализации ESP-NOW");
        return;
    }
    
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = FPSTR(index_html);
        html.replace("%HUB_MAC%", getHubMacAddress());
        request->send(200, "text/html", html);
    });
    
    server.on("/control", HTTP_GET, [](AsyncWebServerRequest *request) {
        String command;
        if (request->hasParam("command")) {
            command = request->getParam("command")->value();
            Serial.print("Получена команда: ");
            Serial.println(command);
            
            if (slaveMac.length() > 0) {
                uint8_t slaveMacAddr[6];
                sscanf(slaveMac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                       &slaveMacAddr[0], &slaveMacAddr[1], &slaveMacAddr[2],
                       &slaveMacAddr[3], &slaveMacAddr[4], &slaveMacAddr[5]);
                memset(&peerInfo, 0, sizeof(peerInfo));
                memcpy(peerInfo.peer_addr, slaveMacAddr, 6);
                peerInfo.channel = 0;
                peerInfo.encrypt = false;
                
                if (esp_now_add_peer(&peerInfo) != ESP_OK) {
                    Serial.println("Ошибка добавления пира");
                    request->send(200, "text/plain", "Ошибка: не удалось добавить устройство");
                    return;
                }
                esp_err_t result = esp_now_send(slaveMacAddr, 
                    (const uint8_t *)command.c_str(), command.length());
                
                if (result == ESP_OK) {
                    request->send(200, "text/plain", "Команда отправлена на устройство: " + slaveMac);
                } else {
                    request->send(200, "text/plain", "Ошибка отправки команды");
                }
            } else {
                request->send(200, "text/plain", "Ошибка: MAC адрес устройства не известен");
            }
        } else {
            request->send(400, "text/plain", "Отсутствует параметр command");
        }
    });
    
    server.begin();
    Serial.println("HTTP сервер запущен");
}

void loop() {}