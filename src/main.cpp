// =============================================================
// KinetiKey V3 — Position-Based Path Matching
//
// CONTROLS:
//   Short press LEFT       → Record new 3-gesture key
//   Hold LEFT 3 seconds    → Erase stored key
//   Press RIGHT            → Unlock attempt
//   BOTH buttons           → Cancel current operation
// =============================================================

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>

#include "pin_config.h"
#include "gpio_reg.h"
#include "gesture.h"
#include "storage.h"

Adafruit_LIS3DH accel = Adafruit_LIS3DH(8);

enum State {
    ST_IDLE, ST_COUNTDOWN, ST_RECORD_WAIT, ST_RECORD_CAPTURE,
    ST_UNLOCK_WAIT, ST_UNLOCK_CAPTURE, ST_UNLOCKED, ST_FAILED
};
enum Mode { MODE_RECORD, MODE_UNLOCK };

static State   state = ST_IDLE;
static Mode    mode  = MODE_RECORD;
static uint8_t gesture_idx = 0;
static GestureBins stored_key[NUM_GESTURES];
static bool    key_exists = false;

static unsigned long state_enter_time = 0;
static unsigned long last_sample_ms = 0;
static unsigned long last_motion_ms = 0;
static bool          saw_motion = false;
static uint8_t       anim_tick = 0;

static void read_accel_raw(int16_t &x, int16_t &y, int16_t &z) {
    accel.read(); x = accel.x; y = accel.y; z = accel.z;
}

static void go_to(State s) {
    state = s; state_enter_time = millis(); saw_motion = false; anim_tick = 0;
}

static bool check_cancel(void) {
    if (both_buttons_held()) {
        Serial.println(F("--- CANCELLED ---"));
        neo_cancel_feedback(); neo_idle_indicator(); go_to(ST_IDLE);
        while (btn_left_raw() || btn_right_raw()) _delay_ms(10);
        return true;
    }
    return false;
}

static void start_countdown(Mode m) {
    mode = m;
    go_to(ST_COUNTDOWN);
    Color c = (m == MODE_RECORD) ? COLOR_PURPLE : COLOR_YELLOW;
    Color pc = (m == MODE_RECORD) ? COLOR_PURPLE : COLOR_GREEN;

    Serial.print(F("  Get ready for gesture "));
    Serial.print(gesture_idx + 1);
    Serial.println(F("..."));

    uint16_t step = COUNTDOWN_MS / NUM_PIXELS;
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        neo_fill(COLOR_OFF);
        for (uint8_t g = 0; g < gesture_idx; g++)
            for (uint8_t p = g*3; p < (g*3)+3 && p < NUM_PIXELS; p++)
                pixel_buf[p] = pc;
        neo_set(NUM_PIXELS-1-i, c);
        neo_show();
        for (uint16_t t = 0; t < step/10; t++) {
            _delay_ms(10);
            if (both_buttons_held()) { check_cancel(); return; }
        }
    }

    neo_fill(c); neo_show(); _delay_ms(300); neo_clear();

    Serial.print(F("  GO! Draw gesture "));
    Serial.print(gesture_idx+1);
    Serial.println(F(" now!"));

    go_to((m == MODE_RECORD) ? ST_RECORD_WAIT : ST_UNLOCK_WAIT);
}

// =============================================================
// SETUP
// =============================================================
void setup() {
    Serial.begin(9600);
    while (!Serial) delay(10);

    gpio_init();
    neo_boot_animation();

    if (!accel.begin()) {
        Serial.println(F("ERROR: LIS3DH not found!"));
        neo_error();
    }
    accel.setRange(LIS3DH_RANGE_4_G);
    accel.setDataRate(LIS3DH_DATARATE_50_HZ);

    Serial.println(F("LIS3DH OK (SPI, +/-4g, 50Hz)"));
    Serial.print(F("V3 Path Matching | Threshold: "));
    Serial.println(MATCH_THRESHOLD, 2);

    key_exists = storage_load(stored_key);
    if (key_exists) {
        Serial.println(F("Key loaded from EEPROM."));
        neo_flash(COLOR_GREEN, 500);
    } else {
        Serial.println(F("No key stored."));
        neo_flash(COLOR_ORANGE, 500);
    }

    neo_idle_indicator();
    go_to(ST_IDLE);

    Serial.println(F(""));
    Serial.println(F("=== KinetiKey V3 ==="));
    Serial.println(F("LEFT short=Record | LEFT 3s=Erase | RIGHT=Unlock | BOTH=Cancel"));
    Serial.println(F(""));
}

