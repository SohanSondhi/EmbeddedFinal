#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

#include <avr/io.h>

#define BTN_LEFT_DDR    DDRD
#define BTN_LEFT_PORT   PORTD
#define BTN_LEFT_PIN    PIND
#define BTN_LEFT_BIT    PD4

#define BTN_RIGHT_DDR   DDRF
#define BTN_RIGHT_PORT  PORTF
#define BTN_RIGHT_PIN   PINF
#define BTN_RIGHT_BIT   PF6

#define SWITCH_DDR      DDRF
#define SWITCH_PORT     PORTF
#define SWITCH_PIN      PINF
#define SWITCH_BIT      PF4

#define NEO_DDR         DDRB
#define NEO_PORT        PORTB
#define NEO_BIT         PB4

#define LED_DDR         DDRC
#define LED_PORT        PORTC
#define LED_BIT         PC7

#define NUM_PIXELS      10
#define NUM_GESTURES    3
#define SAMPLE_RATE_MS  20
#define MAX_SAMPLES     100
#define NUM_PATH_PTS    20

#define STILLNESS_MS    600
#define MIN_SAMPLES     10

// V5: orientation-invariant features
// Correct gestures should be ~0.1-0.3, wrong ~0.5+
// Start at 0.45, tune after testing
#define MATCH_THRESHOLD 0.55f

#define GESTURE_TIMEOUT_MS  30000
#define DEBOUNCE_MS         80
#define SETTLE_MS           1500
#define COUNTDOWN_MS        2000
#define ERASE_HOLD_MS       3000

#define RAW_REST_MAG    8192.0f
#define RAW_MOTION_THR  1500.0f

#endif