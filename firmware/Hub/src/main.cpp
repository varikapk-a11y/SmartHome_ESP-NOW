/**
 * SmartHome ESP-NOW Hub (ESP32)
 * ВЕРСИЯ 5.6: УПРОЩЕННАЯ (102, 103, ТЕПЛИЦА)
 * Исправления:
 * - LED кнопки как в версии 5.5
 * - Желтый сектор при переходе через 0
 */
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <math.h>

// ---- 1. КОНФИГУРАЦИЯ ----
const char* AP_SSID = "SmartHome-Hub";
const char* AP_PASSWORD = "12345678";

// Версии прошивок
const char* HUB_VERSION = "5.6";
const char* NODE_VERSION = "2.1";

// MAC адреса узлов
uint8_t node102MacAddress[] = {0xAC, 0xEB, 0xE6, 0x49, 0x10, 0x28};
uint8_t node103MacAddress[] = {0x88, 0x56, 0xA6, 0x7D, 0x09, 0x64};
uint8_t greenhouseMac[] = {0xE8, 0x9F, 0x6D, 0x87, 0x34, 0x8A};

// Массив узлов
#define NODE_COUNT 2
uint8_t* nodeMacs[NODE_COUNT] = {
    node102MacAddress,
    node103MacAddress
};

int nodeNumbers[NODE_COUNT] = {102, 103};

// Время последнего получения данных
unsigned long lastNodeDataTime[NODE_COUNT] = {0, 0};
const unsigned long NODE_TIMEOUT_MS = 70000;

bool nodeConnectionLost[NODE_COUNT] = {false, false};
bool nodeAlarmState[NODE_COUNT] = {false, false};

// ---- 2. СТРУКТУРЫ ESP-NOW ----
typedef struct esp_now_message {
    char json[256];
    uint8_t sender_id;
} esp_now_message;

// ---- 3. СТРУКТУРА ТЕПЛИЦЫ ----
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

bool securityAlarmActive = false;
unsigned long alarmStartTime = 0;
const unsigned long ALARM_DURATION_MS = 10000;
bool globalAlarmActive = false;

// ---- 5. ДАННЫЕ ЭНКОДЕРА ----
#define ENCODER_HISTORY_SIZE 6
#define ENCODER_BROADCAST_INTERVAL 5000

float prevEncoderAngle = -1.0;
float currentEncoderAngle = -1.0;
float windDirection = 0.0;
float windCurrentSector = 0.0;
bool windMagnet = false;

float encoderHistory[ENCODER_HISTORY_SIZE];
unsigned long historyTimestamps[ENCODER_HISTORY_SIZE];
int historyIndex = 0;
int historyCount = 0;

float maxAngle = -1.0;
float minAngle = 361.0;
unsigned long lastEncoderBroadcastTime = 0;

