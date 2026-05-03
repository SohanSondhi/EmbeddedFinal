#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

#include <avr/io.h>

// Variables used for pin configuration and state tracking for buttons, LEDs, NeoPixels, and other hardware components. These variables are defined here for easy reference and modification throughout the codebase.

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

//SPI to LIS3DH
//CS is on D8
//Device ID is 0x33
#define READ_REG    0x80 
#define INC_REG     0x40 
#define AX_REG      0x28 
#define CFG1_REG    0x20 // Control register 1: used to turn on the accelerometer and enable the X, Y, and Z axes
#define CFG4_REG    0x23 // Control register 4: used to set the accelerometer range to +/- 4g
#define WHO_AM_I    0x0F 

// LIS3DH accelerometer chip select (CS) pin for SPI communication (D8 = PB4)
#define ACCEL_CS_DDR    DDRB
#define ACCEL_CS_PORT   PORTB
#define ACCEL_CS_BIT    PB4

// Gesture config
#define NUM_GESTURES    3 // Number of gestures in the combination
#define SAMPLE_RATE_MS  20 // Time between accelerometer samples during gesture capture (in milliseconds)
#define MAX_SAMPLES     100 // Maximum number of accelerometer samples to capture for each gesture to prevent overflow of the capture buffer (should be enough for 2 seconds of motion at 50Hz, which is longer than typical gesture duration)

// DTW parameters
#define DTW_SAMPLES     30 // Number of samples to downsample the raw gesture data to for DTW comparison 
#define DTW_BAND        8 // Band width for DTW implementation to optimize the DTW calculation while still allowing for some variation in gesture speed

// Motion detection
#define STILLNESS_MS    500 // Time in milliseconds to determine if the user has stopped moving during gesture capture (used to detect the end of a gesture)
#define MIN_SAMPLES     10 // Minimum number of samples required for a valid gesture capture to prevent very short captures from being considered valid gestures (e.g. accidental taps or noise)
#define RAW_REST_MAG    8192.0f // The expected raw magnitude of the accelerometer readings when at rest
#define RAW_MOTION_THR  1500.0f // The threshold above the raw rest magnitude that we consider to indicate motion 

// Normalized magnitude DTW threshold
// After normalization, values are in standard deviations.
#define MATCH_THRESHOLD 0.50f

// Retry config
#define MAX_RETRIES     3 // attempts per gesture before total fail

// Timing
#define GESTURE_TIMEOUT_MS  30000 // Maximum time allowed for the user to complete the entire gesture combination before we reset back to idle and require them to start over (in milliseconds)
#define DEBOUNCE_MS         80 // Time in milliseconds to debounce button presses to prevent multiple triggers from a single press
#define COUNTDOWN_MS        2000 // Time in milliseconds for the countdown animation before starting to capture a gesture for both recording and unlocking
#define ERASE_HOLD_MS       3000 // Time in milliseconds that the left button must be held down to trigger a key erase when in the idle state

#endif