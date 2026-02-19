/**
 * SmartHome ESP-NOW Hub (ESP32)
 * ВЕРСИЯ 6.6: ПОЛНОЦЕННАЯ МЕТЕОСТАНЦИЯ С ПРОГНОЗОМ
 * - Прогноз по давлению (метод Zambretti)
 * - Риск заморозков (метод Броунова)
 * - Тренды давления и температуры
 * - Визуальные индикаторы на веб-странице
 * - Дисплей с русскими надписями (транслит)
 * - SD карта для хранения HTML
 * - RTC часы
 * - Кнопки управления
 * - Зуммер
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <math.h>

// ========== БИБЛИОТЕКИ ДЛЯ ПЕРИФЕРИИ ==========
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <FS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <RTClib.h>

// ========== РУССКИЙ ТРАНСЛИТ ==========
#include "rus_font.h"

// ========== ПИНЫ ДИСПЛЕЯ (VSPI) ==========
#define TFT_CS    5
#define TFT_DC    27
#define TFT_RST   4
#define TFT_LED   32
#define TFT_SCK   18
#define TFT_MOSI  23

// ========== ПИНЫ SD КАРТЫ (HSPI) ==========
#define SD_CS     15
#define SD_MOSI   13
#define SD_MISO   12
#define SD_SCK    14

// ========== ПИНЫ RTC (I2C) ==========
#define RTC_SDA   21
#define RTC_SCL   22

// ========== ПИНЫ КНОПОК И ЗУМЕРА ==========
#define BTN_CYCLE 17
#define BTN_ENTER 16
#define BUZZER_PIN 25
#define BUZZER_CHANNEL 0

// ========== ОБЪЕКТЫ ПЕРИФЕРИИ ==========
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
RTC_DS1307 rtc;
SPIClass tftSPI = SPIClass(VSPI);
SPIClass sdSPI = SPIClass(HSPI);

// ========== КОНФИГУРАЦИЯ ХАБА ==========
const char* AP_SSID = "SmartHome-Hub";
const char* AP_PASSWORD = "12345678";
const char* HUB_VERSION = "6.6";
const char* NODE_VERSION = "2.1";

// MAC адреса узлов
uint8_t node102MacAddress[] = {0xAC, 0xEB, 0xE6, 0x49, 0x10, 0x28};
uint8_t node103MacAddress[] = {0x88, 0x56, 0xA6, 0x7D, 0x09, 0x64};
uint8_t node104MacAddress[] = {0x10, 0x00, 0x3B, 0xB1, 0xA6, 0x9C};
uint8_t node105MacAddress[] = {0x88, 0x56, 0xA6, 0x7C, 0xF2, 0xA8};
uint8_t greenhouseMac[] = {0xE8, 0x9F, 0x6D, 0x87, 0x34, 0x8A};

#define NODE_COUNT 4
uint8_t* nodeMacs[NODE_COUNT] = {node102MacAddress, node103MacAddress, node104MacAddress, node105MacAddress};
int nodeNumbers[NODE_COUNT] = {102, 103, 104, 105};

// Данные узлов
unsigned long lastNodeDataTime[NODE_COUNT] = {0, 0, 0, 0};
const unsigned long NODE_TIMEOUT_MS = 70000;
bool nodeConnectionLost[NODE_COUNT] = {false, false, false, false};
unsigned long connectionLostTime[NODE_COUNT] = {0, 0, 0, 0};
bool nodeAlarmState[NODE_COUNT] = {false, false, false, false};

// ========== ДАННЫЕ УЗЛОВ ДЛЯ ДИСПЛЕЯ ==========
struct NodeDisplayData {
    int id;
    float temp;
    float hum;
    float press;
    bool alarm;
    bool led_state;
    bool connected;
    float wind_angle;
    float wind_sector;
    bool magnet;
} nodeDisplayData[4] = {
    {102, 0, 0, 0, false, false, false, 0, 0, false},
    {103, 0, 0, 0, false, false, false, 0, 0, false},
    {104, 0, 0, 0, false, false, false, 0, 0, false},
    {105, 0, 0, 0, false, false, false, 0, 0, false}
};

// ========== ESP-NOW СТРУКТУРЫ ==========
typedef struct esp_now_message {
    char json[192];
    uint8_t sender_id;
} esp_now_message;

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

// ========== ГЛОБАЛЬНЫЕ ОБЪЕКТЫ ==========
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
esp_now_message outgoingMessage;
esp_now_message incomingMessage;

unsigned long lastGreenhouseUpdate = 0;
const unsigned long GREENHOUSE_UPDATE_INTERVAL = 30000;
bool securityAlarmActive = false;
unsigned long alarmStartTime = 0;
const unsigned long ALARM_DURATION_MS = 10000;

// ========== ДАННЫЕ ЭНКОДЕРА ==========
#define ENCODER_HISTORY_SIZE 60
#define ENCODER_BROADCAST_INTERVAL 5000
#define HISTORY_PERIOD_MS 60000

float prevEncoderAngle = -1.0;
float currentEncoderAngle = -1.0;
float windDirection = 0.0;
float windCurrentSector = 0.0;
bool windMagnet = false;
float encoderHistory[ENCODER_HISTORY_SIZE];
unsigned long historyTimestamps[ENCODER_HISTORY_SIZE];
int historyIndex = 0;
int historyCount = 0;
float maxSectorStart = 0.0;
float maxSectorEnd = 0.0;
float maxSectorWidth = 0.0;
unsigned long maxSectorTimestamp = 0;
float currentSectorStart = 0.0;
float currentSectorEnd = 0.0;
unsigned long lastEncoderBroadcastTime = 0;

// ========== ДАННЫЕ МЕТЕОСТАНЦИИ ==========
#define PRESSURE_HISTORY_SIZE 48
#define FROST_CHECK_HOUR 21

struct WeatherData {
    float pressure;
    float temperature;
    float humidity;
    unsigned long timestamp;
};

WeatherData weatherHistory[PRESSURE_HISTORY_SIZE];
int weatherIndex = 0;
int weatherCount = 0;

float currentPressure = 0;
float currentTemp = 0;
float currentHumidity = 0;
float pressureTrend3h = 0;
float pressureTrend6h = 0;
float pressureTrend12h = 0;
float tempTrend3h = 0;

String shortForecast = "---";
String frostRisk = "---";
String weatherIcon = "☀️";
unsigned long lastForecastUpdate = 0;
const unsigned long FORECAST_UPDATE_INTERVAL = 1800000;

// ========== ПЕРЕМЕННЫЕ ДЛЯ ДИСПЛЕЯ ==========
int displayNodeIndex = 0;
const int NODE_COUNT_DISP = 4;
bool alarmBlinkState = false;
unsigned long lastBlinkTime = 0;
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 500;

// ========== ПЕРЕМЕННЫЕ ДЛЯ RTC ==========
bool rtcOK = false;
DateTime lastRTCRead;
uint32_t lastRTCReadTime = 0;
const uint32_t RTC_READ_INTERVAL = 500;
char timeStr[20];
char dateStr[20];

// ========== ПЕРЕМЕННЫЕ ДЛЯ SD КАРТЫ ==========
bool sdInitialized = false;
uint32_t sdWriteCount = 0;
uint32_t lastSDWrite = 0;
const uint32_t SD_WRITE_INTERVAL = 60000;
uint64_t sdTotalBytes = 0;
uint64_t sdUsedBytes = 0;
uint8_t sdCardType = 0;
char sdErrorMsg[32] = "";

// ========== ПЕРЕМЕННЫЕ ДЛЯ ЗУМЕРА ==========
unsigned long lastBuzzerToggle = 0;
bool buzzerState = false;

// ========== УПРАВЛЕНИЕ СТРАНИЦАМИ ==========
enum DisplayPage {
    PAGE_WEATHER,
    PAGE_NODE_INFO,
    PAGE_SD_MONITOR,
    PAGE_COUNT
};
DisplayPage currentPage = PAGE_WEATHER;

// ========== СОСТОЯНИЯ КНОПОК ==========
bool lastBtnCycle = HIGH;
bool lastBtnEnter = HIGH;
unsigned long lastDebounceCycle = 0;
unsigned long lastDebounceEnter = 0;
unsigned long enterPressStart = 0;
bool enterLongPressHandled = false;
const unsigned long DEBOUNCE_DELAY = 100;
const unsigned long LONG_PRESS_MS = 1000;

// ========== ПРОТОТИПЫ ==========
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void onEspNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void onEspNowDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void sendToNode(uint8_t* mac, String cmd);
void processGreenhouseData(const uint8_t *data);
void processNodeData(const uint8_t *data, int len, int nodeIndex);
void checkNodeConnection();
void updateAlarmState();
void sendConnectionStatusToWeb(int nodeIndex, bool connected);
void processEncoderData(float angle, bool magnet);
void updateHistory(float angle);
void updateMaxMin();
void broadcastEncoderData();
void sendEncoderAlarmStatus(int nodeIndex, bool alarm, const char* message);
void updateWeatherHistory(float pressure, float temp, float humidity);
void calculatePressureTrends();
String generateForecast(float pressure, float trend, float humidity, int month);
String checkFrostRisk(float temp, int hour, int month);
String getWeatherIcon(String forecast);
void broadcastWeatherData();
void initDisplay();
void initRTC();
void initSD();
void updateSDInfo();
void displayWeatherPage();
void displayNodePage();
void displaySDPage();
void drawTimeBar();
void draw_compass(int cx, int cy, int r, float angle, float sector, bool magnet);
void handleButtons();
void buzzerBeep(int durationMs);
void updateAlarmSound();
String formatTime(int value);
void testSDWrite();
void checkHTMLFile();

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== SmartHome ESP-NOW Hub (Версия 6.6) ===");
    Serial.println("=== С РУССКИМ ДИСПЛЕЕМ, SD, RTC, КНОПКАМИ, ЗУМЕРОМ ===");

    // Инициализация пинов и периферии
    pinMode(TFT_LED, OUTPUT);
    digitalWrite(TFT_LED, HIGH);
    
    pinMode(BTN_CYCLE, INPUT_PULLUP);
    pinMode(BTN_ENTER, INPUT_PULLUP);
    
    ledcSetup(BUZZER_CHANNEL, 2000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
    ledcWriteTone(BUZZER_CHANNEL, 0);
    
    initDisplay();
    initRTC();
    initSD();
    checkHTMLFile();
    
    buzzerBeep(100);
    delay(100);
    buzzerBeep(100);

    // Инициализация WiFi и ESP-NOW
    historyCount = 0;
    historyIndex = 0;
    lastEncoderBroadcastTime = 0;
    maxSectorWidth = 0.0;
    weatherCount = 0;
    weatherIndex = 0;
    lastForecastUpdate = 0;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.print("IP адрес: ");
    Serial.println(WiFi.softAPIP());

    // ========== ВЕБ-СЕРВЕР ==========
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (sdInitialized && SD.exists("/index.html")) {
            request->send(SD, "/index.html", "text/html");
            Serial.println("📄 HTML загружен с SD карты");
        } else {
            request->send(200, "text/html", "<h1>SD Card Error</h1><p>HTML file not found</p>");
            Serial.println("❌ HTML файл не найден на SD");
        }
    });

    ws.onEvent(onWebSocketEvent);
    server.addHandler(&ws);
    server.begin();

    WiFi.mode(WIFI_AP_STA);
    esp_now_init();
    esp_now_register_send_cb(onEspNowDataSent);
    esp_now_register_recv_cb(onEspNowDataRecv);

    for (int i = 0; i < NODE_COUNT; i++) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, nodeMacs[i], 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }

    esp_now_peer_info_t greenhousePeerInfo = {};
    memcpy(greenhousePeerInfo.peer_addr, greenhouseMac, 6);
    greenhousePeerInfo.channel = 0;
    greenhousePeerInfo.encrypt = false;
    esp_now_add_peer(&greenhousePeerInfo);

    Serial.println("\n=== ХАБ ГОТОВ К РАБОТЕ ===");
    displayWeatherPage();
}

// ===================== LOOP =====================
void loop() {
    unsigned long now = millis();
    
    // Основные функции хаба
    ws.cleanupClients();
    checkNodeConnection();
    updateAlarmState();
    
    if (now - lastEncoderBroadcastTime >= ENCODER_BROADCAST_INTERVAL) {
        updateMaxMin();
        broadcastEncoderData();
        lastEncoderBroadcastTime = now;
    }
    
    // Функции периферии
    handleButtons();
    updateAlarmSound();
    
    if (currentPage == PAGE_NODE_INFO && nodeDisplayData[displayNodeIndex].alarm) {
        if (now - lastBlinkTime > 500) {
            alarmBlinkState = !alarmBlinkState;
            lastBlinkTime = now;
            displayNodePage();
        }
    }
    
    if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
        lastDisplayUpdate = now;
        drawTimeBar();
    }
    
    if (currentPage == PAGE_SD_MONITOR) {
        static uint32_t lastSDInfoUpdate = 0;
        if (now - lastSDInfoUpdate >= 1000) {
            lastSDInfoUpdate = now;
            updateSDInfo();
            displaySDPage();
        }
    }
    
    if (sdInitialized && (now - lastSDWrite >= SD_WRITE_INTERVAL)) {
        lastSDWrite = now;
        testSDWrite();
    }
    
    delay(10);
}

// ==================== ФУНКЦИИ ESP-NOW ====================

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("Новый клиент: %u\n", client->id());
        broadcastWeatherData();
    }
    else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("Клиент отключен: %u\n", client->id());
    }
    else if (type == WS_EVT_DATA) {
        StaticJsonDocument<200> doc;
        if (!deserializeJson(doc, data, len) && doc.containsKey("command")) {
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
    }
}

void sendToNode(uint8_t* mac, String cmd) {
    char json_cmd[64];
    snprintf(json_cmd, sizeof(json_cmd), "{\"type\":\"command\",\"command\":\"%s\"}", cmd.c_str());
    strncpy(outgoingMessage.json, json_cmd, sizeof(outgoingMessage.json)-1);
    outgoingMessage.sender_id = 1;
    esp_now_send(mac, (uint8_t*)&outgoingMessage, sizeof(outgoingMessage));
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
    if (len > sizeof(incomingMessage)) return;
    memcpy(&incomingMessage, data, len);
    
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, incomingMessage.json);
    if (error) return;

    const char* type = doc["type"];
    int nodeId = nodeNumbers[nodeIndex];
    int displayIndex = nodeId - 102;

    if (displayIndex >= 0 && displayIndex < 4) {
        nodeDisplayData[displayIndex].id = nodeId;
        nodeDisplayData[displayIndex].connected = true;
    }

    if (strcmp(type, "sensor") == 0) {
        JsonObject dataObj = doc["data"];
        float temp = dataObj["AHT20"]["temp"].as<float>();
        float hum = dataObj["AHT20"]["hum"].as<float>();
        float press = dataObj["BMP280"]["press_mmHg"].as<float>();
        
        if (nodeId == 102) {
            currentPressure = press;
            updateWeatherHistory(press, currentTemp, currentHumidity);
        }
        
        if (displayIndex >= 0 && displayIndex < 4) {
            nodeDisplayData[displayIndex].temp = temp;
            nodeDisplayData[displayIndex].hum = hum;
            nodeDisplayData[displayIndex].press = press;
        }
        
        StaticJsonDocument<500> resp;
        resp["type"] = "sensor_data";
        resp["node"] = nodeId;
        resp["aht20"]["temp"] = dataObj["AHT20"]["temp"].as<String>();
        resp["aht20"]["hum"] = dataObj["AHT20"]["hum"].as<String>();
        resp["bmp280"]["temp"] = dataObj["BMP280"]["temp"].as<String>();
        resp["bmp280"]["press"] = dataObj["BMP280"]["press_mmHg"].as<String>();
        
        if (nodeId == 102) {
            JsonObject weather = resp.createNestedObject("weather_data");
            weather["pressure"] = serialized(String(press, 1));
            weather["humidity"] = serialized(String(currentHumidity, 0));
            weather["trend3h"] = serialized(String(pressureTrend3h, 1));
            weather["trend6h"] = serialized(String(pressureTrend6h, 1));
            weather["trend12h"] = serialized(String(pressureTrend12h, 1));
            weather["forecast"] = shortForecast;
            weather["icon"] = weatherIcon;
            weather["frost"] = frostRisk;
        }
        
        String json;
        serializeJson(resp, json);
        ws.textAll(json);
        
        Serial.printf("Данные узла #%d: T=%.1f, P=%.1f, H=%.0f\n", nodeId, temp, press, hum);
        
        // Обновляем компас если текущая страница - метеостанция
        if (currentPage == PAGE_WEATHER) {
            displayWeatherPage();
        }
    }
    else if (strcmp(type, "security") == 0) {
        bool alarm = doc["alarm"];
        nodeAlarmState[nodeIndex] = alarm;
        
        if (displayIndex >= 0 && displayIndex < 4) {
            nodeDisplayData[displayIndex].alarm = alarm;
        }
        
        if (alarm && !securityAlarmActive && nodeId == 102) {
            securityAlarmActive = true;
            alarmStartTime = millis();
        } else if (!alarm && nodeId == 102) {
            securityAlarmActive = false;
        }
        
        StaticJsonDocument<200> resp;
        resp["type"] = "security";
        resp["node"] = nodeId;
        resp["alarm"] = alarm;
        String json;
        serializeJson(resp, json);
        ws.textAll(json);
    }
    else if (strcmp(type, "ack") == 0) {
        const char* cmd = doc["command"];
        if (strcmp(cmd, "LED_ON") == 0) {
            if (displayIndex >= 0 && displayIndex < 4) {
                nodeDisplayData[displayIndex].led_state = true;
            }
            
            StaticJsonDocument<200> resp;
            resp["type"] = "node_status";
            resp["node"] = nodeId;
            resp["state"] = "on";
            String json;
            serializeJson(resp, json);
            ws.textAll(json);
            
            Serial.printf("LED ON #%d\n", nodeId);
            
            // Обновляем страницу узла
            if (currentPage == PAGE_NODE_INFO && displayIndex == displayNodeIndex) {
                displayNodePage();
            }
        }
        else if (strcmp(cmd, "LED_OFF") == 0) {
            if (displayIndex >= 0 && displayIndex < 4) {
                nodeDisplayData[displayIndex].led_state = false;
            }
            
            StaticJsonDocument<200> resp;
            resp["type"] = "node_status";
            resp["node"] = nodeId;
            resp["state"] = "off";
            String json;
            serializeJson(resp, json);
            ws.textAll(json);
            
            Serial.printf("LED OFF #%d\n", nodeId);
            
            // Обновляем страницу узла
            if (currentPage == PAGE_NODE_INFO && displayIndex == displayNodeIndex) {
                displayNodePage();
            }
        }
    }
    else if (strcmp(type, "gpio") == 0) {
        StaticJsonDocument<200> resp;
        resp["type"] = "gpio_status";
        resp["node"] = nodeId;
        if (doc.containsKey("pin") && doc.containsKey("state")) {
            int pin = doc["pin"];
            int state = doc["state"];
            if (pin == 8) {
                resp["gpio8"] = state;
                
                if (displayIndex >= 0 && displayIndex < 4) {
                    nodeDisplayData[displayIndex].led_state = (state == 1);
                }
                
                Serial.printf("GPIO8 #%d = %d\n", nodeId, state);
                
                // Обновляем страницу узла
                if (currentPage == PAGE_NODE_INFO && displayIndex == displayNodeIndex) {
                    displayNodePage();
                }
            }
        }
        String json;
        serializeJson(resp, json);
        ws.textAll(json);
    }
    else if (strcmp(type, "encoder") == 0 && nodeIndex == 0) {
        float angle = doc["angle"];
        bool magnet = doc["magnet"];
        
        if (displayIndex >= 0 && displayIndex < 4) {
            nodeDisplayData[displayIndex].wind_angle = angle;
            nodeDisplayData[displayIndex].magnet = magnet;
        }
        
        if (magnet) {
            processEncoderData(angle, true);
            updateHistory(angle);
            
            if (nodeAlarmState[nodeIndex]) {
                sendEncoderAlarmStatus(nodeIndex, false, "Magnet restored");
                nodeAlarmState[nodeIndex] = false;
                
                if (displayIndex >= 0 && displayIndex < 4) {
                    nodeDisplayData[displayIndex].alarm = false;
                }
            }
            
            Serial.printf("Encoder: %.1f° magnet=yes\n", angle);
        } else {
            if (!nodeAlarmState[nodeIndex]) {
                sendEncoderAlarmStatus(nodeIndex, true, "Magnet lost");
                nodeAlarmState[nodeIndex] = true;
                
                if (displayIndex >= 0 && displayIndex < 4) {
                    nodeDisplayData[displayIndex].alarm = true;
                }
            }
            Serial.printf("Encoder: magnet=NO (%.1f°)\n", angle);
        }
        
        if (displayIndex >= 0 && displayIndex < 4) {
            nodeDisplayData[displayIndex].wind_sector = windCurrentSector;
        }
        
        // Обновляем компас на странице метеостанции при новых данных энкодера
        if (currentPage == PAGE_WEATHER) {
            displayWeatherPage();
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

    currentTemp = atof(temp_out);
    currentHumidity = pkt.hum_in;

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
    
    Serial.println("Greenhouse data updated");
    
    // Обновляем страницу метеостанции при новых данных
    if (currentPage == PAGE_WEATHER) {
        displayWeatherPage();
    }
}

void checkNodeConnection() {
    unsigned long now = millis();
    for (int i = 0; i < NODE_COUNT; i++) {
        if (lastNodeDataTime[i] > 0) {
            if (now - lastNodeDataTime[i] > NODE_TIMEOUT_MS) {
                if (!nodeConnectionLost[i]) {
                    nodeConnectionLost[i] = true;
                    connectionLostTime[i] = now;
                    Serial.printf("Node #%d LOST!\n", nodeNumbers[i]);
                    sendConnectionStatusToWeb(i, false);
                    
                    if (i < 4) {
                        nodeDisplayData[i].connected = false;
                    }
                }
            } else {
                if (nodeConnectionLost[i]) {
                    nodeConnectionLost[i] = false;
                    Serial.printf("Node #%d RESTORED!\n", nodeNumbers[i]);
                    sendConnectionStatusToWeb(i, true);
                    
                    if (i < 4) {
                        nodeDisplayData[i].connected = true;
                    }
                }
            }
        }
    }
}

void sendConnectionStatusToWeb(int nodeIndex, bool connected) {
    StaticJsonDocument<100> doc;
    doc["type"] = connected ? "connection_restored" : "connection_lost";
    doc["node"] = nodeNumbers[nodeIndex];
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void updateAlarmState() {
    if (securityAlarmActive && (millis() - alarmStartTime) > ALARM_DURATION_MS) {
        securityAlarmActive = false;
    }
}

void sendEncoderAlarmStatus(int nodeIndex, bool alarm, const char* message) {
    nodeAlarmState[nodeIndex] = alarm;
    StaticJsonDocument<200> doc;
    doc["type"] = "encoder_alarm";
    doc["node"] = nodeNumbers[nodeIndex];
    doc["alarm"] = alarm;
    doc["message"] = message;
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
    Serial.printf("Encoder alarm #%d: %s\n", nodeNumbers[nodeIndex], message);
}

// ========== ФУНКЦИИ МЕТЕОСТАНЦИИ ==========

void updateWeatherHistory(float pressure, float temp, float humidity) {
    weatherHistory[weatherIndex].pressure = pressure;
    weatherHistory[weatherIndex].temperature = temp;
    weatherHistory[weatherIndex].humidity = humidity;
    weatherHistory[weatherIndex].timestamp = millis();
    
    weatherIndex = (weatherIndex + 1) % PRESSURE_HISTORY_SIZE;
    if (weatherCount < PRESSURE_HISTORY_SIZE) weatherCount++;
    
    calculatePressureTrends();
    
    if (millis() - lastForecastUpdate > FORECAST_UPDATE_INTERVAL) {
        time_t nowTime = time(nullptr);
        struct tm *timeinfo = localtime(&nowTime);
        int month = timeinfo->tm_mon + 1;
        int hour = timeinfo->tm_hour;
        
        shortForecast = generateForecast(pressure, pressureTrend3h, humidity, month);
        weatherIcon = getWeatherIcon(shortForecast);
        frostRisk = checkFrostRisk(temp, hour, month);
        lastForecastUpdate = millis();
        
        broadcastWeatherData();
    }
}

void calculatePressureTrends() {
    if (weatherCount < 6) return;
    unsigned long now = millis();
    float pressureNow = 0, pressure3hAgo = 0, pressure6hAgo = 0, pressure12hAgo = 0;
    int countNow = 0, count3h = 0, count6h = 0, count12h = 0;
    
    for (int i = 0; i < weatherCount; i++) {
        unsigned long age = now - weatherHistory[i].timestamp;
        if (age < 300000) { pressureNow += weatherHistory[i].pressure; countNow++; }
        if (age > 900000 && age < 1170000) { pressure3hAgo += weatherHistory[i].pressure; count3h++; }
        if (age > 1980000 && age < 2220000) { pressure6hAgo += weatherHistory[i].pressure; count6h++; }
        if (age > 4140000 && age < 4380000) { pressure12hAgo += weatherHistory[i].pressure; count12h++; }
    }
    
    if (countNow > 0 && count3h > 0) pressureTrend3h = (pressureNow/countNow) - (pressure3hAgo/count3h);
    if (countNow > 0 && count6h > 0) pressureTrend6h = (pressureNow/countNow) - (pressure6hAgo/count6h);
    if (countNow > 0 && count12h > 0) pressureTrend12h = (pressureNow/countNow) - (pressure12hAgo/count12h);
}

String generateForecast(float pressure, float trend, float humidity, int month) {
    bool isWinter = (month <= 3 || month >= 10);
    
    if (trend > 1.5) {
        if (pressure > 1020) return "Clear, improving";
        if (pressure > 1005) return "Partly cloudy";
        return "Cloudy, no rain";
    } else if (trend > 0.3) {
        if (pressure > 1015) return "Mostly clear";
        return "Cloudy breaks";
    } else if (trend > -0.3) {
        if (pressure > 1020) return "Clear";
        if (pressure > 1010) return "Partly cloudy";
        return "Cloudy";
    } else if (trend > -1.5) {
        if (humidity > 80) return "Possible rain";
        return "Overcast";
    } else {
        if (isWinter) {
            if (pressure < 1000) return "Snow, windy";
            return "Cloudy, snow";
        } else {
            if (humidity > 85) return "Rain, thunder";
            return "Rain";
        }
    }
}

String getWeatherIcon(String forecast) {
    if (forecast.indexOf("Clear") >= 0) return "☀️";
    if (forecast.indexOf("Mostly clear") >= 0) return "🌤️";
    if (forecast.indexOf("Partly cloudy") >= 0) return "⛅";
    if (forecast.indexOf("Cloudy breaks") >= 0) return "🌥️";
    if (forecast.indexOf("Cloudy") >= 0) return "☁️";
    if (forecast.indexOf("Overcast") >= 0) return "☁️☁️";
    if (forecast.indexOf("rain") >= 0) return "🌧️";
    if (forecast.indexOf("thunder") >= 0) return "⛈️";
    if (forecast.indexOf("snow") >= 0) return "❄️";
    return "☀️";
}

String checkFrostRisk(float temp, int hour, int month) {
    if (month < 4 || month > 9) return "❄️ Seasonal";
    
    if (hour >= 20 && hour <= 22) {
        if (temp < 3.0) return "High risk";
        if (temp < 6.0) return "Medium risk";
        if (temp < 10.0) return "Low risk";
    }
    
    if (hour >= 0 && hour <= 6) {
        if (temp < 2.0) return "❄️ Frost!";
        if (temp < 5.0) return "Near zero";
    }
    
    return "No risk";
}

void broadcastWeatherData() {
    if (currentPressure == 0) return;
    
    StaticJsonDocument<400> doc;
    doc["type"] = "weather_update";
    doc["pressure"] = serialized(String(currentPressure, 1));
    doc["humidity"] = serialized(String(currentHumidity, 0));
    doc["trend3h"] = serialized(String(pressureTrend3h, 1));
    doc["trend6h"] = serialized(String(pressureTrend6h, 1));
    doc["trend12h"] = serialized(String(pressureTrend12h, 1));
    doc["forecast"] = shortForecast;
    doc["icon"] = weatherIcon;
    doc["frost"] = frostRisk;
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

// ========== ФУНКЦИИ ВЕТРА ==========

void processEncoderData(float angle, bool magnet) {
    if (prevEncoderAngle < 0) {
        prevEncoderAngle = angle;
        currentEncoderAngle = angle;
        windDirection = angle;
        windCurrentSector = 0.0;
        Serial.printf("First value %.1f°\n", angle);
    } else {
        prevEncoderAngle = currentEncoderAngle;
        currentEncoderAngle = angle;
        
        float diff = fmod(currentEncoderAngle - prevEncoderAngle + 540.0, 360.0) - 180.0;
        windCurrentSector = fabs(diff);
        
        float rad1 = radians(prevEncoderAngle);
        float rad2 = radians(currentEncoderAngle);
        float sumSin = sin(rad1) + sin(rad2);
        float sumCos = cos(rad1) + cos(rad2);
        float meanRad = atan2(sumSin, sumCos);
        windDirection = degrees(meanRad);
        if (windDirection < 0) windDirection += 360.0;
        
        Serial.printf("Dir=%.1f°, sector=%.1f°\n", windDirection, windCurrentSector);
    }
    windMagnet = magnet;
}

void updateHistory(float angle) {
    encoderHistory[historyIndex] = angle;
    historyTimestamps[historyIndex] = millis();
    historyIndex = (historyIndex + 1) % ENCODER_HISTORY_SIZE;
    if (historyCount < ENCODER_HISTORY_SIZE) historyCount++;
}

void updateMaxMin() {
    if (historyCount < 2 || !windMagnet) return;
    unsigned long now = millis();
    
    float periodMin = 361.0, periodMax = -1.0;
    int periodCount = 0;
    
    for (int i = 0; i < historyCount; i++) {
        if (now - historyTimestamps[i] <= HISTORY_PERIOD_MS) {
            float a = encoderHistory[i];
            if (a < periodMin) periodMin = a;
            if (a > periodMax) periodMax = a;
            periodCount++;
        }
    }
    
    if (periodCount >= 2) {
        float periodWidth = periodMax - periodMin;
        if (periodWidth < 0) periodWidth += 360;
        if (periodWidth > 180) periodWidth = 360 - periodWidth;
        
        if (periodWidth > maxSectorWidth) {
            maxSectorWidth = periodWidth;
            float centerAngle = (periodMin + periodMax) / 2.0;
            if (periodMax - periodMin > 180) centerAngle += 180;
            if (centerAngle >= 360) centerAngle -= 360;
            maxSectorStart = centerAngle - maxSectorWidth / 2;
            maxSectorEnd = centerAngle + maxSectorWidth / 2;
            maxSectorStart = fmod(fmod(maxSectorStart, 360) + 360, 360);
            maxSectorEnd = fmod(fmod(maxSectorEnd, 360) + 360, 360);
            maxSectorTimestamp = now;
            
            Serial.printf("NEW MAX: %.1f°\n", maxSectorWidth);
        }
        
        if (now - maxSectorTimestamp > HISTORY_PERIOD_MS) {
            maxSectorWidth = periodWidth;
            maxSectorStart = currentSectorStart;
            maxSectorEnd = currentSectorEnd;
            maxSectorTimestamp = now;
        }
    }
}

void broadcastEncoderData() {
    if (prevEncoderAngle < 0) return;
    
    if (!windMagnet) {
        StaticJsonDocument<128> doc;
        doc["type"] = "wind";
        doc["magnet"] = false;
        doc["stability"] = "no_magnet";
        String json;
        serializeJson(doc, json);
        ws.textAll(json);
        return;
    }
    
    float redStart = fmod(fmod(windDirection - windCurrentSector/2, 360) + 360, 360);
    float redEnd = fmod(fmod(windDirection + windCurrentSector/2, 360) + 360, 360);
    
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
    
    if (maxSectorWidth > 0) {
        doc["history_min"] = serialized(String(maxSectorStart, 0));
        doc["history_max"] = serialized(String(maxSectorEnd, 0));
        doc["history_width"] = serialized(String(maxSectorWidth, 1));
    }
    
    doc["magnet"] = windMagnet;
    doc["stability"] = stability;
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
    
    Serial.printf("Wind: dir=%.1f°, red=%.1f°, yellow=%.1f°\n",
                  windDirection, windCurrentSector, maxSectorWidth);
}

// ========== ФУНКЦИИ ДИСПЛЕЯ ==========

void initDisplay() {
    Serial.print("Init display... ");
    pinMode(TFT_LED, OUTPUT);
    digitalWrite(TFT_LED, HIGH);
    
    tftSPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
    tft.initR(INITR_GREENTAB);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(20, 50);
    tft.print("SmartHome Hub");
    tft.setCursor(20, 70);
    tft.print("Version 6.6");
    delay(2000);
    tft.fillScreen(ST77XX_BLACK);
    Serial.println("OK");
}

void initRTC() {
    Serial.print("Init RTC... ");
    Wire.begin(RTC_SDA, RTC_SCL);
    Wire.setClock(100000);
    
    if (!rtc.begin()) {
        Serial.println("FAIL");
        rtcOK = false;
        return;
    }
    
    if (!rtc.isrunning()) {
        Serial.println("BAT LOW - SET TIME");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        rtcOK = true;
    } else {
        Serial.println("OK");
        rtcOK = true;
    }
    
    lastRTCRead = rtc.now();
}

void initSD() {
    Serial.print("Init SD card... ");
    strcpy(sdErrorMsg, "OK");
    
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    
    sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    
    if (!SD.begin(SD_CS, sdSPI, 4000000)) {
        Serial.println("FAIL");
        sdInitialized = false;
        strcpy(sdErrorMsg, "INIT FAIL");
        return;
    }
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("NO CARD");
        sdInitialized = false;
        strcpy(sdErrorMsg, "NO CARD");
        return;
    }
    
    sdInitialized = true;
    sdCardType = cardType;
    Serial.println("OK");
    
    updateSDInfo();
}

void checkHTMLFile() {
    if (!sdInitialized) {
        Serial.println("❌ SD card not initialized");
        return;
    }
    
    if (SD.exists("/index.html")) {
        File file = SD.open("/index.html");
        size_t size = file.size();
        file.close();
        Serial.printf("✅ HTML file found, size: %d bytes\n", size);
    } else {
        Serial.println("❌ HTML file not found! Create index.html on SD card");
    }
}

void updateSDInfo() {
    if (!sdInitialized) return;
    sdTotalBytes = SD.totalBytes() / (1024 * 1024);
    sdUsedBytes = SD.usedBytes() / (1024 * 1024);
}

void testSDWrite() {
    if (!sdInitialized || !rtcOK) return;
    
    DateTime now = rtc.now();
    sdWriteCount++;
    
    String dataLine = String(now.year()) + "-" +
                      String(now.month()) + "-" +
                      String(now.day()) + " " +
                      String(now.hour()) + ":" +
                      String(now.minute()) + ":" +
                      String(now.second()) + "," +
                      "P:" + String(currentPressure) +
                      ",T:" + String(currentTemp) +
                      ",H:" + String(currentHumidity) + "\n";
    
    File dataFile = SD.open("/weather.csv", FILE_APPEND);
    if (dataFile) {
        if (sdWriteCount == 1) {
            dataFile.println("Date,Time,Pressure,Temp,Hum");
        }
        dataFile.print(dataLine);
        dataFile.close();
        updateSDInfo();
    }
}

String formatTime(int value) {
    if (value < 10) return "0" + String(value);
    return String(value);
}

void drawTimeBar() {
    uint16_t barColor = securityAlarmActive ? ST77XX_RED : ST77XX_GREEN;
    
    if (!rtcOK) {
        tft.fillRect(0, 0, 160, 15, barColor);
        tft.setCursor(5, 3);
        tft.setTextColor(ST77XX_BLUE);
        tft.print(rusToEng("RTC OSHIBKA"));
        return;
    }
    
    if (millis() - lastRTCReadTime >= RTC_READ_INTERVAL) {
        lastRTCRead = rtc.now();
        lastRTCReadTime = millis();
    }
    
    sprintf(timeStr, "%02d:%02d", lastRTCRead.hour(), lastRTCRead.minute());
    sprintf(dateStr, "%02d.%02d.%04d", lastRTCRead.day(), lastRTCRead.month(), lastRTCRead.year());
    
    tft.fillRect(0, 0, 160, 15, barColor);
    tft.setCursor(5, 3);
    tft.setTextColor(ST77XX_BLUE);
    tft.print(timeStr);
    
    tft.setCursor(60, 3);
    tft.print(dateStr);
    
    tft.setCursor(140, 3);
    tft.setTextColor(sdInitialized ? ST77XX_BLUE : ST77XX_RED);
    tft.print(sdInitialized ? "SD" : "NO");
}

void displayWeatherPage() {
    tft.fillRect(0, 16, 160, 144, ST77XX_BLACK);
    
    tft.setCursor(5, 20);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(rusToEng("Meteostanciya"));
    
    tft.setCursor(5, 35);
    tft.print(rusToEng("Temp: "));
    tft.setTextColor(ST77XX_YELLOW);
    tft.print(currentTemp, 1);
    tft.print("C");
    
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(5, 50);
    tft.print(rusToEng("Davlenie: "));
    tft.setTextColor(ST77XX_CYAN);
    tft.print(currentPressure, 1);
    tft.print("mm");
    
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(5, 65);
    tft.print(rusToEng("Vlazhnost: "));
    tft.setTextColor(ST77XX_GREEN);
    tft.print(currentHumidity, 0);
    tft.print("%");
    
    tft.setCursor(5, 85);
    tft.print(rusToEng("Prognoz:"));
    tft.setCursor(5, 97);
    tft.setTextColor(ST77XX_YELLOW);
    tft.print(shortForecast);
    
    tft.setCursor(120, 85);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(weatherIcon);
    tft.setTextSize(1);
    
    tft.setCursor(5, 115);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(rusToEng("Zamorozki: "));
    if (frostRisk.indexOf("High") >= 0) tft.setTextColor(ST77XX_RED);
    else if (frostRisk.indexOf("Medium") >= 0) tft.setTextColor(ST77XX_ORANGE);
    else tft.setTextColor(ST77XX_GREEN);
    tft.print(frostRisk);
    
    draw_compass(120, 115, 18, windDirection, windCurrentSector, windMagnet);
}

void displayNodePage() {
    tft.fillRect(0, 16, 160, 144, ST77XX_BLACK);
    
    NodeDisplayData &node = nodeDisplayData[displayNodeIndex];
    
    if (!node.connected) {
        tft.fillRect(0, 18, 160, 12, ST77XX_RED);
        tft.setCursor(5, 20);
        tft.setTextColor(ST77XX_WHITE);
        tft.print(rusToEng("NET SVYAZI!"));
    } else if (node.alarm) {
        tft.fillRect(0, 18, 160, 12, alarmBlinkState ? ST77XX_RED : ST77XX_BLUE);
        tft.setCursor(5, 20);
        tft.setTextColor(ST77XX_WHITE);
        tft.print(rusToEng("TREVOGA!"));
    }
    
    int yOffset = (node.connected && !node.alarm) ? 25 : 35;
    
    tft.setCursor(5, yOffset);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(rusToEng("Uzel #"));
    tft.print(node.id);
    
    tft.setCursor(5, yOffset + 12);
    tft.print(rusToEng("Temp: "));
    tft.setTextColor(ST77XX_YELLOW);
    tft.print(node.temp, 1);
    tft.print("C");
    
    tft.setCursor(5, yOffset + 24);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(rusToEng("Vlazhn: "));
    tft.setTextColor(ST77XX_GREEN);
    tft.print(node.hum, 0);
    tft.print("%");
    
    tft.setCursor(5, yOffset + 36);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(rusToEng("Davlenie: "));
    tft.setTextColor(ST77XX_CYAN);
    tft.print(node.press, 1);
    tft.print("mm");
    
    tft.setCursor(5, yOffset + 48);
    tft.setTextColor(ST77XX_WHITE);
    tft.print("LED: ");
    if (node.led_state) {
        tft.setTextColor(ST77XX_RED);
        tft.print("ON");
    } else {
        tft.setTextColor(ST77XX_GREEN);
        tft.print("OFF");
    }
    
    tft.fillRect(0, 150, 160, 10, ST77XX_BLUE);
    tft.setCursor(50, 152);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(rusToEng("Uzel "));
    tft.print(displayNodeIndex + 1);
    tft.print("/4");
}

void displaySDPage() {
    tft.fillRect(0, 16, 160, 144, ST77XX_BLACK);
    
    tft.setCursor(5, 20);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(rusToEng("SD KARTA"));
    
    if (!sdInitialized) {
        tft.setCursor(5, 40);
        tft.setTextColor(ST77XX_RED);
        tft.print(rusToEng("OSHI BKA: "));
        tft.print(sdErrorMsg);
        return;
    }
    
    updateSDInfo();
    
    tft.setCursor(5, 35);
    tft.setTextColor(ST77XX_CYAN);
    tft.print(rusToEng("Tip: "));
    tft.setTextColor(ST77XX_WHITE);
    if (sdCardType == CARD_MMC) tft.print("MMC");
    else if (sdCardType == CARD_SD) tft.print("SDSC");
    else if (sdCardType == CARD_SDHC) tft.print("SDHC");
    else tft.print("UNKNOWN");
    
    tft.setCursor(5, 47);
    tft.setTextColor(ST77XX_CYAN);
    tft.print(rusToEng("Razmer: "));
    tft.setTextColor(ST77XX_WHITE);
    tft.print(sdTotalBytes);
    tft.print(" MB");
    
    tft.setCursor(5, 59);
    tft.setTextColor(ST77XX_CYAN);
    tft.print(rusToEng("Zanyato: "));
    tft.setTextColor(ST77XX_YELLOW);
    tft.print(sdUsedBytes);
    tft.print(" MB");
    
    tft.setCursor(5, 71);
    tft.setTextColor(ST77XX_CYAN);
    tft.print(rusToEng("Svobodno: "));
    tft.setTextColor(ST77XX_GREEN);
    tft.print(sdTotalBytes - sdUsedBytes);
    tft.print(" MB");
    
    int percent = (sdUsedBytes * 100) / sdTotalBytes;
    tft.fillRect(5, 86, 150, 10, ST77XX_BLUE);
    tft.fillRect(5, 86, (150 * percent) / 100, 10, ST77XX_GREEN);
    tft.setCursor(60, 87);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(percent);
    tft.print("%");
    
    tft.setCursor(5, 105);
    tft.setTextColor(ST77XX_CYAN);
    tft.print(rusToEng("Zapisey: "));
    tft.setTextColor(ST77XX_WHITE);
    tft.print(sdWriteCount);
    
    tft.setCursor(5, 120);
    tft.setTextColor(ST77XX_CYAN);
    tft.print(rusToEng("Fayl: "));
    tft.setTextColor(ST77XX_WHITE);
    tft.print("weather.csv");
}

void draw_compass(int cx, int cy, int r, float angle, float sector, bool magnet) {
    tft.fillCircle(cx, cy, r, ST77XX_WHITE);
    tft.drawCircle(cx, cy, r, ST77XX_BLACK);
    
    if (!magnet) {
        tft.drawLine(cx - r, cy - r, cx + r, cy + r, ST77XX_RED);
        tft.drawLine(cx - r, cy + r, cx + r, cy - r, ST77XX_RED);
        tft.setCursor(cx - 8, cy - 3);
        tft.setTextColor(ST77XX_RED);
        tft.print("NO");
        return;
    }
    
    tft.setTextColor(ST77XX_BLACK);
    tft.setCursor(cx - 2, cy - r - 2);
    tft.print("N");
    tft.setCursor(cx + r + 2, cy - 3);
    tft.print("E");
    tft.setCursor(cx - 2, cy + r + 2);
    tft.print("S");
    tft.setCursor(cx - r - 8, cy - 3);
    tft.print("W");
    
    if (sector > 0) {
        float start_rad = radians(angle - sector/2 - 90);
        float end_rad = radians(angle + sector/2 - 90);
        for (float a = start_rad; a <= end_rad; a += 0.03) {
            int x1 = cx + r * cos(a);
            int y1 = cy + r * sin(a);
            tft.drawPixel(x1, y1, ST77XX_RED);
        }
    }
    
    float arrow_rad = radians(angle - 90);
    int x_end = cx + (r-4) * cos(arrow_rad);
    int y_end = cy + (r-4) * sin(arrow_rad);
    tft.drawLine(cx, cy, x_end, y_end, ST77XX_BLACK);
    tft.fillCircle(cx, cy, 2, ST77XX_RED);
}

void handleButtons() {
    unsigned long now = millis();
    
    bool btnCycle = digitalRead(BTN_CYCLE);
    bool btnEnter = digitalRead(BTN_ENTER);
    
    if (btnCycle == LOW && lastBtnCycle == HIGH && (now - lastDebounceCycle) > DEBOUNCE_DELAY) {
        lastDebounceCycle = now;
        
        if (currentPage == PAGE_NODE_INFO) {
            displayNodeIndex = (displayNodeIndex + 1) % 4;
            displayNodePage();
        } else {
            currentPage = PAGE_NODE_INFO;
            displayNodePage();
        }
        
        buzzerBeep(30);
    }
    lastBtnCycle = btnCycle;
    
    if (btnEnter == LOW && lastBtnEnter == HIGH && (now - lastDebounceEnter) > DEBOUNCE_DELAY) {
        lastDebounceEnter = now;
        enterPressStart = now;
        enterLongPressHandled = false;
    }
    
    if (btnEnter == HIGH && lastBtnEnter == LOW) {
        if (!enterLongPressHandled && (now - enterPressStart) < LONG_PRESS_MS) {
            currentPage = (DisplayPage)((currentPage + 1) % PAGE_COUNT);
            
            if (currentPage == PAGE_WEATHER) displayWeatherPage();
            else if (currentPage == PAGE_NODE_INFO) displayNodePage();
            else displaySDPage();
            
            buzzerBeep(50);
        }
        lastDebounceEnter = now;
    }
    
    if (btnEnter == LOW && !enterLongPressHandled && (now - enterPressStart) >= LONG_PRESS_MS) {
        enterLongPressHandled = true;
        if (sdInitialized) {
            testSDWrite();
            buzzerBeep(100);
            delay(100);
            buzzerBeep(100);
        }
    }
    
    lastBtnEnter = btnEnter;
}

void buzzerBeep(int durationMs) {
    ledcWriteTone(BUZZER_CHANNEL, 2000);
    delay(durationMs);
    ledcWriteTone(BUZZER_CHANNEL, 0);
}

void updateAlarmSound() {
    bool anyAlarm = false;
    for (int i = 0; i < NODE_COUNT; i++) {
        if (nodeAlarmState[i] && !nodeConnectionLost[i]) {
            anyAlarm = true;
            break;
        }
    }
    
    if (anyAlarm) {
        if (millis() - lastBuzzerToggle > 300) {
            buzzerState = !buzzerState;
            lastBuzzerToggle = millis();
            ledcWriteTone(BUZZER_CHANNEL, buzzerState ? 2000 : 0);
        }
    } else {
        ledcWriteTone(BUZZER_CHANNEL, 0);
        buzzerState = false;
    }
}