// ---- 6. ПРОТОТИПЫ ----
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len);
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void sendToNode(uint8_t* mac, String cmd);
void processGreenhouseData(const uint8_t *data);
void processNodeData(const uint8_t *data, int len, int nodeIndex);
String relayStateToString(uint32_t state);
void checkNodeConnection();
void updateAlarmState();
void sendConnectionStatusToWeb(int nodeIndex, bool connected);
void processEncoderData(float angle, bool magnet);
void updateHistory(float angle);
void updateMaxMin();
void broadcastEncoderData();
void checkGlobalAlarm();

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== SmartHome ESP-NOW Hub (Версия 5.6) ===");
    Serial.println("=== УПРОЩЕННАЯ ВЕРСИЯ: 102, 103, ТЕПЛИЦА ===");

    // Инициализация истории
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
        #aboutBtn {
            font-size: 14px;
            padding: 10px 25px;
            background: #34495e;
            color: white;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            margin: 10px auto 30px;
            display: block;
            width: 250px;
            font-weight: bold;
            transition: all 0.3s;
        }
        #aboutBtn:hover {
            background: #2c3e50;
            transform: translateY(-2px);
            box-shadow: 0 4px 8px rgba(0,0,0,0.1);
        }
        .global-alarm-banner {
            background: #e74c3c;
            color: white;
            padding: 15px;
            border-radius: 10px;
            margin: 20px 0;
            text-align: center;
            font-size: 24px;
            font-weight: bold;
            animation: alarm-pulse 1s infinite;
            display: none;
        }
        @keyframes alarm-pulse {
            0% { opacity: 1; }
            50% { opacity: 0.7; }
            100% { opacity: 1; }
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
            grid-template-columns: repeat(3, 1fr);
            gap: 10px;
            margin-top: 10px;
        }
        .sensor-item {
            background: white;
            padding: 12px;
            border-radius: 8px;
            border-left: 4px solid #3498db;
            min-height: 70px;
            display: flex;
            flex-direction: column;
            justify-content: center;
        }
        .sensor-item.stale-data {
            border-left: 4px solid #e74c3c !important;
            opacity: 0.7;
        }
        .sensor-label {
            font-weight: bold;
            color: #555;
            font-size: 0.85em;
            display: flex;
            justify-content: space-between;
            align-items: center;
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
        .led-toggle-btn {
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
        .led-toggle-btn.led-on {
            background: linear-gradient(135deg, #e74c3c, #c0392b);
        }
        .led-toggle-btn.led-off {
            background: linear-gradient(135deg, #2ecc71, #27ae60);
        }
        .led-toggle-btn.led-unknown {
            background: #95a5a6;
            cursor: not-allowed;
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
        .clearfix { clear: both; }
        
        /* Компас */
        .wind-compact {
            margin-top: 15px;
            padding-top: 10px;
            border-top: 1px dashed #ccc;
            cursor: pointer;
        }
        .direction {
            position: absolute;
            font-size: 12px;
            font-weight: bold;
            color: #e74c3c;
            z-index: 10;
        }
        .n { top: 5px; left: 50%; transform: translateX(-50%); }
        .e { right: 5px; top: 50%; transform: translateY(-50%); }
        .s { bottom: 5px; left: 50%; transform: translateX(-50%); }
        .w { left: 5px; top: 50%; transform: translateY(-50%); }
        
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
        
        /* Модальное окно "О системе" */
        .about-modal {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0,0,0,0.8);
            z-index: 9999;
            justify-content: center;
            align-items: center;
        }
        .about-modal-content {
            background: white;
            border-radius: 20px;
            padding: 30px;
            max-width: 500px;
            width: 90%;
            max-height: 80vh;
            overflow-y: auto;
            position: relative;
        }
        .about-close {
            position: absolute;
            top: 15px;
            right: 20px;
            font-size: 28px;
            font-weight: bold;
            color: #7f8c8d;
            cursor: pointer;
        }
        .about-close:hover {
            color: #e74c3c;
        }
        .about-title {
            font-size: 24px;
            color: #2c3e50;
            margin-bottom: 20px;
            text-align: center;
            border-bottom: 2px solid #3498db;
            padding-bottom: 10px;
        }
        .about-version {
            background: #ecf0f1;
            padding: 15px;
            border-radius: 10px;
            margin-bottom: 15px;
        }
        .about-version-item {
            display: flex;
            justify-content: space-between;
            padding: 8px 0;
            border-bottom: 1px solid #bdc3c7;
        }
        .about-version-item:last-child {
            border-bottom: none;
        }
        .about-device {
            font-weight: bold;
            color: #3498db;
        }
        .about-ver {
            font-family: 'Courier New', monospace;
            background: #2c3e50;
            color: white;
            padding: 3px 10px;
            border-radius: 15px;
        }
        .about-description {
            margin-top: 20px;
            color: #7f8c8d;
            font-size: 14px;
            line-height: 1.6;
        }
        #lastUpdate {
            font-size: 0.75em;
            color: #95a5a6;
            text-align: right;
            margin-top: 10px;
            font-style: italic;
        }
    </style>
</head>
<body>
    <div class="dashboard">
        <h1>🏠 Умный дом ESP-NOW</h1>
        
        <button id="refreshBtn" onclick="refreshAllData()">🔄 ОБНОВИТЬ ВСЕ ДАННЫЕ</button>
        <button id="aboutBtn" onclick="showAboutModal()">ℹ️ О СИСТЕМЕ</button>
        
        <!-- Глобальная тревога -->
        <div id="globalAlarmBanner" class="global-alarm-banner">
            🚨 ГЛОБАЛЬНАЯ ТРЕВОГА 🚨
        </div>
        
        <!-- Узел #102 -->
        <div class="section">
            <div class="section-title">🔧 Узел #102 (Мастерская)</div>
            <div class="section-info">MAC: AC:EB:E6:49:10:28</div>
            
            <div id="securityStatus102" class="security-status security-normal">
                🔒 ОХРАНА: НОРМА
            </div>
            
            <button id="ledToggleBtn102" class="led-toggle-btn led-unknown" onclick="toggleLED(102)">--</button>
            <div class="clearfix"></div>
            
            <div id="nodeSensorData102">
                <div class="sensor-grid">
                    <div class="sensor-item">
                        <span class="sensor-label">🌡️ Температура</span>
                        <span><span class="sensor-value" id="temp102">--</span><span class="sensor-unit">°C</span></span>
                    </div>
                    <div class="sensor-item">
                        <span class="sensor-label">💧 Влажность</span>
                        <span><span class="sensor-value" id="hum102">--</span><span class="sensor-unit">%</span></span>
                    </div>
                    <div class="sensor-item">
                        <span class="sensor-label">📊 Давление</span>
                        <span><span class="sensor-value" id="press102">--</span><span class="sensor-unit">mmHg</span></span>
                    </div>
                </div>
            </div>
            
            <!-- Компас -->
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
                            <path id="windSectorMax" d="" fill="#f1c40f" fill-opacity="0.5"/>
                            <path id="windSector" d="" fill="#e74c3c" fill-opacity="0.7"/>
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
                    </div>
                </div>
            </div>
        </div>

        <!-- Узел #103 -->
        <div class="section">
            <div class="section-title">🔧 Узел #103</div>
            <div class="section-info">MAC: 88:56:A6:7D:09:64</div>
            
            <div id="securityStatus103" class="security-status security-normal">
                🔒 ОХРАНА: НОРМА
            </div>
            
            <button id="ledToggleBtn103" class="led-toggle-btn led-unknown" onclick="toggleLED(103)">--</button>
            <div class="clearfix"></div>
            
            <div id="nodeSensorData103">
                <div class="sensor-grid">
                    <div class="sensor-item">
                        <span class="sensor-label">🌡️ Температура</span>
                        <span><span class="sensor-value" id="temp103">--</span><span class="sensor-unit">°C</span></span>
                    </div>
                    <div class="sensor-item">
                        <span class="sensor-label">💧 Влажность</span>
                        <span><span class="sensor-value" id="hum103">--</span><span class="sensor-unit">%</span></span>
                    </div>
                    <div class="sensor-item">
                        <span class="sensor-label">📊 Давление</span>
                        <span><span class="sensor-value" id="press103">--</span><span class="sensor-unit">mmHg</span></span>
                    </div>
                </div>
            </div>
        </div>

        <!-- Теплица -->
        <div class="section">
            <div class="section-title">🌿 Теплица</div>
            <div class="section-info">MAC: E8:9F:6D:87:34:8A</div>
            <div class="sensor-grid" id="greenhouseData">
                <div class="sensor-item">
                    <span class="sensor-label">Температура (внутри):</span>
                    <span><span class="sensor-value" id="greenhouseTempIn">--</span><span class="sensor-unit">°C</span></span>
                </div>
                <div class="sensor-item">
                    <span class="sensor-label">Температура (улица):</span>
                    <span><span class="sensor-value" id="greenhouseTempOut">--</span><span class="sensor-unit">°C</span></span>
                </div>
                <div class="sensor-item">
                    <span class="sensor-label">Влажность (внутри):</span>
                    <span><span class="sensor-value" id="greenhouseHum">--</span><span class="sensor-unit">%</span></span>
                </div>
            </div>
            <div id="lastUpdate">Ожидание данных от теплицы...</div>
        </div>
    </div>

    <!-- Модальное окно компаса -->
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

    <!-- Модальное окно "О системе" -->
    <div id="aboutModal" class="about-modal" onclick="hideAboutModal()">
        <div class="about-modal-content" onclick="event.stopPropagation()">
            <span class="about-close" onclick="hideAboutModal()">&times;</span>
            <div class="about-title">ℹ️ О системе</div>
            
            <div class="about-version">
                <div class="about-version-item">
                    <span class="about-device">Хаб (ESP32)</span>
                    <span class="about-ver" id="hubVersion">5.6</span>
                </div>
                <div class="about-version-item">
                    <span class="about-device">Узел #102</span>
                    <span class="about-ver" id="node102Version">2.1</span>
                </div>
                <div class="about-version-item">
                    <span class="about-device">Узел #103</span>
                    <span class="about-ver" id="node103Version">2.1</span>
                </div>
            </div>
            
            <div class="about-description">
                <strong>Описание:</strong>
                <ul>
                    <li>ESP-NOW хаб для умного дома</li>
                    <li>Поддержка 2 узлов + теплица</li>
                    <li>Управление LED с веб-интерфейса</li>
                    <li>Ветер: отображение направления и размаха</li>
                </ul>
            </div>
        </div>
    </div>

    <script>
        const ws = new WebSocket('ws://' + window.location.hostname + '/ws');
        let ledState = {102: 'unknown', 103: 'unknown'};
        let buttonLocked = {102: false, 103: false};
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

        function showAboutModal() {
            document.getElementById('aboutModal').style.display = 'flex';
        }

        function hideAboutModal() {
            document.getElementById('aboutModal').style.display = 'none';
        }

        function toggleWindSize() {
            let modal = document.getElementById('windModal');
            if (modal.style.display === 'flex') {
                modal.style.display = 'none';
            } else {
                modal.style.display = 'flex';
                updateLargeWindDisplay();
            }
        }

        function updateLargeWindDisplay() {
            document.getElementById('windAngleLarge').textContent = document.getElementById('windAngle').textContent;
            document.getElementById('sectorWidthLarge').textContent = document.getElementById('sectorWidth').textContent;
            document.getElementById('maxRangeLarge').innerHTML = document.getElementById('maxRange').innerHTML;
            
            let badge = document.getElementById('stabilityBadge');
            let badgeLarge = document.getElementById('stabilityBadgeLarge');
            badgeLarge.textContent = badge.textContent;
            badgeLarge.style.backgroundColor = badge.style.backgroundColor;
            
            let sector = document.getElementById('windSector');
            let sectorLarge = document.getElementById('windSectorLarge');
            if (sector && sectorLarge) {
                sectorLarge.setAttribute('d', sector.getAttribute('d'));
            }
            
            let sectorMax = document.getElementById('windSectorMax');
            let sectorMaxLarge = document.getElementById('windSectorMaxLarge');
            if (sectorMax && sectorMaxLarge) {
                sectorMaxLarge.setAttribute('d', sectorMax.getAttribute('d'));
            }
            
            let arrow = document.getElementById('windArrow');
            let arrowLarge = document.getElementById('windArrowLarge');
            if (arrow && arrowLarge) {
                arrowLarge.setAttribute('transform', arrow.getAttribute('transform'));
            }
        }

        function drawSector(pathId, start, end) {
            let path = document.getElementById(pathId);
            if (!path) return;
            
            // Нормализуем углы
            start = ((start % 360) + 360) % 360;
            end = ((end % 360) + 360) % 360;
            
            // Если сектор переходит через 0, рисуем два сектора
            if (start > end) {
                // Рисуем от start до 360
                drawSectorInternal(path, start, 360);
                // Рисуем от 0 до end
                drawSectorInternal(path, 0, end);
            } else {
                drawSectorInternal(path, start, end);
            }
        }
        
        function drawSectorInternal(path, start, end) {
            if (start >= end) return;
            
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
            let largeArc = angleDiff > 180 ? 1 : 0;
            
            // Добавляем к существующему пути
            let currentD = path.getAttribute('d') || '';
            let newSegment = `M ${cx} ${cy} L ${x1} ${y1} A ${r} ${r} 0 ${largeArc} 1 ${x2} ${y2} Z`;
            
            if (currentD) {
                path.setAttribute('d', currentD + ' ' + newSegment);
            } else {
                path.setAttribute('d', newSegment);
            }
        }

        function rotateArrow(arrowId, deg) {
            let arrow = document.getElementById(arrowId);
            if (arrow) {
                arrow.setAttribute('transform', `rotate(${deg}, 50, 50)`);
            }
        }

        function updateLEDButton(nodeId) {
            let btn = document.getElementById('ledToggleBtn' + nodeId);
            if (!btn) return;
            
            if (ledState[nodeId] === 'on') {
                btn.textContent = '⏸ ВЫКЛЮЧИТЬ LED';
                btn.className = 'led-toggle-btn led-on';
                btn.disabled = false;
            } else if (ledState[nodeId] === 'off') {
                btn.textContent = '▶ ВКЛЮЧИТЬ LED';
                btn.className = 'led-toggle-btn led-off';
                btn.disabled = false;
            } else {
                btn.textContent = '-- (статус неизвестен)';
                btn.className = 'led-toggle-btn led-unknown';
                btn.disabled = true;
            }
        }

        function toggleLED(nodeId) {
            if (buttonLocked[nodeId] || ws.readyState !== WebSocket.OPEN) return;
            let cmd = (ledState[nodeId] === 'on') ? 'LED_OFF' : 'LED_ON';
            buttonLocked[nodeId] = true;
            let btn = document.getElementById('ledToggleBtn' + nodeId);
            btn.disabled = true;
            btn.textContent = '⏳ ОТПРАВКА...';
            
            setTimeout(() => { 
                buttonLocked[nodeId] = false; 
                updateLEDButton(nodeId);
            }, 5000);
            
            ws.send(JSON.stringify({command: cmd, node: nodeId}));
        }

        function refreshAllData() {
            ws.send(JSON.stringify({command: 'GET_STATUS'}));
        }

        function markNodeDataAsStale(nodeId) {
            let items = document.querySelectorAll('#nodeSensorData' + nodeId + ' .sensor-item');
            items.forEach(i => i.classList.add('stale-data'));
            playShortBeep();
        }

        function markNodeDataAsFresh(nodeId) {
            let items = document.querySelectorAll('#nodeSensorData' + nodeId + ' .sensor-item');
            items.forEach(i => i.classList.remove('stale-data'));
        }

        function updateSecurityStatus(nodeId, alarm) {
            let el = document.getElementById('securityStatus' + nodeId);
            if (!el) return;
            
            if (alarm) {
                el.className = 'security-status security-alarm';
                el.innerHTML = '🚨 ТРЕВОГА!';
                playAlarmTone();
            } else {
                el.className = 'security-status security-normal';
                el.innerHTML = '🔒 ОХРАНА: НОРМА';
                
                let anyAlarm = false;
                for (let id of [102, 103]) {
                    let e = document.getElementById('securityStatus' + id);
                    if (e && e.className.includes('security-alarm')) anyAlarm = true;
                }
                if (!anyAlarm) stopAlarm();
            }
        }

        function showGlobalAlarm(active) {
            let banner = document.getElementById('globalAlarmBanner');
            if (banner) {
                banner.style.display = active ? 'block' : 'none';
                if (active) playAlarmTone();
            }
        }

        ws.onmessage = function(event) {
            try {
                let msg = JSON.parse(event.data);
                
                if (msg.type === 'node_status') {
                    ledState[msg.node] = msg.state;
                    buttonLocked[msg.node] = false;
                    updateLEDButton(msg.node);
                }
                else if (msg.type === 'gpio_status' && msg.gpio8 !== undefined) {
                    ledState[msg.node] = msg.gpio8 ? 'on' : 'off';
                    updateLEDButton(msg.node);
                }
                else if (msg.type === 'sensor_data') {
                    if (msg.node === 102) {
                        if (msg.aht20) {
                            document.getElementById('temp102').textContent = msg.aht20.temp;
                            document.getElementById('hum102').textContent = msg.aht20.hum;
                        }
                        if (msg.bmp280) {
                            document.getElementById('press102').textContent = msg.bmp280.press;
                        }
                    } else if (msg.node === 103) {
                        if (msg.aht20) {
                            document.getElementById('temp103').textContent = msg.aht20.temp;
                            document.getElementById('hum103').textContent = msg.aht20.hum;
                        }
                        if (msg.bmp280) {
                            document.getElementById('press103').textContent = msg.bmp280.press;
                        }
                    }
                }
                else if (msg.type === 'security') {
                    updateSecurityStatus(msg.node, msg.alarm);
                }
                else if (msg.type === 'global_alarm') {
                    showGlobalAlarm(msg.state);
                }
                else if (msg.type === 'connection_lost') {
                    markNodeDataAsStale(msg.node);
                }
                else if (msg.type === 'connection_restored') {
                    markNodeDataAsFresh(msg.node);
                }
                else if (msg.type === 'greenhouse_data') {
                    document.getElementById('greenhouseTempIn').textContent = msg.temp_in;
                    document.getElementById('greenhouseTempOut').textContent = msg.temp_out;
                    document.getElementById('greenhouseHum').textContent = msg.hum_in;
                    document.getElementById('lastUpdate').textContent = `Обновлено: ${new Date().toLocaleTimeString()}`;
                }
                else if (msg.type === 'wind') {
                    document.getElementById('windAngle').textContent = msg.angle_avg;
                    document.getElementById('sectorWidth').textContent = msg.sector_width;
                    document.getElementById('maxRange').innerHTML = `${msg.history_min}° - ${msg.history_max}°`;
                    
                    let magnet = document.getElementById('magnetIndicator');
                    let magnetText = document.getElementById('magnetText');
                    if (msg.magnet) {
                        magnet.style.backgroundColor = '#27ae60';
                        magnetText.textContent = 'магнит есть';
                    } else {
                        magnet.style.backgroundColor = '#e74c3c';
                        magnetText.textContent = 'магнит нет';
                    }
                    
                    let stability = msg.stability;
                    let badge = document.getElementById('stabilityBadge');
                    let badgeLarge = document.getElementById('stabilityBadgeLarge');
                    let text = '', color = '';
                    
                    switch(stability) {
                        case 'calm':   text = 'ШТИЛЬ';    color = '#3498db'; break;
                        case 'gusty':  text = 'ПОРЫВЫ';   color = '#e67e22'; break;
                        case 'strong': text = 'СИЛЬНЫЙ';  color = '#e74c3c'; break;
                        case 'storm':  text = 'ШТОРМ';    color = '#8e44ad'; break;
                        default:       text = 'ШТИЛЬ';    color = '#3498db';
                    }
                    
                    badge.textContent = text;
                    badge.style.backgroundColor = color;
                    
                    if (badgeLarge) {
                        badgeLarge.textContent = text;
                        badgeLarge.style.backgroundColor = color;
                    }
                    
                    // Очищаем старые сектора
                    document.getElementById('windSector').setAttribute('d', '');
                    document.getElementById('windSectorLarge').setAttribute('d', '');
                    document.getElementById('windSectorMax').setAttribute('d', '');
                    document.getElementById('windSectorMaxLarge').setAttribute('d', '');
                    
                    // Рисуем новые сектора
                    if (msg.sector_start !== undefined && msg.sector_end !== undefined) {
                        drawSector('windSector', parseFloat(msg.sector_start), parseFloat(msg.sector_end));
                        drawSector('windSectorLarge', parseFloat(msg.sector_start), parseFloat(msg.sector_end));
                    }
                    
                    if (msg.history_min !== undefined && msg.history_max !== undefined) {
                        drawSector('windSectorMax', parseFloat(msg.history_min), parseFloat(msg.history_max));
                        drawSector('windSectorMaxLarge', parseFloat(msg.history_min), parseFloat(msg.history_max));
                    }
                    
                    rotateArrow('windArrow', parseFloat(msg.angle_avg));
                    rotateArrow('windArrowLarge', parseFloat(msg.angle_avg));
                    
                    if (document.getElementById('windModal').style.display === 'flex') {
                        updateLargeWindDisplay();
                    }
                }
            } catch(e) {
                console.error('Error:', e);
            }
        };

        ws.onopen = function() {
            for (let id of [102, 103]) {
                updateLEDButton(id);
            }
            ws.send(JSON.stringify({command: 'GET_STATUS'}));
        };

        ws.onclose = function() {
            for (let id of [102, 103]) {
                ledState[id] = 'unknown';
                updateLEDButton(id);
            }
        };

        // Инициализация при загрузке
        window.onload = function() {
            initAudio();
            updateLEDButton(102);
            updateLEDButton(103);
        };

        // Закрытие модальных окон по ESC
        document.addEventListener('keydown', function(e) {
            if (e.key === 'Escape') {
                hideAboutModal();
                document.getElementById('windModal').style.display = 'none';
            }
        });
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

    // Добавление узлов
    for (int i = 0; i < NODE_COUNT; i++) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, nodeMacs[i], 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
            Serial.printf("❌ Не удалось добавить узел #%d!\n", nodeNumbers[i]);
        } else {
            Serial.printf("✅ Узел #%d добавлен.\n", nodeNumbers[i]);
        }
    }

    // Добавление теплицы
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
    Serial.println("3. Версия хаба: 5.6");
    Serial.println("4. Поддерживается 2 узла (ID 102, 103) и теплица\n");
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
    
    delay(10);
}

