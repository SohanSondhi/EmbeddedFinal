# KinetiKey — "Old Lock, New Twist"
Embedded Challenge Spring 2026

## Hardware
- Adafruit Circuit Playground Classic (ATmega32u4)
- Onboard LIS3DH accelerometer (I2C)
- Onboard NeoPixels (x10), buttons (x2)

## How to Use
1. **Record**: Press LEFT button, perform 3 gestures (hold board in fist)
2. **Unlock**: Press RIGHT button, replicate the 3 gestures

## LED Feedback
- **Purple** = Record mode, progress shown per gesture
- **Yellow** = Unlock mode started
- **Green progress** = Each correct gesture during unlock
- **Green spin animation** = Unlock success
- **Red flash** = Unlock fail

## Build
```
pio run
pio run --target upload
pio device monitor
```

## Project Structure
```
├── platformio.ini          # Board & library config
├── include/
│   ├── pin_config.h        # Pin mappings & constants
│   ├── gpio_reg.h          # Register-level GPIO, NeoPixel driver
│   ├── gesture.h           # Gesture capture, features, matching
│   └── storage.h           # EEPROM save/load
└── src/
    └── main.cpp            # State machine & main logic
```
