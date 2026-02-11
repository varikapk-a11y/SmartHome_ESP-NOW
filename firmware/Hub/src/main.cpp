/**
 * SmartHome ESP-NOW Hub (ESP32)
 * 
 */ВЕРСИЯ 5.2: ПОЛНОСТЬЮ РАБОЧАЯ - ветер, желтый сектор 30 сек, штиль/шторм
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <math.h>

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

unsigned long lastNodeDataTime = 0;
unsigned long lastGreenhouseUpdate = 0;
const unsigned long NODE_TIMEOUT_MS = 70000;
const unsigned long GREENHOUSE_UPDATE_INTERVAL = 30000;

bool nodeConnectionLost = false;
unsigned long connectionLostTime = 0;
const unsigned long CONNECTION_LOST_COOLDOWN = 10000;

bool securityAlarmActive = false;
unsigned long alarmStartTime = 0;
const unsigned long ALARM_DURATION_MS = 10000;

// ---- 5. ДАННЫЕ ЭНКОДЕРА AS5600 - ДВЕ ТОЧКИ + ИСТОРИЯ 30 СЕК ----
#define ENCODER_HISTORY_SIZE 6        // 6 значений = 30 секунд при 5 сек
#define ENCODER_BROADCAST_INTERVAL 5000

// Текущие две точки
float prevEncoderAngle = -1.0;
float currentEncoderAngle = -1.0;
float windDirection = 0.0;
float windCurrentSector = 0.0;
bool windMagnet = false;

// История для желтого сектора
float encoderHistory[ENCODER_HISTORY_SIZE];
unsigned long historyTimestamps[ENCODER_HISTORY_SIZE];
int historyIndex = 0;
int historyCount = 0;

// Максимум и минимум за 30 секунд
float maxAngle = -1.0;
float minAngle = 361.0;
unsigned long lastEncoderBroadcastTime = 0;

