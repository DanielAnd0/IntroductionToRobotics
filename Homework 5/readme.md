# Homework #5 - Stop the Counter!

## Overview

**Stop the Counter!** is an ESP32-based reaction game using a four-digit seven-segment display driven by a **74HC595 shift register** and controlled via an **analog joystick**. 

At startup, the display shows a predetermined **target number**. Upon player command, a rapid countdown begins from a higher starting value. The player must use precision and reaction time to freeze the countdown as close to the target as possible. The game calculates and prints the absolute error ($|\text{Stopped Value} - \text{Target}|$) to the Serial Monitor.

---

##  Hardware Requirements & Pinout

All components operate on a fixed circuit powered at **3.3V**. Current-limiting resistors (1 kΩ) are placed on the display segment lines to protect the LED multiplexing interface.

### Component Wiring Table

| Component / Signal | ESP32 GPIO | Function |
| :--- | :--- | :--- |
| **74HC595 Data (DS)** | `GPIO23` | Serial Data Input |
| **74HC595 Shift Clock (SHCP)** | `GPIO18` | Shift Register Clock Pulse |
| **74HC595 Latch Clock (STCP)** | `GPIO22` | Storage Latch Pulse |
| **Display Digit 1 (D1)** | `GPIO25` | Digit selection (Thousands) |
| **Display Digit 2 (D2)** | `GPIO26` | Digit selection (Hundreds) |
| **Display Digit 3 (D3)** | `GPIO27` | Digit selection (Tens) |
| **Display Digit 4 (D4)** | `GPIO33` | Digit selection (Units) |
| **Joystick X-Axis (VRx)** | `GPIO34` | Analog Input X-Axis |
| **Joystick Y-Axis (VRy)** | `GPIO35` | Analog Input Y-Axis |
| **Joystick Button (SW)** | `GPIO32` | Digital Input Button |

---

## Game Controls & Mechanics

### State Machine Architecture
1. **IDLE (Target State):** The display shows the static target value (e.g., `5000`).
2. **COUNTDOWN:** The counter decrements rapidly from the starting value (e.g., `9999`) toward `0`.
3. **STOPPED (Score State):** The counter freezes at the stopped value. The Serial Monitor displays:
   - Target Value
   - Stopped Value
   - Absolute Error ($|\text{Stopped Value} - \text{Target}|$)

### Joystick Mapping

| Control Input | Action | Description |
| :--- | :--- | :--- |
| **Joystick UP** | **Start** | Initiates the non-blocking countdown from initial value (9999). |
| **Joystick Button (SW)** | **Stop** | Instantly freezes the counter and evaluates the score. |
| **Joystick DOWN** | **Reset** | Returns the display to the initial target state for a new round. |

---

## Technical Highlights

### 1. Bitwise Display Encoding & Leading Zero Suppression
- Display segment patterns are stored as compact 8-bit byte array patterns (`uint8_t`) using custom bit masks.
- No external seven-segment or shift-register libraries are used.
- **Leading-zero suppression logic:** Leading zero digits are blanked with empty segment patterns while ensuring `0` is visually represented when the count reaches zero:
  - `0000` $\rightarrow$ `   0`
  - `0009` $\rightarrow$ `   9`
  - `0042` $\rightarrow$ `  42`
  - `1234` $\rightarrow$ `1234`

### 2. Fully Non-Blocking Timing Architecture
- Built without blocking function calls such as `delay()` or busy loops.
- All display multiplexing and countdown intervals rely strictly on non-blocking state checks using `millis()`.
- Display persistence of vision (POV) remains active and flicker-free during countdown, button debouncing, and state transitions.

---

## Video Demonstration

Link to video demonstration showing game initialization, countdown execution, reaction stop, serial output log, and game reset:
https://youtu.be/SJ_PTR8-ziY
