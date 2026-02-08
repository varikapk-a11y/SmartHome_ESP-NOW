/**
 * SmartHome ESP-NOW Hub (ESP32) с охраной
 * Универсальная версия с JSON структурой
 * ВЕРСИЯ 2.5: Компактный интерфейс с аудио-тревогой
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

// ---- 4. ГЛОБАЛЬНЫЕ ОБЪЕКТЫ И ПЕРЕМЕННЫЕ ----
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
esp_now_message outgoingMessage;
esp_now_message incomingMessage;

unsigned long lastGreenhouseUpdate = 0;
const unsigned long GREENHOUSE_UPDATE_INTERVAL = 30000;

// Переменные для системы охраны
bool securityAlarm = false;
bool contact1Alarm = false;
bool contact2Alarm = false;
unsigned long lastSecurityUpdate = 0;

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
void updateSecurityDisplay();
void processSecurityData(const JsonObject& securityData);

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== SmartHome ESP-NOW Hub (Версия 2.5) ===");
    Serial.println("=== Компактный интерфейс с аудио-тревогой ===");

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
    <title>Умный дом ESP-NOW</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 8px auto;
            padding: 0;
            max-width: 780px;
            background-color: #f8f9fa;
        }
        
        /* ЗАГОЛОВОЧНАЯ СТРОКА */
        .header-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
            padding: 0 5px;
        }
        .header-title {
            color: #2c3e50;
            font-size: 1.6em;
            font-weight: bold;
            margin: 0;
        }
        #refreshBtn {
            font-size: 13px;
            padding: 8px 16px;
            background: #3498db;
            color: white;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            font-weight: bold;
            transition: all 0.3s;
            white-space: nowrap;
        }
        #refreshBtn:hover {
            background: #2980b9;
            transform: translateY(-1px);
        }
        
        .section {
            background: white;
            border-radius: 8px;
            padding: 12px;
            margin: 12px 0;
            box-shadow: 0 2px 4px rgba(0,0,0,0.08);
            border: 1px solid #e0e0e0;
        }
        .section-title {
            font-size: 1.2em;
            margin-bottom: 8px;
            color: #2c3e50;
            border-bottom: 2px solid #3498db;
            padding-bottom: 4px;
            font-weight: bold;
        }
        
        /* СЕКЦИЯ МАСТЕРСКОЙ (с охраной) */
        .workshop-container {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
            margin-top: 8px;
        }
        .security-box {
            background: #f8f9fa;
            padding: 10px;
            border-radius: 6px;
            border: 1px solid #dee2e6;
        }
        .security-item {
            margin-bottom: 8px;
            padding: 6px;
            border-radius: 4px;
            background: white;
            border-left: 3px solid #2ecc71;
        }
        .security-item.alarm {
            border-left-color: #e74c3c;
            background: #ffeaea;
            animation: pulse 1.5s infinite;
        }
        @keyframes pulse {
            0% { box-shadow: 0 0 0 0 rgba(231, 76, 60, 0.3); }
            70% { box-shadow: 0 0 0 6px rgba(231, 76, 60, 0); }
            100% { box-shadow: 0 0 0 0 rgba(231, 76, 60, 0); }
        }
        .security-label {
            font-weight: bold;
            color: #555;
            font-size: 0.85em;
            display: block;
            margin-bottom: 2px;
        }
        .security-status {
            font-size: 0.9em;
            font-weight: bold;
            padding: 3px 8px;
            border-radius: 12px;
            display: inline-block;
        }
        .status-normal {
            background-color: #2ecc71;
            color: white;
        }
        .status-alarm {
            background-color: #e74c3c;
            color: white;
        }
        
        /* КНОПКА УПРАВЛЕНИЯ LED */
        #ledToggleBtn {
            font-size: 13px;
            padding: 8px 16px;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            color: white;
            font-weight: bold;
            transition: all 0.3s;
            width: 100%;
            margin-top: 8px;
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
            opacity: 0.6;
        }
        
        .sensor-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
            gap: 10px;
            margin-top: 8px;
        }
        .sensor-item {
            background: #f8f9fa;
            padding: 10px;
            border-radius: 6px;
            border: 1px solid #dee2e6;
        }
        .sensor-label {
            font-weight: bold;
            color: #555;
            font-size: 0.8em;
            display: block;
            margin-bottom: 3px;
        }
        .sensor-value {
            font-size: 1.3em;
            font-family: 'Courier New', monospace;
            color: #2c3e50;
        }
        .sensor-unit {
            font-size: 0.75em;
            color: #6c757d;
            margin-left: 2px;
        }
        .relay-status {
            display: inline-block;
            padding: 3px 8px;
            border-radius: 12px;
            font-weight: bold;
            font-size: 0.85em;
        }
        .relay-on {
            background-color: #27ae60;
            color: white;
        }
        .relay-off {
            background-color: #e74c3c;
            color: white;
        }
        
        .node-info {
            color: #6c757d;
            font-size: 0.75em;
            margin-bottom: 6px;
        }
        .update-time {
            font-size: 0.7em;
            color: #adb5bd;
            text-align: right;
            margin-top: 6px;
            font-style: italic;
        }
        
        /* АУДИО ЭЛЕМЕНТ (скрыт) */
        #alarmAudio {
            display: none;
        }
        
        /* ИНДИКАТОР ТРЕВОГИ */
        .alarm-indicator {
            position: fixed;
            top: 10px;
            right: 10px;
            background: #e74c3c;
            color: white;
            padding: 5px 10px;
            border-radius: 15px;
            font-weight: bold;
            font-size: 0.8em;
            display: none;
            z-index: 1000;
            box-shadow: 0 2px 8px rgba(0,0,0,0.2);
        }
    </style>
