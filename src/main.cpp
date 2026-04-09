// =============================================================
// KinetiKey — "Old Lock, New Twist"
// Embedded Challenge Spring 2026
//
// Board:  Adafruit Circuit Playground Classic (ATmega32u4 @ 16MHz)
// Accel:  LIS3DH (onboard, I2C @ 0x18)
// IDE:    PlatformIO (Arduino framework)
//
// CONTROLS:
//   LEFT  button → Record new 3-gesture key
//   RIGHT button → Unlock attempt
//   BOTH  buttons held → Cancel current operation / soft reset
//   Slide switch LEFT + LEFT button in IDLE → Erase stored key
//
// FLOW:
//   Record: Press LEFT → draw number 1 → draw number 2 → draw number 3
//           → key saved to EEPROM → back to idle
//
//   Unlock: Press RIGHT → draw number 1 → draw number 2 → draw number 3
//           → if all match → green animation (unlocked!)
//           → if any fails → red flash (locked)
//
// LED COLORS:
//   Blue dots          = idle, ready
//   Purple             = recording mode
//   Yellow             = unlock mode
//   Green progressive  = each correct gesture
//   Green chase        = fully unlocked
//   Red blink          = failed / error
//   Orange blink       = no key stored yet
//   White spinning     = currently capturing gesture
//
// SERIAL MONITOR:
//   Open at 9600 baud for debug info including gesture distances,
//   which is essential for tuning MATCH_THRESHOLD in pin_config.h.
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>

#include "pin_config.h"
#include "gpio_reg.h"
#include "gesture.h"
#include "storage.h"

// =============================================================
// Accelerometer (I2C via Adafruit library — allowed per rules)
// =============================================================
Adafruit_LIS3DH accel = Adafruit_LIS3DH();

// =============================================================
// State Machine
// =============================================================
enum State {
    ST_IDLE,                // Waiting for button press
    ST_RECORD_WAIT,         // Record mode: waiting for user to start gesture
    ST_RECORD_CAPTURE,      // Record mode: capturing gesture data
    ST_UNLOCK_WAIT,         // Unlock mode: waiting for user to start gesture
    ST_UNLOCK_CAPTURE,      // Unlock mode: capturing gesture data
    ST_UNLOCKED,            // Unlock success — show animation
    ST_FAILED               // Unlock failed — show animation
};

static State        state = ST_IDLE;
static uint8_t      gesture_idx = 0;       // Which gesture (0, 1, 2) we're on
static GestureBins  stored_key[NUM_GESTURES];   // Loaded from EEPROM
static bool         key_exists = false;         // Is there a valid key?

// Timing
static unsigned long state_enter_time = 0;      // When we entered current state
static unsigned long last_sample_ms = 0;        // Last accelerometer sample time
static unsigned long last_motion_ms = 0;        // Last time motion was detected
static bool          saw_motion = false;         // Did we see motion this capture?
static uint8_t       anim_tick = 0;              // For spinning animation

// =============================================================
// Helper: Read raw accelerometer data
// =============================================================
static void read_accel_raw(int16_t &x, int16_t &y, int16_t &z) {
    accel.read();       // Updates accel.x, accel.y, accel.z (int16_t)
    x = accel.x;
    y = accel.y;
    z = accel.z;
}

// =============================================================
// Helper: Transition to a new state
// =============================================================
static void go_to(State new_state) {
    state = new_state;
    state_enter_time = millis();
    saw_motion = false;
    anim_tick = 0;
}

// =============================================================
// Helper: Check for cancel (both buttons held)
// Returns true if cancelled — caller should return immediately.
// =============================================================
static bool check_cancel(void) {
    if (both_buttons_held()) {
        Serial.println(F("--- CANCELLED ---"));
        neo_cancel_feedback();
        neo_idle_indicator();
        go_to(ST_IDLE);
        // Wait for buttons to be released
        while (btn_left_raw() || btn_right_raw()) { _delay_ms(10); }
        return true;
    }
    return false;
}

