#define CONTACT1_PIN 2    // GPIO2 (А2)
#define CONTACT2_PIN 4    // GPIO4 (А4)
#define LED_PIN 8         // Встроенный LED

void setup() {
  pinMode(CONTACT1_PIN, INPUT_PULLDOWN);
  pinMode(CONTACT2_PIN, INPUT_PULLDOWN);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  // Читаем состояние пинов (0=замкнут на GND, 1=разомкнут)
  bool pin2_closed = (digitalRead(CONTACT1_PIN) == LOW);  // LOW = замкнут
  bool pin4_closed = (digitalRead(CONTACT2_PIN) == LOW);  // LOW = замкнут
  
  // Логика индикации
  if (!pin2_closed && !pin4_closed) {  // ОБА разомкнуты
    blinkLED(3, 100);  // 3 быстрых моргания
    delay(500);
  } 
  else if (!pin2_closed) {  // Только PIN2 разомкнут
    blinkLED(2, 200);  // 2 моргания для PIN2
    delay(1000);
  }
  else if (!pin4_closed) {  // Только PIN4 разомкнут
    blinkLED(4, 150);  // 4 моргания для PIN4
    delay(1000);
  }
  else {  // ОБА замкнуты (норма)
    // Медленное моргание раз в 2 секунды
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 2000) {
      digitalWrite(LED_PIN, HIGH);
      delay(20);
      digitalWrite(LED_PIN, LOW);
      lastBlink = millis();
    }
    delay(100);
  }
}

// Функция моргания
void blinkLED(int times, int speed) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(speed);
    digitalWrite(LED_PIN, LOW);
    if (i < times - 1) delay(speed);
  }
}