// ---- 6. ПРОТОТИПЫ ----
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len);
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void sendToNode(String cmd);
void processGreenhouseData(const uint8_t *data);
void processNodeData(const uint8_t *data, int len);
String relayStateToString(uint32_t state);
void checkNodeConnection();
void updateAlarmState();
void sendConnectionStatusToWeb(bool connected);
void processEncoderData(float angle, bool magnet);
void updateHistory(float angle);
void updateMaxMin();
void broadcastEncoderData();

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== SmartHome ESP-NOW Hub (Версия 5.2) ===");
    Serial.println("=== ПОЛНОСТЬЮ РАБОЧАЯ ВЕРСИЯ ===");

    // ИНИЦИАЛИЗАЦИЯ ИСТОРИИ
    historyCount = 0;
    historyIndex = 0;
    minAngle = 361.0;
    maxAngle = -1.0;

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
            font-family: Arial;
            background: #2c3e50;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            margin: 0;
            padding: 20px;
        }
        .dashboard {
            max-width: 800px;
            width: 100%;
        }
        h1 {
            color: white;
            text-align: center;
            text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
        }
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
            border-radius: 10px;
            padding: 16px;
            margin: 20px 0;
            box-shadow: 0 4px 6px rgba(0,0,0,0.1);
            text-align: left;
        }
        .section-title {
            font-size: 1.5em;
            margin-bottom: 8px;
            color: #2c3e50;
            border-bottom: 2px solid #3498db;
            padding-bottom: 6px;
            font-weight: bold;
        }
        .section-info {
            color: #7f8c8d;
            font-size: 0.8em;
            margin-bottom: 10px;
            font-style: italic;
        }
        .sensor-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
            gap: 10px;
            margin-top: 10px;
        }
        .sensor-item {
            background: white;
            padding: 8px 12px;
            border-radius: 6px;
            border-left: 4px solid #3498db;
            transition: all 0.3s;
            min-height: 60px;
            display: flex;
            flex-direction: column;
            justify-content: center;
        }
        .sensor-label {
            font-weight: bold;
            color: #555;
            display: block;
            margin-bottom: 2px;
            font-size: 0.85em;
        }
        .sensor-value {
            font-size: 1.5em;
            font-family: 'Courier New', monospace;
            color: #2c3e50;
            font-weight: bold;
            line-height: 1.2;
        }
        .sensor-unit {
            font-size: 0.8em;
            color: #7f8c8d;
            margin-left: 2px;
        }
        .sensor-item.stale-data {
            border-left: 4px solid #e74c3c !important;
            opacity: 0.7;
        }
        .relay-status {
            display: inline-block;
            padding: 3px 8px;
            border-radius: 12px;
            font-weight: bold;
            margin-top: 3px;
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
        #ledToggleBtn {
            font-size: 15px;
            padding: 10px 20px;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            color: white;
            font-weight: bold;
            transition: all 0.3s;
            width: 280px;
            margin: 12px 0;
            float: left;
            min-height: 50px;
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
        .security-status {
            padding: 10px;
            border-radius: 6px;
            margin-top: 12px;
            text-align: center;
            font-weight: bold;
            font-size: 0.95em;
            transition: all 0.3s;
        }
        .security-normal {
            background: linear-gradient(135deg, #27ae60, #2ecc71);
            color: white;
        }
        .security-alarm {
            background: linear-gradient(135deg, #e74c3c, #c0392b);
            color: white;
            animation: alarm-pulse 1s infinite;
        }
        @keyframes alarm-pulse {
            0% { opacity: 1; }
            50% { opacity: 0.7; }
            100% { opacity: 1; }
        }
        
        /* Компас */
        .wind-compact {
            cursor: pointer;
            transition: all 0.3s ease;
        }
        .compass-container {
            position: relative;
            width: 100%;
            height: 100%;
            margin: 0 auto;
        }
        .compass-container svg {
            width: 100%;
            height: 100%;
            position: absolute;
            top: 0;
            left: 0;
        }
        .direction {
            position: absolute;
            font-size: 14px;
            font-weight: bold;
            color: #e74c3c;
            text-shadow: 1px 1px 2px white;
            z-index: 10;
        }
        .n { top: 5px; left: 50%; transform: translateX(-50%); }
        .e { right: 5px; top: 50%; transform: translateY(-50%); }
        .s { bottom: 5px; left: 50%; transform: translateX(-50%); }
        .w { left: 5px; top: 50%; transform: translateY(-50%); }
        
        .wind-stats {
            text-align: center;
            margin-top: 5px;
            padding: 5px;
            background: white;
            border-radius: 8px;
            font-size: 12px;
        }
        .wind-angle {
            font-size: 20px;
            font-weight: bold;
            color: #2c3e50;
        }
        .wind-badge {
            display: inline-block;
            padding: 2px 8px;
            border-radius: 12px;
            font-size: 10px;
            font-weight: bold;
            color: white;
            margin-left: 8px;
        }
        .wind-modal {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0,0,0,0.8);
            z-index: 9998;
            justify-content: center;
            align-items: center;
        }
        .wind-modal-content {
            width: 80vw;
            height: 80vw;
            max-width: 600px;
            max-height: 600px;
            background: white;
            border-radius: 30px;
            padding: 30px;
            position: relative;
        }
        .wind-modal-content .direction {
            font-size: 24px;
        }
        #lastUpdate {
            font-size: 0.75em;
            color: #95a5a6;
            text-align: right;
            margin-top: 10px;
            font-style: italic;
        }
        .clearfix { clear: both; }
        
        /* Легенда */
        .wind-legend {
            display: flex;
            gap: 15px;
            margin-top: 8px;
            font-size: 10px;
            color: #7f8c8d;
        }
        .legend-red {
            display: inline-block;
            width: 12px;
            height: 12px;
            background: #e74c3c;
            border-radius: 2px;
            margin-right: 4px;
        }
        .legend-yellow {
            display: inline-block;
            width: 12px;
            height: 12px;
            background: #f1c40f;
            opacity: 0.7;
            border-radius: 2px;
            margin-right: 4px;
        }
    </style>
</head>
<body>
    <div class="dashboard">
        <h1>🏠 Умный дом ESP-NOW</h1>
        
        <button id="refreshBtn" onclick="refreshNodeData()">🔄 ОБНОВИТЬ ДАННЫЕ</button>
        
        <div class="section">
            <div class="section-title">🔧 Мастерская</div>
            <div class="section-info">MAC: AC:EB:E6:49:10:28</div>
            
            <div id="securityStatus" class="security-status security-normal">
                🔒 ОХРАНА: НОРМА (концевики замкнуты)
            </div>
            
            <button id="ledToggleBtn" class="led-unknown" onclick="toggleLED()">--</button>
            <div class="clearfix"></div>
            
            <div id="nodeSensorData">
                <p>Нажмите "Обновить данные" для получения показаний</p>
            </div>
            
            <!-- БЛОК ВЕТРА -->
            <div id="windBlock" class="wind-compact" onclick="toggleWindSize()" style="margin-top: 15px; padding-top: 10px; border-top: 1px dashed #ccc;">
                <div style="display: flex; align-items: center; margin-bottom: 8px;">
                    <span style="font-weight: bold; color: #2c3e50; font-size: 1.1em;">🌪️ Ветер</span>
                    <span id="magnetIndicator" style="display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-left: 8px; background-color: #95a5a6;"></span>
                    <span id="magnetText" style="margin-left: 4px; font-size: 0.8em; color: #7f8c8d;">магнит</span>
                </div>
                
                <div style="display: flex; align-items: center;">
                    <div style="position: relative; width: 70px; height: 70px; flex-shrink: 0;">
                        <div class="direction n">N</div>
                        <div class="direction e">E</div>
                        <div class="direction s">S</div>
                        <div class="direction w">W</div>
                        <svg viewBox="0 0 100 100">
                            <circle cx="50" cy="50" r="48" fill="#ecf0f1" stroke="#34495e" stroke-width="1"/>
                            <!-- Желтый сектор (мин-макс за 30 сек) -->
                            <path id="windSectorMax" d="" fill="#f1c40f" fill-opacity="0.5"/>
                            <!-- Красный сектор (текущий размах) -->
                            <path id="windSector" d="" fill="#e74c3c" fill-opacity="0.7"/>
                            <!-- Стрелка направления -->
                            <path id="windArrow" d="M50 10 L54 42 L50 50 L46 42 Z" fill="#2c3e50" stroke="white" stroke-width="1"/>
                            <circle cx="50" cy="50" r="4" fill="#34495e" stroke="white" stroke-width="1"/>
                        </svg>
                    </div>
                    
                    <div style="margin-left: 12px; flex-grow: 1;">
                        <div>
                            <span id="windAngle" style="font-size: 18px; font-weight: bold;">--</span>
                            <span style="color: #7f8c8d;">°</span>
                            <span id="stabilityBadge" class="wind-badge">ШТИЛЬ</span>
                        </div>
                        <div style="color: #7f8c8d; font-size: 11px; margin-top: 4px;">
                            <span style="color: #e74c3c;">●</span> ±<span id="sectorWidth">--</span>°
                            <span style="margin-left: 8px; color: #f1c40f;">●</span> <span id="maxRange">---</span>
                        </div>
                        <div class="wind-legend">
                            <span><span class="legend-red"></span> текущий</span>
                            <span><span class="legend-yellow"></span> мин-макс за 30 сек</span>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <div class="section">
            <div class="section-title">🌿 Теплица</div>
            <div class="section-info">MAC: E8:9F:6D:87:34:8A</div>
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
                    <span class="sensor-label">Реле 1 (основное):</span>
                    <span id="relay1State" class="relay-status relay-off">--</span>
                </div>
                <div class="sensor-item">
                    <span class="sensor-label">Реле 2 (доп.):</span>
                    <span id="relay2State" class="relay-status relay-off">--</span>
                </div>
            </div>
            <div id="lastUpdate">Ожидание данных от теплицы...</div>
        </div>
    </div>

    <div id="windModal" class="wind-modal" onclick="toggleWindSize()">
        <div class="wind-modal-content" onclick="event.stopPropagation()">
            <div style="position: relative; width: 100%; height: 100%;">
                <div class="direction n">N</div>
                <div class="direction e">E</div>
                <div class="direction s">S</div>
                <div class="direction w">W</div>
                <svg viewBox="0 0 100 100">
                    <circle cx="50" cy="50" r="48" fill="#ecf0f1" stroke="#34495e" stroke-width="2"/>
                    <path id="windSectorMaxLarge" d="" fill="#f1c40f" fill-opacity="0.5"/>
                    <path id="windSectorLarge" d="" fill="#e74c3c" fill-opacity="0.7"/>
                    <path id="windArrowLarge" d="M50 10 L54 42 L50 50 L46 42 Z" fill="#2c3e50" stroke="white" stroke-width="1.5"/>
                    <circle cx="50" cy="50" r="4" fill="#34495e" stroke="white" stroke-width="2"/>
                </svg>
            </div>
            <div style="text-align: center; margin-top: 20px;">
                <span style="font-size: 32px; font-weight: bold;" id="windAngleLarge">--</span>
                <span style="font-size: 20px; color: #7f8c8d;">°</span>
                <span id="stabilityBadgeLarge" style="display: inline-block; padding: 8px 20px; border-radius: 20px; font-size: 16px; font-weight: bold; color: white; margin-left: 15px;">ШТИЛЬ</span>
                <div style="margin-top: 15px; color: #7f8c8d; font-size: 18px;">
                    <span style="color: #e74c3c;">●</span> ±<span id="sectorWidthLarge">--</span>°
                    <span style="margin-left: 20px; color: #f1c40f;">●</span> <span id="maxRangeLarge">---</span>
                </div>
            </div>
        </div>
    </div>

    <script>
        const ws = new WebSocket('ws://' + window.location.hostname + '/ws');
        let ledState = 'unknown';
        let buttonLocked = false;
        let audioContext = null;
        let alarmInterval = null;
        let isAlarmPlaying = false;
        let alarmTimer = null;

        function initAudio() {
            if (!audioContext) {
                audioContext = new (window.AudioContext || window.webkitAudioContext)();
            }
        }
        document.addEventListener('click', initAudio);

        function playAlarmTone() {
            if (isAlarmPlaying || !audioContext) return;
            isAlarmPlaying = true;
            
            function playPulse(freq, dur) {
                let osc = audioContext.createOscillator();
                let gain = audioContext.createGain();
                osc.connect(gain);
                gain.connect(audioContext.destination);
                osc.frequency.value = freq;
                osc.type = 'sawtooth';
                gain.gain.value = 0.15;
                osc.start();
                gain.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + dur);
                osc.stop(audioContext.currentTime + dur);
            }
            
            alarmInterval = setInterval(() => {
                playPulse(800, 0.1);
                setTimeout(() => playPulse(1200, 0.1), 150);
            }, 500);
            
            alarmTimer = setTimeout(stopAlarm, 10000);
        }

        function stopAlarm() {
            isAlarmPlaying = false;
            if (alarmInterval) clearInterval(alarmInterval);
            if (alarmTimer) clearTimeout(alarmTimer);
            alarmInterval = null;
            alarmTimer = null;
        }

        function playShortBeep() {
            if (!audioContext) return;
            function beep(freq, dur) {
                let osc = audioContext.createOscillator();
                let gain = audioContext.createGain();
                osc.connect(gain);
                gain.connect(audioContext.destination);
                osc.frequency.value = freq;
                osc.type = 'sawtooth';
                gain.gain.value = 0.1;
                osc.start();
                gain.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + dur);
                osc.stop(audioContext.currentTime + dur);
            }
            beep(600, 0.2);
            setTimeout(() => beep(400, 0.3), 300);
        }

        function toggleWindSize() {
            let modal = document.getElementById('windModal');
            if (modal.style.display === 'flex') {
                modal.style.display = 'none';
            } else {
                modal.style.display = 'flex';
                document.getElementById('windAngleLarge').textContent = document.getElementById('windAngle').textContent;
                document.getElementById('sectorWidthLarge').textContent = document.getElementById('sectorWidth').textContent;
                document.getElementById('maxRangeLarge').innerHTML = document.getElementById('maxRange').innerHTML;
                
                let badge = document.getElementById('stabilityBadge');
                let badgeLarge = document.getElementById('stabilityBadgeLarge');
                badgeLarge.textContent = badge.textContent;
                badgeLarge.style.backgroundColor = badge.style.backgroundColor;
                
                let sector = document.getElementById('windSector');
                let sectorLarge = document.getElementById('windSectorLarge');
                sectorLarge.setAttribute('d', sector.getAttribute('d'));
                
                let sectorMax = document.getElementById('windSectorMax');
                let sectorMaxLarge = document.getElementById('windSectorMaxLarge');
                if (sectorMax && sectorMaxLarge) {
                    sectorMaxLarge.setAttribute('d', sectorMax.getAttribute('d'));
                }
                
                let arrow = document.getElementById('windArrow');
                let arrowLarge = document.getElementById('windArrowLarge');
                arrowLarge.setAttribute('transform', arrow.getAttribute('transform'));
            }
        }

        function updateLEDButton() {
            let btn = document.getElementById('ledToggleBtn');
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
            if (buttonLocked || ws.readyState !== WebSocket.OPEN) return;
            let cmd = (ledState === 'on') ? 'LED_OFF' : 'LED_ON';
            buttonLocked = true;
            let btn = document.getElementById('ledToggleBtn');
            btn.disabled = true;
            setTimeout(() => { buttonLocked = false; updateLEDButton(); }, 5000);
            ws.send(JSON.stringify({command: cmd}));
        }

        function refreshNodeData() {
            document.getElementById('nodeSensorData').innerHTML = 
                '<p style="color:#e74c3c;">⏳ Запрос данных отправлен...</p>';
            ws.send(JSON.stringify({command: 'GET_STATUS'}));
        }

        function markNodeDataAsStale() {
            let items = document.querySelectorAll('#nodeSensorData .sensor-item');
            items.forEach(i => i.classList.add('stale-data'));
            let vals = document.querySelectorAll('#nodeSensorData .sensor-value');
            vals.forEach(v => v.classList.add('stale-data'));
            playShortBeep();
        }

        function markNodeDataAsFresh() {
            let items = document.querySelectorAll('#nodeSensorData .sensor-item');
            items.forEach(i => i.classList.remove('stale-data'));
            let vals = document.querySelectorAll('#nodeSensorData .sensor-value');
            vals.forEach(v => v.classList.remove('stale-data'));
        }

        function updateSecurityStatus(alarm, c1, c2) {
            let el = document.getElementById('securityStatus');
            if (alarm) {
                el.className = 'security-status security-alarm';
                let txt = '🚨 ТРЕВОГА! ';
                if (c1 && c2) txt += 'ОБА КОНЦЕВИКА!';
                else if (c1) txt += 'Концевик 1 разорван';
                else if (c2) txt += 'Концевик 2 разорван';
                el.innerHTML = txt;
                playAlarmTone();
            } else {
                el.className = 'security-status security-normal';
                el.innerHTML = '🔒 ОХРАНА: НОРМА';
                stopAlarm();
            }
        }

        function drawSector(pathId, start, end) {
            let path = document.getElementById(pathId);
            if (!path) return;
            let cx = 50, cy = 50, r = 48;
            
            function degToRad(d) {
                return (d - 90) * Math.PI / 180;
            }
            
            let startRad = degToRad(start);
            let endRad = degToRad(end);
            
            let x1 = cx + r * Math.cos(startRad);
            let y1 = cy + r * Math.sin(startRad);
            let x2 = cx + r * Math.cos(endRad);
            let y2 = cy + r * Math.sin(endRad);
            
            let angleDiff = end - start;
            if (angleDiff < 0) angleDiff += 360;
            let largeArc = angleDiff > 180 ? 1 : 0;
            
            let d = `M ${cx} ${cy} L ${x1} ${y1} A ${r} ${r} 0 ${largeArc} 1 ${x2} ${y2} Z`;
            path.setAttribute('d', d);
        }

        function rotateArrow(arrowId, deg) {
            let arrow = document.getElementById(arrowId);
            if (arrow) {
                arrow.setAttribute('transform', `rotate(${deg}, 50, 50)`);
            }
        }

        ws.onmessage = function(event) {
            let msg = JSON.parse(event.data);
            
            if (msg.type === 'node_status') {
                ledState = msg.state;
                buttonLocked = false;
                updateLEDButton();
            }
            else if (msg.type === 'sensor_data') {
                let html = '<div class="sensor-grid">';
                if (msg.aht20) {
                    html += `<div class="sensor-item"><span class="sensor-label">AHT20:</span><span class="sensor-value">${msg.aht20.temp}</span><span class="sensor-unit">°C</span></div>`;
                    html += `<div class="sensor-item"><span class="sensor-label">AHT20:</span><span class="sensor-value">${msg.aht20.hum}</span><span class="sensor-unit">%</span></div>`;
                }
                if (msg.bmp280) {
                    html += `<div class="sensor-item"><span class="sensor-label">BMP280:</span><span class="sensor-value">${msg.bmp280.temp}</span><span class="sensor-unit">°C</span></div>`;
                    html += `<div class="sensor-item"><span class="sensor-label">BMP280:</span><span class="sensor-value">${msg.bmp280.press}</span><span class="sensor-unit">mmHg</span></div>`;
                }
                html += '</div>';
                document.getElementById('nodeSensorData').innerHTML = html;
            }
            else if (msg.type === 'security') {
                updateSecurityStatus(msg.alarm, msg.contact1, msg.contact2);
            }
            else if (msg.type === 'connection_lost') {
                markNodeDataAsStale();
            }
            else if (msg.type === 'connection_restored') {
                markNodeDataAsFresh();
            }
            else if (msg.type === 'gpio_status') {
                if (msg.gpio8 !== undefined) {
                    ledState = msg.gpio8 ? 'on' : 'off';
                    updateLEDButton();
                }
            }
            else if (msg.type === 'greenhouse_data') {
                let vals = document.querySelectorAll('#greenhouseData .sensor-value');
                if (vals.length >= 3) {
                    vals[0].textContent = msg.temp_in;
                    vals[1].textContent = msg.temp_out;
                    vals[2].textContent = msg.hum_in;
                }
                let r1 = document.getElementById('relay1State');
                let r2 = document.getElementById('relay2State');
                r1.textContent = (msg.relay1_state == 1) ? 'ВКЛЮЧЕНО' : 'ВЫКЛЮЧЕНО';
                r1.className = (msg.relay1_state == 1) ? 'relay-status relay-on' : 'relay-status relay-off';
                r2.textContent = (msg.relay2_state == 1) ? 'ВКЛЮЧЕНО' : 'ВЫКЛЮЧЕНО';
                r2.className = (msg.relay2_state == 1) ? 'relay-status relay-on' : 'relay-status relay-off';
                document.getElementById('lastUpdate').textContent = `Обновлено: ${new Date().toLocaleTimeString()}`;
            }
            else if (msg.type === 'wind') {
                document.getElementById('windAngle').textContent = msg.angle_avg;
                document.getElementById('sectorWidth').textContent = msg.sector_width;
                document.getElementById('maxRange').innerHTML = `${msg.history_min}° - ${msg.history_max}°`;
                
                if (document.getElementById('windModal').style.display === 'flex') {
                    document.getElementById('windAngleLarge').textContent = msg.angle_avg;
                    document.getElementById('sectorWidthLarge').textContent = msg.sector_width;
                    document.getElementById('maxRangeLarge').innerHTML = `${msg.history_min}° - ${msg.history_max}°`;
                }
                
                let magnet = document.getElementById('magnetIndicator');
                let magnetText = document.getElementById('magnetText');
                if (msg.magnet) {
                    magnet.style.backgroundColor = '#27ae60';
                    magnetText.textContent = 'магнит есть';
                    magnetText.style.color = '#27ae60';
                } else {
                    magnet.style.backgroundColor = '#e74c3c';
                    magnetText.textContent = 'магнит нет';
                    magnetText.style.color = '#e74c3c';
                }
                
                // СТАБИЛЬНОСТЬ ВЕТРА - РАБОТАЕТ!
                let stability = msg.stability;
                let badge = document.getElementById('stabilityBadge');
                let badgeLarge = document.getElementById('stabilityBadgeLarge');
                let text = '', color = '';
                
                switch(stability) {
                    case 'calm':   text = 'ШТИЛЬ';    color = '#3498db'; break;
                    case 'gusty':  text = 'ПОРЫВИСТЫЙ'; color = '#e67e22'; break;
                    case 'strong': text = 'СИЛЬНЫЙ';   color = '#e74c3c'; break;
                    case 'storm':  text = 'ШТОРМ';     color = '#8e44ad'; break;
                    default:       text = 'ШТИЛЬ';    color = '#3498db';
                }
                
                badge.textContent = text;
                badge.style.backgroundColor = color;
                if (badgeLarge) {
                    badgeLarge.textContent = text;
                    badgeLarge.style.backgroundColor = color;
                }
                
                // Рисуем красный сектор (текущий размах)
                if (msg.sector_start !== undefined && msg.sector_end !== undefined) {
                    drawSector('windSector', parseFloat(msg.sector_start), parseFloat(msg.sector_end));
                    drawSector('windSectorLarge', parseFloat(msg.sector_start), parseFloat(msg.sector_end));
                }
                
                // Рисуем желтый сектор (мин-макс за 30 сек)
                if (msg.history_min !== undefined && msg.history_max !== undefined) {
                    drawSector('windSectorMax', parseFloat(msg.history_min), parseFloat(msg.history_max));
                    drawSector('windSectorMaxLarge', parseFloat(msg.history_min), parseFloat(msg.history_max));
                }
                
                // Поворачиваем стрелку
                rotateArrow('windArrow', parseFloat(msg.angle_avg));
                rotateArrow('windArrowLarge', parseFloat(msg.angle_avg));
            }
        };

        ws.onopen = function() {
            updateLEDButton();
            ws.send(JSON.stringify({command: 'GET_STATUS'}));
        };

        ws.onclose = function() {
            ledState = 'unknown';
            updateLEDButton();
        };

        updateLEDButton();
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
    Serial.println("3. Ветер: желтый сектор 30 сек, штиль/шторм РАБОТАЕТ\n");
}