// =============================================================
// SETUP
// =============================================================
void setup() {
    // --- Serial for debug ---
    Serial.begin(9600);

    // --- Register-level GPIO init ---
    gpio_init();

    // --- Boot animation ---
    neo_boot_animation();

    // --- Init accelerometer ---
    if (!accel.begin(0x18)) {   // I2C address on CP Classic
        Serial.println(F("ERROR: LIS3DH not found!"));
        neo_error();  // Blinks red forever — will not continue
    }

    // Set range to ±4g (good for hand gestures — not too sensitive, not too dull)
    accel.setRange(LIS3DH_RANGE_4_G);
    // Set data rate to 50Hz (matches our SAMPLE_RATE_MS of 20ms)
    accel.setDataRate(LIS3DH_DATARATE_50_HZ);

    Serial.println(F("LIS3DH initialized (I2C, ±4g, 50Hz)"));

    // --- Load key from EEPROM if it exists ---
    key_exists = storage_load(stored_key);
    if (key_exists) {
        Serial.println(F("Stored key loaded from EEPROM."));
        neo_flash(COLOR_GREEN, 300);
    } else {
        Serial.println(F("No stored key found. Record one first."));
        neo_flash(COLOR_ORANGE, 300);
    }

    // --- Go to idle ---
    neo_idle_indicator();
    go_to(ST_IDLE);

    Serial.println(F(""));
    Serial.println(F("=== KinetiKey Ready ==="));
    Serial.println(F("LEFT  button = Record new key"));
    Serial.println(F("RIGHT button = Unlock attempt"));
    Serial.println(F("BOTH  buttons = Cancel / Reset"));
    Serial.println(F("======================"));
    Serial.println(F(""));
}

// =============================================================
// STATE: IDLE
// =============================================================
static void do_idle(void) {
    // --- LEFT button: Record ---
    if (btn_left_pressed()) {
        // If slide switch is to the left AND left button pressed → erase key
        if (switch_left()) {
            Serial.println(F("*** ERASING stored key ***"));
            storage_erase();
            key_exists = false;
            neo_flash(COLOR_RED, 500);
            _delay_ms(200);
            neo_flash(COLOR_RED, 500);
            neo_idle_indicator();
            return;
        }

        Serial.println(F(""));
        Serial.println(F("=== RECORD MODE ==="));
        Serial.println(F("Draw 3 numbers in the air while holding the board."));
        gesture_idx = 0;

        neo_flash(COLOR_PURPLE, 400);

        // Wait for button release + settle time
        while (btn_left_raw()) { _delay_ms(10); }
        _delay_ms(SETTLE_MS);

        go_to(ST_RECORD_WAIT);
        neo_waiting_pulse(COLOR_PURPLE, 0);

        Serial.print(F("Waiting for gesture 1 of 3..."));
        Serial.println();
        return;
    }

    // --- RIGHT button: Unlock ---
    if (btn_right_pressed()) {
        if (!key_exists) {
            Serial.println(F("No key stored! Record one first (LEFT button)."));
            neo_no_key_warning();
            neo_idle_indicator();
            return;
        }

        Serial.println(F(""));
        Serial.println(F("=== UNLOCK MODE ==="));
        Serial.println(F("Replicate your 3-gesture key."));
        gesture_idx = 0;

        neo_flash(COLOR_YELLOW, 400);

        // Wait for button release + settle time
        while (btn_right_raw()) { _delay_ms(10); }
        _delay_ms(SETTLE_MS);

        go_to(ST_UNLOCK_WAIT);
        neo_waiting_pulse(COLOR_YELLOW, 0);

        Serial.print(F("Waiting for gesture 1 of 3..."));
        Serial.println();
        return;
    }
}

// =============================================================
// STATE: RECORD_WAIT — Waiting for motion to start a gesture
// =============================================================
static void do_record_wait(void) {
    if (check_cancel()) return;

    // Timeout check
    if (millis() - state_enter_time > GESTURE_TIMEOUT_MS) {
        Serial.println(F("Timeout — no gesture detected. Returning to idle."));
        neo_flash(COLOR_ORANGE, 300);
        neo_idle_indicator();
        go_to(ST_IDLE);
        return;
    }

    // Blink waiting indicator periodically
    if ((millis() / 500) % 2 == 0) {
        neo_waiting_pulse(COLOR_PURPLE, gesture_idx);
    } else {
        neo_show_progress(gesture_idx, COLOR_PURPLE);
    }

    // Check for motion
    int16_t x, y, z;
    read_accel_raw(x, y, z);

    if (is_moving_raw(x, y, z)) {
        // Motion started — begin capture
        capture_reset();
        capture_add(x, y, z);
        saw_motion = true;
        last_motion_ms = millis();
        last_sample_ms = millis();

        Serial.print(F("  Capturing gesture "));
        Serial.print(gesture_idx + 1);
        Serial.println(F("..."));

        go_to(ST_RECORD_CAPTURE);
        // Keep saw_motion = true (set above, go_to resets it but we set after)
        saw_motion = true;
    }
}

