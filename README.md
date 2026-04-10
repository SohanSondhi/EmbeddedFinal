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
6. Green sweep = **key saved to EEPROM** (survives power cycles)

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

We use **Dynamic Time Warping (DTW)** on **normalized acceleration magnitude** — a proven approach for accelerometer-based gesture recognition on embedded systems.

### Pipeline

1. **Capture** raw XYZ acceleration at 50Hz (up to 2 seconds)
2. **Downsample** to 30 evenly-spaced points
3. **Compute magnitude** for each point: `√(x² + y² + z²)`
4. **Normalize** the magnitude sequence (zero mean, unit standard deviation)
5. **Compare** using banded DTW (Sakoe-Chiba band = 8)
6. If average DTW cost < threshold → **match**

### Why DTW + Magnitude?

- **DTW handles timing variation**: Drawing the same gesture faster or slower still matches, because DTW warps the time axis to find optimal alignment
- **Magnitude is orientation-invariant**: The scalar `√(x² + y² + z²)` doesn't depend on which way the axes point, so different grip angles produce the same value
- **Normalization removes scale**: Big and small versions of the same gesture produce the same normalized profile
- **Memory-efficient**: Rolling 2-row DTW uses only 248 bytes instead of the full 30×30 matrix (3,600 bytes)

### Recommended Gestures

Use physically distinct motions for best discrimination:
- **Punch** — single forward thrust (one sharp spike)
- **Shake** — quick left-right waggle (two alternating spikes)
- **Circle** — smooth circular motion (sustained sinusoidal pattern)

Avoid drawing numbers or letters in the air — the acceleration profiles are too similar between different characters.

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

# Port may vary — check: ls /dev/cu.usbmodem*

# Serial monitor (9600 baud)
pio device monitor
```

## Project Structure

```
├── platformio.ini              # Board config, CircuitPlayground library
├── README.md                   # This file
├── CLAUDE.md                   # AI-assisted development context
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
- **Storage**: 3 gesture templates in ATmega32u4's 1KB EEPROM with magic byte + XOR checksum (542 bytes total)
- **Memory**: ~84% flash, ~63% RAM

## LED Feedback

| Pattern | Meaning |
|---------|---------|
| Cyan sweep | Boot |
| Two blue dots | Idle |
| Purple countdown → flash | Get ready → GO (record) |
| Yellow countdown → flash | Get ready → GO (unlock) |
| White spinning dot | Capturing |
| Purple progress (3 LEDs per gesture) | Recording progress |
| Green progress | Unlock progress |
| Green chase → solid → fade | UNLOCKED |
| Red flash ×5 | FAILED (all retries used) |
| Orange blink | Retry / too short / timeout |
| Red fill (progressive) | Erase countdown |

---

## Version History

### V1 — Raw Acceleration Bins
Binned raw XYZ into 10 time slots, normalized each axis independently. **Failed**: per-axis normalization destroyed inter-axis relationships — every gesture looked identical after processing.

### V2 — Global Energy Normalization
Same binning but normalized all axes by a single shared energy value. **Worked** in controlled tests (correct: 0.6–0.75, wrong: 1.35+), but completely orientation-dependent — different grip angle = different axis values = no match.

### V3 — Position Tracking (Double Integration)
Integrated acceleration twice to reconstruct hand position path. **Failed**: double integration amplified tiny errors into wildly divergent trajectories.

### V4 — Velocity Tracking (Single Integration)
Single integration to velocity. Less drift than V3 but still orientation-dependent. Same fundamental problem as V2.

### V5 — Orientation-Invariant Scalar Features
Used energy (`|accel| - gravity`) and jerk (`|Δaccel|`) as orientation-invariant scalars. Fixed-bin comparison (not DTW). **Partially worked** but inconsistent — similar gestures produced overlapping distance ranges. Threshold was borderline.