void sendConnectionStatusToWeb(int nodeIndex, bool connected) {
    StaticJsonDocument<100> doc;
    doc["type"] = connected ? "connection_restored" : "connection_lost";
    doc["node"] = nodeNumbers[nodeIndex];
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void checkNodeConnection() {
    unsigned long now = millis();
    for (int i = 0; i < NODE_COUNT; i++) {
        if (lastNodeDataTime[i] > 0) {
            if (now - lastNodeDataTime[i] > NODE_TIMEOUT_MS) {
                if (!nodeConnectionLost[i]) {
                    nodeConnectionLost[i] = true;
                    Serial.printf("⚠️ СВЯЗЬ С УЗЛОМ #%d ПОТЕРЯНА!\n", nodeNumbers[i]);
                    sendConnectionStatusToWeb(i, false);
                    checkGlobalAlarm();
                }
            } else {
                if (nodeConnectionLost[i]) {
                    nodeConnectionLost[i] = false;
                    Serial.printf("✅ СВЯЗЬ С УЗЛОМ #%d ВОССТАНОВЛЕНА!\n", nodeNumbers[i]);
                    sendConnectionStatusToWeb(i, true);
                    checkGlobalAlarm();
                }
            }
        }
    }
}

void updateAlarmState() {
    if (securityAlarmActive && (millis() - alarmStartTime) > ALARM_DURATION_MS) {
        securityAlarmActive = false;
        checkGlobalAlarm();
    }
}

void checkGlobalAlarm() {
    bool newGlobalAlarm = false;
    for (int i = 0; i < NODE_COUNT; i++) {
        if (nodeAlarmState[i] || nodeConnectionLost[i]) {
            newGlobalAlarm = true;
            break;
        }
    }
    
    if (newGlobalAlarm != globalAlarmActive) {
        globalAlarmActive = newGlobalAlarm;
        if (globalAlarmActive) {
            Serial.println("🚨 ГЛОБАЛЬНАЯ ТРЕВОГА!");
        } else {
            Serial.println("✅ Глобальная тревога снята");
        }
        
        StaticJsonDocument<100> doc;
        doc["type"] = "global_alarm";
        doc["state"] = globalAlarmActive;
        String json;
        serializeJson(doc, json);
        ws.textAll(json);
    }
}

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        StaticJsonDocument<100> doc;
        doc["type"] = "global_alarm";
        doc["state"] = globalAlarmActive;
        String json;
        serializeJson(doc, json);
        client->text(json);
    }
    else if (type == WS_EVT_DATA) {
        StaticJsonDocument<200> doc;
        if (!deserializeJson(doc, data, len) && doc.containsKey("command")) {
            String cmd = doc["command"].as<String>();
            int targetNode = doc["node"] | 102;
            
            if (cmd == "GET_STATUS") {
                // Запрашиваем статус у всех узлов
                for (int i = 0; i < NODE_COUNT; i++) {
                    sendToNode(nodeMacs[i], "GET_STATUS");
                }
            } else {
                uint8_t* targetMac = (targetNode == 102) ? node102MacAddress : node103MacAddress;
                if (targetMac) {
                    sendToNode(targetMac, cmd);
                }
            }
        }
    }
}