void loop() {
    ws.cleanupClients();
    checkNodeConnection();
    updateAlarmState();
    
    unsigned long now = millis();
    if (now - lastEncoderBroadcastTime >= ENCODER_BROADCAST_INTERVAL) {
        updateMaxMin();
        broadcastEncoderData();
        lastEncoderBroadcastTime = now;
    }
    
    delay(100);
}

void sendConnectionStatusToWeb(bool connected) {
    StaticJsonDocument<100> doc;
    doc["type"] = connected ? "connection_restored" : "connection_lost";
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void checkNodeConnection() {
    unsigned long now = millis();
    if (lastNodeDataTime > 0) {
        if (now - lastNodeDataTime > NODE_TIMEOUT_MS) {
            if (!nodeConnectionLost) {
                nodeConnectionLost = true;
                connectionLostTime = now;
                Serial.println("⚠️ СВЯЗЬ С УЗЛОМ ПОТЕРЯНА!");
                sendConnectionStatusToWeb(false);
            }
        } else {
            if (nodeConnectionLost) {
                nodeConnectionLost = false;
                Serial.println("✅ СВЯЗЬ С УЗЛОМ ВОССТАНОВЛЕНА!");
                sendConnectionStatusToWeb(true);
            }
        }
    }
}

void updateAlarmState() {
    if (securityAlarmActive && (millis() - alarmStartTime) > ALARM_DURATION_MS) {
        securityAlarmActive = false;
    }
}

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
        StaticJsonDocument<200> doc;
        if (!deserializeJson(doc, data, len) && doc.containsKey("command")) {
            sendToNode(doc["command"].as<String>());
        }
    }
}

