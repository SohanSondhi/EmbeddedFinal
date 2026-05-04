#include <Arduino.h>

#include "pin_config.h"
#include "gpio_reg.h"
#include "gesture.h"
#include "storage.h"
#include "spi_reg.h"

enum State {
    ST_IDLE,
    ST_RECORD_WAIT,
    ST_RECORD_CAPTURE,
    ST_UNLOCK_WAIT,
    ST_UNLOCK_CAPTURE,
    ST_UNLOCKED,
    ST_FAILED
};

enum Mode { MODE_RECORD, MODE_UNLOCK };

static State state = ST_IDLE;
static Mode mode = MODE_RECORD;
static uint8_t gesture_idx = 0;
static GestureBins stored_key[NUM_GESTURES];
static bool key_exists = false;
static unsigned long state_enter_time = 0, last_sample_ms = 0, last_motion_ms = 0;
static bool saw_motion = false;
static uint8_t anim_tick = 0, retries_left = MAX_RETRIES;

static void read_accel_raw(int16_t &x, int16_t &y, int16_t &z) {
    accel_spi_read_xyz(x, y, z);
}

static void go_to(State s) {
    state = s;
    state_enter_time = millis();
    saw_motion = false;
    anim_tick = 0;
}

static bool check_cancel(void) {
    if (both_buttons_held()) {
        Serial.println(F("--- CANCELLED ---"));
        neo_cancel_feedback();
        neo_idle_indicator();
        go_to(ST_IDLE);
        while (btn_left_raw() || btn_right_raw()) _delay_ms(10);
        return true;
    }
    return false;
}

// Countdown animation before each gesture capture. Returns false if cancelled.
static bool do_countdown(void) {
    uint32_t c  = (mode == MODE_RECORD) ? COLOR_PURPLE : COLOR_YELLOW;
    uint32_t pc = (mode == MODE_RECORD) ? COLOR_PURPLE : COLOR_GREEN;

    Serial.print(F("  Ready G")); Serial.print(gesture_idx + 1);
    if (mode == MODE_UNLOCK) {
        Serial.print(F(" (")); Serial.print(retries_left); Serial.print(F(" tries)"));
    }
    Serial.println();

    uint16_t step = COUNTDOWN_MS / NUM_PIXELS;
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        CircuitPlayground.strip.clear();
        for (uint8_t g = 0; g < gesture_idx; g++)
            for (uint8_t p = g*3; p < (g*3)+3 && p < NUM_PIXELS; p++) neo_set(p, pc);
        neo_set(NUM_PIXELS - 1 - i, c);
        neo_show();
        for (uint16_t t = 0; t < step/10; t++) {
            _delay_ms(10);
            if (both_buttons_held()) { check_cancel(); return false; }
        }
    }

    neo_fill(c); neo_show(); _delay_ms(300); neo_clear();
    Serial.print(F("  GO! G")); Serial.println(gesture_idx + 1);
    return true;
}

void setup() {
    Serial.begin(9600);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 2000) _delay_ms(10);

    gpio_init();
    neo_boot_animation();

    accel_spi_init();
    if (accel_spi_ok()) {
        Serial.println(F("LIS3DH SPI OK"));
    } else {
        Serial.println(F("LIS3DH SPI check failed"));
        neo_flash(COLOR_ORANGE, 500);
    }

    key_exists = storage_load(stored_key);
    if (key_exists) { Serial.println(F("Key loaded.")); neo_flash(COLOR_GREEN, 500); }
    else             { Serial.println(F("No key stored. LEFT to record.")); neo_flash(COLOR_ORANGE, 500); }

    neo_idle_indicator();
    go_to(ST_IDLE);

    Serial.println(F("\n=== KinetiKey ==="));
    Serial.print(F(" Retries=")); Serial.println(MAX_RETRIES);
    Serial.println(F("LEFT=Record | RIGHT=Unlock | LEFT3s=Erase key | BOTH=Cancel\n"));
}

// 1) Idle
static void do_idle(void) {
    if (btn_left_pressed()) {
        unsigned long hs = millis();
        uint8_t lp = 0;

        while (btn_left_raw()) {
            unsigned long held = millis() - hs;
            uint8_t px = (held * NUM_PIXELS) / ERASE_HOLD_MS;
            if (px > NUM_PIXELS) px = NUM_PIXELS;
            if (px != lp) {
                CircuitPlayground.strip.clear();
                for (uint8_t p = 0; p < px; p++) neo_set(p, COLOR_RED);
                neo_show();
                lp = px;
            }
            if (held >= ERASE_HOLD_MS) {
                Serial.println(F("*** KEY ERASED ***"));
                storage_erase();
                key_exists = false;
                neo_erase_animation();
                neo_idle_indicator();
                while (btn_left_raw()) _delay_ms(10);
                return;
            }
            _delay_ms(10);
        }

        neo_clear(); _delay_ms(100);
        Serial.println(F("\n=== RECORD ==="));
        gesture_idx = 0;
        mode = MODE_RECORD;
        neo_flash(COLOR_PURPLE, 300);
        if (!do_countdown()) return;
        go_to(ST_RECORD_WAIT);
        return;
    }

    if (btn_right_pressed()) {
        if (!key_exists) {
            Serial.println(F("No key! LEFT to record."));
            neo_no_key_warning();
            neo_idle_indicator();
            return;
        }

        Serial.println(F("\n=== UNLOCK ==="));
        gesture_idx = 0;
        retries_left = MAX_RETRIES;
        mode = MODE_UNLOCK;
        neo_flash(COLOR_YELLOW, 300);
        while (btn_right_raw()) _delay_ms(10);
        if (!do_countdown()) return;
        go_to(ST_UNLOCK_WAIT);
        return;
    }
}