// =============================================================
// IDLE
// =============================================================
static void do_idle(void) {
    if (btn_left_raw()) {
        unsigned long hold_start = millis();
        uint8_t last_px = 0;
        while (btn_left_raw()) {
            unsigned long held = millis() - hold_start;
            uint8_t px = (held * NUM_PIXELS) / ERASE_HOLD_MS;
            if (px > NUM_PIXELS) px = NUM_PIXELS;
            if (px != last_px) {
                neo_fill(COLOR_OFF);
                for (uint8_t p = 0; p < px; p++) neo_set(p, COLOR_RED);
                neo_show();
                last_px = px;
            }
            if (held >= ERASE_HOLD_MS) {
                Serial.println(F("*** ERASING stored key ***"));
                storage_erase(); key_exists = false;
                neo_erase_animation(); neo_idle_indicator();
                while (btn_left_raw()) _delay_ms(10);
                return;
            }
            _delay_ms(10);
        }
        neo_clear(); _delay_ms(100);
        Serial.println(F(""));
        Serial.println(F("=== RECORD MODE ==="));
        gesture_idx = 0;
        neo_flash(COLOR_PURPLE, 400);
        start_countdown(MODE_RECORD);
        return;
    }

    if (btn_right_pressed()) {
        if (!key_exists) {
            Serial.println(F("No key! Press LEFT to record."));
            neo_no_key_warning(); neo_idle_indicator();
            return;
        }
        Serial.println(F(""));
        Serial.println(F("=== UNLOCK MODE ==="));
        gesture_idx = 0;
        neo_flash(COLOR_YELLOW, 400);
        while (btn_right_raw()) _delay_ms(10);
        start_countdown(MODE_UNLOCK);
        return;
    }
}

static void do_countdown(void) {
    if (millis() - state_enter_time > 5000) go_to(ST_IDLE);
}

// =============================================================
// RECORD
// =============================================================
static void do_record_wait(void) {
    if (check_cancel()) return;
    if (millis() - state_enter_time > GESTURE_TIMEOUT_MS) {
        Serial.println(F("Timeout.")); neo_flash(COLOR_ORANGE,300); neo_idle_indicator(); go_to(ST_IDLE); return;
    }
    if ((millis()/400)%2==0) neo_waiting_pulse(COLOR_PURPLE, gesture_idx);
    else neo_show_progress(gesture_idx, COLOR_PURPLE);

    int16_t x,y,z; read_accel_raw(x,y,z);
    if (is_moving_raw(x,y,z)) {
        capture_reset(); capture_add(x,y,z);
        saw_motion = true; last_motion_ms = millis(); last_sample_ms = millis();
        Serial.print(F("  Capturing gesture ")); Serial.print(gesture_idx+1); Serial.println(F("..."));
        go_to(ST_RECORD_CAPTURE); saw_motion = true;
    }
}

static void do_record_capture(void) {
    if (check_cancel()) return;
    unsigned long now = millis();
    if (now - last_sample_ms < SAMPLE_RATE_MS) return;
    last_sample_ms = now;
    anim_tick++; neo_capturing_tick(COLOR_PURPLE, gesture_idx, anim_tick);

    int16_t x,y,z; read_accel_raw(x,y,z);
    bool moving = is_moving_raw(x,y,z);

    if (moving) { capture_add(x,y,z); last_motion_ms = now; saw_motion = true; }
    else {
        capture_add(x,y,z);
        if (saw_motion && (now - last_motion_ms > STILLNESS_MS)) {
            if (!capture_valid()) {
                Serial.println(F("  Too short!")); neo_flash(COLOR_ORANGE,300);
                start_countdown(MODE_RECORD); return;
            }
            capture_dump_serial();
            GestureBins bins = capture_finalize();
            stored_key[gesture_idx] = bins;
            bins_dump_serial(&bins, gesture_idx);
            gesture_idx++;
            Serial.print(F("  Gesture ")); Serial.print(gesture_idx); Serial.println(F("/3 recorded!"));

            if (gesture_idx >= NUM_GESTURES) {
                storage_save(stored_key); key_exists = true;
                Serial.println(F("\n*** KEY SAVED ***\n"));
                neo_show_progress(NUM_GESTURES, COLOR_PURPLE); _delay_ms(1000);
                neo_saved_animation(); neo_idle_indicator(); go_to(ST_IDLE);
            } else {
                neo_show_progress(gesture_idx, COLOR_PURPLE);
                Serial.println(F("  Reposition..."));
                for (uint16_t i=0; i<200; i++) _delay_ms(10);
                start_countdown(MODE_RECORD);
            }
            return;
        }
    }
    if (raw_count >= MAX_SAMPLES) {
        if (!capture_valid()) { neo_flash(COLOR_ORANGE,200); start_countdown(MODE_RECORD); return; }
        capture_dump_serial();
        GestureBins bins = capture_finalize();
        stored_key[gesture_idx] = bins;
        gesture_idx++;
        if (gesture_idx >= NUM_GESTURES) {
            storage_save(stored_key); key_exists = true;
            Serial.println(F("*** KEY SAVED ***"));
            neo_saved_animation(); neo_idle_indicator(); go_to(ST_IDLE);
        } else {
            neo_show_progress(gesture_idx, COLOR_PURPLE);
            for (uint16_t i=0; i<200; i++) _delay_ms(10);
            start_countdown(MODE_RECORD);
        }
    }
}

