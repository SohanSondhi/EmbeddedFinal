// =============================================================
// KinetiKey — "Old Lock, New Twist"
// Embedded Challenge Spring 2026
//
// Board:  Adafruit Circuit Playground Classic (ATmega32u4)
// Accel:  LIS3DH (onboard, I2C)
// IDE:    PlatformIO
//
// Left Button  = RECORD key sequence
// Right Button = UNLOCK attempt
// NeoPixels    = Visual feedback
// =============================================================

#include <Arduino.h>               // Arduino framework (for millis, Serial, Wire)
#include <Adafruit_LIS3DH.h>       // Accelerometer driver (allowed)
#include <Adafruit_Sensor.h>

#include "pin_config.h"
#include "gpio_reg.h"
#include "gesture.h"
#include "storage.h"

// =============================================================
// Accelerometer Instance (using I2C — library handles comms)
// =============================================================
Adafruit_LIS3DH lis = Adafruit_LIS3DH();

// =============================================================
// State Machine
// =============================================================
enum State {
    STATE_IDLE,             // waiting for button press
    STATE_RECORD_WAIT,      // record mode — waiting for motion
    STATE_RECORD_CAPTURE,   // record mode — capturing gesture
    STATE_UNLOCK_WAIT,      // unlock mode — waiting for motion
    STATE_UNLOCK_CAPTURE,   // unlock mode — capturing gesture
    STATE_UNLOCKED,         // unlock success
    STATE_FAILED            // unlock failed
};

State current_state = STATE_IDLE;
uint8_t gesture_index = 0;         // which gesture (0, 1, 2) we're on
GestureFeatures attempt_key[NUM_GESTURES];  // temp storage for unlock attempt

unsigned long last_sample_time = 0;
unsigned long motion_stop_time = 0;
bool was_moving = false;

// How long of stillness to consider a gesture "done"
#define STILLNESS_THRESHOLD_MS  400

// =============================================================
// Setup
// =============================================================
void setup() {
    // Init serial for debug
    Serial.begin(9600);

    // Init register-level GPIO
    gpio_init();

    // Boot animation
    neo_clear();
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        neo_set(i, COLOR_BLUE);
        neo_show();
        _delay_ms(50);
    }
    neo_clear();

    // Init accelerometer
    if (!lis.begin(0x18)) {  // I2C address on CP Classic
        Serial.println(F("LIS3DH not found!"));
        // Error indication: all red
        neo_fill(COLOR_RED);
        neo_show();
        while (1);
    }
    lis.setRange(LIS3DH_RANGE_4_G);
    lis.setDataRate(LIS3DH_DATARATE_50_HZ);
    Serial.println(F("LIS3DH OK"));

    // Try loading existing key from EEPROM
    if (storage_load_key(stored_key)) {
        key_loaded = true;
        Serial.println(F("Key loaded from EEPROM"));
    } else {
        Serial.println(F("No key stored"));
    }

    // Show idle state
    neo_ready_pulse();

    Serial.println(F("KinetiKey Ready"));
    Serial.println(F("LEFT btn = Record | RIGHT btn = Unlock"));
}

// =============================================================
// Read Accelerometer (returns m/s^2)
// =============================================================
void read_accel(float &x, float &y, float &z) {
    sensors_event_t event;
    lis.getEvent(&event);
    x = event.acceleration.x;
    y = event.acceleration.y;
    z = event.acceleration.z;
}

// =============================================================
// State Handlers
// =============================================================

void handle_idle() {
    // Check left button → Record
    if (btn_left_debounced()) {
        Serial.println(F("--- RECORD MODE ---"));
        gesture_index = 0;
        current_state = STATE_RECORD_WAIT;

        // Purple flash = record mode
        neo_flash(COLOR_PURPLE, 500);
        neo_show_progress(0, COLOR_PURPLE);

        Serial.println(F("Perform gesture 1..."));
    }

    // Check right button → Unlock
    if (btn_right_debounced()) {
        if (!key_loaded) {
            Serial.println(F("No key stored! Record first."));
            neo_flash(COLOR_RED, 300);
            neo_ready_pulse();
            return;
        }
        Serial.println(F("--- UNLOCK MODE ---"));
        gesture_index = 0;
        current_state = STATE_UNLOCK_WAIT;

        // Yellow flash = unlock mode
        neo_flash(COLOR_YELLOW, 500);
        neo_show_progress(0, COLOR_YELLOW);

        Serial.println(F("Perform gesture 1..."));
    }
}

void handle_record_wait() {
    float x, y, z;
    read_accel(x, y, z);

    if (is_moving(x, y, z)) {
        // Motion detected — start capturing
        capture_reset();
        capture_add_sample(x, y, z);
        was_moving = true;
        last_sample_time = millis();
        current_state = STATE_RECORD_CAPTURE;
        Serial.println(F("Capturing..."));
    }
}