void sendToNode(String cmd) {
    char json_cmd[64];
    snprintf(json_cmd, sizeof(json_cmd), "{\"type\":\"command\",\"command\":\"%s\"}", cmd.c_str());
    strncpy(outgoingMessage.json, json_cmd, sizeof(outgoingMessage.json)-1);
    outgoingMessage.json[sizeof(outgoingMessage.json)-1] = '\0';
    outgoingMessage.sender_id = 1;
    esp_now_send(nodeMacAddress, (uint8_t*)&outgoingMessage, sizeof(outgoingMessage));
}

void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    if (memcmp(mac_addr, nodeMacAddress, 6) == 0) {
        lastNodeDataTime = millis();
        processNodeData(incomingData, len);
    }
    else if (memcmp(mac_addr, greenhouseMac, 6) == 0) {
        if (len == sizeof(greenhouse_packet)) {
            processGreenhouseData(incomingData);
        }
    }
}

void processNodeData(const uint8_t *data, int len) {
    if (len > sizeof(incomingMessage)) {
        Serial.println("❌ Пакет слишком большой!");
        return;
    }
    
    memcpy(&incomingMessage, data, len);
    
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, incomingMessage.json);
    if (error) {
        Serial.print("❌ JSON ошибка: ");
        Serial.println(error.c_str());
        return;
    }

    const char* type = doc["type"];

    if (strcmp(type, "sensor") == 0) {
        JsonObject dataObj = doc["data"];
        StaticJsonDocument<300> resp;
        resp["type"] = "sensor_data";
        if (dataObj.containsKey("AHT20")) {
            resp["aht20"]["temp"] = dataObj["AHT20"]["temp"].as<String>();
            resp["aht20"]["hum"] = dataObj["AHT20"]["hum"].as<String>();
        }
        if (dataObj.containsKey("BMP280")) {
            resp["bmp280"]["temp"] = dataObj["BMP280"]["temp"].as<String>();
            resp["bmp280"]["press"] = dataObj["BMP280"]["press_mmHg"].as<String>();
        }
        String json;
        serializeJson(resp, json);
        ws.textAll(json);
    }
    else if (strcmp(type, "security") == 0) {
        bool alarm = doc["alarm"];
        bool c1 = doc["contact1"];
        bool c2 = doc["contact2"];
        
        if (alarm && !securityAlarmActive) {
            securityAlarmActive = true;
            alarmStartTime = millis();
            Serial.println("🚨 ТРЕВОГА!");
        } else if (!alarm) {
            securityAlarmActive = false;
        }
        
        StaticJsonDocument<200> resp;
        resp["type"] = "security";
        resp["alarm"] = alarm;
        resp["contact1"] = c1;
        resp["contact2"] = c2;
        String json;
        serializeJson(resp, json);
        ws.textAll(json);
    }
    else if (strcmp(type, "ack") == 0) {
        const char* cmd = doc["command"];
        if (strcmp(cmd, "LED_ON") == 0) {
            StaticJsonDocument<200> resp;
            resp["type"] = "node_status";
            resp["state"] = "on";
            String json;
            serializeJson(resp, json);
            ws.textAll(json);
        }
        else if (strcmp(cmd, "LED_OFF") == 0) {
            StaticJsonDocument<200> resp;
            resp["type"] = "node_status";
            resp["state"] = "off";
            String json;
            serializeJson(resp, json);
            ws.textAll(json);
        }
    }
    else if (strcmp(type, "gpio") == 0) {
        StaticJsonDocument<200> resp;
        resp["type"] = "gpio_status";
        if (doc.containsKey("pin") && doc.containsKey("state")) {
            int pin = doc["pin"];
            int state = doc["state"];
            if (pin == 8) resp["gpio8"] = state;
        }
        String json;
        serializeJson(resp, json);
        ws.textAll(json);
    }
    else if (strcmp(type, "encoder") == 0) {
        float angle = doc["angle"];
        bool magnet = doc["magnet"];
        
        if (magnet) {
            processEncoderData(angle, true);
            updateHistory(angle);
        }
    }
}

