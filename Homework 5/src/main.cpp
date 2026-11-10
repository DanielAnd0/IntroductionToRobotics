#include <Arduino.h>

const uint8_t joystickXPin = 34;
const uint8_t joystickYPin = 35;
const uint8_t joystickSWPin = 32;
// // Joystick reading interval
// const uint32_t JOYSTICK_INTERVAL_MS = 150;
// uint32_t lastJoystickReadMs = 0;


// 74HC595 connections
const uint8_t dataPin = 23; // DS
const uint8_t clockPin = 18; // SHCP
const uint8_t latchPin = 22; // STCP
// Four-digit display connections
const uint8_t displayDigits[4] = {25, 26, 27, 33};
const uint8_t displayCount = 4;
// Set according to your display type
const bool COMMON_ANODE = false;
// Display control logic
const uint8_t DIGIT_ON = COMMON_ANODE ? HIGH : LOW;
const uint8_t DIGIT_OFF = COMMON_ANODE ? LOW : HIGH;
// Segment order: A B C D E F G DP
const uint8_t byteEncodings[16] = {
0b11111100, // 0
0b01100000, // 1
0b11011010, // 2
0b11110010, // 3
0b01100110, // 4
0b10110110, // 5
0b10111110, // 6
0b11100000, // 7
0b11111110, // 8
0b11110110, // 9
0b11101110, // A
0b00111110, // b
0b10011100, // C
0b01111010, // d
0b10011110, // E
0b10001110 // F
};

uint8_t displayBuffer[4] = {
byteEncodings[1],
byteEncodings[2],
byteEncodings[3],
byteEncodings[4]
};
// Multiplexing timing
const uint32_t REFRESH_INTERVAL_MS = 2;
uint32_t lastRefreshMs = 0;
uint8_t currentDigit = 0;

uint32_t lastChangeMs = 0;
bool showSecondNumber = false;


// Game Settings & Values
const uint16_t TARGET_VALUE = 5000;
const uint16_t START_VALUE  = 9999;
const uint32_t COUNTDOWN_INTERVAL_MS = 1; 

uint16_t currentCount = TARGET_VALUE;
uint32_t lastCountdownMs = 0;

// Game State Machine Definition
enum GameState {
  STATE_IDLE,      // Showing target value
  STATE_COUNTDOWN, // Rapid countdown in progress
  STATE_STOPPED    // Counter frozen, score evaluated
};

GameState currentState = STATE_IDLE;

// Joystick Analog Thresholds (12-bit ADC: 0 - 4095)
const uint16_t THRESHOLD_UP   = 3200;
const uint16_t THRESHOLD_DOWN = 1000;
const uint32_t DEBOUNCE_DELAY_MS = 50;

// Edge & Debounce tracking for Joystick
struct JoystickTracker {
  bool upTriggered = false;
  bool downTriggered = false;
  bool lastRawButtonState = HIGH;
  bool debouncedButtonState = HIGH;
  uint32_t lastDebounceTime = 0;
} joy;

enum JoystickAction {
  ACTION_NONE,
  ACTION_UP,
  ACTION_DOWN,
  ACTION_STOP
};

// Manually transmit an 8-bit segment pattern
void writeReg(uint8_t logicalPattern) {
// Convert logical segments to electrical levels
uint8_t pattern = COMMON_ANODE
? static_cast<uint8_t>(~logicalPattern)
: logicalPattern;
digitalWrite(latchPin, LOW);
// Transmit 8 bits, MSB first
for (int i = 7; i >= 0; i--) {
// Extract the current bit
uint8_t bit = (pattern >> i) & 0x01;
// Generate the clock signal manually
digitalWrite(clockPin, LOW);
digitalWrite(dataPin, bit);
digitalWrite(clockPin, HIGH);
digitalWrite(clockPin, LOW);
}
// Update the parallel outputs
digitalWrite(latchPin, HIGH);
}


void deactivateAllDigits() {
for (uint8_t i = 0; i < displayCount; i++) {
digitalWrite(displayDigits[i], DIGIT_OFF);
}
}

void activateDisplay(uint8_t displayNumber) {
deactivateAllDigits();
if (displayNumber < displayCount) {
digitalWrite(
displayDigits[displayNumber],
DIGIT_ON
);
}
}

bool check_for_zero = false;

void refreshDisplay() {
uint32_t now = millis();
// Is it time to refresh the next digit?
if (now - lastRefreshMs < REFRESH_INTERVAL_MS) {
return;
}
lastRefreshMs = now;
// Prevent ghosting
deactivateAllDigits();
// Update the segments for the current digit

if(currentDigit == 0) check_for_zero = true;
if(currentDigit == displayCount-1) check_for_zero = false;

// Enable the selected digit

if(check_for_zero && displayBuffer[currentDigit] == byteEncodings[0])
{

} else { 
    if (check_for_zero && displayBuffer[currentDigit] != byteEncodings[0])    
        check_for_zero = false;
    writeReg(displayBuffer[currentDigit]);
    activateDisplay(currentDigit);
} 

// Move to the next digit
currentDigit = (currentDigit + 1) % displayCount;
}

