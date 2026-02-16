/**
 * SmartHome ESP-NOW Hub (ESP32)
 * ВЕРСИЯ 5.7: УСТАВКИ ДЛЯ ВСЕХ ДАТЧИКОВ + ПРОГНОЗ ПОГОДЫ
 * Добавлена поддержка 4 узлов (ID 102, 103, 104, 105)
 * Добавлена кнопка "О системе" с версиями прошивок
 */
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <math.h>
#include <Preferences.h>

// ---- 1. КОНФИГУРАЦИЯ ----
const char* AP_SSID = "SmartHome-Hub";
const char* AP_PASSWORD = "12345678";

// Версии прошивок
const char* HUB_VERSION = "5.7";
const char* NODE_VERSION = "2.1";  // Все узлы используют одну версию

// MAC адреса узлов
// Узел #102 - основной, с энкодером
uint8_t node102MacAddress[] = {0xAC, 0xEB, 0xE6, 0x49, 0x10, 0x28};
// Узел #103
uint8_t node103MacAddress[] = {0x88, 0x56, 0xA6, 0x7D, 0x09, 0x64};
// Узел #104
uint8_t node104MacAddress[] = {0x10, 0x00, 0x3B, 0xB1, 0xA6, 0x9C};
// Узел #105
uint8_t node105MacAddress[] = {0x88, 0x56, 0xA6, 0x7C, 0xF2, 0xA8};

// MAC устройства "Теплица"
uint8_t greenhouseMac[] = {0xE8, 0x9F, 0x6D, 0x87, 0x34, 0x8A};

// Массив всех узлов для удобства
#define NODE_COUNT 4
uint8_t* nodeMacs[NODE_COUNT] = {
    node102MacAddress,
    node103MacAddress,
    node104MacAddress,
    node105MacAddress
};

// Номера узлов для отображения
int nodeNumbers[NODE_COUNT] = {102, 103, 104, 105};

// Время последнего получения данных от каждого узла
unsigned long lastNodeDataTime[NODE_COUNT] = {0, 0, 0, 0};
const unsigned long NODE_TIMEOUT_MS = 70000;

// Флаги потери связи для каждого узла
bool nodeConnectionLost[NODE_COUNT] = {false, false, false, false};
unsigned long connectionLostTime[NODE_COUNT] = {0, 0, 0, 0};
const unsigned long CONNECTION_LOST_COOLDOWN = 10000;

// Статусы тревоги для каждого узла (для глобальной тревоги, но без вывода в веб)
bool nodeAlarmState[NODE_COUNT] = {false, false, false, false};

