#include <Arduino.h>

void setup() {
  pinMode(3, INPUT_PULLUP);  // Подтяжка к HIGH
  pinMode(4, INPUT_PULLUP);  // Подтяжка к HIGH
  pinMode(8, OUTPUT);        // LED (активный LOW)
  digitalWrite(8, HIGH);     // LED выключен
}

void loop() {
  // С PULLUP: 0=замкнут на GND, 1=разомкнут
  bool pin3_closed = (digitalRead(3) == 0);  // true если замкнут
  bool pin4_closed = (digitalRead(4) == 0);  // true если замкнут
  
  if (pin3_closed && pin4_closed) {
    // Оба замкнуты: 3 быстрых
    for(int i=0; i<3; i++) {
      digitalWrite(8, LOW); delay(150);
      digitalWrite(8, HIGH); delay(150);
    }
    delay(500);
  } 
  else if (pin3_closed) {
    // Только GPIO3 замкнут: 2 моргания
    digitalWrite(8, LOW); delay(300);
    digitalWrite(8, HIGH); delay(300);
    digitalWrite(8, LOW); delay(300);
    digitalWrite(8, HIGH); delay(1000);
  }
  else if (pin4_closed) {
    // Только GPIO4 замкнут: 4 моргания
    for(int i=0; i<4; i++) {
      digitalWrite(8, LOW); delay(200);
      digitalWrite(8, HIGH); delay(200);
    }
    delay(1000);
  }
  else {
    // Оба разомкнуты: ничего не делаем (LED не горит)
    delay(100);
  }
}