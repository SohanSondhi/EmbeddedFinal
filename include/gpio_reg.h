#ifndef GPIO_REG_H
#define GPIO_REG_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "pin_config.h"

// =============================================================
// Register-Level GPIO Driver
// No digitalWrite/digitalRead/pinMode allowed
// =============================================================

// --- Color struct for NeoPixels ---
struct Color {
    uint8_t r, g, b;
};

// Predefined colors
const Color COLOR_OFF     = {0, 0, 0};
const Color COLOR_RED     = {50, 0, 0};
const Color COLOR_GREEN   = {0, 50, 0};
const Color COLOR_BLUE    = {0, 0, 50};
const Color COLOR_YELLOW  = {50, 50, 0};
const Color COLOR_WHITE   = {20, 20, 20};
const Color COLOR_PURPLE  = {30, 0, 50};

// NeoPixel buffer
static Color pixel_buffer[NUM_PIXELS];

// =============================================================
// Init
// =============================================================
inline void gpio_init() {
    // Buttons as INPUT with pull-up
    BTN_LEFT_DDR  &= ~(1 << BTN_LEFT_BIT);   // input
    BTN_LEFT_PORT |=  (1 << BTN_LEFT_BIT);    // pull-up

    BTN_RIGHT_DDR  &= ~(1 << BTN_RIGHT_BIT);
    BTN_RIGHT_PORT |=  (1 << BTN_RIGHT_BIT);

    // Slide switch as INPUT with pull-up
    SWITCH_DDR  &= ~(1 << SWITCH_BIT);
    SWITCH_PORT |=  (1 << SWITCH_BIT);

    // Onboard red LED as OUTPUT
    LED_DDR |= (1 << LED_BIT);
    LED_PORT &= ~(1 << LED_BIT);  // off

    // NeoPixel data pin as OUTPUT
    NEO_DDR |= (1 << NEO_BIT);
    NEO_PORT &= ~(1 << NEO_BIT);  // low
}

// =============================================================
// Button Reading (active LOW on CPClassic)
// =============================================================
inline bool btn_left_pressed() {
    return !(BTN_LEFT_PIN & (1 << BTN_LEFT_BIT));
}

inline bool btn_right_pressed() {
    return !(BTN_RIGHT_PIN & (1 << BTN_RIGHT_BIT));
}

inline bool slide_switch_left() {
    return !(SWITCH_PIN & (1 << SWITCH_BIT));
}

// Simple debounce — call in loop, returns true on fresh press
inline bool btn_left_debounced() {
    static bool last = false;
    bool now = btn_left_pressed();
    if (now && !last) {
        last = now;
        _delay_ms(50);
        return true;
    }
    last = now;
    return false;
}

inline bool btn_right_debounced() {
    static bool last = false;
    bool now = btn_right_pressed();
    if (now && !last) {
        last = now;
        _delay_ms(50);
        return true;
    }
    last = now;
    return false;
}

// =============================================================
// Onboard Red LED
// =============================================================
inline void led_on()  { LED_PORT |=  (1 << LED_BIT); }
inline void led_off() { LED_PORT &= ~(1 << LED_BIT); }
inline void led_toggle() { LED_PORT ^= (1 << LED_BIT); }

// =============================================================
// NeoPixel Driver (WS2812B via bit-bang, register level)
// Timing for 16MHz ATmega32u4:
//   T0H = 0.35us (~6 cycles)
//   T1H = 0.70us (~11 cycles)
//   T0L = 0.80us (~13 cycles)
//   T1L = 0.60us (~10 cycles)
// =============================================================

static void neo_send_byte(uint8_t byte) {
    volatile uint8_t *port = &NEO_PORT;
    uint8_t hi = NEO_PORT | (1 << NEO_BIT);
    uint8_t lo = NEO_PORT & ~(1 << NEO_BIT);

    for (uint8_t bit = 0; bit < 8; bit++) {
        if (byte & 0x80) {
            // Send 1: high ~0.7us, low ~0.6us
            *port = hi;
            asm volatile(
                "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
                "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
            );
            *port = lo;
            asm volatile(
                "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
                "nop\n\t" "nop\n\t"
            );
        } else {
            // Send 0: high ~0.35us, low ~0.8us
            *port = hi;
            asm volatile(
                "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
            );
            *port = lo;
            asm volatile(
                "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
                "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
            );
        }
        byte <<= 1;
    }
}

// Push buffer to NeoPixels (WS2812B order: GRB)
inline void neo_show() {
    cli();  // disable interrupts for timing
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        neo_send_byte(pixel_buffer[i].g);
        neo_send_byte(pixel_buffer[i].r);
        neo_send_byte(pixel_buffer[i].b);
    }
    sei();  // re-enable interrupts
    _delay_us(80);  // latch
}

// Set single pixel color
inline void neo_set(uint8_t index, Color c) {
    if (index < NUM_PIXELS) {
        pixel_buffer[index] = c;
    }
}

// Set all pixels to same color
inline void neo_fill(Color c) {
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        pixel_buffer[i] = c;
    }
}

// Clear all pixels
inline void neo_clear() {
    neo_fill(COLOR_OFF);
    neo_show();
}

// =============================================================
// Feedback Patterns
// =============================================================

// Flash all pixels a color briefly
inline void neo_flash(Color c, uint16_t ms) {
    neo_fill(c);
    neo_show();
    for (uint16_t i = 0; i < ms / 10; i++) _delay_ms(10);
    neo_clear();
}

// Light up N pixels to show progress (0-3 gestures done)
inline void neo_show_progress(uint8_t count, Color c) {
    neo_fill(COLOR_OFF);
    // Light up pixels in groups for each completed gesture
    // Gesture 0: pixels 0-2, Gesture 1: pixels 3-5, Gesture 2: pixels 6-8
    for (uint8_t g = 0; g < count && g < NUM_GESTURES; g++) {
        for (uint8_t p = g * 3; p < (g * 3) + 3 && p < NUM_PIXELS; p++) {
            pixel_buffer[p] = c;
        }
    }
    neo_show();
}

// Unlock success animation
inline void neo_unlock_animation() {
    // Spin green around the ring
    for (uint8_t round = 0; round < 3; round++) {
        for (uint8_t i = 0; i < NUM_PIXELS; i++) {
            neo_fill(COLOR_OFF);
            neo_set(i, COLOR_GREEN);
            neo_set((i + 1) % NUM_PIXELS, COLOR_GREEN);
            neo_show();
            _delay_ms(50);
        }
    }
    // All green
    neo_fill(COLOR_GREEN);
    neo_show();
    for (uint8_t i = 0; i < 200; i++) _delay_ms(10);  // 2s hold
    neo_clear();
}

// Unlock fail animation
inline void neo_fail_animation() {
    for (uint8_t i = 0; i < 3; i++) {
        neo_fill(COLOR_RED);
        neo_show();
        _delay_ms(200);
        neo_clear();
        _delay_ms(200);
    }
}

// Ready/idle pulse
inline void neo_ready_pulse() {
    neo_set(0, COLOR_BLUE);
    neo_set(5, COLOR_BLUE);
    neo_show();
}

#endif // GPIO_REG_H