// =============================================================
// STATE: RECORD_CAPTURE — Recording gesture data
// =============================================================
static void do_record_capture(void) {
    if (check_cancel()) return;

    unsigned long now = millis();

    // Sampling rate limiter
    if (now - last_sample_ms < SAMPLE_RATE_MS) return;
    last_sample_ms = now;

    // Animate
    anim_tick++;
    neo_capturing_tick(COLOR_PURPLE, gesture_idx, anim_tick);

    // Read sample
    int16_t x, y, z;
    read_accel_raw(x, y, z);

    bool moving = is_moving_raw(x, y, z);

    if (moving) {
        capture_add(x, y, z);
        last_motion_ms = now;
        saw_motion = true;
    } else {
        // Still add samples during brief pauses (part of the number shape)
        capture_add(x, y, z);

        // Check if stillness has lasted long enough to end gesture
        if (saw_motion && (now - last_motion_ms > STILLNESS_MS)) {
            // Gesture done — finalize
            if (!capture_valid()) {
                Serial.println(F("  Too short! Try again."));
                neo_flash(COLOR_ORANGE, 200);
                go_to(ST_RECORD_WAIT);
                neo_waiting_pulse(COLOR_PURPLE, gesture_idx);
                return;
            }

            GestureBins bins = capture_finalize();
            stored_key[gesture_idx] = bins;

            gesture_idx++;
            Serial.print(F("  Gesture "));
            Serial.print(gesture_idx);
            Serial.println(F(" of 3 recorded."));

            if (gesture_idx >= NUM_GESTURES) {
                // All 3 recorded — save to EEPROM
                storage_save(stored_key);
                key_exists = true;

                Serial.println(F(""));
                Serial.println(F("*** KEY SAVED TO EEPROM ***"));
                Serial.println(F(""));

                neo_show_progress(NUM_GESTURES, COLOR_PURPLE);
                _delay_ms(300);
                neo_saved_animation();
                neo_idle_indicator();
                go_to(ST_IDLE);
            } else {
                // More gestures to record
                neo_show_progress(gesture_idx, COLOR_PURPLE);
                _delay_ms(500);  // Brief pause between gestures

                Serial.print(F("Waiting for gesture "));
                Serial.print(gesture_idx + 1);
                Serial.println(F(" of 3..."));

                go_to(ST_RECORD_WAIT);
                neo_waiting_pulse(COLOR_PURPLE, gesture_idx);
            }
            return;
        }
    }

    // Safety: buffer full → force finalize
    if (raw_count >= MAX_SAMPLES) {
        Serial.println(F("  (buffer full, finalizing)"));
        if (!capture_valid()) {
            neo_flash(COLOR_ORANGE, 200);
            go_to(ST_RECORD_WAIT);
            return;
        }

        GestureBins bins = capture_finalize();
        stored_key[gesture_idx] = bins;
        gesture_idx++;

        Serial.print(F("  Gesture "));
        Serial.print(gesture_idx);
        Serial.println(F(" of 3 recorded."));

        if (gesture_idx >= NUM_GESTURES) {
            storage_save(stored_key);
            key_exists = true;
            Serial.println(F("*** KEY SAVED TO EEPROM ***"));
            neo_show_progress(NUM_GESTURES, COLOR_PURPLE);
            _delay_ms(300);
            neo_saved_animation();
            neo_idle_indicator();
            go_to(ST_IDLE);
        } else {
            neo_show_progress(gesture_idx, COLOR_PURPLE);
            _delay_ms(500);
            Serial.print(F("Waiting for gesture "));
            Serial.print(gesture_idx + 1);
            Serial.println(F(" of 3..."));
            go_to(ST_RECORD_WAIT);
        }
    }
}

// =============================================================
// STATE: UNLOCK_WAIT — Waiting for motion to start a gesture
// =============================================================
static void do_unlock_wait(void) {
    if (check_cancel()) return;

    // Timeout
    if (millis() - state_enter_time > GESTURE_TIMEOUT_MS) {
        Serial.println(F("Timeout. Returning to idle."));
        neo_flash(COLOR_ORANGE, 300);
        neo_idle_indicator();
        go_to(ST_IDLE);
        return;
    }

    // Blink waiting indicator
    if ((millis() / 500) % 2 == 0) {
        neo_waiting_pulse(COLOR_YELLOW, gesture_idx);
    } else {
        neo_show_progress(gesture_idx, COLOR_GREEN);
    }

    // Check for motion
    int16_t x, y, z;
    read_accel_raw(x, y, z);

    if (is_moving_raw(x, y, z)) {
        capture_reset();
        capture_add(x, y, z);
        saw_motion = true;
        last_motion_ms = millis();
        last_sample_ms = millis();

        Serial.print(F("  Capturing gesture "));
        Serial.print(gesture_idx + 1);
        Serial.println(F("..."));

        go_to(ST_UNLOCK_CAPTURE);
        saw_motion = true;
    }
}

