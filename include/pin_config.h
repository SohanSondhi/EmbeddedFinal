#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

#include <avr/io.h>

// Left Button (D4 = PD4) — ACTIVE HIGH
#define BTN_LEFT_DDR    DDRD
#define BTN_LEFT_PORT   PORTD
#define BTN_LEFT_PIN    PIND
#define BTN_LEFT_BIT    PD4

// Right Button (D19 = PF6) — ACTIVE HIGH
#define BTN_RIGHT_DDR   DDRF
#define BTN_RIGHT_PORT  PORTF
#define BTN_RIGHT_PIN   PINF
#define BTN_RIGHT_BIT   PF6

// Red LED (D13 = PC7)
#define LED_DDR         DDRC
#define LED_PORT        PORTC
#define LED_BIT         PC7

// NeoPixel / Accel CS shared pin
#define NEO_PIN         8
#define NUM_PIXELS      10

// Gesture config
#define NUM_GESTURES    3
#define SAMPLE_RATE_MS  20
#define MAX_SAMPLES     100

// DTW parameters
#define DTW_SAMPLES     30
#define DTW_BAND        8

// Motion detection
#define STILLNESS_MS    500
#define MIN_SAMPLES     10
#define RAW_REST_MAG    8192.0f
#define RAW_MOTION_THR  1500.0f

// Normalized magnitude DTW threshold
// After normalization, values are in standard deviations.
// Correct gestures should be ~0.2-0.5, wrong should be ~0.8+
// Start at 0.7, tune after testing.
#define MATCH_THRESHOLD 0.65f

// Retry config
#define MAX_RETRIES     3           // attempts per gesture before total fail

// Timing
#define GESTURE_TIMEOUT_MS  30000
#define DEBOUNCE_MS         80
#define COUNTDOWN_MS        2000
#define ERASE_HOLD_MS       3000

#endif