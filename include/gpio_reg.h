#ifndef GPIO_REG_H
#define GPIO_REG_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "pin_config.h"

// =============================================================
// Register-Level GPIO Driver
// NO digitalWrite / digitalRead / pinMode used anywhere.
//
// CP Classic buttons are ACTIVE HIGH (pressed = 1, released = 0)
// because the board has external pull-down resistors.
// =============================================================

// --- RGB Color ---
struct Color {
    uint8_t r, g, b;
};

// Predefined colors (kept dim to save power and not blind you)
static const Color COLOR_OFF     = {0, 0, 0};
static const Color COLOR_RED     = {40, 0, 0};
static const Color COLOR_GREEN   = {0, 40, 0};
static const Color COLOR_BLUE    = {0, 0, 40};
static const Color COLOR_YELLOW  = {40, 30, 0};
static const Color COLOR_PURPLE  = {25, 0, 40};
static const Color COLOR_CYAN    = {0, 30, 30};
static const Color COLOR_WHITE   = {20, 20, 20};
static const Color COLOR_ORANGE  = {40, 15, 0};

// NeoPixel framebuffer
static volatile Color pixel_buf[NUM_PIXELS];

// =============================================================
// Initialization
// =============================================================
static inline void gpio_init(void) {
    // --- Buttons: INPUT, no pull-up needed (board has pull-downs) ---
    BTN_LEFT_DDR  &= ~(1 << BTN_LEFT_BIT);     // input
    BTN_LEFT_PORT &= ~(1 << BTN_LEFT_BIT);      // no pull-up

    BTN_RIGHT_DDR  &= ~(1 << BTN_RIGHT_BIT);
    BTN_RIGHT_PORT &= ~(1 << BTN_RIGHT_BIT);

    // --- Slide switch: INPUT ---
    SWITCH_DDR  &= ~(1 << SWITCH_BIT);
    SWITCH_PORT &= ~(1 << SWITCH_BIT);

    // --- Red LED: OUTPUT, initially off ---
    LED_DDR  |=  (1 << LED_BIT);
    LED_PORT &= ~(1 << LED_BIT);

    // --- NeoPixel data: OUTPUT, initially low ---
    NEO_DDR  |=  (1 << NEO_BIT);
    NEO_PORT &= ~(1 << NEO_BIT);

    // Clear pixel buffer
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        pixel_buf[i] = COLOR_OFF;
    }
}

// =============================================================
// Button Reading — ACTIVE HIGH on CP Classic
// =============================================================
static inline bool btn_left_raw(void) {
    return (BTN_LEFT_PIN & (1 << BTN_LEFT_BIT)) != 0;
}

static inline bool btn_right_raw(void) {
    return (BTN_RIGHT_PIN & (1 << BTN_RIGHT_BIT)) != 0;
}

static inline bool switch_left(void) {
    return (SWITCH_PIN & (1 << SWITCH_BIT)) != 0;
}

// Debounced button read — returns true once on press edge
static bool btn_left_pressed(void) {
    static bool prev = false;
    bool now = btn_left_raw();
    if (now && !prev) {
        prev = true;
        _delay_ms(DEBOUNCE_MS);
        return true;
    }
    prev = now;
    return false;
}

static bool btn_right_pressed(void) {
    static bool prev = false;
    bool now = btn_right_raw();
    if (now && !prev) {
        prev = true;
        _delay_ms(DEBOUNCE_MS);
        return true;
    }
    prev = now;
    return false;
}

// Check if both buttons are currently held (for reset/cancel)
static inline bool both_buttons_held(void) {
    return btn_left_raw() && btn_right_raw();
}

// =============================================================
// Onboard Red LED
// =============================================================
static inline void led_on(void)     { LED_PORT |=  (1 << LED_BIT); }
static inline void led_off(void)    { LED_PORT &= ~(1 << LED_BIT); }
static inline void led_toggle(void) { LED_PORT ^=  (1 << LED_BIT); }

// =============================================================
// NeoPixel WS2812B Bit-Bang Driver
//
// Protocol: Single-wire, 800kHz. Data order: GRB, MSB first.
// At 16MHz, 1 cycle = 62.5ns.
//
// Bit timing (WS2812B datasheet):
//   T0H = 0.40us ±0.15us  →  ~6 cycles
//   T0L = 0.85us ±0.15us  →  ~14 cycles
//   T1H = 0.80us ±0.15us  →  ~13 cycles
//   T1L = 0.45us ±0.15us  →  ~7 cycles
//   Reset: >50us low
//
// We use inline assembly for cycle-accurate timing.
// Interrupts MUST be disabled during transmission.
// =============================================================