// 2) Record wait
static void do_record_wait(void) {
    if (check_cancel()) return;

    if (millis() - state_enter_time > GESTURE_TIMEOUT_MS) {
        Serial.println(F("Timeout."));
        neo_flash(COLOR_ORANGE, 300);
        neo_idle_indicator();
        go_to(ST_IDLE);
        return;
    }

    if ((millis()/400)%2==0) neo_waiting_pulse(COLOR_PURPLE, gesture_idx);
    else neo_show_progress(gesture_idx, COLOR_PURPLE);

    int16_t x, y, z;
    read_accel_raw(x, y, z);

    if (is_moving_raw(x, y, z)) {
        capture_reset();
        capture_add(x, y, z);
        saw_motion = true;
        last_motion_ms = millis();
        last_sample_ms = millis();
        Serial.print(F("  Capturing G")); Serial.println(gesture_idx + 1);
        go_to(ST_RECORD_CAPTURE);
    }
}

// 3) Record capture
static void do_record_capture(void) {
    if (check_cancel()) return;

    unsigned long now = millis();
    if (now - last_sample_ms < SAMPLE_RATE_MS) return;

    last_sample_ms = now;
    anim_tick++;
    neo_capturing_tick(COLOR_PURPLE, gesture_idx, anim_tick);

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
                Serial.println(F("  Too short!"));
                neo_flash(COLOR_ORANGE, 300);
                if (!do_countdown()) return;
                go_to(ST_RECORD_WAIT); return;
            }
            capture_dump_serial();
            GestureBins bins = capture_finalize();
            stored_key[gesture_idx] = bins;
            bins_dump_serial(&bins, gesture_idx);

            gesture_idx++;
            Serial.print(F("  G")); Serial.print(gesture_idx); Serial.println(F("/3 recorded!"));

            if (gesture_idx >= NUM_GESTURES) {
                storage_save(stored_key);
                key_exists = true;
                Serial.println(F("\n*** KEY SAVED ***\n"));
                neo_show_progress(NUM_GESTURES, COLOR_PURPLE);
                for (uint16_t i = 0; i < 100; i++) _delay_ms(10);
                neo_saved_animation();
                neo_idle_indicator();
                go_to(ST_IDLE);
            } else {
                neo_show_progress(gesture_idx, COLOR_PURPLE);
                for (uint16_t i = 0; i < 200; i++) _delay_ms(10);
                if (!do_countdown()) return;
                go_to(ST_RECORD_WAIT);
            }
            return;
        }
    }

    if (raw_count >= MAX_SAMPLES) {
        if (!capture_valid()) {
            neo_flash(COLOR_ORANGE, 200);
            if (!do_countdown()) return;
            go_to(ST_RECORD_WAIT);
            return;
        }
        capture_dump_serial();
        GestureBins bins = capture_finalize();
        stored_key[gesture_idx] = bins;
        gesture_idx++;

        if (gesture_idx >= NUM_GESTURES) {
            storage_save(stored_key);
            key_exists = true;
            Serial.println(F("*** KEY SAVED ***"));
            neo_saved_animation();
            neo_idle_indicator();
            go_to(ST_IDLE);
        } else {
            neo_show_progress(gesture_idx, COLOR_PURPLE);
            for (uint16_t i = 0; i < 200; i++) _delay_ms(10);
            if (!do_countdown()) return;
            go_to(ST_RECORD_WAIT);
        }
    }
}

// 4) Unlock wait
static void do_unlock_wait(void) {
    if (check_cancel()) return;

    if (millis() - state_enter_time > GESTURE_TIMEOUT_MS) {
        Serial.println(F("Timeout."));
        neo_flash(COLOR_ORANGE, 300);
        neo_idle_indicator();
        go_to(ST_IDLE);
        return;
    }

    if ((millis()/400)%2==0) neo_waiting_pulse(COLOR_YELLOW, gesture_idx);
    else neo_show_progress(gesture_idx, COLOR_GREEN);

    int16_t x, y, z;
    read_accel_raw(x, y, z);

    if (is_moving_raw(x, y, z)) {
        capture_reset();
        capture_add(x, y, z);
        saw_motion = true;
        last_motion_ms = millis();
        last_sample_ms = millis();
        Serial.print(F("  Capturing G")); Serial.println(gesture_idx + 1);
        go_to(ST_UNLOCK_CAPTURE);
    }
}

