#include <Arduino.h>
#include <Wire.h>

// AS5600 адрес и регистры
#define AS5600_ADDR 0x36
#define ANGLE_H_REG 0x0E
#define ANGLE_L_REG 0x0F
#define STATUS_REG 0x0B

// I2C пины - ваши настройки
#define I2C_SDA 1
#define I2C_SCL 0

// Буфер для чтения
uint8_t angle_data[2];

uint16_t readRawAngle() {
    Wire.beginTransmission(AS5600_ADDR);
    Wire.write(ANGLE_H_REG);
    Wire.endTransmission(false);
    
    Wire.requestFrom(AS5600_ADDR, 2);
    if (Wire.available() >= 2) {
        angle_data[0] = Wire.read();
        angle_data[1] = Wire.read();
        return (angle_data[0] << 8) | angle_data[1];
    }
    return 0;
}

void setup() {
    // Критическая задержка для USB-CDC
    delay(3000);
    
    // Инициализация USB Serial - работает благодаря вашим флагам
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n╔══════════════════════════════════╗");
    Serial.println("║     AS5600 + ESP32-C3 TEST      ║");
    Serial.println("╚══════════════════════════════════╝");
    Serial.println();
    
    // Инициализация I2C на ваших пинах
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);  // Начинаем с низкой скорости
    
    Serial.print("I2C initialized on SDA=");
    Serial.print(I2C_SDA);
    Serial.print(", SCL=");
    Serial.println(I2C_SCL);
    
    // Проверка подключения AS5600
    Wire.beginTransmission(AS5600_ADDR);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
        Serial.println("✅ AS5600 detected at 0x36");
        
        // Проверка магнита
        Wire.beginTransmission(AS5600_ADDR);
        Wire.write(STATUS_REG);
        Wire.endTransmission(false);
        Wire.requestFrom(AS5600_ADDR, 1);
        
        if (Wire.available()) {
            byte status = Wire.read();
            Serial.print("Status register: 0x");
            Serial.println(status, HEX);
            
            if (status & 0x20) {
                Serial.println("✅ Magnet detected");
            } else {
                Serial.println("❌ Magnet NOT detected");
            }
        }
        
    } else {
        Serial.println("❌ AS5600 NOT found!");
        Serial.print("Error code: ");
        Serial.println(error);
        Serial.println("Check wiring: SDA=GPIO1, SCL=GPIO0, VCC=3.3V, GND");
    }
    
    Serial.println("\n--- Starting angle readings ---");
    Serial.println("Angle(°)\tRaw\tStatus");
    Serial.println("--------------------------------");
}

void loop() {
    uint16_t raw_angle = readRawAngle();
    float angle_deg = (raw_angle * 360.0) / 4096.0;
    
    // Чтение статуса
    Wire.beginTransmission(AS5600_ADDR);
    Wire.write(STATUS_REG);
    Wire.endTransmission(false);
    Wire.requestFrom(AS5600_ADDR, 1);
    byte status = Wire.available() ? Wire.read() : 0;
    
    // Форматированный вывод
    Serial.print(angle_deg, 2);
    Serial.print("°\t");
    Serial.print(raw_angle);
    Serial.print("\t");
    Serial.print("0x");
    Serial.println(status, HEX);
    
    delay(100);
}