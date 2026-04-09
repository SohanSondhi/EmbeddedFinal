#ifndef GPIO_REG_H
#define GPIO_REG_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "pin_config.h"

struct Color { uint8_t r, g, b; };

static const Color COLOR_OFF    = {0,0,0};
static const Color COLOR_RED    = {150,0,0};
static const Color COLOR_GREEN  = {0,150,0};
static const Color COLOR_BLUE   = {0,0,150};
static const Color COLOR_YELLOW = {150,100,0};
static const Color COLOR_PURPLE = {80,0,150};
static const Color COLOR_CYAN   = {0,100,100};
static const Color COLOR_WHITE  = {100,100,100};
static const Color COLOR_ORANGE = {150,40,0};

static Color pixel_buf[NUM_PIXELS];

static inline void gpio_init(void) {
    BTN_LEFT_DDR  &= ~(1 << BTN_LEFT_BIT);
    BTN_LEFT_PORT &= ~(1 << BTN_LEFT_BIT);
    BTN_RIGHT_DDR &= ~(1 << BTN_RIGHT_BIT);
    BTN_RIGHT_PORT&= ~(1 << BTN_RIGHT_BIT);
    SWITCH_DDR    &= ~(1 << SWITCH_BIT);
    SWITCH_PORT   &= ~(1 << SWITCH_BIT);
    LED_DDR  |=  (1 << LED_BIT);
    LED_PORT &= ~(1 << LED_BIT);
    NEO_DDR  |=  (1 << NEO_BIT);
    NEO_PORT &= ~(1 << NEO_BIT);
    for (uint8_t i = 0; i < NUM_PIXELS; i++) pixel_buf[i] = COLOR_OFF;
}

// Buttons — ACTIVE HIGH on CP Classic
static inline bool btn_left_raw(void)  { return (BTN_LEFT_PIN  & (1<<BTN_LEFT_BIT))  != 0; }
static inline bool btn_right_raw(void) { return (BTN_RIGHT_PIN & (1<<BTN_RIGHT_BIT)) != 0; }
static inline bool switch_left(void)   { return (SWITCH_PIN    & (1<<SWITCH_BIT))    != 0; }
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

static inline void led_on(void)     { LED_PORT |=  (1<<LED_BIT); }
static inline void led_off(void)    { LED_PORT &= ~(1<<LED_BIT); }
static inline void led_toggle(void) { LED_PORT ^=  (1<<LED_BIT); }

// =============================================================
// NeoPixel WS2812B — 8MHz AVR
//
// KEY FIX: Disable SPI before sending NeoPixel data because
// pin 8 (PB4) is shared between NeoPixel data and LIS3DH SPI CS.
// The SPI hardware interferes with PORTB writes if left enabled.
// This is what the official Adafruit CircuitPlayground library does.
//
// Timing: Adafruit-proven 8MHz structure.
// Each bit = 10 cycles = 1.25us = 800kHz.
// =============================================================

static void neo_send_byte(uint8_t b) {
    volatile uint8_t *port = &NEO_PORT;
    uint8_t hi   = *port |  (1 << NEO_BIT);
    uint8_t lo   = *port & ~(1 << NEO_BIT);
    uint8_t bits = 8;

    asm volatile(
        "neo8_%=:"                     "\n\t"
        "  st   %a[port], %[hi]"      "\n\t"  // 1c  HIGH
        "  sbrc %[byte], 7"           "\n\t"  // 1c  (skip if bit=0)
        "  rjmp .+4"                  "\n\t"  // 2c  (bit=1: jump to nop)
        "  st   %a[port], %[lo]"      "\n\t"  // 1c  (bit=0: early LOW)
        "  rjmp .+4"                  "\n\t"  // 2c  (skip to lsl)
        "  nop"                       "\n\t"  // 1c  (bit=1: extra HIGH)
        "  st   %a[port], %[lo]"      "\n\t"  // 1c  (bit=1: late LOW)
        "  lsl  %[byte]"             "\n\t"  // 1c
        "  dec  %[bits]"             "\n\t"  // 1c
        "  brne neo8_%="             "\n\t"  // 2c
        : [byte] "+r" (b), [bits] "+r" (bits)
        : [port] "e" (port), [hi] "r" (hi), [lo] "r" (lo)
    );
}