</head>
<body>
    <!-- ЗАГОЛОВОЧНАЯ СТРОКА -->
    <div class="header-row">
        <h1 class="header-title">🏠 Умный дом ESP-NOW</h1>
        <button id="refreshBtn" onclick="sendCommand('GET_STATUS')">🔄 ОБНОВИТЬ</button>
    </div>

    <!-- СЕКЦИЯ МАСТЕРСКОЙ (с охраной) -->
    <div class="section">
        <div class="section-title">🔧 Мастерская (ID: 101)</div>
        <div class="node-info">MAC: AC:EB:E6:49:10:28 | Концевики: GPIO3, GPIO4</div>
        
        <div class="workshop-container">
            <!-- ЛЕВАЯ КОЛОНКА: Датчики и управление -->
            <div>
                <div id="nodeSensorData">
                    <div class="sensor-grid">
                        <div class="sensor-item">
                            <span class="sensor-label">AHT20 (темп.):</span>
                            <span class="sensor-value">--</span><span class="sensor-unit">°C</span>
                        </div>
                        <div class="sensor-item">
                            <span class="sensor-label">AHT20 (влажн.):</span>
                            <span class="sensor-value">--</span><span class="sensor-unit">%</span>
                        </div>
                        <div class="sensor-item">
                            <span class="sensor-label">BMP280 (темп.):</span>
                            <span class="sensor-value">--</span><span class="sensor-unit">°C</span>
                        </div>
                        <div class="sensor-item">
                            <span class="sensor-label">BMP280 (давл.):</span>
                            <span class="sensor-value">--</span><span class="sensor-unit">мм</span>
                        </div>
                    </div>
                </div>
                
                <button id="ledToggleBtn" class="led-unknown" onclick="toggleLED()">--</button>
            </div>
            
            <!-- ПРАВАЯ КОЛОНКА: Система охраны -->
            <div class="security-box">
                <div style="font-weight: bold; color: #2c3e50; margin-bottom: 8px; font-size: 0.9em;">
                    🔒 Система охраны
                </div>
                
                <div class="security-item" id="contact1Item">
                    <span class="security-label">Концевик 1 (GPIO3)</span>
                    <span id="contact1Status" class="security-status status-normal">НОРМА</span>
                </div>
                
                <div class="security-item" id="contact2Item">
                    <span class="security-label">Концевик 2 (GPIO4)</span>
                    <span id="contact2Status" class="security-status status-normal">НОРМА</span>
                </div>
                
                <div id="securityLastUpdate" class="update-time">Ожидание данных...</div>
            </div>
        </div>
    </div>

    <!-- СЕКЦИЯ ТЕПЛИЦЫ -->
    <div class="section">
        <div class="section-title">🌿 Теплица (ID: 102)</div>
        <div class="node-info">MAC: E8:9F:6D:87:34:8A | Автообновление: 30 сек.</div>
        
        <div class="sensor-grid" id="greenhouseData">
            <div class="sensor-item">
                <span class="sensor-label">Темп. внутри:</span>
                <span class="sensor-value">--</span><span class="sensor-unit">°C</span>
            </div>
            <div class="sensor-item">
                <span class="sensor-label">Темп. улица:</span>
                <span class="sensor-value">--</span><span class="sensor-unit">°C</span>
            </div>
            <div class="sensor-item">
                <span class="sensor-label">Влажность:</span>
                <span class="sensor-value">--</span><span class="sensor-unit">%</span>
            </div>
            <div class="sensor-item">
                <span class="sensor-label">Реле 1:</span>
                <span id="relay1State" class="relay-status relay-off">--</span>
            </div>
            <div class="sensor-item">
                <span class="sensor-label">Реле 2:</span>
                <span id="relay2State" class="relay-status relay-off">--</span>
            </div>
        </div>
        <div id="lastUpdate" class="update-time">Ожидание данных...</div>
    </div>

    <!-- АУДИО ДЛЯ ТРЕВОГИ (базовые тоны для Android) -->
    <audio id="alarmAudio" preload="auto">
        <source src="data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAZGF0YQQAAAAAAA==" type="audio/wav">
    </audio>
    
    <!-- ИНДИКАТОР ТРЕВОГИ -->
    <div id="alarmIndicator" class="alarm-indicator">🚨 ТРЕВОГА!</div>

    <script>
        const ws = new WebSocket('ws://' + window.location.hostname + '/ws');
        let ledState = 'unknown';
        let buttonLocked = false;
        let securityAlarm = false;
        let contact1Alarm = false;
        let contact2Alarm = false;
        let alarmAudio = null;
        let alarmTimeout = null;
        let isAudioPlaying = false;

        ws.onopen = function() {
            console.log('✅ WebSocket подключён');
            updateLEDButton();
            initAudio();
            setTimeout(() => {
                if (ledState === 'unknown') {
                    sendCommand('GET_STATUS');
                }
            }, 1000);
        };

        function initAudio() {
            alarmAudio = document.getElementById('alarmAudio');
            if (!alarmAudio) {
                console.log('Аудио элемент не найден');
                return;
            }
            
            // Создаем простой аудио сигнал тревоги (бип-бип)
            try {
                const audioContext = new (window.AudioContext || window.webkitAudioContext)();
                const oscillator = audioContext.createOscillator();
                const gainNode = audioContext.createGain();
                
                oscillator.connect(gainNode);
                gainNode.connect(audioContext.destination);
                
                oscillator.frequency.value = 800;
                oscillator.type = 'sine';
                gainNode.gain.value = 0;
                
                // Создаем бип-бип сигнал
                let time = audioContext.currentTime;
                gainNode.gain.setValueAtTime(0, time);
                
                // Первый бип
                gainNode.gain.linearRampToValueAtTime(0.3, time + 0.1);
                gainNode.gain.linearRampToValueAtTime(0, time + 0.2);
                
                // Пауза
                gainNode.gain.setValueAtTime(0, time + 0.3);
                
                // Второй бип
                gainNode.gain.linearRampToValueAtTime(0.3, time + 0.4);
                gainNode.gain.linearRampToValueAtTime(0, time + 0.5);
                
                oscillator.start(time);
                oscillator.stop(time + 0.6);
                
                console.log('Аудио система инициализирована');
            } catch (e) {
                console.log('Web Audio API не поддерживается:', e);
                fallbackAudio();
            }
        }

        function fallbackAudio() {
            // Резервный вариант: вибрация на мобильных
            if (navigator.vibrate) {
                console.log('Используется вибрация вместо звука');
            }
        }

        function playAlarmSound() {
            if (isAudioPlaying) return;
            
            isAudioPlaying = true;
            const alarmIndicator = document.getElementById('alarmIndicator');
            
            // Показываем индикатор тревоги
            alarmIndicator.style.display = 'block';
            
            // Вибрируем на мобильных
            if (navigator.vibrate) {
                navigator.vibrate([200, 100, 200, 100, 200, 100, 500]);
            }
            
            // Пытаемся воспроизвести звук
            if (alarmAudio) {
                try {
                    alarmAudio.currentTime = 0;
                    alarmAudio.play().catch(e => {
                        console.log('Ошибка воспроизведения звука:', e);
                        visualAlarm();
                    });
                } catch (e) {
                    visualAlarm();
                }
            } else {
                visualAlarm();
            }
            
            // Останавливаем через 10 секунд
            if (alarmTimeout) clearTimeout(alarmTimeout);
            alarmTimeout = setTimeout(stopAlarm, 10000);
        }

        function visualAlarm() {
            // Визуальная тревога если звук не работает
            const items = document.querySelectorAll('.security-item.alarm');
            items.forEach(item => {
                item.style.animation = 'pulse 0.8s infinite';
            });
            
            // Мигаем заголовком
            const title = document.querySelector('.header-title');
            let blinkCount = 0;
            const blinkInterval = setInterval(() => {
                title.style.color = title.style.color === 'red' ? '#2c3e50' : 'red';
                blinkCount++;
                if (blinkCount > 10) {
                    clearInterval(blinkInterval);
                    title.style.color = '#2c3e50';
                }
            }, 500);
        }

        function stopAlarm() {
            isAudioPlaying = false;
            const alarmIndicator = document.getElementById('alarmIndicator');
            alarmIndicator.style.display = 'none';
            
            if (alarmAudio) {
                alarmAudio.pause();
                alarmAudio.currentTime = 0;
            }
            
            if (navigator.vibrate) {
                navigator.vibrate(0);
            }
            
            const items = document.querySelectorAll('.security-item');
            items.forEach(item => {
                item.style.animation = '';
            });
        }

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

        function updateSecurityDisplay() {
            const contact1Elem = document.getElementById('contact1Status');
            const contact2Elem = document.getElementById('contact2Status');
            const contact1Item = document.getElementById('contact1Item');
            const contact2Item = document.getElementById('contact2Item');
            const securityUpdateElem = document.getElementById('securityLastUpdate');
            
            // Концевик 1
            if (contact1Alarm) {
                contact1Elem.textContent = 'ТРЕВОГА';
                contact1Elem.className = 'security-status status-alarm';
                contact1Item.className = 'security-item alarm';
            } else {
                contact1Elem.textContent = 'НОРМА';
                contact1Elem.className = 'security-status status-normal';
                contact1Item.className = 'security-item';
            }
            
            // Концевик 2
            if (contact2Alarm) {
                contact2Elem.textContent = 'ТРЕВОГА';
                contact2Elem.className = 'security-status status-alarm';
                contact2Item.className = 'security-item alarm';
            } else {
                contact2Elem.textContent = 'НОРМА';
                contact2Elem.className = 'security-status status-normal';
                contact2Item.className = 'security-item';
            }
            
            // Общая тревога
            const newSecurityAlarm = contact1Alarm || contact2Alarm;
            if (newSecurityAlarm && !securityAlarm) {
                // Только если перешли из состояния "норма" в "тревога"
                playAlarmSound();
            } else if (!newSecurityAlarm && securityAlarm) {
                // Только если перешли из "тревога" в "норма"
                stopAlarm();
            }
            securityAlarm = newSecurityAlarm;
            
            // Время обновления
            const now = new Date();
            securityUpdateElem.textContent = 'Обновлено: ' + now.toLocaleTimeString().slice(0, 5);
        }

        function toggleLED() {
            if (buttonLocked || ws.readyState !== WebSocket.OPEN) return;
            
            const btn = document.getElementById('ledToggleBtn');
            const newCmd = (ledState === 'on') ? 'LED_OFF' : 'LED_ON';
            
            buttonLocked = true;
            btn.disabled = true;
            btn.style.opacity = '0.6';
            
            setTimeout(() => {
                if (buttonLocked) {
                    buttonLocked = false;
                    updateLEDButton();
                }
            }, 5000);
            
            ws.send(JSON.stringify({command: newCmd}));
        }

        function sendCommand(cmd) {
            if (ws.readyState !== WebSocket.OPEN) return;
            ws.send(JSON.stringify({command: cmd}));
        }

        ws.onmessage = function(event) {
            const msg = JSON.parse(event.data);
            console.log('Получено:', msg.type);

            if (msg.type === 'node_status') {
                ledState = msg.state;
                buttonLocked = false;
                updateLEDButton();
            }
            else if (msg.type === 'sensor_data') {
                if (msg.aht20) {
                    document.querySelector('#nodeSensorData .sensor-item:nth-child(1) .sensor-value').textContent = msg.aht20.temp;
                    document.querySelector('#nodeSensorData .sensor-item:nth-child(2) .sensor-value').textContent = msg.aht20.hum;
                }
                if (msg.bmp280) {
                    document.querySelector('#nodeSensorData .sensor-item:nth-child(3) .sensor-value').textContent = msg.bmp280.temp;
                    document.querySelector('#nodeSensorData .sensor-item:nth-child(4) .sensor-value').textContent = msg.bmp280.press_mmHg;
                }
            }
            else if (msg.type === 'security_data') {
                contact1Alarm = msg.contact1;
                contact2Alarm = msg.contact2;
                updateSecurityDisplay();
            }
            else if (msg.type === 'greenhouse_data') {
                if (msg.temp_in) {
                    document.querySelector('#greenhouseData .sensor-item:nth-child(1) .sensor-value').textContent = msg.temp_in;
                }
                if (msg.temp_out) {
                    document.querySelector('#greenhouseData .sensor-item:nth-child(2) .sensor-value').textContent = msg.temp_out;
                }
                if (msg.hum_in) {
                    document.querySelector('#greenhouseData .sensor-item:nth-child(3) .sensor-value').textContent = msg.hum_in;
                }
                if (msg.relay1) {
                    const relay1Elem = document.getElementById('relay1State');
                    relay1Elem.textContent = msg.relay1;
                    relay1Elem.className = msg.relay1 === 'ВКЛ' ? 'relay-status relay-on' : 'relay-status relay-off';
                }
                if (msg.relay2) {
                    const relay2Elem = document.getElementById('relay2State');
                    relay2Elem.textContent = msg.relay2;
                    relay2Elem.className = msg.relay2 === 'ВКЛ' ? 'relay-status relay-on' : 'relay-status relay-off';
                }
                
                const now = new Date();
                document.getElementById('lastUpdate').textContent = 'Обновлено: ' + now.toLocaleTimeString().slice(0, 5);
            }
        };

        // Остановка тревоги при уходе со страницы
        window.addEventListener('beforeunload', stopAlarm);
        window.addEventListener('pagehide', stopAlarm);
        
        // Инициализация
        updateSecurityDisplay();
    </script>
