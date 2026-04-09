# KinetiKey — "Old Lock, New Twist"

**Embedded Challenge Spring 2026**

A gesture-based combination lock using an accelerometer. Hold the board in your fist, perform three gestures to set a key, then replicate them to unlock.

---

## Hardware

- **Adafruit Circuit Playground Classic** (ATmega32u4 @ 8MHz)
- Onboard LIS3DH accelerometer (SPI)
- Onboard 10× NeoPixel ring + red LED (#13)
- Onboard 2× push buttons
- No external components required

## How to Use

### Record a Key
1. **Short press LEFT button** → Record mode starts
2. LED countdown (2 seconds) → flash = GO
3. Hold board in fist, **perform gesture 1** (e.g., punch forward)
4. **Hold still ~0.6 seconds** → system detects gesture end
5. Progress LEDs light up, countdown for next gesture
6. Repeat for gesture 2 and 3
7. Green sweep = **key saved to EEPROM** (survives power off)

### Unlock
1. **Press RIGHT button** → Unlock mode
2. Replicate the same 3 gestures in order
3. Each correct gesture → green progress LEDs
4. All 3 correct → **green chase animation = UNLOCKED**
5. Any wrong gesture → **red flash = LOCKED**

### Other Controls
| Action | How |
|--------|-----|
| Record new key | Short press LEFT button |
| Unlock | Press RIGHT button |
| Erase stored key | Hold LEFT button 3 seconds (red LEDs fill as warning) |
| Cancel operation | Hold BOTH buttons |

### Recommended Gestures
Drawing numbers in the air produces inconsistent acceleration patterns. Use **distinct physical motions** instead:
- **Punch** — thrust forward and back
- **Shake** — quick left-right waggle
- **Knock** — up-down taps

These produce very different energy signatures and are easy to repeat consistently.

---

## Build & Flash

```bash
# Compile
pio run

# Upload (manual bootloader method — most reliable):
# 1. Double-click RESET button on board
# 2. Red LED starts pulsing = bootloader mode
# 3. Immediately run:
~/.platformio/packages/tool-avrdude/bin/avrdude \
  -p atmega32u4 -c avr109 \
  -C ~/.platformio/packages/tool-avrdude/avrdude.conf \
  -P /dev/cu.usbmodem1101 \
  -U flash:w:.pio/build/cpclassic/firmware.hex:i

# Serial monitor (9600 baud)
pio device monitor
```

---

## Development Journey

### V1 — Raw Acceleration Bins
Binned raw X/Y/Z acceleration into 10 time slices, normalized per-axis. **Failed**: per-axis normalization destroyed the relationships between axes — every gesture looked the same after processing.

### V2 — Global Energy Normalization
Fixed normalization by dividing all axes by a single shared energy value, preserving shape. **Worked** when tested with consistent grip orientation (correct: 0.6–0.75, wrong: 1.35+, threshold: 1.0). **Failed** when grip orientation varied between recording and unlocking — same gesture in a different hand position produced completely different X/Y/Z values.

### V3 — Position Path Tracking
Double-integrated acceleration → velocity → position to reconstruct the actual hand path. **Failed**: double integration amplified tiny starting differences into wildly divergent position trajectories. Same gesture produced inconsistent paths.

### V4 — Velocity Trajectory
Single integration (acceleration → velocity). Less drift than position, more meaningful than raw acceleration. **Partially worked** but still orientation-dependent. Same root problem as V2.

### V5 — Orientation-Invariant Features (Current)
Abandoned per-axis comparisons entirely. Uses two scalar features that are independent of how the board is held:
1. **Energy** = magnitude of acceleration minus gravity (how hard you're moving)
2. **Jerk** = magnitude of acceleration change between samples (how quickly you change direction)

Includes automatic trimming of stationary tails, interpolation resampling to 20 points, and per-feature normalization.

**Current results**: correct gestures ≈ 0.2–0.4, wrong gestures ≈ 0.5+. Threshold at 0.45. Best discrimination so far, but still requires tuning and consistent gesture technique.

---

## Known Issues

### NeoPixels Not Working
Pin 8 (PB4) is shared between the NeoPixel data line and LIS3DH SPI chip select. Despite multiple attempts (16MHz timing, 8MHz timing, SPI disable before transmission), the NeoPixels do not light. Only the red LED (#13) provides visual feedback currently. The Adafruit CircuitPlayground library's handling of this shared pin needs to be studied for a proper fix.

### Gesture Matching Fragility
The algorithm is sensitive to how consistently you perform gestures. Simple, distinct motions (punch, shake, knock) work much better than complex drawings (numbers in the air). Threshold tuning is ongoing.

---

## Project Structure

```
├── platformio.ini              # circuitplay_classic, Adafruit LIS3DH lib
├── README.md                   # This file
├── CLAUDE.md                   # Detailed context for AI-assisted development
├── include/
│   ├── pin_config.h            # Pin mappings, thresholds, timing constants
│   ├── gpio_reg.h              # Register-level GPIO, NeoPixel driver, animations
│   ├── gesture.h               # V5 orientation-invariant gesture matching
│   └── storage.h               # EEPROM save/load with checksum
└── src/
    └── main.cpp                # State machine, countdown, record/unlock flow
```

## Technical Notes

- **All GPIO uses direct register access** (`DDRx`, `PORTx`, `PINx`). No `digitalWrite`, `digitalRead`, or `pinMode` in our code.
- NeoPixel driver is bit-banged via inline AVR assembly (8MHz timing).
- Accelerometer communication uses the Adafruit LIS3DH library over SPI.
- Gesture comparison uses orientation-invariant scalar features (energy + jerk).
- Key stored in ATmega32u4's 1KB EEPROM with magic number and XOR checksum.
- Serial output at 9600 baud provides raw data dumps and distance values for debugging.

## What Still Needs Work

1. **Fix NeoPixels** — Study Adafruit CircuitPlayground library source for pin 8 handling
2. **Tune gesture threshold** — Test with simple gestures, find optimal MATCH_THRESHOLD
3. **Video demo** — Record successful record/unlock/fail sequence
4. **Code cleanup** — Final comments and polish