void handle_record_capture() {
    unsigned long now = millis();

    // Sample at fixed rate
    if (now - last_sample_time < SAMPLE_RATE_MS) return;
    last_sample_time = now;

    float x, y, z;
    read_accel(x, y, z);

    bool moving = is_moving(x, y, z);

    if (moving) {
        capture_add_sample(x, y, z);
        was_moving = true;
        motion_stop_time = now;
    } else {
        if (was_moving) {
            // Just stopped moving, start stillness timer
            motion_stop_time = now;
            was_moving = false;
        }

        // If still long enough, gesture is done
        if (now - motion_stop_time > STILLNESS_THRESHOLD_MS && sample_count > 5) {
            // Extract features and store
            stored_key[gesture_index] = capture_finalize();

            gesture_index++;
            Serial.print(F("Gesture "));
            Serial.print(gesture_index);
            Serial.println(F(" recorded!"));

            // Show progress
            neo_show_progress(gesture_index, COLOR_PURPLE);

            if (gesture_index >= NUM_GESTURES) {
                // All 3 recorded — save to EEPROM
                storage_save_key(stored_key);
                key_loaded = true;

                Serial.println(F("Key saved to EEPROM!"));
                neo_flash(COLOR_GREEN, 1000);
                neo_ready_pulse();
                current_state = STATE_IDLE;
            } else {
                Serial.print(F("Perform gesture "));
                Serial.println(gesture_index + 1);
                current_state = STATE_RECORD_WAIT;
            }
        }
    }

    // Safety: if buffer full, force finalize
    if (sample_count >= MAX_SAMPLES) {
        stored_key[gesture_index] = capture_finalize();
        gesture_index++;

        neo_show_progress(gesture_index, COLOR_PURPLE);

        if (gesture_index >= NUM_GESTURES) {
            storage_save_key(stored_key);
            key_loaded = true;
            Serial.println(F("Key saved to EEPROM!"));
            neo_flash(COLOR_GREEN, 1000);
            neo_ready_pulse();
            current_state = STATE_IDLE;
        } else {
            current_state = STATE_RECORD_WAIT;
        }
    }
}

void handle_unlock_wait() {
    float x, y, z;
    read_accel(x, y, z);

    if (is_moving(x, y, z)) {
        capture_reset();
        capture_add_sample(x, y, z);
        was_moving = true;
        last_sample_time = millis();
        current_state = STATE_UNLOCK_CAPTURE;
        Serial.println(F("Capturing..."));
    }
}

void handle_unlock_capture() {
    unsigned long now = millis();

    if (now - last_sample_time < SAMPLE_RATE_MS) return;
    last_sample_time = now;

    float x, y, z;
    read_accel(x, y, z);

    bool moving = is_moving(x, y, z);

    if (moving) {
        capture_add_sample(x, y, z);
        was_moving = true;
        motion_stop_time = now;
    } else {
        if (was_moving) {
            motion_stop_time = now;
            was_moving = false;
        }

        if (now - motion_stop_time > STILLNESS_THRESHOLD_MS && sample_count > 5) {
            // Extract and compare
            attempt_key[gesture_index] = capture_finalize();

            float dist = gesture_distance(&attempt_key[gesture_index],
                                          &stored_key[gesture_index]);
            Serial.print(F("Gesture "));
            Serial.print(gesture_index + 1);
            Serial.print(F(" distance: "));
            Serial.println(dist);

            if (gesture_matches(&attempt_key[gesture_index],
                                &stored_key[gesture_index])) {
                // This gesture matched
                gesture_index++;
                Serial.println(F("  -> MATCH!"));

                neo_show_progress(gesture_index, COLOR_GREEN);

                if (gesture_index >= NUM_GESTURES) {
                    // ALL MATCHED — UNLOCKED!
                    Serial.println(F("*** UNLOCKED ***"));
                    current_state = STATE_UNLOCKED;
                } else {
                    Serial.print(F("Perform gesture "));
                    Serial.println(gesture_index + 1);
                    current_state = STATE_UNLOCK_WAIT;
                }
            } else {
                // Mismatch — fail
                Serial.println(F("  -> NO MATCH"));
                current_state = STATE_FAILED;
            }
        }
    }

    if (sample_count >= MAX_SAMPLES) {
        attempt_key[gesture_index] = capture_finalize();

        if (gesture_matches(&attempt_key[gesture_index],
                            &stored_key[gesture_index])) {
            gesture_index++;
            neo_show_progress(gesture_index, COLOR_GREEN);
            if (gesture_index >= NUM_GESTURES) {
                current_state = STATE_UNLOCKED;
            } else {
                current_state = STATE_UNLOCK_WAIT;
            }
        } else {
            current_state = STATE_FAILED;
        }
    }
}

void handle_unlocked() {
    neo_unlock_animation();
    Serial.println(F("Returning to idle..."));
    neo_ready_pulse();
    current_state = STATE_IDLE;
}

void handle_failed() {
    neo_fail_animation();
    Serial.println(F("Try again. RIGHT btn = Unlock"));
    neo_ready_pulse();
    current_state = STATE_IDLE;
}

// =============================================================
// Main Loop
// =============================================================
void loop() {
    switch (current_state) {
        case STATE_IDLE:            handle_idle();              break;
        case STATE_RECORD_WAIT:     handle_record_wait();       break;
        case STATE_RECORD_CAPTURE:  handle_record_capture();    break;
        case STATE_UNLOCK_WAIT:     handle_unlock_wait();       break;
        case STATE_UNLOCK_CAPTURE:  handle_unlock_capture();    break;
        case STATE_UNLOCKED:        handle_unlocked();          break;
        case STATE_FAILED:          handle_failed();            break;
    }
}