void sendToNode(uint8_t* mac, String cmd) {
    char json_cmd[64];
    snprintf(json_cmd, sizeof(json_cmd), "{\"type\":\"command\",\"command\":\"%s\"}", cmd.c_str());
    strncpy(outgoingMessage.json, json_cmd, sizeof(outgoingMessage.json)-1);
    outgoingMessage.json[sizeof(outgoingMessage.json)-1] = '\0';
    outgoingMessage.sender_id = 1;
    esp_err_t result = esp_now_send(mac, (uint8_t*)&outgoingMessage, sizeof(outgoingMessage));
    if (result != ESP_OK) {
        Serial.println("❌ Ошибка отправки команды");
    }
}

void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    for (int i = 0; i < NODE_COUNT; i++) {
        if (memcmp(mac_addr, nodeMacs[i], 6) == 0) {
            lastNodeDataTime[i] = millis();
            processNodeData(incomingData, len, i);
            return;
        }
    }
    
    if (memcmp(mac_addr, greenhouseMac, 6) == 0) {
        if (len == sizeof(greenhouse_packet)) {
            processGreenhouseData(incomingData);
        }
    }
}

void processNodeData(const uint8_t *data, int len, int nodeIndex) {
    if (len > sizeof(incomingMessage)) {
        Serial.println("❌ Пакет слишком большой!");
        return;
    }
    
    memcpy(&incomingMessage, data, len);
    
    StaticJsonDocument<384> doc;
    DeserializationError error = deserializeJson(doc, incomingMessage.json);
    if (error) {
        Serial.print("❌ JSON ошибка: ");
        Serial.println(error.c_str());
        return;
    }

    const char* type = doc["type"];
    int nodeId = nodeNumbers[nodeIndex];

    if (strcmp(type, "sensor") == 0) {
        JsonObject dataObj = doc["data"];
        
        StaticJsonDocument<300> resp;
        resp["type"] = "sensor_data";
        resp["node"] = nodeId;
        
        if (dataObj.containsKey("AHT20")) {
            resp["aht20"]["temp"] = String(dataObj["AHT20"]["temp"].as<float>(), 1);
            resp["aht20"]["hum"] = String(dataObj["AHT20"]["hum"].as<float>(), 1);
        }
        if (dataObj.containsKey("BMP280")) {
            resp["bmp280"]["temp"] = String(dataObj["BMP280"]["temp"].as<float>(), 1);
            resp["bmp280"]["press"] = String(dataObj["BMP280"]["press_mmHg"].as<float>(), 1);
        }
        
        String json;
        serializeJson(resp, json);
        ws.textAll(json);
    }
    else if (strcmp(type, "security") == 0) {
        bool alarm = doc["alarm"];
        bool c1 = doc["contact1"];
        bool c2 = doc["contact2"];
        
        nodeAlarmState[nodeIndex] = alarm;
        
        if (alarm && !securityAlarmActive && nodeId == 102) {
            securityAlarmActive = true;
            alarmStartTime = millis();
            Serial.println("🚨 ТРЕВОГА (узел #102)!");
        } else if (!alarm && nodeId == 102) {
            securityAlarmActive = false;
        }
        
        checkGlobalAlarm();
        
        StaticJsonDocument<200> resp;
        resp["type"] = "security";
        resp["node"] = nodeId;
        resp["alarm"] = alarm;
        resp["contact1"] = c1;
        resp["contact2"] = c2;
        String json;
        serializeJson(resp, json);
        ws.textAll(json);
    }
    else if (strcmp(type, "ack") == 0) {
        const char* cmd = doc["command"];
        StaticJsonDocument<200> resp;
        resp["type"] = "node_status";
        resp["node"] = nodeId;
        if (strcmp(cmd, "LED_ON") == 0) {
            resp["state"] = "on";
        } else if (strcmp(cmd, "LED_OFF") == 0) {
            resp["state"] = "off";
        }
        String json;
        serializeJson(resp, json);
        ws.textAll(json);
    }
    else if (strcmp(type, "gpio") == 0) {
        StaticJsonDocument<200> resp;
        resp["type"] = "gpio_status";
        resp["node"] = nodeId;
        if (doc.containsKey("pin") && doc.containsKey("state") && doc["pin"] == 8) {
            resp["gpio8"] = doc["state"].as<int>();
            Serial.printf("📊 GPIO статус для узла #%d: LED %s\n", nodeId, doc["state"].as<int>() ? "ON" : "OFF");
        }
        String json;
        serializeJson(resp, json);
        ws.textAll(json);
    }
    else if (strcmp(type, "encoder") == 0 && nodeIndex == 0) {
        float angle = doc["angle"];
        bool magnet = doc["magnet"];
        
        processEncoderData(angle, magnet);
        if (magnet) {
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
    resp["hum_in"] = (int)pkt.hum_in;
    resp["relay1_state"] = pkt.relay1_state;
    resp["relay2_state"] = pkt.relay2_state;

    String json;
    serializeJson(resp, json);
    ws.textAll(json);
}

String relayStateToString(uint32_t state) {
    return (state == 1) ? "ВКЛЮЧЕНО" : "ВЫКЛЮЧЕНО";
}

// ========== ВЕТЕР ==========
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
    }
    
    windMagnet = magnet;
}

