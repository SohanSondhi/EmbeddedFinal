#ifndef GESTURE_H
#define GESTURE_H

#include <math.h>
#include <string.h>
#include "pin_config.h"

// =============================================================
// Gesture Engine V5 — Orientation-Invariant Matching
//
// PROBLEM WITH V1-V4: Gestures use X, Y, Z axis values, but the
// user holds the board in different orientations each time. Same
// physical motion → totally different X/Y/Z data → no match.
//
// SOLUTION: Use features that DON'T depend on orientation.
//
// Feature 1: ENERGY = |accel| - gravity
//   = how hard you're moving, regardless of direction
//   Drawing "1" (one stroke) → one peak
//   Drawing "3" (two curves) → two peaks
//
// Feature 2: JERK = |accel[i] - accel[i-1]|
//   = how quickly you change direction
//   Straight stroke → low jerk
//   Sharp turn → high jerk spike
//
// Both features are SCALAR (not vector) → orientation invariant.
//
// Pipeline:
//   1. Capture raw samples
//   2. Compute energy & jerk for each sample
//   3. Trim stationary tails (only keep active motion)
//   4. Resample trimmed profiles to NUM_PATH_PTS points
//   5. Normalize each feature to [0, 1]
//   6. Compare via Euclidean distance
// =============================================================

struct GestureBins {
    float energy[NUM_PATH_PTS];
    float jerk[NUM_PATH_PTS];
};

struct RawSample { int16_t x, y, z; };
static RawSample raw_buf[MAX_SAMPLES];
static uint16_t  raw_count = 0;

static void capture_reset(void) { raw_count = 0; }

static bool capture_add(int16_t x, int16_t y, int16_t z) {
    if (raw_count >= MAX_SAMPLES) return false;
    raw_buf[raw_count].x = x;
    raw_buf[raw_count].y = y;
    raw_buf[raw_count].z = z;
    raw_count++;
    return true;
}

static bool capture_valid(void) { return raw_count >= MIN_SAMPLES; }

static void capture_dump_serial(void) {
    Serial.println(F("--- RAW DATA START ---"));
    Serial.println(F("i,x,y,z"));
    for (uint16_t i = 0; i < raw_count; i++) {
        Serial.print(i); Serial.print(F(","));
        Serial.print(raw_buf[i].x); Serial.print(F(","));
        Serial.print(raw_buf[i].y); Serial.print(F(","));
        Serial.println(raw_buf[i].z);
    }
    Serial.println(F("--- RAW DATA END ---"));
}

// =============================================================
// Compute magnitude of a raw sample
// =============================================================
static float raw_mag(uint16_t i) {
    float x = raw_buf[i].x, y = raw_buf[i].y, z = raw_buf[i].z;
    return sqrt(x * x + y * y + z * z);
}