static void neo_show(void) {
    // *** CRITICAL: Disable SPI so it stops interfering with PB4 ***
    uint8_t spcr_save = SPCR;
    SPCR = 0;

    // Ensure PB4 is output and LOW for reset pulse
    NEO_DDR |= (1 << NEO_BIT);
    NEO_PORT &= ~(1 << NEO_BIT);
    _delay_us(80);

    cli();
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        neo_send_byte(pixel_buf[i].g);
        neo_send_byte(pixel_buf[i].r);
        neo_send_byte(pixel_buf[i].b);
    }
    sei();

    _delay_us(80);

    // Deassert LIS3DH CS (HIGH = not selected)
    NEO_PORT |= (1 << NEO_BIT);

    // *** Restore SPI ***
    SPCR = spcr_save;
}

// Pixel helpers
static inline void neo_set(uint8_t idx, Color c) { if (idx < NUM_PIXELS) pixel_buf[idx] = c; }
static void neo_fill(Color c) { for (uint8_t i = 0; i < NUM_PIXELS; i++) pixel_buf[i] = c; }
static void neo_clear(void) { neo_fill(COLOR_OFF); neo_show(); }

static void neo_flash(Color c, uint16_t ms) {
    neo_fill(c); neo_show();
    for (uint16_t i = 0; i < ms/10; i++) _delay_ms(10);
    neo_clear();
}

static void neo_show_progress(uint8_t done, Color c) {
    neo_fill(COLOR_OFF);
    uint8_t lit = done * 3;
    if (lit > NUM_PIXELS) lit = NUM_PIXELS;
    for (uint8_t i = 0; i < lit; i++) pixel_buf[i] = c;
    if (done > 0 && done < NUM_GESTURES) {
        Color dim = {(uint8_t)(c.r/4),(uint8_t)(c.g/4),(uint8_t)(c.b/4)};
        pixel_buf[9] = dim;
    }
    neo_show();
}

static void neo_waiting_pulse(Color c, uint8_t gn) {
    neo_fill(COLOR_OFF);
    for (uint8_t g = 0; g < gn; g++)
        for (uint8_t p = g*3; p < (g*3)+3 && p < NUM_PIXELS; p++)
            pixel_buf[p] = c;
    Color dim = {(uint8_t)(c.r/5),(uint8_t)(c.g/5),(uint8_t)(c.b/5)};
    uint8_t s = gn * 3;
    for (uint8_t p = s; p < s+3 && p < NUM_PIXELS; p++) pixel_buf[p] = dim;
    neo_show();
}

static void neo_capturing_tick(Color c, uint8_t gn, uint8_t tick) {
    neo_fill(COLOR_OFF);
    for (uint8_t g = 0; g < gn; g++)
        for (uint8_t p = g*3; p < (g*3)+3 && p < NUM_PIXELS; p++)
            pixel_buf[p] = c;
    uint8_t pos = (gn*3) + (tick%3);
    if (pos < NUM_PIXELS) pixel_buf[pos] = COLOR_WHITE;
    neo_show();
}

static void neo_success_animation(void) {
    for (uint8_t r = 0; r < 3; r++)
        for (uint8_t i = 0; i < NUM_PIXELS; i++) {
            neo_fill(COLOR_OFF);
            neo_set(i, COLOR_GREEN);
            neo_set((i+1)%NUM_PIXELS, COLOR_GREEN);
            neo_set((i+2)%NUM_PIXELS, COLOR_GREEN);
            neo_show(); _delay_ms(50);
        }
    neo_fill(COLOR_GREEN); neo_show(); led_on();
    for (uint16_t i = 0; i < 300; i++) _delay_ms(10);
    led_off();
    for (uint8_t b = 150; b > 10; b -= 10) {
        Color g = {0,b,0}; neo_fill(g); neo_show(); _delay_ms(30);
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
    for (uint8_t i = 0; i < NUM_PIXELS; i++) { neo_set(i, COLOR_GREEN); neo_show(); _delay_ms(80); }
    for (uint16_t i = 0; i < 150; i++) _delay_ms(10);
    neo_clear();
}

static void neo_erase_animation(void) {
    for (uint8_t i = 0; i < 2; i++) {
        neo_fill(COLOR_RED); neo_show(); led_on(); _delay_ms(500);
        neo_clear(); led_off(); _delay_ms(300);
    }
    neo_fill(COLOR_RED); neo_show(); _delay_ms(200);
    for (uint8_t i = 0; i < NUM_PIXELS; i++) { neo_set(i, COLOR_OFF); neo_show(); _delay_ms(80); }
}

static void neo_idle_indicator(void) {
    neo_fill(COLOR_OFF); neo_set(0, COLOR_BLUE); neo_set(5, COLOR_BLUE); neo_show();
}

static void neo_boot_animation(void) {
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        neo_fill(COLOR_OFF); neo_set(i, COLOR_CYAN); neo_show(); _delay_ms(40);
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