void setDisplayNumber(uint16_t number) {
// Keep only the last four decimal digits
number %= 10000;
// Fill the display from right to left
for (int i = displayCount - 1; i >= 0; i--) {
uint8_t digit = number % 10;
displayBuffer[i] = byteEncodings[digit];
number /= 10;
}
}

JoystickAction readJoystick() {
  JoystickAction action = ACTION_NONE;
  uint32_t now = millis();

  // 1. Read Analog Y-Axis (UP / DOWN) with non-blocking edge detection
  uint16_t yValue = analogRead(joystickYPin);

  if (yValue > THRESHOLD_UP) {
    if (!joy.upTriggered) {
      joy.upTriggered = true;
      action = ACTION_UP;
    }
  } else {
    joy.upTriggered = false;
  }

  if (yValue < THRESHOLD_DOWN) {
    if (!joy.downTriggered) {
      joy.downTriggered = true;
      action = ACTION_DOWN;
    }
  } else {
    joy.downTriggered = false;
  }

  // 2. Read Digital Button (SW - Stop Command) with non-blocking debounce
  bool rawButton = digitalRead(joystickSWPin);
  if (rawButton != joy.lastRawButtonState) {
    joy.lastDebounceTime = now;
  }
  joy.lastRawButtonState = rawButton;

  if ((now - joy.lastDebounceTime) > DEBOUNCE_DELAY_MS) {
    if (rawButton != joy.debouncedButtonState) {
      joy.debouncedButtonState = rawButton;
      if (joy.debouncedButtonState == LOW) { // Button active LOW
        action = ACTION_STOP;
      }
    }
  }

  return action;
}


void setup() {
pinMode(dataPin, OUTPUT);
pinMode(clockPin, OUTPUT);
pinMode(latchPin, OUTPUT);
digitalWrite(dataPin, LOW);
digitalWrite(clockPin, LOW);
digitalWrite(latchPin, LOW);
// pinMode(joystickSWPin, INPUT_PULLUP);
// analogReadResolution(12);
Serial.begin(115200);

// Initially disable all four digits
for (uint8_t i = 0; i < displayCount; i++) {
pinMode(displayDigits[i], OUTPUT);
digitalWrite(displayDigits[i], DIGIT_OFF);
}
// Initially turn off all segments
writeReg(0b00000000);
// Enable only the first digit
digitalWrite(displayDigits[0], DIGIT_ON);

// Initialize Joystick Pins
  pinMode(joystickSWPin, INPUT_PULLUP);
  analogReadResolution(12);

  // Set initial display state
  setDisplayNumber(TARGET_VALUE);
  Serial.println("=================================");
  Serial.println("   Stop the Counter! Ready.      ");
  Serial.println(" Joystick UP    -> Start         ");
  Serial.println(" Joystick BUTTON-> Stop          ");
  Serial.println(" Joystick DOWN  -> Reset         ");
  Serial.println("=================================");

}

void loop() {
  refreshDisplay();

  uint32_t now = millis();
  JoystickAction action = readJoystick();

  // State Machine Controller
  switch (currentState) {
    case STATE_IDLE:
      setDisplayNumber(TARGET_VALUE);

      if (action == ACTION_UP) {
        currentCount = START_VALUE;
        setDisplayNumber(currentCount);
        lastCountdownMs = now;
        currentState = STATE_COUNTDOWN;
        Serial.println("\n--- COUNTDOWN STARTED ---");
      }
      break;

    case STATE_COUNTDOWN:
      // Non-blocking countdown tick
      if (now - lastCountdownMs >= COUNTDOWN_INTERVAL_MS) {
        lastCountdownMs = now;

        if (currentCount > 0) {
          currentCount--;
          setDisplayNumber(currentCount);
        } else {
          // Reached zero automatically
          currentState = STATE_STOPPED;
          Serial.println("\n--- COUNTER REACHED ZERO ---");
          Serial.printf("Target Value:  %u\n", TARGET_VALUE);
          Serial.printf("Stopped Value: %u\n", currentCount);
          Serial.printf("Absolute Error: %u\n", (uint16_t)abs((int)currentCount - (int)TARGET_VALUE));
        }
      }

      // Player stops the counter
      if (action == ACTION_STOP) {
        currentState = STATE_STOPPED;
        uint16_t absError = (uint16_t)abs((int)currentCount - (int)TARGET_VALUE);

        Serial.println("\n--- COUNTER STOPPED ---");
        Serial.printf("Target Value:   %u\n", TARGET_VALUE);
        Serial.printf("Stopped Value:  %u\n", currentCount);
        Serial.printf("Absolute Error: %u\n", absError);
      }
      break;

    case STATE_STOPPED:
      // Display freezes on stopped value until reset
      setDisplayNumber(currentCount);

      if (action == ACTION_DOWN) {
        currentState = STATE_IDLE;
        setDisplayNumber(TARGET_VALUE);
        Serial.println("\n--- GAME RESET TO TARGET ---");
      }
      break;
  }
}