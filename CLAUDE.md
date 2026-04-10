# CLAUDE.md — KinetiKey Project Context

**Read this first before doing anything.**

---

## Project Overview

**KinetiKey** — Gesture-based combination lock.
Embedded Challenge Spring 2026. Hold board in fist, perform 3 gestures to record a key, replicate to unlock.

## Hardware

- **Board**: Adafruit Circuit Playground Classic (ATmega32u4 @ 8MHz)
- **Accelerometer**: LIS3DH via SPI (managed by CircuitPlayground library)
- **NeoPixels**: 10x WS2812B on pin 8 (managed by CircuitPlayground library)
- **Buttons**: Left (#4, PD4), Right (#19, PF6) — ACTIVE HIGH (external pull-downs)
- **Red LED**: #13 (PC7)
- Pin 8 shared between NeoPixel data and LIS3DH SPI CS — CircuitPlayground library handles this internally

## Team & Repo

- GitHub: `SohanSondhi/EmbeddedFinal`
- Branch workflow: feature branches → PR → merge to main
- Sichen (SichenLiang) primary developer, 2 teammates

## Build & Upload

```bash
cd ~/EmbeddedFinal
pio run

# Manual upload (auto-reset unreliable):
# 1. Double-click RESET (red LED pulses = bootloader)
# 2. Immediately run:
~/.platformio/packages/tool-avrdude/bin/avrdude \
  -p atmega32u4 -c avr109 \
  -C ~/.platformio/packages/tool-avrdude/avrdude.conf \
  -P /dev/cu.usbmodem101 \
  -U flash:w:.pio/build/cpclassic/firmware.hex:i

# Port may vary — check: ls /dev/cu.usbmodem*
pio device monitor  # 9600 baud
```

## File Structure

```
├── platformio.ini          # CircuitPlayground library only
├── README.md
├── CLAUDE.md               ← YOU ARE HERE
├── include/
│   ├── pin_config.h        # Constants, thresholds, pin defs
│   ├── gpio_reg.h          # NeoPixel (via CircuitPlayground.strip) + register-level GPIO
│   ├── gesture.h           # DTW on normalized magnitude
│   └── storage.h           # EEPROM save/load with checksum
└── src/
    └── main.cpp            # State machine, record/unlock/retry flow
```

## Algorithm: DTW on Normalized Magnitude (V7.2)

**Pipeline:**
1. Capture raw int16_t XYZ at 50Hz (up to 100 samples)
2. Downsample to 30 points (evenly spaced)
3. Compute magnitude: `sqrt(x² + y² + z²)` for each point
4. Normalize: subtract mean, divide by std dev → zero-mean unit-variance
5. Compare via banded DTW (Sakoe-Chiba band=8, rolling 2-row = 248 bytes)
6. DTW cost / N < threshold → match

**Why this works:**
- Magnitude is a scalar → orientation invariant (doesn't matter how you hold the board)
- Normalization removes scale → big/small gestures match
- DTW warps time → fast/slow versions of same gesture match

**Known limitation:** Extreme orientation changes (e.g., fully horizontal → fully vertical) still affect magnitude because gravity contributes differently. Fix: subtract gravity before normalization (`mag - 8192`). Not yet implemented.

**Threshold:** `MATCH_THRESHOLD = 0.7` (normalized units). Correct gestures typically 0.15-0.5, wrong gestures 0.8+.

## NeoPixels

**Solution:** `Adafruit_CircuitPlayground` library. This is the official library for this exact board. It handles:
- Pin 8 SPI/NeoPixel sharing (disables SPI before data transmission)
- 8MHz bit-bang timing
- 74AHCT125 level shifter (3.3V → 5V for NeoPixel data)

All NeoPixel operations go through `CircuitPlayground.strip`. Our wrapper functions (`neo_show()`, `neo_set()`, etc.) call the library internally.

## Controls

| Action | How |
|--------|-----|
| Record new key | Short press LEFT |
| Unlock attempt | Press RIGHT |
| Erase stored key | Hold LEFT 3 seconds |
| Cancel operation | Hold BOTH buttons |

## Retry Logic

- Each gesture gets 3 attempts during unlock
- Failed gesture → retry from THAT gesture (not from gesture 1)
- Pass a gesture → retries reset to 3 for next gesture
- All 3 retries exhausted on any gesture → total failure, return to idle

## LED Patterns

| Pattern | Meaning |
|---------|---------|
| Cyan sweep | Boot animation |
| Two blue dots | Idle, ready |
| Purple countdown | Recording countdown |
| Yellow countdown | Unlock countdown |
| White spinning dot | Capturing gesture |
| Purple progress (3 LEDs/gesture) | Recording progress |
| Green progress | Unlock progress |
| Green chase → solid → fade | UNLOCKED |
| Red flash ×5 | FAILED |
| Orange flash | Warning (too short, timeout, retry) |
| Red progressive fill | Erase countdown |

## Key Constants (pin_config.h)

| Constant | Value | Purpose |
|----------|-------|---------|
| `MATCH_THRESHOLD` | 0.7 | DTW distance cutoff |
| `MAX_RETRIES` | 3 | Attempts per gesture |
| `DTW_SAMPLES` | 30 | Downsampled points |
| `DTW_BAND` | 8 | Sakoe-Chiba band width |
| `STILLNESS_MS` | 500 | Still = gesture done |
| `RAW_MOTION_THR` | 1500 | Motion detection sensitivity |
| `COUNTDOWN_MS` | 2000 | LED countdown duration |
| `ERASE_HOLD_MS` | 3000 | Long-press to erase |

## EEPROM Layout

- Byte 0: Magic (0xAB)
- Byte 1: XOR checksum
- Bytes 2+: 3 × GestureBins (180 bytes each = 540 total)
- Total: 542 / 1024 bytes

## Memory Usage

- Flash: ~84% (24K / 28K)
- RAM: ~63% (1610 / 2560 bytes)
- DTW computation uses ~488 bytes stack (temporary)

## What Still Needs Work

1. **Gravity subtraction** — subtract 8192 from magnitude before normalization for better orientation invariance
2. **Threshold tuning** — collect more data, find optimal value
3. **Video demo** — required for grading
4. **Code comments** — add inline documentation for grading

## Project Requirements Compliance

- ✅ PlatformIO
- ✅ Register-level GPIO for buttons and LED (DDRx, PORTx, PINx)
- ✅ Onboard accelerometer only
- ✅ Adafruit library for sensor communication (allowed by professor)
- ✅ CircuitPlayground library for NeoPixels (same category as sensor library — HAL/driver)
- ✅ EEPROM storage survives power cycles
- ✅ LED feedback for all states
- ✅ 3-gesture sequence record and replay