</body>
</html>
)rawliteral";
        request->send(200, "text/html", html);
    });

    ws.onEvent(onWebSocketEvent);
    server.addHandler(&ws);
    server.begin();
    Serial.println("[1] Веб-сервер запущен.");

    WiFi.mode(WIFI_AP_STA);
    Serial.print("[2] STA MAC адрес хаба: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ Ошибка инициализации ESP-NOW!");
        while (1) delay(1000);
    }
    Serial.println("[3] ESP-NOW инициализирован.");

    esp_now_register_send_cb(onEspNowDataSent);
    esp_now_register_recv_cb(onEspNowDataRecv);

    esp_now_peer_info_t nodePeer = {};
    memcpy(nodePeer.peer_addr, nodeMacAddress, 6);
    nodePeer.channel = 0;
    nodePeer.encrypt = false;
    if (esp_now_add_peer(&nodePeer) != ESP_OK) {
        Serial.println("[4A] ❌ Не удалось добавить узел как пир!");
    } else {
        Serial.println("[4A] ✅ Основной узел добавлен как пир.");
    }

    esp_now_peer_info_t greenhousePeer = {};
    memcpy(greenhousePeer.peer_addr, greenhouseMac, 6);
    greenhousePeer.channel = 0;
    greenhousePeer.encrypt = false;
    if (esp_now_add_peer(&greenhousePeer) != ESP_OK) {
        Serial.println("[4B] ❌ Не удалось добавить теплицу как пир!");
    } else {
        Serial.println("[4B] ✅ Теплица добавлена как пир.");
    }

    Serial.println("\n=== ХАБ ГОТОВ К РАБОТЕ ===");
    Serial.println("Подключитесь к Wi-Fi: SmartHome-Hub");
    Serial.println("Откройте в браузере IP адрес выше");
}