void processGreenhouseData(const uint8_t *data) {
    greenhouse_packet pkt;
    memcpy(&pkt, data, sizeof(pkt));

    unsigned long now = millis();
    if (now - lastGreenhouseUpdate < GREENHOUSE_UPDATE_INTERVAL) {
        return;
    }
    lastGreenhouseUpdate = now;

    char temp_in[5] = {0};
    char temp_out[5] = {0};
    strncpy(temp_in, pkt.temp_in, 4);
    strncpy(temp_out, pkt.temp_out, 4);

    StaticJsonDocument<300> resp;
    resp["type"] = "greenhouse_data";
    resp["temp_in"] = temp_in;
    resp["temp_out"] = temp_out;
    resp["hum_in"] = pkt.hum_in;
    resp["relay1_state"] = pkt.relay1_state;
    resp["relay2_state"] = pkt.relay2_state;

    String json;
    serializeJson(resp, json);
    ws.textAll(json);
}

String relayStateToString(uint32_t state) {
    return (state == 1) ? "ВКЛЮЧЕНО" : "ВЫКЛЮЧЕНО";
}

// ========== ВЕТЕР: ДВЕ ТОЧКИ + ЖЕЛТЫЙ СЕКТОР 30 СЕК + ШТИЛЬ/ШТОРМ ==========
void processEncoderData(float angle, bool magnet) {
    if (prevEncoderAngle < 0) {
        prevEncoderAngle = angle;
        currentEncoderAngle = angle;
        windDirection = angle;
        windCurrentSector = 0.0;
        Serial.printf("🌪️ Ветер: первое значение %.1f°\n", angle);
    } else {
        prevEncoderAngle = currentEncoderAngle;
        currentEncoderAngle = angle;
        
        float rad1 = radians(prevEncoderAngle);
        float rad2 = radians(currentEncoderAngle);
        float sumSin = sin(rad1) + sin(rad2);
        float sumCos = cos(rad1) + cos(rad2);
        float meanRad = atan2(sumSin, sumCos);
        windDirection = degrees(meanRad);
        if (windDirection < 0) windDirection += 360.0;
        
        float diff = fmod(currentEncoderAngle - prevEncoderAngle + 540.0, 360.0) - 180.0;
        windCurrentSector = fabs(diff);
        
        Serial.printf("🌪️ Ветер: prev=%.1f°, curr=%.1f°, напр=%.1f°, сектор=%.1f°\n", 
                      prevEncoderAngle, currentEncoderAngle, windDirection, windCurrentSector);
    }
    
    windMagnet = magnet;
}

