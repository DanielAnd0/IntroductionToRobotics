#include <Arduino.h>
const int buttonPin = 21;
const int ledPin = 18;
const int builtLed = 2;
volatile bool buttonEvent = false;
bool changeState = false;
int ledState = 0;
int lastReading = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 30;
const unsigned long blinkInterval = 1000;
unsigned long previousBuiltTime = 0;
unsigned long previousBlinkTime = 0;
unsigned long setTimer = 0;
int builtLedState = LOW;
// Interrupt Service Routine
void IRAM_ATTR handleButtonInterrupt() {
buttonEvent = true;
}

enum LampMode {
MODE_OFF,
MODE_DIM,
MODE_FULL,
MODE_BLINK,
MODE_TURBO
};
LampMode previousState = MODE_OFF;
void setup() {
pinMode(buttonPin, INPUT_PULLUP);
pinMode(ledPin, OUTPUT);
pinMode(builtLed, OUTPUT);
digitalWrite(ledPin, ledState);
Serial.begin(115200);
attachInterrupt(
digitalPinToInterrupt(buttonPin),
handleButtonInterrupt,
FALLING
);
}
void loop() {

unsigned long currentTime = millis();
int reading = digitalRead(buttonPin);
// Reset the timer whenever the raw input changes.

if(reading != lastReading){
  lastDebounceTime = currentTime;
  if(lastReading == LOW) 
      buttonEvent = false;
  setTimer = currentTime;
}

if(currentTime - previousBuiltTime >= blinkInterval)
{
  previousBuiltTime = currentTime;
  builtLedState = !builtLedState;
  digitalWrite(builtLed, builtLedState);
}

if(currentTime - lastDebounceTime >= debounceDelay && buttonEvent){  //on confirmed pressed
  setTimer = currentTime;
  lastDebounceTime = currentTime;
  buttonEvent = false;
  Serial.println("Button interrupt detected!");
}

 if(changeState && reading == HIGH && currentTime - lastDebounceTime >= debounceDelay){ //on confirmed release
  switch (previousState) {
    case MODE_OFF: ledState = 64; 
        previousState = MODE_DIM;
        break;
    case MODE_DIM: ledState = 255;
        previousState = MODE_FULL;
        break;
    case MODE_FULL: ledState = 255; 
        previousState = MODE_BLINK; 
        previousBlinkTime = currentTime;
        break;
    case MODE_BLINK: ledState = 0; 
        previousState = MODE_OFF; 
        break;
    case MODE_TURBO: ledState = 255; //if turbo switch to blink
        previousState = MODE_BLINK;
        previousBlinkTime = currentTime; 
        break;
}
  lastDebounceTime = currentTime;
  analogWrite(ledPin, ledState);
  changeState = false;
  }

if(currentTime - setTimer >= 1000 && previousState != MODE_TURBO && reading == LOW) //after 1 second pressed turbo mode is triggered
{
   setTimer = currentTime;
   previousState = MODE_TURBO;
   previousBlinkTime = currentTime;
}

if(previousState == MODE_TURBO && (currentTime - previousBlinkTime)*1.0 >= blinkInterval/2)
{
  if(ledState)
    ledState = 0;
  else ledState = 255;
  analogWrite(ledPin, ledState);
  previousBlinkTime = currentTime;
}

if(previousState == MODE_BLINK && currentTime - previousBlinkTime >= blinkInterval)
{
  if(ledState)
    ledState = 0;
  else ledState = 255;
  analogWrite(ledPin, ledState);

  previousBlinkTime = currentTime;
}

lastReading = reading;
}