// =============================================================
// STATE: UNLOCK_CAPTURE — Capturing and comparing gesture
// =============================================================
static void do_unlock_capture(void) {
    if (check_cancel()) return;

    unsigned long now = millis();

    if (now - last_sample_ms < SAMPLE_RATE_MS) return;
    last_sample_ms = now;

    anim_tick++;
    neo_capturing_tick(COLOR_YELLOW, gesture_idx, anim_tick);

    int16_t x, y, z;
    read_accel_raw(x, y, z);

    bool moving = is_moving_raw(x, y, z);

    if (moving) {
        capture_add(x, y, z);
        last_motion_ms = now;
        saw_motion = true;
    } else {
        capture_add(x, y, z);

        if (saw_motion && (now - last_motion_ms > STILLNESS_MS)) {
            if (!capture_valid()) {
                Serial.println(F("  Too short! Try again."));
                neo_flash(COLOR_ORANGE, 200);
                go_to(ST_UNLOCK_WAIT);
                neo_waiting_pulse(COLOR_YELLOW, gesture_idx);
                return;
            }

            // Finalize and compare
            GestureBins attempt = capture_finalize();
            float dist = gesture_distance(&attempt, &stored_key[gesture_idx]);

            Serial.print(F("  Gesture "));
            Serial.print(gesture_idx + 1);
            Serial.print(F(" distance: "));
            Serial.print(dist, 3);
            Serial.print(F("  threshold: "));
            Serial.print(MATCH_THRESHOLD, 3);

            if (gesture_matches(&attempt, &stored_key[gesture_idx])) {
                Serial.println(F("  → MATCH"));
                gesture_idx++;

                if (gesture_idx >= NUM_GESTURES) {
                    // ALL MATCHED — UNLOCKED
                    Serial.println(F(""));
                    Serial.println(F("***** UNLOCKED! *****"));
                    Serial.println(F(""));
                    go_to(ST_UNLOCKED);
                } else {
                    neo_show_progress(gesture_idx, COLOR_GREEN);
                    _delay_ms(500);

                    Serial.print(F("Waiting for gesture "));
                    Serial.print(gesture_idx + 1);
                    Serial.println(F(" of 3..."));

                    go_to(ST_UNLOCK_WAIT);
                    neo_waiting_pulse(COLOR_YELLOW, gesture_idx);
                }
            } else {
                Serial.println(F("  → NO MATCH"));
                Serial.println(F(""));
                go_to(ST_FAILED);
            }
            return;
        }
    }

    // Buffer full → force compare
    if (raw_count >= MAX_SAMPLES) {
        Serial.println(F("  (buffer full, comparing)"));
        if (!capture_valid()) {
            neo_flash(COLOR_ORANGE, 200);
            go_to(ST_UNLOCK_WAIT);
            return;
        }

        GestureBins attempt = capture_finalize();
        float dist = gesture_distance(&attempt, &stored_key[gesture_idx]);

        Serial.print(F("  Distance: "));
        Serial.println(dist, 3);

        if (gesture_matches(&attempt, &stored_key[gesture_idx])) {
            Serial.println(F("  → MATCH"));
            gesture_idx++;
            if (gesture_idx >= NUM_GESTURES) {
                go_to(ST_UNLOCKED);
            } else {
                neo_show_progress(gesture_idx, COLOR_GREEN);
                _delay_ms(500);
                go_to(ST_UNLOCK_WAIT);
            }
        } else {
            Serial.println(F("  → NO MATCH"));
            go_to(ST_FAILED);
        }
    }
}

// =============================================================
// STATE: UNLOCKED — Success!
// =============================================================
static void do_unlocked(void) {
    neo_success_animation();
    Serial.println(F("Lock re-engaged. Returning to idle."));
    Serial.println(F(""));
    neo_idle_indicator();
    go_to(ST_IDLE);
}

// =============================================================
// STATE: FAILED — Wrong gesture
// =============================================================
static void do_failed(void) {
    neo_fail_animation();
    Serial.println(F("Unlock failed. Press RIGHT to try again."));
    Serial.println(F(""));
    neo_idle_indicator();
    go_to(ST_IDLE);
}

// =============================================================
// MAIN LOOP
// =============================================================
void loop() {
    switch (state) {
        case ST_IDLE:           do_idle();              break;
        case ST_RECORD_WAIT:    do_record_wait();       break;
        case ST_RECORD_CAPTURE: do_record_capture();    break;
        case ST_UNLOCK_WAIT:    do_unlock_wait();       break;
        case ST_UNLOCK_CAPTURE: do_unlock_capture();    break;
        case ST_UNLOCKED:       do_unlocked();          break;
        case ST_FAILED:         do_failed();            break;
    }
}