// 5) Unlock capture
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
                Serial.println(F("  Too short!"));
                neo_flash(COLOR_ORANGE, 300);
                if (!do_countdown()) return;
                go_to(ST_UNLOCK_WAIT);
                return;
            }

            capture_dump_serial();
            GestureBins attempt = capture_finalize();
            bins_dump_serial(&attempt, gesture_idx);
            float dist = gesture_distance(&attempt, &stored_key[gesture_idx]);
            Serial.print(F("  G")); Serial.print(gesture_idx + 1);
            Serial.print(F(" dist=")); Serial.print(dist, 3);
            Serial.print(F(" thr=")); Serial.print(MATCH_THRESHOLD, 3);

            if (gesture_matches(&attempt, &stored_key[gesture_idx])) {
                Serial.println(F(" MATCH!"));
                gesture_idx++;
                retries_left = MAX_RETRIES;
                if (gesture_idx >= NUM_GESTURES) {
                    Serial.println(F("\n***** UNLOCKED! *****\n"));
                    go_to(ST_UNLOCKED);
                } else {
                    neo_show_progress(gesture_idx, COLOR_GREEN);
                    for (uint16_t i = 0; i < 200; i++) _delay_ms(10);
                    if (!do_countdown()) return;
                    go_to(ST_UNLOCK_WAIT);
                }
            } else {
                retries_left--;
                Serial.print(F(" NO MATCH (")); Serial.print(retries_left); Serial.println(F(" left)"));
                if (retries_left == 0) {
                    Serial.println(F("\n*** ALL RETRIES USED ***\n"));
                    go_to(ST_FAILED);
                } else {
                    neo_flash(COLOR_ORANGE, 400);
                    neo_show_progress(gesture_idx, COLOR_GREEN);
                    for (uint16_t i = 0; i < 100; i++) _delay_ms(10);
                    if (!do_countdown()) return;
                    go_to(ST_UNLOCK_WAIT);
                }
            }
            return;
        }
    }

    if (raw_count >= MAX_SAMPLES) {
        if (!capture_valid()) {
            neo_flash(COLOR_ORANGE, 200);
            if (!do_countdown()) return;
            go_to(ST_UNLOCK_WAIT);
            return;
        }
        capture_dump_serial();
        GestureBins attempt = capture_finalize();
        float dist = gesture_distance(&attempt, &stored_key[gesture_idx]);
        Serial.print(F("  dist=")); Serial.print(dist, 3);

        if (gesture_matches(&attempt, &stored_key[gesture_idx])) {
            Serial.println(F(" MATCH!"));
            gesture_idx++;
            retries_left = MAX_RETRIES;
            if (gesture_idx >= NUM_GESTURES) {
                Serial.println(F("\n***** UNLOCKED! *****\n"));
                go_to(ST_UNLOCKED);
            } else {
                neo_show_progress(gesture_idx, COLOR_GREEN);
                for (uint16_t i = 0; i < 200; i++) _delay_ms(10);
                if (!do_countdown()) return;
                go_to(ST_UNLOCK_WAIT);
            }
        } else {
            retries_left--;
            Serial.print(F(" NO MATCH (")); Serial.print(retries_left); Serial.println(F(" left)"));
            if (retries_left == 0) {
                Serial.println(F("\n*** ALL RETRIES USED ***\n"));
                go_to(ST_FAILED);
            } else {
                Serial.println(F(" TRY AGAIN"));
                neo_flash(COLOR_ORANGE, 400);
                if (!do_countdown()) return;
                go_to(ST_UNLOCK_WAIT);
            }
        }
    }
}

// 6) Unlocked
static void do_unlocked(void) {
    neo_success_animation();
    Serial.println(F("Locked. Returning to idle.\n"));
    neo_idle_indicator();
    go_to(ST_IDLE);
}

// 7) Failed
static void do_failed(void) {
    neo_fail_animation();
    Serial.println(F("Failed. RIGHT to retry.\n"));
    neo_idle_indicator();
    go_to(ST_IDLE);
}

void loop() {
    switch (state) {
        case ST_IDLE: 
            do_idle(); 
            break; 

        case ST_RECORD_WAIT: 
            do_record_wait(); 
            break;

        case ST_RECORD_CAPTURE: 
            do_record_capture(); 
            break;

        case ST_UNLOCK_WAIT: 
            do_unlock_wait(); 
            break;

        case ST_UNLOCK_CAPTURE: 
            do_unlock_capture(); 
            break; 

        case ST_UNLOCKED: 
            do_unlocked(); 
            break;

        case ST_FAILED: 
            do_failed(); 
            break;
    }
}
