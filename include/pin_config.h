#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

#include <avr/io.h>

// =============================================================
// KinetiKey V2 — Pin Configuration & Constants
// Board: Adafruit Circuit Playground Classic (ATmega32u4 @ 16MHz)
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
// Gesture Configuration
// =============================================================

#define NUM_GESTURES        3
#define SAMPLE_RATE_MS      20       // 50Hz
#define MAX_SAMPLES         100      // 2 seconds max per gesture
#define NUM_BINS            16       // V2: more bins = more detail

// =============================================================
// Motion Detection
// =============================================================
#define MOTION_UPPER        13.0f
#define MOTION_LOWER        6.5f

// How long still before gesture is "done" (ms)
#define STILLNESS_MS        300

// Minimum samples for a valid gesture
#define MIN_SAMPLES         10

// =============================================================
// Matching Threshold — V2 TUNING
//
// With V2 normalization (global energy, not per-axis),
// correct gestures should be ~0.3-0.8
// wrong gestures should be ~1.0-2.0+
//
// Start at 1.0, adjust after testing.
// Serial prints distance for every attempt.
// =============================================================
#define MATCH_THRESHOLD     1.0f

// =============================================================
// Timing
// =============================================================
#define GESTURE_TIMEOUT_MS  30000    // 30s to start a gesture
#define DEBOUNCE_MS         80
#define SETTLE_MS           1500     // Time between gestures to reposition

// Countdown before listening for gesture (ms)
#define COUNTDOWN_MS        2000     // "Get ready" period

#endif // PIN_CONFIG_H