static void neo_send_byte(uint8_t byte) {
    volatile uint8_t *port = &NEO_PORT;
    uint8_t hi = *port |  (1 << NEO_BIT);
    uint8_t lo = *port & ~(1 << NEO_BIT);
    uint8_t bits = 8;

    // Unrolled inner loop with precise cycle counting
    asm volatile(
        "neo_bit_loop_%=:              \n\t"
        // --- Test highest bit ---
        "   sbrs %[byte], 7           \n\t"  // 1c skip if bit set
        "   rjmp neo_zero_%=          \n\t"  // 2c jump to zero path

        // --- Send ONE bit ---
        "   st   %a[port], %[hi]      \n\t"  // 1c  → HIGH
        "   nop \n\t nop \n\t nop \n\t"       // 3c
        "   nop \n\t nop \n\t nop \n\t"       // 3c
        "   nop \n\t nop \n\t nop \n\t"       // 3c  = ~13c high total
        "   nop \n\t"                          // 1c
        "   st   %a[port], %[lo]      \n\t"  // 1c  → LOW
        "   nop \n\t nop \n\t nop \n\t"       // 3c
        "   nop \n\t"                          // 1c  = ~7c low
        "   rjmp neo_next_%=          \n\t"   // 2c

        // --- Send ZERO bit ---
        "neo_zero_%=:                  \n\t"
        "   st   %a[port], %[hi]      \n\t"  // 1c  → HIGH
        "   nop \n\t nop \n\t nop \n\t"       // 3c
        "   nop \n\t"                          // 1c  = ~6c high total
        "   st   %a[port], %[lo]      \n\t"  // 1c  → LOW
        "   nop \n\t nop \n\t nop \n\t"       // 3c
        "   nop \n\t nop \n\t nop \n\t"       // 3c
        "   nop \n\t nop \n\t nop \n\t"       // 3c
        "   nop \n\t nop \n\t"                 // 2c  = ~14c low

        // --- Advance to next bit ---
        "neo_next_%=:                  \n\t"
        "   lsl  %[byte]              \n\t"   // 1c shift left
        "   dec  %[bits]              \n\t"   // 1c
        "   brne neo_bit_loop_%=      \n\t"   // 2c/1c loop

        : [byte] "+r" (byte),
          [bits] "+r" (bits)
        : [port] "e" (port),
          [hi]   "r" (hi),
          [lo]   "r" (lo)
    );
}

// Push pixel buffer to the NeoPixel strip
static void neo_show(void) {
    cli();  // Disable interrupts — timing critical
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        neo_send_byte(pixel_buf[i].g);  // GRB order
        neo_send_byte(pixel_buf[i].r);
        neo_send_byte(pixel_buf[i].b);
    }
    sei();  // Re-enable interrupts
    _delay_us(80); 
    NEO_PORT |= (1 << NEO_BIT); // Latch / reset pulse
}

// =============================================================
// Pixel Buffer Manipulation
// =============================================================
static inline void neo_set(uint8_t idx, Color c) {
    if (idx < NUM_PIXELS) pixel_buf[idx] = c;
}

static void neo_fill(Color c) {
    for (uint8_t i = 0; i < NUM_PIXELS; i++) pixel_buf[i] = c;
}

static void neo_clear(void) {
    neo_fill(COLOR_OFF);
    neo_show();
}

// =============================================================
// LED Feedback Patterns
// =============================================================

// Brief flash of all pixels
static void neo_flash(Color c, uint16_t duration_ms) {
    neo_fill(c);
    neo_show();
    for (uint16_t i = 0; i < duration_ms / 10; i++) {
        _delay_ms(10);
    }
    neo_clear();
}

// Show gesture progress: light up 3 pixels per completed gesture
// Gesture 0 done → pixels 0,1,2 lit
// Gesture 1 done → pixels 0-5 lit
// Gesture 2 done → pixels 0-8 lit
static void neo_show_progress(uint8_t completed, Color c) {
    neo_fill(COLOR_OFF);
    uint8_t lit = completed * 3;
    if (lit > NUM_PIXELS) lit = NUM_PIXELS;
    for (uint8_t i = 0; i < lit; i++) {
        pixel_buf[i] = c;
    }
    // Also light pixel 9 as a dim indicator that system is active
    if (completed < NUM_GESTURES) {
        pixel_buf[9] = {c.r / 4, c.g / 4, c.b / 4};
    }
    neo_show();
}

