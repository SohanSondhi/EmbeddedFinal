#ifndef GPIO_REG_H
#define GPIO_REG_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <Adafruit_CircuitPlayground.h>
#include "pin_config.h"

// Color constants (packed RGB for NeoPixel library)
#define COLOR_OFF     0x000000
#define COLOR_RED     0x960000
#define COLOR_GREEN   0x009600
#define COLOR_BLUE    0x000096
#define COLOR_YELLOW  0x966400
#define COLOR_PURPLE  0x500096
#define COLOR_CYAN    0x006464
#define COLOR_WHITE   0x646464
#define COLOR_ORANGE  0x962800

// All pin variables are listed in pin_config.h for easy reference and modification
static void gpio_init(void) {
    // Buttons: input, no pull-up (board has pull-downs)
    BTN_LEFT_DDR  &= ~(1 << BTN_LEFT_BIT);
    BTN_LEFT_PORT &= ~(1 << BTN_LEFT_BIT);
    BTN_RIGHT_DDR &= ~(1 << BTN_RIGHT_BIT);
    BTN_RIGHT_PORT&= ~(1 << BTN_RIGHT_BIT);

    // Red LED: output, off
    LED_DDR  |=  (1 << LED_BIT);
    LED_PORT &= ~(1 << LED_BIT);

    // Initialize CircuitPlayground (NeoPixels, sensors, etc.)
    CircuitPlayground.begin();
    CircuitPlayground.strip.setBrightness(80);
    CircuitPlayground.strip.clear();
    CircuitPlayground.strip.show();
}

// Buttons — ACTIVE HIGH, register-level

static inline bool btn_left_raw(void)  { return (BTN_LEFT_PIN  & (1 << BTN_LEFT_BIT))  != 0; } // check if left button is pressed
static inline bool btn_right_raw(void) { return (BTN_RIGHT_PIN & (1 << BTN_RIGHT_BIT)) != 0; } // check if right button is pressed
static inline bool both_buttons_held(void) { return btn_left_raw() && btn_right_raw(); } // check if both buttons are held simultaneously

static bool btn_left_pressed(void) { // Check if the left button was just pressed (transition from not pressed to pressed)
    static bool prev = false;
    bool now = btn_left_raw();
    if (now && !prev) { prev = true; _delay_ms(DEBOUNCE_MS); return true; }
    prev = now; return false;
}

static bool btn_right_pressed(void) { // Check if the right button was just pressed (transition from not pressed to pressed)
    static bool prev = false;
    bool now = btn_right_raw();
    if (now && !prev) { prev = true; _delay_ms(DEBOUNCE_MS); return true; }
    prev = now; return false;
}

// Red LED — register-level
static inline void led_on(void)     { LED_PORT |=  (1 << LED_BIT); }
static inline void led_off(void)    { LED_PORT &= ~(1 << LED_BIT); }
static inline void led_toggle(void) { LED_PORT ^=  (1 << LED_BIT); }

// The library handles SPI disable, 8MHz timing, level shifter.

static void neo_show(void) {
    CircuitPlayground.strip.show(); // Send current pixel data to the NeoPixel strip to update the colors displayed on the LEDs. 
}

static void neo_set(uint8_t idx, uint32_t color) {
    CircuitPlayground.strip.setPixelColor(idx, color); // Set the color of a specific NeoPixel at index idx to the specified color value
}

static void neo_fill(uint32_t color) { // Fill all NeoPixels with the specified color
    for (uint8_t i = 0; i < NUM_PIXELS; i++)
        CircuitPlayground.strip.setPixelColor(i, color);
}

static void neo_clear(void) {
    CircuitPlayground.strip.clear(); // Clear all NeoPixels
    neo_show(); // Update the NeoPixel strip to reflect the cleared state (turn off all LEDs)
}

static void neo_flash(uint32_t color, uint16_t ms) { // Flash all NeoPixels with the specified color for a certain duration (ms milliseconds)
    neo_fill(color);
    neo_show();
    for (uint16_t i = 0; i < ms / 10; i++) _delay_ms(10);
    neo_clear();
}

// Progress & Status — one green dot per completed gesture at pixels 0, 1, 2
static void neo_show_progress(uint8_t done, uint32_t color) {
    CircuitPlayground.strip.clear();
    for (uint8_t i = 0; i < done && i < NUM_GESTURES; i++) neo_set(i, COLOR_GREEN);
    neo_show();
}

static void neo_waiting_pulse(uint32_t color, uint8_t gn) {
    CircuitPlayground.strip.clear();
    for (uint8_t i = 0; i < gn && i < NUM_GESTURES; i++) neo_set(i, COLOR_GREEN);
    neo_show();
}

static void neo_capturing_tick(uint32_t color, uint8_t gn, uint8_t tick) {
    CircuitPlayground.strip.clear();
    for (uint8_t i = 0; i < gn && i < NUM_GESTURES; i++) neo_set(i, COLOR_GREEN);
    neo_set(gn, COLOR_WHITE);
    neo_show();
}

// Animations
static void neo_success_animation(void) {
    neo_fill(COLOR_GREEN); neo_show(); led_on();
    for (uint16_t i = 0; i < 100; i++) _delay_ms(10);
    led_off(); neo_clear();
}

static void neo_fail_animation(void) {
    neo_flash(COLOR_RED, 400); _delay_ms(300);
    neo_flash(COLOR_RED, 400); _delay_ms(300);
    neo_flash(COLOR_RED, 400);
}

static void neo_saved_animation(void) {
    neo_flash(COLOR_GREEN, 500);
}

static void neo_erase_animation(void) {
    neo_flash(COLOR_RED, 500); _delay_ms(300);
    neo_flash(COLOR_RED, 500);
}

static void neo_idle_indicator(void) { // Idle indicator: the first and last NeoPixels light up blue to indicate that the system is idle and waiting for user interaction.
    CircuitPlayground.strip.clear(); // Clear all NeoPixels before setting the idle indicator
    neo_set(0, COLOR_BLUE); // Set the first NeoPixel to blue to indicate idle state
    neo_set(5, COLOR_BLUE); // Set the last NeoPixel to blue to indicate idle state
    neo_show();
}

static void neo_boot_animation(void) {
    neo_flash(COLOR_CYAN, 300);
    _delay_ms(200);
}

static void neo_error(void) { // Error state: all NeoPixels flash red continuously and the red LED is on to indicate a critical error state. 
    while (1) { 
        neo_fill(COLOR_RED); 
        neo_show(); 
        _delay_ms(500); 
        neo_clear(); 
        _delay_ms(500); }
}

static void neo_no_key_warning(void) { // No key warning: all NeoPixels flash orange 3 times to indicate that there is no stored key and the user needs to set up a key before they can unlock.
    neo_flash(COLOR_ORANGE, 400); 
    _delay_ms(100); 
    neo_flash(COLOR_ORANGE, 400);
}

static void neo_cancel_feedback(void) { neo_flash(COLOR_YELLOW, 300); } // Cancel feedback: all NeoPixels flash yellow once to provide feedback that the current unlock attempt has been cancelled and the user can start over.

#endif