# KinetiKey — "Old Lock, New Twist"

**Embedded Challenge Spring 2026**

A gesture-based combination lock using accelerometer-driven Dynamic Time Warping. Hold the board in your fist, perform three distinct gestures to set a key, then replicate them to unlock.

---

## Hardware

- **Adafruit Circuit Playground Classic** (ATmega32u4 @ 8MHz)
- Onboard LIS3DH 3-axis accelerometer (SPI)
- Onboard 10× NeoPixel ring for visual feedback
- Onboard push buttons and red LED
- No external components required

## How It Works

### Recording a Key
1. **Short press LEFT** → Record mode
2. LED countdown (2s) → flash = GO
3. Perform gesture (punch, shake, circle, etc.) while holding board in fist
4. Hold still ~0.5s → gesture captured
5. Repeat for all 3 gestures
6. Green sweep = **key saved to EEPROM**

### Unlocking
1. **Press RIGHT** → Unlock mode
2. Replicate the same 3 gestures in order
3. Each correct gesture → green progress LEDs
4. Up to **3 retries per gesture** — failed gesture resumes from where you left off
5. All 3 correct → **green chase animation = UNLOCKED**

### Other Controls

| Action | How |
|--------|-----|
| Record new key | Short press LEFT |
| Unlock | Press RIGHT |
| Erase stored key | Hold LEFT 3 seconds |
| Cancel any operation | Hold BOTH buttons |

## Gesture Recognition Algorithm

We use **Dynamic Time Warping (DTW)** on **normalized acceleration magnitude** — a proven approach for accelerometer-based gesture recognition on microcontrollers.

### Pipeline

1. **Capture** raw XYZ acceleration at 50Hz (up to 2 seconds)
2. **Downsample** to 30 evenly-spaced points
3. **Compute magnitude** for each point: `√(x² + y² + z²)`
4. **Normalize** the magnitude sequence (zero mean, unit standard deviation)
5. **Compare** using banded DTW (Sakoe-Chiba band = 8)
6. If average DTW cost < threshold → **match**

### Why DTW + Magnitude?

- **DTW handles timing variation**: Drawing the same gesture faster or slower still matches, because DTW warps the time axis to find optimal alignment.
- **Magnitude is orientation-invariant**: The scalar `√(x² + y² + z²)` doesn't depend on which way the axes point, so different grip angles produce the same value.
- **Normalization removes scale**: Big and small versions of the same gesture produce the same normalized profile.
- **Memory-efficient**: Rolling 2-row DTW uses only 248 bytes instead of the full 30×30 matrix (3,600 bytes).

### Recommended Gestures

Use physically distinct motions for best discrimination:
- **Punch** — single forward thrust (one sharp spike)
- **Shake** — quick left-right waggle (two alternating spikes)
- **Circle** — smooth circular motion (sustained sinusoidal pattern)

## Build & Flash

```bash
# Compile
pio run

# Upload (manual bootloader — most reliable):
# 1. Double-click RESET on board
# 2. Red LED pulses = bootloader mode
# 3. Immediately run:
~/.platformio/packages/tool-avrdude/bin/avrdude \
  -p atmega32u4 -c avr109 \
  -C ~/.platformio/packages/tool-avrdude/avrdude.conf \
  -P /dev/cu.usbmodem101 \
  -U flash:w:.pio/build/cpclassic/firmware.hex:i

# Serial monitor (9600 baud)
pio device monitor
```

## Project Structure

```
├── platformio.ini              # Board config, CircuitPlayground library
├── README.md                   # This file
├── CLAUDE.md                   # Development context
├── include/
│   ├── pin_config.h            # Pin mappings, thresholds, timing
│   ├── gpio_reg.h              # Register-level GPIO, NeoPixel animations
│   ├── gesture.h               # DTW gesture matching engine
│   └── storage.h               # EEPROM save/load with checksum
└── src/
    └── main.cpp                # State machine, record/unlock/retry
```

## Technical Details

- **GPIO**: All button reading and LED control uses direct register access (`DDRx`, `PORTx`, `PINx`)
- **NeoPixels**: Driven via CircuitPlayground library (handles shared pin 8, SPI conflicts, level shifter, and 8MHz timing)
- **Accelerometer**: LIS3DH at ±4g range, 50Hz data rate, SPI interface
- **Storage**: 3 gesture templates stored in ATmega32u4's 1KB EEPROM with magic byte and XOR checksum (542 bytes total)
- **Memory**: ~84% flash, ~63% RAM

## LED Feedback

| Pattern | Meaning |
|---------|---------|
| Cyan sweep | Boot |
| Two blue dots | Idle |
| Purple countdown → flash | Get ready → GO (record) |
| Yellow countdown → flash | Get ready → GO (unlock) |
| White spinning dot | Capturing |
| Green chase → solid → fade | UNLOCKED |
| Red flash ×5 | FAILED |
| Orange blink | Retry / warning |