// ===================== LOOP =====================
void loop() {
    ws.cleanupClients();
    delay(10);
}

// ===================== ФУНКЦИИ =====================
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("✅ WebSocket клиент #%u подключен\n", client->id());
        client->ping();
    }
    else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("❌ WebSocket клиент #%u отключен\n", client->id());
    }
    else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            String message = String((char*)data);
            
            StaticJsonDocument<128> doc;
            DeserializationError error = deserializeJson(doc, message);
            
            if (!error) {
                const char* command = doc["command"];
                Serial.printf("📨 WebSocket команда: %s\n", command);
                
                if (strcmp(command, "LED_ON") == 0) {
                    sendToNode("{\"type\":\"command\",\"command\":\"LED_ON\"}");
                }
                else if (strcmp(command, "LED_OFF") == 0) {
                    sendToNode("{\"type\":\"command\",\"command\":\"LED_OFF\"}");
                }
                else if (strcmp(command, "GET_STATUS") == 0) {
                    sendToNode("{\"type\":\"command\",\"command\":\"GET_STATUS\"}");
                }
            }
        }
    }
}

void sendToNode(String cmd) {
    strncpy(outgoingMessage.json, cmd.c_str(), sizeof(outgoingMessage.json)-1);
    outgoingMessage.json[sizeof(outgoingMessage.json)-1] = '\0';
    outgoingMessage.sender_id = 255;
    
    esp_err_t result = esp_now_send(nodeMacAddress, (uint8_t *) &outgoingMessage, sizeof(outgoingMessage));
    if (result != ESP_OK) {
        Serial.printf("⚠️ Ошибка отправки команды: %d\n", result);
    } else {
        Serial.print("📤 Команда отправлена: ");
        Serial.println(cmd);
    }
}

