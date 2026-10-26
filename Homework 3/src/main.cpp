#include <Arduino.h>

// put function declarations here:

const int builtInLed = 2;
const int externalLed = 18;
const int R = 25 ;
const int G = 26 ;
const int B = 27 ;
const int passiveBuzzer = 23;
const int potentiometer = 34;
const int blinkInterval = 1000;
unsigned long currentMillis = 0;
unsigned long previousMillis = 0;
bool state = false;

enum Speed{
  Low,
  Medium,
  High
};

Speed previousSpeed = Low;
unsigned long buzzerStartTime = 0;
bool isBuzzerBeeping = false;

void setup() {
    Serial.begin(115200);
  // put your setup code here, to run once:
    pinMode(builtInLed, OUTPUT);
    // pinMode(potentiometer, INPUT);
    pinMode(externalLed, OUTPUT);
    pinMode(R, OUTPUT);
    pinMode(G, OUTPUT);
    pinMode(B, OUTPUT);
    pinMode(passiveBuzzer, OUTPUT);
    pinMode(potentiometer, INPUT);  
}

void loop() {
    currentMillis = millis();
    if(currentMillis - previousMillis >= blinkInterval){
      previousMillis = currentMillis;
      state = !state;
      digitalWrite(builtInLed, state);
    }
    int potValue = analogRead(potentiometer);
    int brightness = map(potValue, 0, 4095, 0, 255);
    analogWrite(externalLed, brightness);
    
    Speed speed = Low;

    if (brightness >= 85 && brightness < 170) {
      speed = Medium;
    } else if (brightness >= 170) {
      speed = High;
    }
    switch (speed) {
      case Low:
        digitalWrite(B, LOW);
        digitalWrite(G, HIGH);
        digitalWrite(R, HIGH);
        break;
      case Medium:
        digitalWrite(B, HIGH );
        digitalWrite(G, LOW);
        digitalWrite(R, HIGH);
        break;
      case High:
        digitalWrite(B, HIGH);
        digitalWrite(G, HIGH);
        digitalWrite(R, LOW);
        if (previousSpeed != High) {
        tone(passiveBuzzer, 2000); // Start 2000Hz tone
        isBuzzerBeeping = true;
        buzzerStartTime = currentMillis;
      }
        break;
    }

    previousSpeed = speed;

  Serial.print("Potentiomter: ");
  Serial.println(potValue);
  if (isBuzzerBeeping && (currentMillis - buzzerStartTime >= 500)) {
    noTone(passiveBuzzer);
    isBuzzerBeeping = false;
  }

}