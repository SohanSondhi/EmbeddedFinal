#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

#include <avr/io.h>

// =============================================================
// KinetiKey — Pin Configuration & Constants
// Board: Adafruit Circuit Playground Classic (ATmega32u4 @ 16MHz)
//
// IMPORTANT: On CP Classic, buttons are ACTIVE HIGH (external
// pull-down resistors on board). Pressed = HIGH, Released = LOW.
// =============================================================

// ----- Left Button (Arduino D4 = ATmega PD4) -----
#define BTN_LEFT_DDR    DDRD
#define BTN_LEFT_PORT   PORTD
#define BTN_LEFT_PIN    PIND
#define BTN_LEFT_BIT    PD4

// ----- Right Button (Arduino D19/A1 = ATmega PF6) -----
#define BTN_RIGHT_DDR   DDRF
#define BTN_RIGHT_PORT  PORTF
#define BTN_RIGHT_PIN   PINF
#define BTN_RIGHT_BIT   PF6

// ----- Slide Switch (Arduino D21/A3 = ATmega PF4) -----
#define SWITCH_DDR      DDRF
#define SWITCH_PORT     PORTF
#define SWITCH_PIN      PINF
#define SWITCH_BIT      PF4

// ----- NeoPixel Data (Arduino D8 = ATmega PB4) -----
#define NEO_DDR         DDRB
#define NEO_PORT        PORTB
#define NEO_BIT         PB4

// ----- Onboard Red LED (Arduino D13 = ATmega PC7) -----
#define LED_DDR         DDRC
#define LED_PORT        PORTC
#define LED_BIT         PC7

// ----- NeoPixel Count -----
#define NUM_PIXELS      10

// =============================================================
// Gesture Configuration — TUNE THESE ON THE ACTUAL BOARD
// =============================================================

// How many gestures form one "password"
#define NUM_GESTURES        3

// Accelerometer sampling interval (ms). 20ms = 50Hz.
#define SAMPLE_RATE_MS      20

// Max raw samples per gesture. 100 samples @ 50Hz = 2 seconds max.
// Drawing a number in the air takes ~1-2 seconds.
#define MAX_SAMPLES         100

// Number of time bins for feature extraction.
// The raw samples get compressed into this many bins per axis.
// More bins = more detail but more storage. 10 is a good balance.
#define NUM_BINS            10

// Features per gesture = NUM_BINS * 3 axes = 30 floats = 120 bytes
// Total for 3 gestures = 360 bytes (fits in 1KB EEPROM)

// =============================================================
// Motion Detection Thresholds
// =============================================================

// Acceleration magnitude at rest is ~9.8 m/s² (gravity).
// If magnitude deviates beyond these bounds, we consider it "moving".
#define MOTION_UPPER        13.0f   // m/s² — above this = moving
#define MOTION_LOWER        6.5f    // m/s² — below this = moving

// How long (ms) of stillness before a gesture is considered "done"
#define STILLNESS_MS        500

// Minimum number of samples for a valid gesture (reject accidental taps)
#define MIN_SAMPLES         10

// =============================================================
// Gesture Matching Threshold
// =============================================================
// This is the MOST IMPORTANT tunable. It's the max allowed distance
// between a recorded gesture and an unlock attempt.
//
// Lower = stricter (fewer false positives, more false negatives)
// Higher = looser  (more false positives, fewer false negatives)
//
// START with 2.0, then adjust based on testing:
//   - If it never unlocks even with correct gestures: increase
//   - If it unlocks with wrong gestures: decrease
//
// Monitor Serial output — it prints the distance for every attempt.
#define MATCH_THRESHOLD     2.0f

// =============================================================
// Timing
// =============================================================

// How long to wait for user to start a gesture before timeout (ms)
#define GESTURE_TIMEOUT_MS  8000

// Debounce delay for buttons (ms)
#define DEBOUNCE_MS         80

// Post-button-press delay before listening for motion (ms)
// Prevents the button press itself from being detected as motion
#define SETTLE_MS           500

#endif // PIN_CONFIG_H
