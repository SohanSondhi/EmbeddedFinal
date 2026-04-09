#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

#include <avr/io.h>

// =============================================================
// KinetiKey Pin Configuration
// Board: Adafruit Circuit Playground Classic (ATmega32u4)
// =============================================================

// --- Left Button (Arduino D4 = PD4) ---
#define BTN_LEFT_DDR    DDRD
#define BTN_LEFT_PORT   PORTD
#define BTN_LEFT_PIN    PIND
#define BTN_LEFT_BIT    PD4

// --- Right Button (Arduino D19 = PF6) ---
#define BTN_RIGHT_DDR   DDRF
#define BTN_RIGHT_PORT  PORTF
#define BTN_RIGHT_PIN   PINF
#define BTN_RIGHT_BIT   PF6

// --- Slide Switch (Arduino D21 = PF4) ---
#define SWITCH_DDR      DDRF
#define SWITCH_PORT     PORTF
#define SWITCH_PIN      PINF
#define SWITCH_BIT      PF4

// --- NeoPixel Data (Arduino D8 = PB4) ---
#define NEO_DDR         DDRB
#define NEO_PORT        PORTB
#define NEO_BIT         PB4

// --- Onboard Red LED (Arduino D13 = PC7) ---
#define LED_DDR         DDRC
#define LED_PORT        PORTC
#define LED_BIT         PC7

// --- NeoPixel Config ---
#define NUM_PIXELS      10

// --- Gesture Config ---
#define NUM_GESTURES    3           // 3-gesture combo
#define SAMPLE_RATE_MS  20          // 50Hz sampling
#define MAX_SAMPLES     75          // max samples per gesture (1.5s)
#define GESTURE_TIMEOUT_MS 2000     // timeout waiting for gesture

// --- Matching Threshold ---
// Lower = stricter, Higher = more forgiving
// Tune this during testing
#define MATCH_THRESHOLD 1.5f

#endif // PIN_CONFIG_H