// ---- 2. УНИВЕРСАЛЬНАЯ СТРУКТУРА ESP-NOW ----
typedef struct esp_now_message {
    char json[256];
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

bool securityAlarmActive = false;
unsigned long alarmStartTime = 0;
const unsigned long ALARM_DURATION_MS = 10000;

// Глобальная тревога (только для логики, без вывода в веб)
bool globalAlarmActive = false;

// Для хранения уставок
Preferences prefs;

// ---- 5. СТРУКТУРА УСТАВОК ДЛЯ ДАТЧИКОВ ----
struct SensorLimit {
    bool enabled;
    float value;
};

struct NodeLimits {
    SensorLimit temp_min;
    SensorLimit temp_max;
    SensorLimit hum_min;
    SensorLimit hum_max;
    SensorLimit press_min;
    SensorLimit press_max;
    SensorLimit wind_storm;      // порог шторма (м/с)
    SensorLimit wind_change;      // порог смены направления (градусы)
};

// Уставки для каждого узла
NodeLimits nodeLimits[NODE_COUNT];

// История давления для прогноза
#define PRESSURE_HISTORY_SIZE 12  // 12 значений = 3 часа при 15 мин интервале
float pressureHistory[NODE_COUNT][PRESSURE_HISTORY_SIZE];
unsigned long pressureTimestamps[NODE_COUNT][PRESSURE_HISTORY_SIZE];
int pressureIndex[NODE_COUNT] = {0, 0, 0, 0};
int pressureCount[NODE_COUNT] = {0, 0, 0, 0};

// История ветра для определения смены направления
#define WIND_HISTORY_SIZE 30  // 30 значений = 30 минут при 1 мин интервале (будем накапливать)
float windDirectionHistory[WIND_HISTORY_SIZE];
unsigned long windDirectionTimestamps[WIND_HISTORY_SIZE];
int windDirectionIndex = 0;
int windDirectionCount = 0;

// История влажности для детекции дождя
#define HUMIDITY_HISTORY_SIZE 12  // 12 значений = 1 час при 5 мин интервале
float humidityHistory[NODE_COUNT][HUMIDITY_HISTORY_SIZE];
unsigned long humidityTimestamps[NODE_COUNT][HUMIDITY_HISTORY_SIZE];
int humidityIndex[NODE_COUNT] = {0, 0, 0, 0};
int humidityCount[NODE_COUNT] = {0, 0, 0, 0};

// ---- 6. ДАННЫЕ ЭНКОДЕРА AS5600 - ДВЕ ТОЧКИ + ИСТОРИЯ 30 СЕК ----
#define ENCODER_HISTORY_SIZE 6        // 6 значений = 30 секунд при 5 сек
#define ENCODER_BROADCAST_INTERVAL 5000
#define ENCODER_TIMEOUT_MS 10000      // 10 сек без данных - тревога

// Текущие две точки
float prevEncoderAngle = -1.0;
float currentEncoderAngle = -1.0;
float windDirection = 0.0;
float windCurrentSector = 0.0;
bool windMagnet = false;
unsigned long lastEncoderDataTime = 0;  // Для отслеживания потери сигнала

// История для желтого сектора
float encoderHistory[ENCODER_HISTORY_SIZE];
unsigned long historyTimestamps[ENCODER_HISTORY_SIZE];
int historyIndex = 0;
int historyCount = 0;

// Максимум и минимум за 30 секунд
float maxAngle = -1.0;
float minAngle = 361.0;
unsigned long lastEncoderBroadcastTime = 0;

// ---- 7. ПРОТОТИПЫ ----
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
void loadLimitsFromPrefs();
void saveLimitsToPrefs();
void updatePressureHistory(int nodeIndex, float pressure);
String getWeatherForecast(int nodeIndex);
void updateWindDirectionHistory(float direction);
void updateHumidityHistory(int nodeIndex, float humidity);
void checkWeatherAlarms(int nodeIndex);

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== SmartHome ESP-NOW Hub (Версия 5.7) ===");
    Serial.println("=== УСТАВКИ ДЛЯ ВСЕХ ДАТЧИКОВ + ПРОГНОЗ ПОГОДЫ ===");

    // ИНИЦИАЛИЗАЦИЯ ИСТОРИИ
    historyCount = 0;
    historyIndex = 0;
    minAngle = 361.0;
    maxAngle = -1.0;
    lastEncoderDataTime = 0;
    
    // Загрузка уставок из памяти
    prefs.begin("hub", false);
    loadLimitsFromPrefs();

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
            padding: 12px;
            border-radius: 8px;
            border-left: 4px solid #3498db;
            cursor: pointer;
            transition: all 0.2s;
            position: relative;
            min-height: 80px;
            display: flex;
            flex-direction: column;
            justify-content: center;
        }
        .sensor-item:hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 8px rgba(0,0,0,0.2);
        }
        .sensor-item.alarm {
            border-left: 4px solid #e74c3c;
            background: #fff5f5;
        }
        .sensor-item.warning {
            border-left: 4px solid #f39c12;
            background: #fff9e6;
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
        .sensor-limits {
            font-size: 0.7em;
            color: #7f8c8d;
            margin-top: 4px;
            font-family: monospace;
        }
        .sensor-limits.active {
            color: #e67e22;
            font-weight: bold;
        }
        .limits-indicator {
            display: inline-block;
            background: #e67e22;
            color: white;
            padding: 2px 8px;
            border-radius: 12px;
            font-size: 10px;
            margin-left: 8px;
        }
        .weather-forecast {
            margin-top: 10px;
            padding: 8px;
            background: #e8f4fd;
            border-radius: 6px;
            font-size: 0.9em;
            text-align: center;
        }
        .weather-forecast.rain {
            background: #d4e6f1;
            color: #2875a7;
        }
        .weather-forecast.sun {
            background: #fcf3cf;
            color: #b7950b;
        }
        .weather-forecast.storm {
            background: #fadbd8;
            color: #c0392b;
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
        .wind-badge.warning {
            background: #ffa500 !important;
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
        .legend-orange {
            display: inline-block;
            width: 12px;
            height: 12px;
            background: #ffa500;
            border-radius: 2px;
            margin-right: 4px;
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
            transition: color 0.3s;
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
        .about-description ul {
            padding-left: 20px;
        }
        .about-description li {
            margin: 5px 0;
        }
        
        /* Модальное окно уставок */
        .modal {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0,0,0,0.8);
            z-index: 10000;
            justify-content: center;
            align-items: center;
        }
        .modal-content {
            background: white;
            border-radius: 20px;
            padding: 30px;
            max-width: 400px;
            width: 90%;
            position: relative;
        }
        .modal-close {
            position: absolute;
            top: 15px;
            right: 20px;
            font-size: 28px;
            font-weight: bold;
            color: #7f8c8d;
            cursor: pointer;
        }
        .modal-close:hover {
            color: #e74c3c;
        }
        .modal-title {
            font-size: 20px;
            color: #2c3e50;
            margin-bottom: 20px;
            text-align: center;
            border-bottom: 2px solid #3498db;
            padding-bottom: 10px;
        }
        .limits-form {
            display: flex;
            flex-direction: column;
            gap: 20px;
        }
        .limit-row {
            display: flex;
            align-items: center;
            gap: 15px;
            padding: 10px;
            background: #f5f5f5;
            border-radius: 8px;
        }
        .limit-label {
            width: 60px;
            font-weight: bold;
            color: #2c3e50;
        }
        .limit-checkbox {
            width: 20px;
            height: 20px;
            cursor: pointer;
        }
        .limit-input {
            width: 100px;
            padding: 8px;
            border: 2px solid #bdc3c7;
            border-radius: 5px;
            font-size: 16px;
            text-align: center;
        }
        .limit-input:focus {
            border-color: #3498db;
            outline: none;
        }
        .limit-buttons {
            display: flex;
            gap: 5px;
        }
        .limit-btn {
            width: 40px;
            height: 40px;
            border: none;
            border-radius: 5px;
            background: #3498db;
            color: white;
            font-size: 18px;
            font-weight: bold;
            cursor: pointer;
        }
        .limit-btn:hover {
            background: #2980b9;
        }
        .limit-btn:active {
            transform: scale(0.95);
        }
        .save-btn {
            background: #27ae60;
            color: white;
            border: none;
            padding: 15px;
            border-radius: 8px;
            font-size: 16px;
            font-weight: bold;
            cursor: pointer;
            margin-top: 10px;
        }
        .save-btn:hover {
            background: #2ecc71;
        }
        .forecast-badge {
            display: inline-block;
            padding: 2px 8px;
            border-radius: 12px;
            font-size: 10px;
            font-weight: bold;
            color: white;
            margin-left: 8px;
        }
        .forecast-rain { background: #3498db; }
        .forecast-sun { background: #f39c12; }
        .forecast-cloud { background: #95a5a6; }
        .forecast-storm { background: #e74c3c; animation: alarm-pulse 1s infinite; }
    </style>
</head>
<body>
    <div class="dashboard">
        <h1>🏠 Умный дом ESP-NOW</h1>
        
        <button id="refreshBtn" onclick="refreshAllData()">🔄 ОБНОВИТЬ ВСЕ ДАННЫЕ</button>
        <button id="aboutBtn" onclick="showAboutModal()">ℹ️ О СИСТЕМЕ</button>
        
        <div class="section">
            <div class="section-title">🔧 Узел #102 (Мастерская, с энкодером)</div>
            <div class="section-info">MAC: AC:EB:E6:49:10:28</div>
            
            <div id="securityStatus102" class="security-status security-normal">
                🔒 ОХРАНА: НОРМА (концевики замкнуты)
            </div>
            
            <button id="ledToggleBtn102" class="led-toggle-btn led-unknown" onclick="toggleLED(102)">--</button>
            <div class="clearfix"></div>
            
            <div id="nodeSensorData102">
                <div class="sensor-grid">
                    <!-- Температура -->
                    <div class="sensor-item" id="sensor-temp-102" onclick="openLimitsModal('temp', 102)">
                        <div class="sensor-label">
                            🌡️ Температура
                            <span class="limits-indicator" id="temp-indicator-102" style="display: none;">⚙️</span>
                        </div>
                        <div>
                            <span class="sensor-value" id="temp-value-102">--</span>
                            <span class="sensor-unit">°C</span>
                        </div>
                        <div class="sensor-limits" id="temp-limits-102"></div>
                    </div>
                    
                    <!-- Влажность -->
                    <div class="sensor-item" id="sensor-hum-102" onclick="openLimitsModal('hum', 102)">
                        <div class="sensor-label">
                            💧 Влажность
                            <span class="limits-indicator" id="hum-indicator-102" style="display: none;">⚙️</span>
                        </div>
                        <div>
                            <span class="sensor-value" id="hum-value-102">--</span>
                            <span class="sensor-unit">%</span>
                        </div>
                        <div class="sensor-limits" id="hum-limits-102"></div>
                    </div>
                    
                    <!-- Давление -->
                    <div class="sensor-item" id="sensor-press-102" onclick="openLimitsModal('press', 102)">
                        <div class="sensor-label">
                            📊 Давление
                            <span class="limits-indicator" id="press-indicator-102" style="display: none;">⚙️</span>
                            <span id="press-forecast-102" class="forecast-badge"></span>
                        </div>
                        <div>
                            <span class="sensor-value" id="press-value-102">--</span>
                            <span class="sensor-unit">mmHg</span>
                        </div>
                        <div class="sensor-limits" id="press-limits-102"></div>
                    </div>
                </div>
            </div>
            
            <!-- Прогноз погоды -->
            <div id="weather-forecast-102" class="weather-forecast">
                🌤️ Прогноз: стабильно
            </div>
            
            <!-- БЛОК ВЕТРА (только для узла #102) -->
            <div id="windBlock" class="wind-compact" onclick="toggleWindSize()" style="margin-top: 15px; padding-top: 10px; border-top: 1px dashed #ccc;">
                <div style="display: flex; align-items: center; margin-bottom: 8px;">
                    <span style="font-weight: bold; color: #2c3e50; font-size: 1.1em;">🌪️ Ветер</span>
                    <span id="magnetIndicator" style="display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-left: 8px; background-color: #95a5a6;"></span>
                    <span id="magnetText" style="margin-left: 4px; font-size: 0.8em; color: #7f8c8d;">магнит</span>
                    <span id="wind-change-indicator" class="forecast-badge" style="display: none;">↺</span>
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
                            <span><span class="legend-orange"></span> нет сигнала</span>
                        </div>
                    </div>
                </div>
            </div>
        </div>

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
                    <!-- Температура -->
                    <div class="sensor-item" id="sensor-temp-103" onclick="openLimitsModal('temp', 103)">
                        <div class="sensor-label">
                            🌡️ Температура
                            <span class="limits-indicator" id="temp-indicator-103" style="display: none;">⚙️</span>
                        </div>
                        <div>
                            <span class="sensor-value" id="temp-value-103">--</span>
                            <span class="sensor-unit">°C</span>
                        </div>
                        <div class="sensor-limits" id="temp-limits-103"></div>
                    </div>
                    
                    <!-- Влажность -->
                    <div class="sensor-item" id="sensor-hum-103" onclick="openLimitsModal('hum', 103)">
                        <div class="sensor-label">
                            💧 Влажность
                            <span class="limits-indicator" id="hum-indicator-103" style="display: none;">⚙️</span>
                        </div>
                        <div>
                            <span class="sensor-value" id="hum-value-103">--</span>
                            <span class="sensor-unit">%</span>
                        </div>
                        <div class="sensor-limits" id="hum-limits-103"></div>
                    </div>
                    
                    <!-- Давление -->
                    <div class="sensor-item" id="sensor-press-103" onclick="openLimitsModal('press', 103)">
                        <div class="sensor-label">
                            📊 Давление
                            <span class="limits-indicator" id="press-indicator-103" style="display: none;">⚙️</span>
                            <span id="press-forecast-103" class="forecast-badge"></span>
                        </div>
                        <div>
                            <span class="sensor-value" id="press-value-103">--</span>
                            <span class="sensor-unit">mmHg</span>
                        </div>
                        <div class="sensor-limits" id="press-limits-103"></div>
                    </div>
                </div>
            </div>
            
            <!-- Прогноз погоды -->
            <div id="weather-forecast-103" class="weather-forecast">
                🌤️ Прогноз: стабильно
            </div>
        </div>

        <div class="section">
            <div class="section-title">🔧 Узел #104</div>
            <div class="section-info">MAC: 10:00:3B:B1:A6:9C</div>
            
            <div id="securityStatus104" class="security-status security-normal">
                🔒 ОХРАНА: НОРМА
            </div>
            
            <button id="ledToggleBtn104" class="led-toggle-btn led-unknown" onclick="toggleLED(104)">--</button>
            <div class="clearfix"></div>
            
            <div id="nodeSensorData104">
                <div class="sensor-grid">
                    <!-- Температура -->
                    <div class="sensor-item" id="sensor-temp-104" onclick="openLimitsModal('temp', 104)">
                        <div class="sensor-label">
                            🌡️ Температура
                            <span class="limits-indicator" id="temp-indicator-104" style="display: none;">⚙️</span>
                        </div>
                        <div>
                            <span class="sensor-value" id="temp-value-104">--</span>
                            <span class="sensor-unit">°C</span>
                        </div>
                        <div class="sensor-limits" id="temp-limits-104"></div>
                    </div>
                    
                    <!-- Влажность -->
                    <div class="sensor-item" id="sensor-hum-104" onclick="openLimitsModal('hum', 104)">
                        <div class="sensor-label">
                            💧 Влажность
                            <span class="limits-indicator" id="hum-indicator-104" style="display: none;">⚙️</span>
                        </div>
                        <div>
                            <span class="sensor-value" id="hum-value-104">--</span>
                            <span class="sensor-unit">%</span>
                        </div>
                        <div class="sensor-limits" id="hum-limits-104"></div>
                    </div>
                    
                    <!-- Давление -->
                    <div class="sensor-item" id="sensor-press-104" onclick="openLimitsModal('press', 104)">
                        <div class="sensor-label">
                            📊 Давление
                            <span class="limits-indicator" id="press-indicator-104" style="display: none;">⚙️</span>
                            <span id="press-forecast-104" class="forecast-badge"></span>
                        </div>
                        <div>
                            <span class="sensor-value" id="press-value-104">--</span>
                            <span class="sensor-unit">mmHg</span>
                        </div>
                        <div class="sensor-limits" id="press-limits-104"></div>
                    </div>
                </div>
            </div>
            
            <!-- Прогноз погоды -->
            <div id="weather-forecast-104" class="weather-forecast">
                🌤️ Прогноз: стабильно
            </div>
        </div>

        <div class="section">
            <div class="section-title">🔧 Узел #105</div>
            <div class="section-info">MAC: 88:56:A6:7C:F2:A8</div>
            
            <div id="securityStatus105" class="security-status security-normal">
                🔒 ОХРАНА: НОРМА
            </div>
            
            <button id="ledToggleBtn105" class="led-toggle-btn led-unknown" onclick="toggleLED(105)">--</button>
            <div class="clearfix"></div>
            
            <div id="nodeSensorData105">
                <div class="sensor-grid">
                    <!-- Температура -->
                    <div class="sensor-item" id="sensor-temp-105" onclick="openLimitsModal('temp', 105)">
                        <div class="sensor-label">
                            🌡️ Температура
                            <span class="limits-indicator" id="temp-indicator-105" style="display: none;">⚙️</span>
                        </div>
                        <div>
                            <span class="sensor-value" id="temp-value-105">--</span>
                            <span class="sensor-unit">°C</span>
                        </div>
                        <div class="sensor-limits" id="temp-limits-105"></div>
                    </div>
                    
                    <!-- Влажность -->
                    <div class="sensor-item" id="sensor-hum-105" onclick="openLimitsModal('hum', 105)">
                        <div class="sensor-label">
                            💧 Влажность
                            <span class="limits-indicator" id="hum-indicator-105" style="display: none;">⚙️</span>
                        </div>
                        <div>
                            <span class="sensor-value" id="hum-value-105">--</span>
                            <span class="sensor-unit">%</span>
                        </div>
                        <div class="sensor-limits" id="hum-limits-105"></div>
                    </div>
                    
                    <!-- Давление -->
                    <div class="sensor-item" id="sensor-press-105" onclick="openLimitsModal('press', 105)">
                        <div class="sensor-label">
                            📊 Давление
                            <span class="limits-indicator" id="press-indicator-105" style="display: none;">⚙️</span>
                            <span id="press-forecast-105" class="forecast-badge"></span>
                        </div>
                        <div>
                            <span class="sensor-value" id="press-value-105">--</span>
                            <span class="sensor-unit">mmHg</span>
                        </div>
                        <div class="sensor-limits" id="press-limits-105"></div>
                    </div>
                </div>
            </div>
            
            <!-- Прогноз погоды -->
            <div id="weather-forecast-105" class="weather-forecast">
                🌤️ Прогноз: стабильно
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

    <!-- Модальное окно уставок -->
    <div id="limitsModal" class="modal">
        <div class="modal-content">
            <span class="modal-close" onclick="closeLimitsModal()">&times;</span>
            <div class="modal-title" id="modal-title">Настройка уставок</div>
            
            <div class="limits-form">
                <!-- МИНИМУМ -->
                <div class="limit-row">
                    <span class="limit-label">МИН</span>
                    <input type="checkbox" id="min-enable" class="limit-checkbox">
                    <input type="number" id="min-value" class="limit-input" step="0.1" value="0">
                    <div class="limit-buttons">
                        <button class="limit-btn" onclick="adjustMin(-1)">-1</button>
                        <button class="limit-btn" onclick="adjustMin(1)">+1</button>
                    </div>
                </div>
                
                <!-- МАКСИМУМ -->
                <div class="limit-row">
                    <span class="limit-label">МАКС</span>
                    <input type="checkbox" id="max-enable" class="limit-checkbox">
                    <input type="number" id="max-value" class="limit-input" step="0.1" value="0">
                    <div class="limit-buttons">
                        <button class="limit-btn" onclick="adjustMax(-1)">-1</button>
                        <button class="limit-btn" onclick="adjustMax(1)">+1</button>
                    </div>
                </div>
                
                <button class="save-btn" onclick="saveLimits()">💾 СОХРАНИТЬ</button>
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
                    <span class="about-ver" id="hubVersion">5.7</span>
                </div>
                <div class="about-version-item">
                    <span class="about-device">Узел #102 (с энкодером)</span>
                    <span class="about-ver" id="node102Version">2.1</span>
                </div>
                <div class="about-version-item">
                    <span class="about-device">Узел #103</span>
                    <span class="about-ver" id="node103Version">2.1</span>
                </div>
                <div class="about-version-item">
                    <span class="about-device">Узел #104</span>
                    <span class="about-ver" id="node104Version">2.1</span>
                </div>
                <div class="about-version-item">
                    <span class="about-device">Узел #105</span>
                    <span class="about-ver" id="node105Version">2.1</span>
                </div>
                <div class="about-version-item">
                    <span class="about-device">Теплица</span>
                    <span class="about-ver">1.0</span>
                </div>
            </div>
            
            <div class="about-description">
                <strong>Описание:</strong>
                <ul>
                    <li>ESP-NOW хаб для умного дома</li>
                    <li>Поддержка 4 узлов (ESP32-C3) + теплица</li>
                    <li>Датчики: AHT20, BMP280, AS5600 (энкодер)</li>
                    <li>Охрана с концевиками (GPIO3, GPIO4)</li>
                    <li>Управление LED (GPIO8) с веб-интерфейса</li>
                    <li>Ветер: отображение направления, размаха, желтый сектор 30 сек, штиль/шторм</li>
                    <li>Автоопределение потери связи (70 сек)</li>
                    <li>Уставки для всех датчиков с настройкой мин/макс</li>
                    <li>Прогноз погоды по давлению (Zambretti алгоритм)</li>
                    <li>Детекция дождя по влажности</li>
                    <li>Предупреждение о смене ветра</li>
                </ul>
                <strong>Версия хаба:</strong> 5.7<br>
                <strong>Версия узлов:</strong> 2.1<br>
                <strong>Дата сборки:</strong> 2024
            </div>
        </div>
    </div>

    <script>
        const ws = new WebSocket('ws://' + window.location.hostname + '/ws');
        let ledState = {102: 'unknown', 103: 'unknown', 104: 'unknown', 105: 'unknown'};
        let buttonLocked = {102: false, 103: false, 104: false, 105: false};
        let audioContext = null;
        let alarmInterval = null;
        let isAlarmPlaying = false;

        // Текущий выбранный датчик для модального окна
        let currentSensor = {
            nodeId: 102,
            type: 'temp'
        };

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
        }

        function stopAlarm() {
            isAlarmPlaying = false;
            if (alarmInterval) {
                clearInterval(alarmInterval);
                alarmInterval = null;
            }
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

        // Звуки для разных событий
        function playPressureDropSound() {
            if (!audioContext) return;
            function play(freq, dur) {
                let osc = audioContext.createOscillator();
                let gain = audioContext.createGain();
                osc.connect(gain);
                gain.connect(audioContext.destination);
                osc.frequency.value = freq;
                osc.type = 'sine';
                gain.gain.value = 0.1;
                osc.start();
                gain.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + dur);
                osc.stop(audioContext.currentTime + dur);
            }
            play(400, 0.3);
            setTimeout(() => play(300, 0.5), 400);
        }

        function playRainSound() {
            if (!audioContext) return;
            for (let i = 0; i < 5; i++) {
                setTimeout(() => {
                    let osc = audioContext.createOscillator();
                    let gain = audioContext.createGain();
                    osc.connect(gain);
                    gain.connect(audioContext.destination);
                    osc.frequency.value = 800 + Math.random() * 400;
                    osc.type = 'white';
                    gain.gain.value = 0.05;
                    osc.start();
                    gain.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + 0.1);
                    osc.stop(audioContext.currentTime + 0.1);
                }, i * 150);
            }
        }

        function playWindChangeSound() {
            if (!audioContext) return;
            let osc = audioContext.createOscillator();
            let gain = audioContext.createGain();
            osc.connect(gain);
            gain.connect(audioContext.destination);
            osc.frequency.value = 600;
            osc.type = 'sawtooth';
            gain.gain.value = 0.1;
            osc.start();
            gain.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + 0.5);
            osc.stop(audioContext.currentTime + 0.5);
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

        function updateLEDButton(nodeId) {
            let btn = document.getElementById('ledToggleBtn' + nodeId);
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
            setTimeout(() => { buttonLocked[nodeId] = false; updateLEDButton(nodeId); }, 5000);
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

        function updateSecurityStatus(nodeId, alarm, c1, c2) {
            let el = document.getElementById('securityStatus' + nodeId);
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
                let anyAlarm = false;
                for (let id of [102, 103, 104, 105]) {
                    let statusEl = document.getElementById('securityStatus' + id);
                    if (statusEl && statusEl.className.includes('security-alarm')) {
                        anyAlarm = true;
                        break;
                    }
                }
                if (!anyAlarm) stopAlarm();
            }
        }

        // Функции для модального окна уставок
        function openLimitsModal(type, nodeId = 102) {
            currentSensor.nodeId = nodeId;
            currentSensor.type = type;
            
            let suffix = nodeId + '-' + type;
            // Здесь должны быть загружены текущие уставки из данных
            // В реальном коде они придут с хаба
            
            let sensorNames = { temp: 'Температура', hum: 'Влажность', press: 'Давление' };
            document.getElementById('modal-title').innerHTML = 
                `Узел #${nodeId} — ${sensorNames[type]}`;
            
            document.getElementById('limitsModal').style.display = 'flex';
        }

        function closeLimitsModal() {
            document.getElementById('limitsModal').style.display = 'none';
        }

        function adjustMin(delta) {
            let input = document.getElementById('min-value');
            let val = parseFloat(input.value) + delta;
            input.value = val.toFixed(1);
        }

        function adjustMax(delta) {
            let input = document.getElementById('max-value');
            let val = parseFloat(input.value) + delta;
            input.value = val.toFixed(1);
        }

        function saveLimits() {
            // Отправляем уставки на хаб
            ws.send(JSON.stringify({
                type: 'set_limits',
                node: currentSensor.nodeId,
                sensor: currentSensor.type,
                min: {
                    enabled: document.getElementById('min-enable').checked,
                    value: parseFloat(document.getElementById('min-value').value)
                },
                max: {
                    enabled: document.getElementById('max-enable').checked,
                    value: parseFloat(document.getElementById('max-value').value)
                }
            }));
            closeLimitsModal();
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
                ledState[msg.node] = msg.state;
                buttonLocked[msg.node] = false;
                updateLEDButton(msg.node);
            }
            else if (msg.type === 'sensor_data') {
                let nodeId = msg.node;
                if (msg.temp !== undefined) {
                    document.getElementById('temp-value-' + nodeId).textContent = msg.temp.toFixed(1);
                }
                if (msg.hum !== undefined) {
                    document.getElementById('hum-value-' + nodeId).textContent = msg.hum.toFixed(1);
                }
                if (msg.press !== undefined) {
                    document.getElementById('press-value-' + nodeId).textContent = msg.press.toFixed(1);
                }
            }
            else if (msg.type === 'security') {
                updateSecurityStatus(msg.node, msg.alarm, msg.contact1, msg.contact2);
            }
            else if (msg.type === 'connection_lost') {
                markNodeDataAsStale(msg.node);
            }
            else if (msg.type === 'connection_restored') {
                markNodeDataAsFresh(msg.node);
            }
            else if (msg.type === 'gpio_status') {
                if (msg.gpio8 !== undefined) {
                    ledState[msg.node] = msg.gpio8 ? 'on' : 'off';
                    updateLEDButton(msg.node);
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
                let badge = document.getElementById('stabilityBadge');
                let badgeLarge = document.getElementById('stabilityBadgeLarge');
                
                if (msg.stability === 'no_signal') {
                    magnet.style.backgroundColor = '#ffa500';
                    magnetText.textContent = 'нет сигнала';
                    magnetText.style.color = '#ffa500';
                    badge.textContent = 'НЕТ СИГНАЛА';
                    badge.className = 'wind-badge warning';
                    if (badgeLarge) {
                        badgeLarge.textContent = 'НЕТ СИГНАЛА';
                        badgeLarge.className = 'wind-badge warning';
                    }
                    drawSector('windSector', 0, 360);
                    drawSector('windSectorLarge', 0, 360);
                } else if (msg.stability === 'no_magnet') {
                    magnet.style.backgroundColor = '#ffa500';
                    magnetText.textContent = 'магнит?';
                    magnetText.style.color = '#ffa500';
                    badge.textContent = 'МАГНИТ?';
                    badge.className = 'wind-badge warning';
                    if (badgeLarge) {
                        badgeLarge.textContent = 'МАГНИТ?';
                        badgeLarge.className = 'wind-badge warning';
                    }
                    drawSector('windSector', 0, 360);
                    drawSector('windSectorLarge', 0, 360);
                } else {
                    if (msg.magnet) {
                        magnet.style.backgroundColor = '#27ae60';
                        magnetText.textContent = 'магнит есть';
                        magnetText.style.color = '#27ae60';
                    } else {
                        magnet.style.backgroundColor = '#e74c3c';
                        magnetText.textContent = 'магнит нет';
                        magnetText.style.color = '#e74c3c';
                    }
                    
                    let stability = msg.stability;
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
                    badge.className = 'wind-badge';
                    if (badgeLarge) {
                        badgeLarge.textContent = text;
                        badgeLarge.style.backgroundColor = color;
                        badgeLarge.className = 'wind-badge';
                    }
                    
                    if (msg.sector_start !== undefined && msg.sector_end !== undefined) {
                        drawSector('windSector', parseFloat(msg.sector_start), parseFloat(msg.sector_end));
                        drawSector('windSectorLarge', parseFloat(msg.sector_start), parseFloat(msg.sector_end));
                    }
                    
                    if (msg.history_min !== undefined && msg.history_max !== undefined) {
                        drawSector('windSectorMax', parseFloat(msg.history_min), parseFloat(msg.history_max));
                        drawSector('windSectorMaxLarge', parseFloat(msg.history_min), parseFloat(msg.history_max));
                    }
                }
                
                rotateArrow('windArrow', parseFloat(msg.angle_avg));
                rotateArrow('windArrowLarge', parseFloat(msg.angle_avg));
            }
            else if (msg.type === 'limits_update') {
                // Обновление отображения уставок
                let suffix = msg.node + '-' + msg.sensor;
                let indicator = document.getElementById(msg.sensor + '-indicator-' + msg.node);
                
                if (msg.min.enabled || msg.max.enabled) {
                    indicator.style.display = 'inline-block';
                    
                    let limitsText = '';
                    if (msg.min.enabled) limitsText += `↓${msg.min.value.toFixed(1)}`;
                    if (msg.max.enabled) {
                        if (msg.min.enabled) limitsText += ' ';
                        limitsText += `↑${msg.max.value.toFixed(1)}`;
                    }
                    document.getElementById(msg.sensor + '-limits-' + msg.node).innerHTML = limitsText;
                    document.getElementById(msg.sensor + '-limits-' + msg.node).className = 'sensor-limits active';
                } else {
                    indicator.style.display = 'none';
                    document.getElementById(msg.sensor + '-limits-' + msg.node).innerHTML = '';
                    document.getElementById(msg.sensor + '-limits-' + msg.node).className = 'sensor-limits';
                }
            }
            else if (msg.type === 'weather_forecast') {
                let forecastEl = document.getElementById('weather-forecast-' + msg.node);
                forecastEl.className = 'weather-forecast ' + msg.forecast_class;
                forecastEl.innerHTML = msg.forecast_text;
                
                let badge = document.getElementById('press-forecast-' + msg.node);
                badge.className = 'forecast-badge forecast-' + msg.trend;
                badge.textContent = msg.trend === 'rain' ? '🌧️' : (msg.trend === 'sun' ? '☀️' : '☁️');
            }
            else if (msg.type === 'weather_alarm') {
                if (msg.alarm_type === 'pressure_drop') {
                    playPressureDropSound();
                } else if (msg.alarm_type === 'rain') {
                    playRainSound();
                } else if (msg.alarm_type === 'wind_change') {
                    playWindChangeSound();
                }
            }
        };

        ws.onopen = function() {
            for (let id of [102, 103, 104, 105]) {
                updateLEDButton(id);
            }
            ws.send(JSON.stringify({command: 'GET_STATUS'}));
        };

        ws.onclose = function() {
            for (let id of [102, 103, 104, 105]) {
                ledState[id] = 'unknown';
                updateLEDButton(id);
            }
        };

        for (let id of [102, 103, 104, 105]) {
            updateLEDButton(id);
        }

        // Закрытие модальных окон по ESC
        document.addEventListener('keydown', function(e) {
            if (e.key === 'Escape') {
                hideAboutModal();
                document.getElementById('windModal').style.display = 'none';
                closeLimitsModal();
            }
        });

        // Закрытие по клику вне окна
        window.onclick = function(event) {
            let modal = document.getElementById('limitsModal');
            if (event.target === modal) {
                closeLimitsModal();
            }
        };
    </script>
</body>
</html>
        )rawliteral";
        request->send(200, "text/html", html);
    });

    // API для получения уставок
    server.on("/api/limits", HTTP_GET, [](AsyncWebServerRequest *request) {
        int nodeId = request->arg("node").toInt();
        String sensor = request->arg("sensor");
        
        int nodeIdx = -1;
        for (int i = 0; i < NODE_COUNT; i++) {
            if (nodeNumbers[i] == nodeId) {
                nodeIdx = i;
                break;
            }
        }
        
        if (nodeIdx == -1) {
            request->send(404, "application/json", "{\"error\":\"Node not found\"}");
            return;
        }
        
        StaticJsonDocument<200> doc;
        if (sensor == "temp") {
            doc["min"]["enabled"] = nodeLimits[nodeIdx].temp_min.enabled;
            doc["min"]["value"] = nodeLimits[nodeIdx].temp_min.value;
            doc["max"]["enabled"] = nodeLimits[nodeIdx].temp_max.enabled;
            doc["max"]["value"] = nodeLimits[nodeIdx].temp_max.value;
        } else if (sensor == "hum") {
            doc["min"]["enabled"] = nodeLimits[nodeIdx].hum_min.enabled;
            doc["min"]["value"] = nodeLimits[nodeIdx].hum_min.value;
            doc["max"]["enabled"] = nodeLimits[nodeIdx].hum_max.enabled;
            doc["max"]["value"] = nodeLimits[nodeIdx].hum_max.value;
        } else if (sensor == "press") {
            doc["min"]["enabled"] = nodeLimits[nodeIdx].press_min.enabled;
            doc["min"]["value"] = nodeLimits[nodeIdx].press_min.value;
            doc["max"]["enabled"] = nodeLimits[nodeIdx].press_max.enabled;
            doc["max"]["value"] = nodeLimits[nodeIdx].press_max.value;
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
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

    // Добавление всех узлов как пиров
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
    Serial.println("3. Версия хаба: 5.7 (уставки + прогноз)");
    Serial.println("4. Поддерживается 4 узла (ID 102, 103, 104, 105)");
    Serial.println("5. Уставки сохраняются в памяти\n");
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
                    connectionLostTime[i] = now;
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
    
    // Проверка потери сигнала энкодера
    if (lastEncoderDataTime > 0 && (millis() - lastEncoderDataTime) > ENCODER_TIMEOUT_MS) {
        newGlobalAlarm = true;
    }
    
    if (newGlobalAlarm != globalAlarmActive) {
        globalAlarmActive = newGlobalAlarm;
        if (globalAlarmActive) {
            Serial.println("🚨 ГЛОБАЛЬНАЯ ТРЕВОГА!");
        } else {
            Serial.println("✅ Глобальная тревога снята");
        }
    }
}

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
        StaticJsonDocument<256> doc;
        if (!deserializeJson(doc, data, len)) {
            if (doc.containsKey("command")) {
                String cmd = doc["command"].as<String>();
                int targetNode = doc["node"] | 102;
                
                uint8_t* targetMac = nullptr;
                switch(targetNode) {
                    case 102: targetMac = node102MacAddress; break;
                    case 103: targetMac = node103MacAddress; break;
                    case 104: targetMac = node104MacAddress; break;
                    case 105: targetMac = node105MacAddress; break;
                    default: targetMac = node102MacAddress;
                }
                
                if (targetMac) {
                    sendToNode(targetMac, cmd);
                }
            }
            else if (doc.containsKey("type") && doc["type"] == "set_limits") {
                // Обработка установки уставок
                int nodeId = doc["node"];
                String sensor = doc["sensor"];
                bool min_enabled = doc["min"]["enabled"];
                float min_value = doc["min"]["value"];
                bool max_enabled = doc["max"]["enabled"];
                float max_value = doc["max"]["value"];
                
                int nodeIdx = -1;
                for (int i = 0; i < NODE_COUNT; i++) {
                    if (nodeNumbers[i] == nodeId) {
                        nodeIdx = i;
                        break;
                    }
                }
                
                if (nodeIdx != -1) {
                    if (sensor == "temp") {
                        nodeLimits[nodeIdx].temp_min.enabled = min_enabled;
                        nodeLimits[nodeIdx].temp_min.value = min_value;
                        nodeLimits[nodeIdx].temp_max.enabled = max_enabled;
                        nodeLimits[nodeIdx].temp_max.value = max_value;
                    } else if (sensor == "hum") {
                        nodeLimits[nodeIdx].hum_min.enabled = min_enabled;
                        nodeLimits[nodeIdx].hum_min.value = min_value;
                        nodeLimits[nodeIdx].hum_max.enabled = max_enabled;
                        nodeLimits[nodeIdx].hum_max.value = max_value;
                    } else if (sensor == "press") {
                        nodeLimits[nodeIdx].press_min.enabled = min_enabled;
                        nodeLimits[nodeIdx].press_min.value = min_value;
                        nodeLimits[nodeIdx].press_max.enabled = max_enabled;
                        nodeLimits[nodeIdx].press_max.value = max_value;
                    }
                    
                    saveLimitsToPrefs();
                    
                    // Отправляем подтверждение
                    StaticJsonDocument<200> resp;
                    resp["type"] = "limits_update";
                    resp["node"] = nodeId;
                    resp["sensor"] = sensor;
                    resp["min"]["enabled"] = min_enabled;
                    resp["min"]["value"] = min_value;
                    resp["max"]["enabled"] = max_enabled;
                    resp["max"]["value"] = max_value;
                    String json;
                    serializeJson(resp, json);
                    ws.textAll(json);
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
    esp_now_send(mac, (uint8_t*)&outgoingMessage, sizeof(outgoingMessage));
}

void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    // Проверяем, от какого узла пришли данные
    for (int i = 0; i < NODE_COUNT; i++) {
        if (memcmp(mac_addr, nodeMacs[i], 6) == 0) {
            lastNodeDataTime[i] = millis();
            processNodeData(incomingData, len, i);
            return;
        }
    }
    
    // Проверяем теплицу
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
        
        float temp = 0, hum = 0, press = 0;
        
        if (dataObj.containsKey("AHT20")) {
            temp = dataObj["AHT20"]["temp"];
            hum = dataObj["AHT20"]["hum"];
        }
        if (dataObj.containsKey("BMP280")) {
            press = dataObj["BMP280"]["press_mmHg"];
        }
        
        // Обновляем историю для прогнозов
        updatePressureHistory(nodeIndex, press);
        updateHumidityHistory(nodeIndex, hum);
        
        // Проверяем уставки
        checkWeatherAlarms(nodeIndex);
        
        StaticJsonDocument<300> resp;
        resp["type"] = "sensor_data";
        resp["node"] = nodeId;
        resp["temp"] = temp;
        resp["hum"] = hum;
        resp["press"] = press;
        
        String json;
        serializeJson(resp, json);
        ws.textAll(json);
        
        // Отправляем прогноз
        String forecast = getWeatherForecast(nodeIndex);
        StaticJsonDocument<200> forecastResp;
        forecastResp["type"] = "weather_forecast";
        forecastResp["node"] = nodeId;
        forecastResp["forecast_text"] = forecast;
        if (forecast.indexOf("дождь") >= 0) {
            forecastResp["forecast_class"] = "rain";
            forecastResp["trend"] = "rain";
        } else if (forecast.indexOf("ясно") >= 0) {
            forecastResp["forecast_class"] = "sun";
            forecastResp["trend"] = "sun";
        } else {
            forecastResp["forecast_class"] = "";
            forecastResp["trend"] = "cloud";
        }
        String forecastJson;
        serializeJson(forecastResp, forecastJson);
        ws.textAll(forecastJson);
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
        if (strcmp(cmd, "LED_ON") == 0) {
            StaticJsonDocument<200> resp;
            resp["type"] = "node_status";
            resp["node"] = nodeId;
            resp["state"] = "on";
            String json;
            serializeJson(resp, json);
            ws.textAll(json);
        }
        else if (strcmp(cmd, "LED_OFF") == 0) {
            StaticJsonDocument<200> resp;
            resp["type"] = "node_status";
            resp["node"] = nodeId;
            resp["state"] = "off";
            String json;
            serializeJson(resp, json);
            ws.textAll(json);
        }
    }
    else if (strcmp(type, "gpio") == 0) {
        StaticJsonDocument<200> resp;
        resp["type"] = "gpio_status";
        resp["node"] = nodeId;
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
        if (nodeIndex == 0) {
            float angle = doc["angle"];
            bool magnet = doc["magnet"];
            
            processEncoderData(angle, magnet);
            if (magnet) {
                updateHistory(angle);
                updateWindDirectionHistory(angle);
            }
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

// ========== ЗАГРУЗКА И СОХРАНЕНИЕ УСТАВОК ==========
void loadLimitsFromPrefs() {
    for (int i = 0; i < NODE_COUNT; i++) {
        String baseKey = "node" + String(nodeNumbers[i]);
        
        // Температура
        nodeLimits[i].temp_min.enabled = prefs.getBool((baseKey + "_tmin_en").c_str(), false);
        nodeLimits[i].temp_min.value = prefs.getFloat((baseKey + "_tmin_val").c_str(), 18.0);
        nodeLimits[i].temp_max.enabled = prefs.getBool((baseKey + "_tmax_en").c_str(), false);
        nodeLimits[i].temp_max.value = prefs.getFloat((baseKey + "_tmax_val").c_str(), 25.0);
        
        // Влажность
        nodeLimits[i].hum_min.enabled = prefs.getBool((baseKey + "_hmin_en").c_str(), false);
        nodeLimits[i].hum_min.value = prefs.getFloat((baseKey + "_hmin_val").c_str(), 30.0);
        nodeLimits[i].hum_max.enabled = prefs.getBool((baseKey + "_hmax_en").c_str(), false);
        nodeLimits[i].hum_max.value = prefs.getFloat((baseKey + "_hmax_val").c_str(), 70.0);
        
        // Давление
        nodeLimits[i].press_min.enabled = prefs.getBool((baseKey + "_pmin_en").c_str(), false);
        nodeLimits[i].press_min.value = prefs.getFloat((baseKey + "_pmin_val").c_str(), 730.0);
        nodeLimits[i].press_max.enabled = prefs.getBool((baseKey + "_pmax_en").c_str(), false);
        nodeLimits[i].press_max.value = prefs.getFloat((baseKey + "_pmax_val").c_str(), 770.0);
        
        // Ветер (для узла #102)
        if (i == 0) {
            nodeLimits[i].wind_storm.enabled = prefs.getBool((baseKey + "_wstorm_en").c_str(), false);
            nodeLimits[i].wind_storm.value = prefs.getFloat((baseKey + "_wstorm_val").c_str(), 15.0);
            nodeLimits[i].wind_change.enabled = prefs.getBool((baseKey + "_wchange_en").c_str(), false);
            nodeLimits[i].wind_change.value = prefs.getFloat((baseKey + "_wchange_val").c_str(), 90.0);
        }
    }
}

void saveLimitsToPrefs() {
    for (int i = 0; i < NODE_COUNT; i++) {
        String baseKey = "node" + String(nodeNumbers[i]);
        
        // Температура
        prefs.putBool((baseKey + "_tmin_en").c_str(), nodeLimits[i].temp_min.enabled);
        prefs.putFloat((baseKey + "_tmin_val").c_str(), nodeLimits[i].temp_min.value);
        prefs.putBool((baseKey + "_tmax_en").c_str(), nodeLimits[i].temp_max.enabled);
        prefs.putFloat((baseKey + "_tmax_val").c_str(), nodeLimits[i].temp_max.value);
        
        // Влажность
        prefs.putBool((baseKey + "_hmin_en").c_str(), nodeLimits[i].hum_min.enabled);
        prefs.putFloat((baseKey + "_hmin_val").c_str(), nodeLimits[i].hum_min.value);
        prefs.putBool((baseKey + "_hmax_en").c_str(), nodeLimits[i].hum_max.enabled);
        prefs.putFloat((baseKey + "_hmax_val").c_str(), nodeLimits[i].hum_max.value);
        
        // Давление
        prefs.putBool((baseKey + "_pmin_en").c_str(), nodeLimits[i].press_min.enabled);
        prefs.putFloat((baseKey + "_pmin_val").c_str(), nodeLimits[i].press_min.value);
        prefs.putBool((baseKey + "_pmax_en").c_str(), nodeLimits[i].press_max.enabled);
        prefs.putFloat((baseKey + "_pmax_val").c_str(), nodeLimits[i].press_max.value);
        
        // Ветер (для узла #102)
        if (i == 0) {
            prefs.putBool((baseKey + "_wstorm_en").c_str(), nodeLimits[i].wind_storm.enabled);
            prefs.putFloat((baseKey + "_wstorm_val").c_str(), nodeLimits[i].wind_storm.value);
            prefs.putBool((baseKey + "_wchange_en").c_str(), nodeLimits[i].wind_change.enabled);
            prefs.putFloat((baseKey + "_wchange_val").c_str(), nodeLimits[i].wind_change.value);
        }
    }
    prefs.end();
    prefs.begin("hub", false);
}

// ========== ПРОГНОЗ ПОГОДЫ ПО ДАВЛЕНИЮ (ZAMBRETTI) ==========
void updatePressureHistory(int nodeIndex, float pressure) {
    pressureHistory[nodeIndex][pressureIndex[nodeIndex]] = pressure;
    pressureTimestamps[nodeIndex][pressureIndex[nodeIndex]] = millis();
    pressureIndex[nodeIndex] = (pressureIndex[nodeIndex] + 1) % PRESSURE_HISTORY_SIZE;
    if (pressureCount[nodeIndex] < PRESSURE_HISTORY_SIZE) {
        pressureCount[nodeIndex]++;
    }
}

String getWeatherForecast(int nodeIndex) {
    if (pressureCount[nodeIndex] < 2) {
        return "🌤️ Сбор данных...";
    }
    
    // Находим самое старое значение за последние 3 часа
    unsigned long now = millis();
    float oldestPressure = -1;
    float newestPressure = -1;
    int validCount = 0;
    
    for (int i = 0; i < pressureCount[nodeIndex]; i++) {
        if (now - pressureTimestamps[nodeIndex][i] <= 10800000) { // 3 часа
            if (oldestPressure < 0) {
                oldestPressure = pressureHistory[nodeIndex][i];
            }
            newestPressure = pressureHistory[nodeIndex][i];
            validCount++;
        }
    }
    
    if (validCount < 2) {
        return "🌤️ Стабильно";
    }
    
    // Вычисляем тренд
    float change = newestPressure - oldestPressure;
    float changePerHour = change * 3600000.0 / (10800000.0); // мм рт.ст. в час
    
    // Zambretti алгоритм
    if (changePerHour < -0.5) {
        // Быстрое падение
        if (newestPressure < 740) {
            return "🌧️🚨 Шторм! Давление падает";
        } else if (newestPressure < 750) {
            return "🌧️ Дождь, давление падает";
        } else {
            return "☁️ Облачно, давление падает";
        }
    } else if (changePerHour > 0.5) {
        // Быстрый рост
        if (newestPressure > 760) {
            return "☀️ Ясно, давление растет";
        } else {
            return "⛅ Переменно, давление растет";
        }
    } else {
        // Стабильно
        if (newestPressure > 760) {
            return "☀️ Ясно, давление стабильно";
        } else if (newestPressure < 740) {
            return "🌧️ Пасмурно, давление низкое";
        } else {
            return "☁️ Облачно, давление стабильно";
        }
    }
}

// ========== ДЕТЕКЦИЯ ДОЖДЯ ПО ВЛАЖНОСТИ ==========
void updateHumidityHistory(int nodeIndex, float humidity) {
    humidityHistory[nodeIndex][humidityIndex[nodeIndex]] = humidity;
    humidityTimestamps[nodeIndex][humidityIndex[nodeIndex]] = millis();
    humidityIndex[nodeIndex] = (humidityIndex[nodeIndex] + 1) % HUMIDITY_HISTORY_SIZE;
    if (humidityCount[nodeIndex] < HUMIDITY_HISTORY_SIZE) {
        humidityCount[nodeIndex]++;
    }
}

// ========== ИСТОРИЯ ВЕТРА ==========
void updateWindDirectionHistory(float direction) {
    windDirectionHistory[windDirectionIndex] = direction;
    windDirectionTimestamps[windDirectionIndex] = millis();
    windDirectionIndex = (windDirectionIndex + 1) % WIND_HISTORY_SIZE;
    if (windDirectionCount < WIND_HISTORY_SIZE) {
        windDirectionCount++;
    }
}

// ========== ПРОВЕРКА ПОГОДНЫХ ТРЕВОГ ==========
void checkWeatherAlarms(int nodeIndex) {
    // Проверка резкого падения давления
    if (pressureCount[nodeIndex] >= 2) {
        unsigned long now = millis();
        float oldestPressure = -1;
        float newestPressure = -1;
        
        for (int i = 0; i < pressureCount[nodeIndex]; i++) {
            if (now - pressureTimestamps[nodeIndex][i] <= 10800000) {
                if (oldestPressure < 0) {
                    oldestPressure = pressureHistory[nodeIndex][i];
                }
                newestPressure = pressureHistory[nodeIndex][i];
            }
        }
        
        if (oldestPressure > 0 && newestPressure > 0) {
            float changePerHour = (newestPressure - oldestPressure) * 3600000.0 / 10800000.0;
            
            if (changePerHour < -1.0) {
                // Резкое падение давления
                StaticJsonDocument<100> alarm;
                alarm["type"] = "weather_alarm";
                alarm["alarm_type"] = "pressure_drop";
                alarm["node"] = nodeNumbers[nodeIndex];
                String json;
                serializeJson(alarm, json);
                ws.textAll(json);
            }
        }
    }
    
    // Проверка дождя по влажности
    if (humidityCount[nodeIndex] >= 2) {
        unsigned long now = millis();
        float oldestHum = -1;
        float newestHum = -1;
        
        for (int i = 0; i < humidityCount[nodeIndex]; i++) {
            if (now - humidityTimestamps[nodeIndex][i] <= 900000) { // 15 минут
                if (oldestHum < 0) {
                    oldestHum = humidityHistory[nodeIndex][i];
                }
                newestHum = humidityHistory[nodeIndex][i];
            }
        }
        
        if (oldestHum > 0 && newestHum > 0) {
            float change = newestHum - oldestHum;
            if (change > 15.0) {
                // Резкий скачок влажности
                StaticJsonDocument<100> alarm;
                alarm["type"] = "weather_alarm";
                alarm["alarm_type"] = "rain";
                alarm["node"] = nodeNumbers[nodeIndex];
                String json;
                serializeJson(alarm, json);
                ws.textAll(json);
            }
        }
    }
    
    // Проверка смены ветра (только для узла #102)
    if (nodeIndex == 0 && windDirectionCount >= 2) {
        unsigned long now = millis();
        float oldestDir = -1;
        float newestDir = windDirection;
        
        for (int i = 0; i < windDirectionCount; i++) {
            if (now - windDirectionTimestamps[i] <= 1800000) { // 30 минут
                oldestDir = windDirectionHistory[i];
                break;
            }
        }
        
        if (oldestDir >= 0) {
            float diff = fabs(newestDir - oldestDir);
            if (diff > 180) diff = 360 - diff;
            
            if (diff > 90) {
                StaticJsonDocument<100> alarm;
                alarm["type"] = "weather_alarm";
                alarm["alarm_type"] = "wind_change";
                alarm["node"] = nodeNumbers[nodeIndex];
                String json;
                serializeJson(alarm, json);
                ws.textAll(json);
            }
        }
    }
}

// ========== ВЕТЕР: ДВЕ ТОЧКИ + ЖЕЛТЫЙ СЕКТОР 30 СЕК + ШТИЛЬ/ШТОРМ ==========
void processEncoderData(float angle, bool magnet) {
    lastEncoderDataTime = millis();
    
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
    checkGlobalAlarm();
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
    bool encoderTimeout = (millis() - lastEncoderDataTime) > ENCODER_TIMEOUT_MS;
    
    if (encoderTimeout) {
        stability = "no_signal";
        redStart = 0;
        redEnd = 360;
        Serial.println("⚠️ ТРЕВОГА: Нет сигнала от энкодера!");
    } else if (!windMagnet) {
        stability = "no_magnet";
        redStart = 0;
        redEnd = 360;
        Serial.println("⚠️ ПРЕДУПРЕЖДЕНИЕ: Магнит не обнаружен");
    } else {
        redStart = windDirection - windCurrentSector / 2;
        redEnd = windDirection + windCurrentSector / 2;
        redStart = fmod(fmod(redStart, 360) + 360, 360);
        redEnd = fmod(fmod(redEnd, 360) + 360, 360);
        
        if (windCurrentSector < 10) stability = "calm";
        else if (windCurrentSector < 30) stability = "gusty";
        else if (windCurrentSector < 60) stability = "strong";
        else stability = "storm";
    }
    
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