void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);
    
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.printf("✅ Данные успешно отправлены на %s\n", macStr);
    } else {
        Serial.printf("⚠️ Ошибка отправки на %s\n", macStr);
    }
}

void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    bool fromNode = (memcmp(mac_addr, nodeMacAddress, 6) == 0);
    bool fromGreenhouse = (memcmp(mac_addr, greenhouseMac, 6) == 0);
    
    if (!fromNode && !fromGreenhouse) {
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        Serial.printf("⚠️ Неизвестный MAC: %s, длина: %d\n", macStr, len);
        return;
    }
    
    if (fromGreenhouse) {
        processGreenhouseData(incomingData);
    } else if (fromNode) {
        processNodeData(incomingData, len);
    }
}

void processGreenhouseData(const uint8_t *data) {
    if (sizeof(greenhouse_packet) != 84) {
        Serial.printf("❌ Размер структуры теплицы неверный: %d (ожидалось 84)\n", sizeof(greenhouse_packet));
        return;
    }
    
    greenhouse_packet packet;
    memcpy(&packet, data, sizeof(greenhouse_packet));
    
    String temp_in = String(packet.temp_in);
    String temp_out = String(packet.temp_out);
    float hum_in = packet.hum_in / 10.0;
    
    Serial.println("\n=== ДАННЫЕ ОТ ТЕПЛИЦЫ ===");
    Serial.printf("Температура внутри: %s°C\n", temp_in.c_str());
    Serial.printf("Температура снаружи: %s°C\n", temp_out.c_str());
    Serial.printf("Влажность внутри: %.1f%%\n", hum_in);
    Serial.printf("Реле 1: %s\n", relayStateToString(packet.relay1_state));
    Serial.printf("Реле 2: %s\n", relayStateToString(packet.relay2_state));
    
    String json = "{";
    json += "\"type\":\"greenhouse_data\",";
    json += "\"temp_in\":\"" + temp_in + "\",";
    json += "\"temp_out\":\"" + temp_out + "\",";
    json += "\"hum_in\":\"" + String(hum_in, 1) + "\",";
    json += "\"relay1\":\"" + relayStateToString(packet.relay1_state) + "\",";
    json += "\"relay2\":\"" + relayStateToString(packet.relay2_state) + "\"";
    json += "}";
    
    ws.textAll(json);
    lastGreenhouseUpdate = millis();
}

