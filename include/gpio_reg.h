#ifndef GPIO_REG_H
#define GPIO_REG_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <Adafruit_CircuitPlayground.h>
#include "pin_config.h"

// =============================================================
// GPIO Driver — Official CircuitPlayground Library for NeoPixels
//
// After 4 hours of hand-written assembly that never worked,
// we use the library that Adafruit built and tested for THIS
// EXACT board. It handles the level shifter, SPI conflicts,
// and 8MHz timing internally.
//
// Buttons & Red LED: still register-level (project requirement).
// NeoPixels: CircuitPlayground.strip (library/driver, allowed).
// =============================================================

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

// =============================================================
// Init — CircuitPlayground.begin() handles NeoPixels properly
// =============================================================
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

// =============================================================
// Buttons — ACTIVE HIGH, register-level
// =============================================================
static inline bool btn_left_raw(void)  { return (BTN_LEFT_PIN  & (1 << BTN_LEFT_BIT))  != 0; }
static inline bool btn_right_raw(void) { return (BTN_RIGHT_PIN & (1 << BTN_RIGHT_BIT)) != 0; }
static inline bool both_buttons_held(void) { return btn_left_raw() && btn_right_raw(); }

static bool btn_left_pressed(void) {
    static bool prev = false;
    bool now = btn_left_raw();
    if (now && !prev) { prev = true; _delay_ms(DEBOUNCE_MS); return true; }
    prev = now; return false;
}
static bool btn_right_pressed(void) {
    static bool prev = false;
    bool now = btn_right_raw();
    if (now && !prev) { prev = true; _delay_ms(DEBOUNCE_MS); return true; }
    prev = now; return false;
}

// =============================================================
// Red LED — register-level
// =============================================================
static inline void led_on(void)     { LED_PORT |=  (1 << LED_BIT); }
static inline void led_off(void)    { LED_PORT &= ~(1 << LED_BIT); }
static inline void led_toggle(void) { LED_PORT ^=  (1 << LED_BIT); }

// =============================================================
// NeoPixel helpers — via CircuitPlayground.strip
// The library handles SPI disable, 8MHz timing, level shifter.
// =============================================================
static void neo_show(void) {
    CircuitPlayground.strip.show();
}

static void neo_set(uint8_t idx, uint32_t color) {
    CircuitPlayground.strip.setPixelColor(idx, color);
}

static void neo_fill(uint32_t color) {
    for (uint8_t i = 0; i < NUM_PIXELS; i++)
        CircuitPlayground.strip.setPixelColor(i, color);
}

static void neo_clear(void) {
    CircuitPlayground.strip.clear();
    neo_show();
}

static void neo_flash(uint32_t color, uint16_t ms) {
    neo_fill(color); neo_show();
    for (uint16_t i = 0; i < ms / 10; i++) _delay_ms(10);
    neo_clear();
}

// =============================================================
// Progress & Status
// =============================================================
static void neo_show_progress(uint8_t done, uint32_t color) {
    CircuitPlayground.strip.clear();
    uint8_t lit = done * 3;
    if (lit > NUM_PIXELS) lit = NUM_PIXELS;
    for (uint8_t i = 0; i < lit; i++) neo_set(i, color);
    if (done > 0 && done < NUM_GESTURES) {
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        neo_set(9, CircuitPlayground.strip.Color(r/4, g/4, b/4));
    }
    neo_show();
}

static void neo_waiting_pulse(uint32_t color, uint8_t gn) {
    CircuitPlayground.strip.clear();
    for (uint8_t g = 0; g < gn; g++)
        for (uint8_t p = g*3; p < (g*3)+3 && p < NUM_PIXELS; p++)
            neo_set(p, color);
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    uint32_t dim = CircuitPlayground.strip.Color(r/5, g/5, b/5);
    uint8_t s = gn * 3;
    for (uint8_t p = s; p < s+3 && p < NUM_PIXELS; p++) neo_set(p, dim);
    neo_show();
}

static void neo_capturing_tick(uint32_t color, uint8_t gn, uint8_t tick) {
    CircuitPlayground.strip.clear();
    for (uint8_t g = 0; g < gn; g++)
        for (uint8_t p = g*3; p < (g*3)+3 && p < NUM_PIXELS; p++)
            neo_set(p, color);
    uint8_t pos = (gn*3) + (tick%3);
    if (pos < NUM_PIXELS) neo_set(pos, COLOR_WHITE);
    neo_show();
}

// =============================================================
// Animations
// =============================================================
static void neo_success_animation(void) {
    for (uint8_t r = 0; r < 3; r++)
        for (uint8_t i = 0; i < NUM_PIXELS; i++) {
            CircuitPlayground.strip.clear();
            neo_set(i, COLOR_GREEN);
            neo_set((i+1)%NUM_PIXELS, COLOR_GREEN);
            neo_set((i+2)%NUM_PIXELS, COLOR_GREEN);
            neo_show(); _delay_ms(50);
        }
    neo_fill(COLOR_GREEN); neo_show(); led_on();
    for (uint16_t i = 0; i < 300; i++) _delay_ms(10);
    led_off();
    for (uint8_t b = 150; b > 10; b -= 10) {
        neo_fill(CircuitPlayground.strip.Color(0, b, 0)); neo_show(); _delay_ms(30);
    }
    neo_clear();
}

static void neo_fail_animation(void) {
    for (uint8_t i = 0; i < 5; i++) {
        neo_fill(COLOR_RED); neo_show(); led_on(); _delay_ms(400);
        neo_clear(); led_off(); _delay_ms(300);
    }
}

static void neo_saved_animation(void) {
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        neo_set(i, COLOR_GREEN); neo_show(); _delay_ms(80);
    }
    for (uint16_t i = 0; i < 150; i++) _delay_ms(10);
    neo_clear();
}

static void neo_erase_animation(void) {
    for (uint8_t i = 0; i < 2; i++) {
        neo_fill(COLOR_RED); neo_show(); led_on(); _delay_ms(500);
        neo_clear(); led_off(); _delay_ms(300);
    }
}

static void neo_idle_indicator(void) {
    CircuitPlayground.strip.clear();
    neo_set(0, COLOR_BLUE);
    neo_set(5, COLOR_BLUE);
    neo_show();
}

static void neo_boot_animation(void) {
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        CircuitPlayground.strip.clear();
        neo_set(i, COLOR_CYAN);
        neo_show();
        _delay_ms(40);
    }
    neo_clear(); _delay_ms(200);
}

static void neo_error(void) {
    while (1) { neo_fill(COLOR_RED); neo_show(); _delay_ms(500); neo_clear(); _delay_ms(500); }
}

static void neo_no_key_warning(void) {
    neo_flash(COLOR_ORANGE, 400); _delay_ms(100); neo_flash(COLOR_ORANGE, 400);
}

static void neo_cancel_feedback(void) { neo_flash(COLOR_YELLOW, 300); }

#endif