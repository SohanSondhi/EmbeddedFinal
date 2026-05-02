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

// Progress & Status
static void neo_show_progress(uint8_t done, uint32_t color) { // Show progress of the current gesture capture by lighting up certain numbers of NeoPixels based on how many gestures have been completed so far in the current unlock attempt.
    CircuitPlayground.strip.clear(); // Clear all NeoPixels before showing progress
    uint8_t lit = done * 3; // Each gesture corresponds to 3 NeoPixels
    if (lit > NUM_PIXELS) lit = NUM_PIXELS; // Avoid out-of-bounds
    for (uint8_t i = 0; i < lit; i++) neo_set(i, color); // Light up the NeoPixels corresponding to completed gestures with the specified color
    if (done > 0 && done < NUM_GESTURES) { // If at least one gesture is completed but not all gestures are completed, light up the next 3 NeoPixels with a dimmed version of the color to indicate the current gesture in progress.
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        neo_set(9, CircuitPlayground.strip.Color(r/4, g/4, b/4));
    }
    neo_show();
}

static void neo_waiting_pulse(uint32_t color, uint8_t gn) { // Show a pulsing animation on the NeoPixels while waiting for the user to perform the next gesture in the combination. 
    for (uint8_t g = 0; g < gn; g++) // Light up the NeoPixels corresponding to completed gestures with the specified color
        for (uint8_t p = g*3; p < (g*3)+3 && p < NUM_PIXELS; p++) // Light up the NeoPixels corresponding to completed gestures with the specified color
            neo_set(p, color); // Set the color of the NeoPixels corresponding to completed gestures to the specified color
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    uint32_t dim = CircuitPlayground.strip.Color(r/5, g/5, b/5); // Create a dimmed version of the specified color for the pulsing effect while waiting for the next gesture
    uint8_t s = gn * 3;

    for (uint8_t p = s; p < s+3 && p < NUM_PIXELS; p++) neo_set(p, dim);
    neo_show();
}

static void neo_capturing_tick(uint32_t color, uint8_t gn, uint8_t tick) { // Show a ticking animation on the NeoPixels while capturing the current gesture.
    for (uint8_t g = 0; g < gn; g++) // Light up the NeoPixels corresponding to completed gestures with the specified color
        for (uint8_t p = g*3; p < (g*3)+3 && p < NUM_PIXELS; p++) // Light up the NeoPixels corresponding to completed gestures with the specified color
            neo_set(p, color);
    uint8_t pos = (gn*3) + (tick%3); // Calculate the position of the ticking NeoPixel based on the current gesture number (gn) and the tick count (tick)
    if (pos < NUM_PIXELS) neo_set(pos, COLOR_WHITE);
    neo_show();
}

// Animations
static void neo_success_animation(void) { // Success animation: a green "comet" that runs through the NeoPixels 3 times, followed by all NeoPixels lighting up green and then fading out while the red LED is on for a moment. 
    for (uint8_t r = 0; r < 3; r++) // Run the "comet" animation 3 times
        for (uint8_t i = 0; i < NUM_PIXELS; i++) { 
            CircuitPlayground.strip.clear(); // Clear all NeoPixels before setting the next position of the "comet"
            neo_set(i, COLOR_GREEN); // Set the current position of the "comet" to green
            neo_set((i+1)%NUM_PIXELS, COLOR_GREEN); // Set the next position of the "comet" to green to create a trailing effect
            neo_set((i+2)%NUM_PIXELS, COLOR_GREEN); // Set the next-next position of the "comet" to green to create a trailing effect
            neo_show(); // Update the NeoPixel strip to show the current state of the "comet" animation
            _delay_ms(50); // Delay to control the speed of the "comet" animation
        }
    neo_fill(COLOR_GREEN); // Light up all NeoPixels to green
    neo_show(); 
    led_on();

    for (uint16_t i = 0; i < 300; i++) _delay_ms(10); 
    led_off();
    for (uint8_t b = 150; b > 10; b -= 10) { // Fade out the NeoPixels by gradually decreasing their brightness while keeping the color green. 
        neo_fill(CircuitPlayground.strip.Color(0, b, 0)); 
        neo_show(); 
        _delay_ms(30);
    }
    neo_clear(); // Clear the NeoPixels at the end of the animation to turn them all off
}

static void neo_fail_animation(void) { // Failure animation: all NeoPixels flash red 5 times while the red LED is on, then all NeoPixels turn off and the red LED turns off.
    for (uint8_t i = 0; i < 5; i++) { // Flash 5 times
        neo_fill(COLOR_RED); // Fill all NeoPixels with red color to indicate failure
        neo_show(); 
        led_on(); // Turn on the red LED to indicate failure
        _delay_ms(400); // Delay to keep the NeoPixels and red LED on for a moment
        neo_clear(); // Clear all NeoPixels to turn them off
        led_off(); // Turn off the red LED
        _delay_ms(300); // Delay before the next flash
    }
}

static void neo_saved_animation(void) { // Saved animation: a green "comet" that runs through the NeoPixels once, followed by all NeoPixels lighting up green one by one with a short delay, then all NeoPixels stay on for a moment before turning off.
    for (uint8_t i = 0; i < NUM_PIXELS; i++) { // Run the "comet" animation once
        neo_set(i, COLOR_GREEN); // Set the current position of the "comet" to green
        neo_show(); 
        _delay_ms(80); // Delay to control the speed of the "comet" animation
    }
    for (uint16_t i = 0; i < 150; i++) _delay_ms(10); // Delay before filling all NeoPixels to green
    neo_clear(); // Clear all NeoPixels before filling them to green one by one
}

static void neo_erase_animation(void) { // Erase animation: all NeoPixels flash red 3 times while the red LED is on, then all NeoPixels turn off and the red LED turns off.
    for (uint8_t i = 0; i < 2; i++) { // Flash 3 times
        neo_fill(COLOR_RED); // Fill all NeoPixels with red color to indicate erasing
        neo_show(); 
        led_on(); // Turn on the red LED to indicate erasing
        _delay_ms(500); // Delay to keep the NeoPixels and red LED on for a moment
        neo_clear(); // Clear all NeoPixels to turn them off
        led_off(); // Turn off the red LED
        _delay_ms(300); // Delay before the next flash
    }
}

static void neo_idle_indicator(void) { // Idle indicator: the first and last NeoPixels light up blue to indicate that the system is idle and waiting for user interaction.
    CircuitPlayground.strip.clear(); // Clear all NeoPixels before setting the idle indicator
    neo_set(0, COLOR_BLUE); // Set the first NeoPixel to blue to indicate idle state
    neo_set(5, COLOR_BLUE); // Set the last NeoPixel to blue to indicate idle state
    neo_show();
}

static void neo_boot_animation(void) { // Boot animation: a cyan "comet" that runs through the NeoPixels once to indicate that the system is starting up and ready for use.
    for (uint8_t i = 0; i < NUM_PIXELS; i++) { // Run the "comet" animation once
        CircuitPlayground.strip.clear();
        neo_set(i, COLOR_CYAN); // Set the current position of the "comet" to cyan to indicate booting up
        neo_show(); // Update the NeoPixel strip to show the current state of the "comet" animation
        _delay_ms(40); // Delay to control the speed of the "comet" animation
    }
    neo_clear();
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