void processNodeData(const uint8_t *data, int len) {
    if (len != sizeof(esp_now_message)) {
        Serial.printf("⚠️ Неверная длина пакета от узла: %d (ожидалось %d)\n", len, sizeof(esp_now_message));
        return;
    }
    
    memcpy(&incomingMessage, data, sizeof(incomingMessage));
    
    Serial.print("📥 JSON от узла: ");
    Serial.println(incomingMessage.json);
    
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, incomingMessage.json);
    
    if (error) {
        Serial.print("❌ Ошибка парсинга JSON от узла: ");
        Serial.println(error.c_str());
        return;
    }
    
    const char* type = doc["type"];
    
    if (strcmp(type, "sensor") == 0) {
        JsonObject dataObj = doc["data"];
        JsonObject aht20 = dataObj["AHT20"];
        JsonObject bmp280 = dataObj["BMP280"];
        
        String json = "{";
        json += "\"type\":\"sensor_data\",";
        
        if (!aht20.isNull()) {
            json += "\"aht20\":{";
            json += "\"temp\":\"" + String(aht20["temp"].as<float>(), 1) + "\",";
            json += "\"hum\":\"" + String(aht20["hum"].as<float>(), 1) + "\"";
            json += "},";
        }
        
        if (!bmp280.isNull()) {
            json += "\"bmp280\":{";
            json += "\"temp\":\"" + String(bmp280["temp"].as<float>(), 1) + "\",";
            json += "\"press_mmHg\":\"" + String(bmp280["press_mmHg"].as<float>(), 1) + "\"";
            json += "}";
        }
        
        if (json.endsWith(",")) {
            json.remove(json.length() - 1);
        }
        
        json += "}";
        
        ws.textAll(json);
    }
    else if (strcmp(type, "gpio") == 0) {
        int state = doc["state"];
        String json = "{\"type\":\"node_status\",\"state\":\"" + String(state == 1 ? "on" : "off") + "\"}";
        ws.textAll(json);
    }
    else if (strcmp(type, "ack") == 0) {
        const char* command = doc["command"];
        Serial.printf("✅ Подтверждение от узла: %s\n", command);
    }
    else if (strcmp(type, "security") == 0) {
        JsonObject securityData = doc.as<JsonObject>();
        processSecurityData(securityData);
    }
}

void processSecurityData(const JsonObject& securityData) {
    securityAlarm = securityData["alarm"];
    contact1Alarm = securityData["contact1"];
    contact2Alarm = securityData["contact2"];
    
    Serial.println("\n=== ДАННЫЕ ОХРАНЫ ===");
    Serial.printf("Общая тревога: %s\n", securityAlarm ? "ДА" : "НЕТ");
    Serial.printf("Концевик 1 (GPIO3): %s\n", contact1Alarm ? "ТРЕВОГА" : "НОРМА");
    Serial.printf("Концевик 2 (GPIO4): %s\n", contact2Alarm ? "ТРЕВОГА" : "НОРМА");
    
    String json = "{";
    json += "\"type\":\"security_data\",";
    json += "\"alarm\":" + String(securityAlarm ? "true" : "false") + ",";
    json += "\"contact1\":" + String(contact1Alarm ? "true" : "false") + ",";
    json += "\"contact2\":" + String(contact2Alarm ? "true" : "false");
    json += "}";
    
    ws.textAll(json);
    lastSecurityUpdate = millis();
}

String relayStateToString(uint32_t state) {
    return (state == 1) ? "ВКЛ" : "ВЫКЛ";
}