// =============================================================
// UNLOCK
// =============================================================
static void do_unlock_wait(void) {
    if (check_cancel()) return;
    if (millis() - state_enter_time > GESTURE_TIMEOUT_MS) {
        Serial.println(F("Timeout.")); neo_flash(COLOR_ORANGE,300); neo_idle_indicator(); go_to(ST_IDLE); return;
    }
    if ((millis()/400)%2==0) neo_waiting_pulse(COLOR_YELLOW, gesture_idx);
    else neo_show_progress(gesture_idx, COLOR_GREEN);

    int16_t x,y,z; read_accel_raw(x,y,z);
    if (is_moving_raw(x,y,z)) {
        capture_reset(); capture_add(x,y,z);
        saw_motion = true; last_motion_ms = millis(); last_sample_ms = millis();
        Serial.print(F("  Capturing gesture ")); Serial.print(gesture_idx+1); Serial.println(F("..."));
        go_to(ST_UNLOCK_CAPTURE); saw_motion = true;
    }
}

static void do_unlock_capture(void) {
    if (check_cancel()) return;
    unsigned long now = millis();
    if (now - last_sample_ms < SAMPLE_RATE_MS) return;
    last_sample_ms = now;
    anim_tick++; neo_capturing_tick(COLOR_YELLOW, gesture_idx, anim_tick);

    int16_t x,y,z; read_accel_raw(x,y,z);
    bool moving = is_moving_raw(x,y,z);

    if (moving) { capture_add(x,y,z); last_motion_ms = now; saw_motion = true; }
    else {
        capture_add(x,y,z);
        if (saw_motion && (now - last_motion_ms > STILLNESS_MS)) {
            if (!capture_valid()) {
                Serial.println(F("  Too short!")); neo_flash(COLOR_ORANGE,300);
                start_countdown(MODE_UNLOCK); return;
            }
            capture_dump_serial();
            GestureBins attempt = capture_finalize();
            bins_dump_serial(&attempt, gesture_idx);
            float dist = gesture_distance(&attempt, &stored_key[gesture_idx]);

            Serial.print(F("  G")); Serial.print(gesture_idx+1);
            Serial.print(F(" dist: ")); Serial.print(dist, 3);
            Serial.print(F("  thr: ")); Serial.print(MATCH_THRESHOLD, 3);

            if (gesture_matches(&attempt, &stored_key[gesture_idx])) {
                Serial.println(F("  -> MATCH!"));
                gesture_idx++;
                if (gesture_idx >= NUM_GESTURES) {
                    Serial.println(F("\n***** UNLOCKED! *****\n"));
                    go_to(ST_UNLOCKED);
                } else {
                    neo_show_progress(gesture_idx, COLOR_GREEN);
                    Serial.println(F("  Reposition..."));
                    for (uint16_t i=0; i<200; i++) _delay_ms(10);
                    start_countdown(MODE_UNLOCK);
                }
            } else {
                Serial.println(F("  -> NO MATCH"));
                go_to(ST_FAILED);
            }
            return;
        }
    }
    if (raw_count >= MAX_SAMPLES) {
        if (!capture_valid()) { neo_flash(COLOR_ORANGE,200); start_countdown(MODE_UNLOCK); return; }
        capture_dump_serial();
        GestureBins attempt = capture_finalize();
        float dist = gesture_distance(&attempt, &stored_key[gesture_idx]);
        Serial.print(F("  dist: ")); Serial.println(dist,3);
        if (gesture_matches(&attempt, &stored_key[gesture_idx])) {
            gesture_idx++;
            if (gesture_idx >= NUM_GESTURES) go_to(ST_UNLOCKED);
            else { neo_show_progress(gesture_idx, COLOR_GREEN); for(uint16_t i=0;i<200;i++) _delay_ms(10); start_countdown(MODE_UNLOCK); }
        } else { Serial.println(F("  -> NO MATCH")); go_to(ST_FAILED); }
    }
}

static void do_unlocked(void) {
    neo_success_animation();
    Serial.println(F("Lock re-engaged.\n"));
    neo_idle_indicator(); go_to(ST_IDLE);
}

static void do_failed(void) {
    neo_fail_animation();
    Serial.println(F("Failed. RIGHT to retry.\n"));
    neo_idle_indicator(); go_to(ST_IDLE);
}

void loop() {
    switch (state) {
        case ST_IDLE:           do_idle();           break;
        case ST_COUNTDOWN:      do_countdown();      break;
        case ST_RECORD_WAIT:    do_record_wait();    break;
        case ST_RECORD_CAPTURE: do_record_capture(); break;
        case ST_UNLOCK_WAIT:    do_unlock_wait();    break;
        case ST_UNLOCK_CAPTURE: do_unlock_capture(); break;
        case ST_UNLOCKED:       do_unlocked();       break;
        case ST_FAILED:         do_failed();         break;
    }
}