void updateHistory(float angle) {
    encoderHistory[historyIndex] = angle;
    historyTimestamps[historyIndex] = millis();
    historyIndex = (historyIndex + 1) % ENCODER_HISTORY_SIZE;
    if (historyCount < ENCODER_HISTORY_SIZE) {
        historyCount++;
        Serial.printf("📝 История: добавлен %.1f°, всего записей: %d\n", angle, historyCount);
    }
}

void updateMaxMin() {
    if (historyCount == 0) return;
    
    unsigned long now = millis();
    float currentMin = 361.0;
    float currentMax = -1.0;
    int validCount = 0;
    
    for (int i = 0; i < historyCount; i++) {
        if (now - historyTimestamps[i] <= 30000) { // 30 секунд
            float a = encoderHistory[i];
            if (a < currentMin) currentMin = a;
            if (a > currentMax) currentMax = a;
            validCount++;
        }
    }
    
    if (validCount > 0 && currentMax >= 0) {
        maxAngle = currentMax;
        minAngle = currentMin;
        Serial.printf("📊 Желтый сектор: мин=%.1f°, макс=%.1f° (%d записей)\n", 
                     minAngle, maxAngle, validCount);
    }
}

void broadcastEncoderData() {
    if (prevEncoderAngle < 0) return;
    
    float redStart = windDirection - windCurrentSector / 2;
    float redEnd = windDirection + windCurrentSector / 2;
    redStart = fmod(fmod(redStart, 360) + 360, 360);
    redEnd = fmod(fmod(redEnd, 360) + 360, 360);
    
    // СТАБИЛЬНОСТЬ ПО ТЕКУЩЕМУ РАЗМАХУ
    String stability;
    if (windCurrentSector < 10) stability = "calm";
    else if (windCurrentSector < 30) stability = "gusty";
    else if (windCurrentSector < 60) stability = "strong";
    else stability = "storm";
    
    StaticJsonDocument<256> doc;
    doc["type"] = "wind";
    doc["angle_avg"] = serialized(String(windDirection, 1));
    doc["sector_width"] = serialized(String(windCurrentSector, 1));
    doc["sector_start"] = serialized(String(redStart, 0));
    doc["sector_end"] = serialized(String(redEnd, 0));
    doc["history_min"] = serialized(String(minAngle, 0));
    doc["history_max"] = serialized(String(maxAngle, 0));
    doc["magnet"] = windMagnet;
    doc["stability"] = stability;
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}