// "Waiting for gesture" — gentle pulse on one pixel
static void neo_waiting_pulse(Color c, uint8_t gesture_num) {
    neo_fill(COLOR_OFF);
    // Show previous progress
    for (uint8_t g = 0; g < gesture_num; g++) {
        for (uint8_t p = g * 3; p < (g * 3) + 3 && p < NUM_PIXELS; p++) {
            pixel_buf[p] = c;
        }
    }
    // Blink the next group
    uint8_t start = gesture_num * 3;
    Color dim = {c.r / 5, c.g / 5, c.b / 5};
    for (uint8_t p = start; p < start + 3 && p < NUM_PIXELS; p++) {
        pixel_buf[p] = dim;
    }
    neo_show();
}

// "Currently capturing" — spinning dot on remaining pixels
static void neo_capturing_tick(Color c, uint8_t gesture_num, uint8_t tick) {
    neo_fill(COLOR_OFF);
    // Show previous completed gestures
    for (uint8_t g = 0; g < gesture_num; g++) {
        for (uint8_t p = g * 3; p < (g * 3) + 3 && p < NUM_PIXELS; p++) {
            pixel_buf[p] = c;
        }
    }
    // Spinning dot in the current gesture's 3 pixels
    uint8_t start = gesture_num * 3;
    uint8_t pos = start + (tick % 3);
    if (pos < NUM_PIXELS) {
        pixel_buf[pos] = COLOR_WHITE;
    }
    neo_show();
}

// ---- SUCCESS: Green chase animation then solid green ----
static void neo_success_animation(void) {
    // Chase around the ring 2 times
    for (uint8_t round = 0; round < 2; round++) {
        for (uint8_t i = 0; i < NUM_PIXELS; i++) {
            neo_fill(COLOR_OFF);
            neo_set(i, COLOR_GREEN);
            neo_set((i + 1) % NUM_PIXELS, COLOR_GREEN);
            neo_set((i + 2) % NUM_PIXELS, COLOR_GREEN);
            neo_show();
            _delay_ms(60);
        }
    }
    // All green solid for 3 seconds
    neo_fill(COLOR_GREEN);
    neo_show();
    led_on();
    for (uint16_t i = 0; i < 300; i++) _delay_ms(10);
    led_off();
    neo_clear();
}

// ---- FAIL: Red blink 3 times ----
static void neo_fail_animation(void) {
    for (uint8_t i = 0; i < 3; i++) {
        neo_fill(COLOR_RED);
        neo_show();
        led_on();
        _delay_ms(250);
        neo_clear();
        led_off();
        _delay_ms(250);
    }
}

// ---- RECORDING SAVED: Purple → Green sweep ----
static void neo_saved_animation(void) {
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        neo_set(i, COLOR_GREEN);
        neo_show();
        _delay_ms(80);
    }
    for (uint16_t i = 0; i < 100; i++) _delay_ms(10);
    neo_clear();
}

// ---- IDLE indicator: Two blue dots ----
static void neo_idle_indicator(void) {
    neo_fill(COLOR_OFF);
    neo_set(0, COLOR_BLUE);
    neo_set(5, COLOR_BLUE);
    neo_show();
}

// ---- BOOT animation ----
static void neo_boot_animation(void) {
    // Quick cyan sweep
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        neo_fill(COLOR_OFF);
        neo_set(i, COLOR_CYAN);
        neo_show();
        _delay_ms(40);
    }
    neo_clear();
    _delay_ms(200);
}

// ---- ERROR indicator (e.g., accel not found) ----
static void neo_error(void) {
    while (1) {
        neo_fill(COLOR_RED);
        neo_show();
        _delay_ms(500);
        neo_clear();
        _delay_ms(500);
    }
}

// ---- "No key stored" warning ----
static void neo_no_key_warning(void) {
    neo_flash(COLOR_ORANGE, 300);
    _delay_ms(100);
    neo_flash(COLOR_ORANGE, 300);
}

// ---- Cancel/Reset feedback ----
static void neo_cancel_feedback(void) {
    neo_flash(COLOR_YELLOW, 200);
}

#endif // GPIO_REG_H
