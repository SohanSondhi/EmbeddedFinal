# KinetiKey — "Old Lock, New Twist"

**Embedded Challenge Spring 2026**

A gesture-based combination lock that uses accelerometer data to record and verify a 3-gesture key sequence. Instead of dialing numbers on a padlock, you hold the board in your fist and draw three numbers in the air. The system records the acceleration pattern of each gesture and stores it. To unlock, you replicate the same three gestures — if they match, you're in.

---

## Hardware

- **Adafruit Circuit Playground Classic** (ATmega32u4 @ 16MHz)
- Onboard **LIS3DH** 3-axis accelerometer (SPI, CS = pin 8)
- Onboard **10x NeoPixel** ring (WS2812B on pin 8, shared with accel CS)
- Onboard **2x push buttons** (#4 left, #19 right)
- **No external wiring or components required** — everything is on the board

---

## How to Use

### Step 1: Record Your Key

1. Press the **LEFT button (#4)**
2. A 2-second LED countdown begins — get ready
3. When the LEDs flash, **draw a number in the air** (e.g., "1") while holding the board in your fist
4. **Hold still** for about 0.3 seconds — the system detects you've finished
5. Another countdown begins for the next gesture
6. Repeat for gesture 2 (e.g., "2") and gesture 3 (e.g., "3")
7. After all 3 gestures, the LEDs sweep green = **key saved to EEPROM**

Your key persists through power cycles — unplug and replug, and the key is still stored.

### Step 2: Unlock

1. Press the **RIGHT button (#19)**
2. Same countdown, same process — draw the same 3 numbers in the same order
3. After each gesture, the system compares it to the stored key:
   - **Green LEDs progress** = that gesture matched
   - **Red flash** = mismatch, unlock failed, start over
4. All 3 match → **green chase animation = UNLOCKED!**

### Step 3: Other Controls

| Action | How |
|--------|-----|
| **Record new key** | Press LEFT button (overwrites existing key) |
| **Unlock** | Press RIGHT button |
| **Cancel** (during record/unlock) | Hold BOTH buttons simultaneously |
| **Erase stored key** | Hold BOTH buttons for 0.5 seconds while in idle |

### Tips for Best Results

- **Hold the board in a closed fist** — grip it firmly so your hand motion translates directly to the accelerometer
- **Draw in the air, not on a table** — the accelerometer needs large, clear acceleration patterns. Table drawing produces weak signals
- **Use distinct gestures** — "1" (vertical stroke), "2" (curve + horizontal), "3" (two curves) are very different acceleration patterns and work well
- **Be consistent** — draw at roughly the same speed and size each time. The system tolerates variation, but not wildly different motions
- **Hold still briefly between gestures** — the system needs ~0.3 seconds of stillness to know you're done with one gesture before starting the next
- **The countdown helps** — after the LED countdown flashes, start drawing immediately

---

## LED Feedback Guide

| LED Pattern | Meaning |
|-------------|---------|
| Two blue dots | Idle, ready for input |
| Purple countdown → flash | Get ready → GO! (record mode) |
| Yellow countdown → flash | Get ready → GO! (unlock mode) |
| White spinning dot | Currently capturing your gesture |
| Purple filling up | Recording progress (1/3, 2/3, 3/3) |
| Green filling up | Unlock progress (each correct gesture) |
| Green sweep | Key successfully saved to EEPROM |
| Green chase animation | **UNLOCKED!** |
| Red blink (3x) | Unlock failed — wrong gesture |
| Orange blink | No key stored / gesture too short / timeout |

---

## Build & Flash

### Prerequisites
- [PlatformIO](https://platformio.org/) installed (CLI or VS Code extension)
- USB cable connected to Circuit Playground Classic

### Commands
```bash
# Compile
pio run

# Upload (if auto-upload works)
pio run --target upload

# If upload fails (board stuck), use manual bootloader:
# 1. Double-click the small RESET button on the board
# 2. Red LED starts pulsing = bootloader mode
# 3. Immediately run:
~/.platformio/packages/tool-avrdude/bin/avrdude \
  -p atmega32u4 -c avr109 \
  -C ~/.platformio/packages/tool-avrdude/avrdude.conf \
  -P /dev/cu.usbmodem1101 \
  -U flash:w:.pio/build/cpclassic/firmware.hex:i

# Open Serial Monitor (9600 baud) for debug output
pio device monitor
```

### Serial Monitor Output
The Serial Monitor at 9600 baud shows:
- Current state and mode
- Raw accelerometer data for each gesture (CSV format)
- Binned feature vectors after processing
- **Distance values** for unlock attempts (critical for threshold tuning)
- Match/no-match results

---

## Tuning

The most important parameter is `MATCH_THRESHOLD` in `include/pin_config.h`.

Open Serial Monitor during unlock attempts and observe the distance values:
- **Correct gestures** should produce distances around **0.5 – 0.8**
- **Wrong gestures** should produce distances around **1.3+**
- The threshold (default **1.0**) should sit between these ranges

If the system is too strict (rejects correct gestures), increase the threshold.
If the system is too loose (accepts wrong gestures), decrease the threshold.

Other tunable parameters in `include/pin_config.h`:
- `STILLNESS_MS` (300ms) — how long to hold still before a gesture ends
- `COUNTDOWN_MS` (2000ms) — countdown duration before each gesture
- `SETTLE_MS` (1500ms) — reposition time between gestures
- `GESTURE_TIMEOUT_MS` (30000ms) — how long to wait before timeout

Motion sensitivity in `include/gesture.h`:
- `RAW_MOTION_THR` (1500) — lower = more sensitive to small movements

---

## How It Works (Technical)

### Gesture Recognition Pipeline

1. **Capture**: Raw int16_t accelerometer samples at 50Hz (X, Y, Z axes) during the gesture
2. **Binning**: Compress variable-length samples into 16 fixed time bins (averages per bin). This normalizes for speed — drawing fast or slow produces the same bin pattern
3. **Normalization**: Subtract per-axis mean (removes gravity offset), then divide all axes by a single global energy norm. This preserves the shape ratio between axes while normalizing for amplitude
4. **Comparison**: Euclidean distance between the 48-dimensional normalized feature vectors (16 bins × 3 axes). Lower distance = more similar

### Why Global Normalization Matters

An earlier version normalized each axis independently (dividing X by X's std dev, Y by Y's, Z by Z's). This destroyed the relationship between axes — a vertical stroke and a horizontal stroke looked identical after normalization. The current version divides all axes by the same factor, preserving the shape.

### Storage

The 3-gesture key is stored in the ATmega32u4's 1KB EEPROM:
- Byte 0: Magic number (0xAB) to indicate valid data
- Byte 1: XOR checksum for data integrity
- Bytes 2–577: Three GestureBins structs (16 bins × 3 axes × 4 bytes × 3 gestures = 576 bytes)

### GPIO Implementation

Per project requirements, all GPIO operations use direct register access:
- **Buttons**: Read via `PINx` registers (e.g., `PIND & (1 << PD4)`)
- **LED**: Controlled via `PORTx` registers (e.g., `PORTC |= (1 << PC7)`)
- **NeoPixels**: Bit-banged using inline AVR assembly with cycle-counted timing for WS2812B protocol at 16MHz. No NeoPixel library used.

The Adafruit LIS3DH library is used for accelerometer communication over SPI, and Arduino framework functions (`millis()`, `Serial`, `SPI`) are used for timing and communication — these are not GPIO functions.

---

## Project Structure

```
├── platformio.ini              # Board config (circuitplay_classic) & library deps
├── include/
│   ├── pin_config.h            # Pin mappings, all tunable constants & thresholds
│   ├── gpio_reg.h              # Register-level GPIO: buttons, LED, NeoPixel bit-bang
│   ├── gesture.h               # Capture, bin, normalize, compare, debug dump
│   └── storage.h               # EEPROM save/load with magic number & checksum
├── src/
│   └── main.cpp                # State machine, countdown, record/unlock flow
└── README.md
```

---

## Visualization

A gesture visualizer tool (`gesture_visualizer.jsx`) is available for debugging. Paste Serial Monitor output into the tool to see X/Y/Z acceleration plots for each captured gesture, along with match/no-match status and distance values.

---

## Team

Embedded Challenge Spring 2026