void updateHistory(float angle) {
    encoderHistory[historyIndex] = angle;
    historyTimestamps[historyIndex] = millis();
    historyIndex = (historyIndex + 1) % ENCODER_HISTORY_SIZE;
    if (historyCount < ENCODER_HISTORY_SIZE) {
        historyCount++;
    }
}

void updateMaxMin() {
    if (historyCount == 0) return;
    
    unsigned long now = millis();
    float currentMin = 361.0;
    float currentMax = -1.0;
    int validCount = 0;
    
    for (int i = 0; i < historyCount; i++) {
        if (now - historyTimestamps[i] <= 30000) {
            float a = encoderHistory[i];
            if (a < currentMin) currentMin = a;
            if (a > currentMax) currentMax = a;
            validCount++;
        }
    }
    
    if (validCount > 0 && currentMax >= 0) {
        if (currentMin < 60 && currentMax > 300) {
            // Переход через 0 - нормальные значения
            maxAngle = currentMax;
            minAngle = currentMin;
        } else {
            maxAngle = currentMax;
            minAngle = currentMin;
        }
    }
}

void broadcastEncoderData() {
    if (prevEncoderAngle < 0) return;
    
    float redStart, redEnd;
    String stability;
    
    if (!windMagnet) {
        stability = "no_magnet";
        redStart = 0;
        redEnd = 360;
    } else {
        redStart = fmod(windDirection - windCurrentSector / 2 + 360.0, 360.0);
        redEnd = fmod(windDirection + windCurrentSector / 2 + 360.0, 360.0);
        
        if (windCurrentSector < 10) stability = "calm";
        else if (windCurrentSector < 30) stability = "gusty";
        else if (windCurrentSector < 60) stability = "strong";
        else stability = "storm";
    }
    
    StaticJsonDocument<256> doc;
    doc["type"] = "wind";
    doc["angle_avg"] = String(windDirection, 1);
    doc["sector_width"] = String(windCurrentSector, 1);
    doc["sector_start"] = String(redStart, 1);
    doc["sector_end"] = String(redEnd, 1);
    doc["history_min"] = String(minAngle, 1);
    doc["history_max"] = String(maxAngle, 1);
    doc["magnet"] = windMagnet;
    doc["stability"] = stability;
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}