### V7 — DTW with Direction-Aware Cost
Switched from fixed bins to Dynamic Time Warping (inspired by [CieloStrive's DTW project](https://github.com/CieloStrive/GESTURE-RECOGNITION-DYNAMIC-TIME-WARPING) for the same course). Used L2 distance with cosine-similarity weighting. **Worked** but still orientation-dependent because it compared raw XYZ vectors.

### V7.1 — DTW with Magnitude-Only Cost
Replaced direction-aware cost with pure magnitude comparison (`|accel_a| vs |accel_b|`). Fixed orientation dependency but size-dependent — drawing the same gesture bigger or smaller changed the magnitude scale.

### V7.2 — DTW on Normalized Magnitude (Current)
Added zero-mean unit-variance normalization to the magnitude sequence before DTW. This made comparison invariant to orientation, gesture size, and timing simultaneously. Also added retry logic (3 attempts per gesture, resume from failed gesture). Switched NeoPixels to CircuitPlayground library, which finally resolved the persistent LED issue.

### NeoPixel Journey
Pin 8 is shared between NeoPixel data and LIS3DH SPI CS. Attempted hand-written 8MHz assembly bit-bang (multiple timing variations), `Adafruit_NeoPixel` library with manual SPI disable, and standalone `Adafruit_NeoPixel` before SPI initialization. None worked. Final solution: `Adafruit_CircuitPlayground` library, which is the official library for this exact board and handles the level shifter, SPI conflicts, and timing internally.

---

## Future Improvements

### Gravity Subtraction
Currently, magnitude = `√(x² + y² + z²)` includes gravity (~8192 raw units). When the board tilts, gravity redistributes across axes but the total magnitude stays ~8192. However, during motion, the gravity component shifts the magnitude baseline depending on orientation. Subtracting rest magnitude before normalization (`mag - 8192` or better, tracking a rolling gravity estimate) would improve orientation invariance for extreme angle changes.

### Adaptive Threshold
Currently `MATCH_THRESHOLD` is a fixed constant (0.7). A smarter approach: during recording, capture two copies of each gesture (template + verification), compute their DTW distance as a "self-distance", and set the threshold per-gesture as `self_distance × multiplier`. This accounts for gestures that are inherently harder to reproduce consistently.

### More DTW Features
Instead of magnitude alone, combine multiple orientation-invariant features in the DTW cost function: magnitude, jerk (|Δaccel|), and zero-crossing rate. Weight them to improve discrimination between gestures with similar force profiles but different directional patterns.

### Gyroscope Integration
If a board with a gyroscope (e.g., MPU6050) were available, rotational data would dramatically improve gesture discrimination. Rotation is already orientation-invariant and captures directional information that magnitude alone loses.

### Continuous Gesture Segmentation
Currently the user must hold still for 0.5s to end a gesture. More advanced: detect gesture boundaries automatically using energy thresholds and sliding windows, allowing seamless back-to-back gesture input.

### Security Hardening
- Add a lockout timer after all retries exhausted (e.g., 30 second cooldown)
- Encrypt EEPROM storage (currently plaintext — someone could read templates via ISP)
- Add a "panic erase" gesture that wipes the key instantly

---

## For Teammates — Testing Needed!

**Please test extensively and open GitHub Issues for any problems you find.**

### What to Test

1. **Record + Unlock (same grip)**: Record punch → shake → circle. Unlock with same gestures. Should pass consistently.

2. **Wrong gestures**: Record punch → shake → circle. Try to unlock with completely different gestures (e.g., tap → wave → jab). Should fail.

3. **Orientation tolerance**: Record a gesture normally. Try unlocking with the board tilted ~20° differently. Should still pass for small angle changes. Report the angle at which it starts failing.

4. **Speed variation**: Record a gesture at normal speed. Unlock with the same gesture done faster or slower. Should pass (DTW handles this).

5. **Power cycle**: Record a key, unplug the board, plug back in. Key should still be there (EEPROM).

6. **Erase**: Hold LEFT for 3 seconds. Key should be erased. Confirm by pressing RIGHT (should say "No key").

7. **Retry logic**: During unlock, intentionally fail a gesture. Confirm you get 3 attempts per gesture and it resumes from the failed one, not from gesture 1.

8. **NeoPixel feedback**: Verify all LED patterns match the table above. Report any state where LEDs don't match expected behavior.

9. **Edge cases**: What happens if you don't move at all? What if you shake during countdown? What if you press both buttons during capture?

### How to Report Issues

Open a GitHub Issue with:
- What you did (step by step)
- What you expected
- What actually happened
- Serial monitor output (copy-paste the relevant section)
- The `dist=X.XXX thr=0.700` values if it's a matching issue

### Serial Monitor

Always run `pio device monitor` while testing. It shows:
- Raw accelerometer data per gesture
- Normalized magnitude profiles
- DTW distance values and match/no-match decisions
- Retry count

These values are critical for debugging. **Always include them in issue reports.**