// =============================================================
// Compute jerk magnitude between consecutive samples
// =============================================================
static float raw_jerk(uint16_t i) {
    if (i == 0) return 0;
    float dx = raw_buf[i].x - raw_buf[i - 1].x;
    float dy = raw_buf[i].y - raw_buf[i - 1].y;
    float dz = raw_buf[i].z - raw_buf[i - 1].z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

// =============================================================
// Find active region (trim stationary tails)
// Returns start and end indices of motion
// =============================================================
static void find_active_region(uint16_t *start, uint16_t *end) {
    // Compute mean magnitude (≈ gravity at rest)
    float mean_mag = 0;
    for (uint16_t i = 0; i < raw_count; i++) mean_mag += raw_mag(i);
    mean_mag /= raw_count;

    float threshold = 800.0f;  // min deviation from gravity to count as moving

    // Find first sample with significant motion
    *start = 0;
    for (uint16_t i = 0; i < raw_count; i++) {
        if (fabs(raw_mag(i) - mean_mag) > threshold || raw_jerk(i) > threshold) {
            *start = (i > 2) ? i - 2 : 0;  // include 2 samples before
            break;
        }
    }

    // Find last sample with significant motion
    *end = raw_count - 1;
    for (uint16_t i = raw_count - 1; i > *start; i--) {
        if (fabs(raw_mag(i) - mean_mag) > threshold || raw_jerk(i) > threshold) {
            *end = (i + 3 < raw_count) ? i + 2 : raw_count - 1;
            break;
        }
    }

    // Ensure minimum length
    if (*end - *start < MIN_SAMPLES) {
        *start = 0;
        *end = raw_count - 1;
    }
}

// =============================================================
// Build feature profiles and resample to NUM_PATH_PTS points
// =============================================================

static GestureBins capture_extract(void) {
    GestureBins result;
    memset(&result, 0, sizeof(result));
    if (raw_count < MIN_SAMPLES) return result;

    // Find active region
    uint16_t start, end;
    find_active_region(&start, &end);
    uint16_t active_len = end - start + 1;

    Serial.print(F("  Active region: ")); Serial.print(start);
    Serial.print(F("-")); Serial.print(end);
    Serial.print(F(" (")); Serial.print(active_len); Serial.println(F(" samples)"));

    // Compute mean magnitude over active region (gravity baseline)
    float mean_mag = 0;
    for (uint16_t i = start; i <= end; i++) mean_mag += raw_mag(i);
    mean_mag /= active_len;

    // Resample energy and jerk to NUM_PATH_PTS using linear interpolation
    for (uint8_t p = 0; p < NUM_PATH_PTS; p++) {
        // Map output point p to input sample index (float)
        float t = (float)p / (NUM_PATH_PTS - 1) * (active_len - 1) + start;
        uint16_t lo = (uint16_t)t;
        uint16_t hi = lo + 1;
        if (hi > end) hi = end;
        float frac = t - lo;

        // Interpolated energy
        float e_lo = fabs(raw_mag(lo) - mean_mag);
        float e_hi = fabs(raw_mag(hi) - mean_mag);
        result.energy[p] = e_lo + frac * (e_hi - e_lo);

        // Interpolated jerk
        float j_lo = raw_jerk(lo);
        float j_hi = raw_jerk(hi);
        result.jerk[p] = j_lo + frac * (j_hi - j_lo);
    }

    return result;
}

// =============================================================
// Normalize each feature to [0, 1]
// =============================================================

static void normalize_features(GestureBins *g) {
    float max_e = 0.001f, max_j = 0.001f;

    for (uint8_t i = 0; i < NUM_PATH_PTS; i++) {
        if (g->energy[i] > max_e) max_e = g->energy[i];
        if (g->jerk[i] > max_j) max_j = g->jerk[i];
    }

    for (uint8_t i = 0; i < NUM_PATH_PTS; i++) {
        g->energy[i] /= max_e;
        g->jerk[i] /= max_j;
    }
}

// =============================================================
// Debug print
// =============================================================
static void bins_dump_serial(const GestureBins *g, uint8_t gnum) {
    Serial.print(F("  [Features G")); Serial.print(gnum + 1); Serial.println(F("]"));
    for (uint8_t i = 0; i < NUM_PATH_PTS; i++) {
        Serial.print(F("    ")); Serial.print(i);
        Serial.print(F(": e=")); Serial.print(g->energy[i], 3);
        Serial.print(F(" j=")); Serial.println(g->jerk[i], 3);
    }
}

// =============================================================
// Distance: Euclidean between normalized feature profiles
// =============================================================

static float gesture_distance(const GestureBins *a, const GestureBins *b) {
    float dist = 0;
    for (uint8_t i = 0; i < NUM_PATH_PTS; i++) {
        float de = a->energy[i] - b->energy[i];
        float dj = a->jerk[i] - b->jerk[i];
        dist += de * de + dj * dj;
    }
    return sqrt(dist / NUM_PATH_PTS);
}

static bool gesture_matches(const GestureBins *attempt, const GestureBins *stored) {
    return gesture_distance(attempt, stored) < MATCH_THRESHOLD;
}

// =============================================================
// Full pipeline
// =============================================================

static GestureBins capture_finalize(void) {
    GestureBins g = capture_extract();
    normalize_features(&g);
    return g;
}

// =============================================================
// Motion detection
// =============================================================

static bool is_moving_raw(int16_t x, int16_t y, int16_t z) {
    float mag = sqrt((float)x * x + (float)y * y + (float)z * z);
    return fabs(mag - RAW_REST_MAG) > RAW_MOTION_THR;
}

#endif