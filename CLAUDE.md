# CLAUDE.md — KinetiKey Project Context

**Read this first before doing anything. This is the full project history and current state.**

---

## Project Overview

**KinetiKey** — "Old Lock, New Twist"
Embedded Challenge Spring 2026 term project.

A gesture-based combination lock. User holds the board in their fist, performs 3 gestures to record a "key", then replicates the same 3 gestures to unlock. Like a combination padlock but with physical motions instead of numbers.

## Hardware

- **Board**: Adafruit Circuit Playground Classic
- **MCU**: ATmega32u4 @ **8MHz** (NOT 16MHz — this matters for NeoPixel timing)
- **Accelerometer**: LIS3DH, connected via **SPI** (NOT I2C), CS = pin 8
- **NeoPixels**: 10x WS2812B on pin 8 (**shared with LIS3DH SPI CS** — this is the root of the NeoPixel bug)
- **Buttons**: Left (#4, PD4), Right (#19, PF6) — **ACTIVE HIGH** (board has external pull-downs)
- **Red LED**: #13 (PC7)
- **No external wiring** — everything is onboard

## Project Requirements (from professor)

- Record a 3-gesture sequence using accelerometer data
- Store sequence on microcontroller (EEPROM)
- User replicates gestures within tolerances to unlock
- Each successful gesture indicated by LEDs
- Successful unlock shown by visual indicator
- Must use PlatformIO
- Only the onboard accelerometer allowed
- **GPIO must use register-level access** (no `digitalWrite`, `digitalRead`, `pinMode`)
- Allowed to use Adafruit LIS3DH library for sensor communication
- Allowed to use Arduino framework functions (`millis()`, `Serial`, `SPI`)

## Team

- GitHub repo: `SohanSondhi/EmbeddedFinal`
- Branch workflow: feature branches → PR → merge to main
- Sichen (SichenLiang) is primary developer
- Two other teammates

## Build & Upload

```bash
cd ~/EmbeddedFinal
pio run

# Upload often fails with auto-reset. Manual method:
# 1. Double-click RESET button on board (red LED pulses = bootloader mode)
# 2. Immediately run:
~/.platformio/packages/tool-avrdude/bin/avrdude \
  -p atmega32u4 -c avr109 \
  -C ~/.platformio/packages/tool-avrdude/avrdude.conf \
  -P /dev/cu.usbmodem1101 \
  -U flash:w:.pio/build/cpclassic/firmware.hex:i

# Serial monitor:
pio device monitor
```

The `platformio.ini` uses `board = circuitplay_classic` (NOT `adafruit_circuitplayground_classic` which doesn't exist in PlatformIO).

---

## File Structure

```
├── platformio.ini
├── README.md
├── CLAUDE.md              ← YOU ARE HERE
├── include/
│   ├── pin_config.h       ← All tunable constants, pin definitions
│   ├── gpio_reg.h         ← Register-level GPIO, NeoPixel bit-bang, LED animations
│   ├── gesture.h          ← Gesture capture, feature extraction, matching algorithm
│   └── storage.h          ← EEPROM save/load with magic number + checksum
└── src/
    └── main.cpp           ← State machine, button handling, record/unlock flow
```

## Current State (as of latest push)

### What Works
- **LIS3DH accelerometer**: Reading via SPI using Adafruit library ✅
- **Buttons**: Register-level reading, debounced, active-high ✅
- **Red LED (#13)**: Register-level toggle ✅
- **State machine**: Full record/unlock/erase/cancel flow ✅
- **EEPROM storage**: Save/load with checksum, survives power cycles ✅
- **Gesture capture**: Raw sample recording at 50Hz ✅
- **Serial debug output**: Raw data CSV, feature vectors, distance values ✅
- **Long-press erase**: Hold left button 3 seconds to erase key ✅
- **Cancel**: Both buttons to cancel any operation ✅

### What Does NOT Work — KNOWN BUGS

#### 1. NeoPixels DO NOT LIGHT UP (Critical)
**Only the #13 red LED works. All 10 NeoPixels remain dark.**

Root cause: Pin 8 (PB4) is shared between NeoPixel data line and LIS3DH SPI chip select. When the SPI peripheral is active, it interferes with bit-banging NeoPixel data on the same pin.

**What we tried:**
- 16MHz NeoPixel timing → wrong, board is 8MHz
- 8MHz timing with various NOP counts → still no light
- Adafruit-style 8MHz assembly (sbrc/rjmp trick, 10 cycles/bit) → still no light
- Disabling SPI (`SPCR = 0`) before NeoPixel transmission, re-enabling after → still no light

**What to try next:**
- Look at the actual Adafruit CircuitPlayground library source code to see exactly how they handle the shared pin 8 between NeoPixels and LIS3DH SPI
- Possibly need to fully deinit SPI, not just clear SPCR (may need to reset pin directions for MOSI/SCK/MISO)
- Possibly need to use the Adafruit_CPlay_NeoPixel library directly (but this uses `digitalWrite` internally which violates GPIO register requirement — may need to extract their approach)
- Consider using `Adafruit_NeoPixel` library for just the NeoPixel communication (it does its own bit-bang with proven timing) and argue that it's a "driver" like the LIS3DH library

#### 2. Gesture Recognition — Accuracy Inconsistent
**The matching algorithm works in concept but is fragile in practice.**

**Algorithm versions attempted:**

1. **V1 — Raw acceleration binning** (first attempt)
   - Binned raw XYZ acceleration into 10 time bins
   - Per-axis normalization (zero mean, unit variance)
   - RESULT: Per-axis normalization destroyed inter-axis relationships. All gestures looked identical after normalization. Random circles matched as well as correct gestures.

2. **V2 — Global energy normalization** (fixed V1's normalization)
   - Same binning but normalize all axes by ONE shared energy value
   - Preserves axis ratios (shape)
   - RESULT: Worked! Correct gestures: distance 0.6-0.75. Wrong gestures: 1.35+. Threshold 1.0 separated them cleanly.
   - PROBLEM: Only worked when user held board in same orientation. Different grip = different XYZ values = no match, even for same gesture.

3. **V3 — Position tracking (double integration)**
   - Accel → velocity → position via double integration
   - Detrending to remove drift
   - RESULT: Double integration amplified small errors. Same gesture produced wildly different position paths. Inconsistent.

4. **V4 — Velocity tracking (single integration)**
   - Accel → velocity via single integration
   - Detrending + interpolation resampling
   - RESULT: Still orientation-dependent. Same fundamental problem as V2 — different grip orientation = different axis values.

5. **V5 — Orientation-invariant features (CURRENT)**
   - Uses scalar features that don't depend on which way axes point:
     - **Energy**: |accel| - gravity (how hard you're moving)
     - **Jerk**: |accel[i] - accel[i-1]| (how fast direction changes)
   - Trims stationary tails, only processes active motion
   - Interpolation resampling to 20 points
   - Per-feature normalization to [0, 1]
   - RESULT: Most promising so far. Correct gesture 1 matched at 0.203 distance. But still inconsistent — same gesture sometimes 0.2, sometimes 0.6. Threshold at 0.45 is borderline.

**Current threshold**: `MATCH_THRESHOLD 0.45f` in `pin_config.h`

**Recommendation for next session:**
- Increase threshold to 0.55 for more forgiveness
- Test with **simple, distinct gestures** (punch forward, shake left-right, knock up-down) instead of drawing numbers — drawing produces very inconsistent acceleration patterns
- Consider combining V5 features with additional features like total gesture duration, number of zero-crossings, dominant frequency
- Consider using DTW (Dynamic Time Warping) for comparison instead of Euclidean distance — DTW handles timing variations much better but is memory-intensive

### Controls

| Action | How |
|--------|-----|
| Record new key | Short press LEFT button |
| Unlock attempt | Press RIGHT button |
| Erase stored key | Hold LEFT button for 3 seconds (red LEDs should fill up as warning) |
| Cancel operation | Hold BOTH buttons simultaneously |

### LED Behavior (intended, but NeoPixels don't work yet)

| Pattern | Meaning |
|---------|---------|
| Two blue dots | Idle |
| Purple countdown → flash | Get ready → GO (record) |
| Yellow countdown → flash | Get ready → GO (unlock) |
| White spinning | Capturing gesture |
| Purple progress (3 LEDs per gesture) | Recording progress |
| Green progress | Unlock progress |
| Green chase → solid 3s | UNLOCKED |
| Red flash ×5 | FAILED |
| Orange flash ×2 | No key / timeout |
| Red progressive fill | Erase countdown |

Currently only the #13 red LED blinks for fail/success.

### Serial Monitor Output

At 9600 baud, shows:
- State transitions
- Raw accelerometer CSV data per gesture (can paste into visualizer)
- Feature vectors (energy + jerk values at each resampled point)
- Distance values and match/no-match results
- Active region detection (trimmed samples)

### Key Constants to Tune (in pin_config.h)

- `MATCH_THRESHOLD` — Most important. Currently 0.45. Higher = more forgiving.
- `STILLNESS_MS` — 600ms. How long still before gesture ends.
- `RAW_MOTION_THR` — 1500. Motion detection sensitivity.
- `NUM_PATH_PTS` — 20. Feature resolution.
- `ERASE_HOLD_MS` — 3000. Long-press duration for erase.

### Gesture Visualizer

A React JSX file (`gesture_visualizer.jsx`) exists that can plot gesture data. Paste Serial Monitor output to see XYZ acceleration curves. Note: this shows raw acceleration, not the processed features.

---

## What Needs to Be Done

1. **Fix NeoPixels** — The #1 priority. Without visual feedback, the project loses significant grading points (LED feedback is explicitly required). Study the Adafruit CircuitPlayground library source to understand how they handle the pin 8 sharing.

2. **Stabilize gesture matching** — Test with simple gestures (punch, shake, knock), tune threshold. May need to add more features or switch to DTW.

3. **Video demo** — Required for grading. Should show: record 3 gestures, successful unlock, failed unlock with wrong gestures.

4. **Code cleanup** — Comments, remove debug prints for final version, ensure all GPIO is register-level.

5. **Push final version** — Commit and merge to main.
