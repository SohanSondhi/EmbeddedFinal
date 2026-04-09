#ifndef GESTURE_H
#define GESTURE_H

#include <math.h>
#include <string.h>
#include "pin_config.h"

// =============================================================
// Gesture Engine V2 — Air-Drawn Number Recognition
//
// V1 BUG: Per-axis normalization destroyed inter-axis relationships.
//          A circle and a straight line looked identical after
//          normalizing each axis independently.
//
// V2 FIX: Normalize all axes by ONE shared factor (total energy).
//          This preserves the SHAPE (ratio between axes) while
//          still allowing different drawing sizes.
//
// Also added: debug dump to Serial for visualization.
// =============================================================

// --- Binned feature vector for one gesture ---
struct GestureBins {
    float x[NUM_BINS];
    float y[NUM_BINS];
    float z[NUM_BINS];
};

// --- Raw sample storage ---
struct RawSample {
    int16_t x, y, z;
};

// Global raw buffer — reused for every gesture capture
static RawSample raw_buf[MAX_SAMPLES];
static uint16_t  raw_count = 0;

// =============================================================
// Capture Functions
// =============================================================

static void capture_reset(void) {
    raw_count = 0;
}

static bool capture_add(int16_t x, int16_t y, int16_t z) {
    if (raw_count >= MAX_SAMPLES) return false;
    raw_buf[raw_count].x = x;
    raw_buf[raw_count].y = y;
    raw_buf[raw_count].z = z;
    raw_count++;
    return true;
}

static bool capture_valid(void) {
    return raw_count >= MIN_SAMPLES;
}

// =============================================================
// Debug: Dump raw samples to Serial as CSV
// Paste output into a plotter to see gesture shape.
// Format: index,x,y,z
// =============================================================
static void capture_dump_serial(void) {
    Serial.println(F("--- RAW DATA START ---"));
    Serial.println(F("i,x,y,z"));
    for (uint16_t i = 0; i < raw_count; i++) {
        Serial.print(i);
        Serial.print(F(","));
        Serial.print(raw_buf[i].x);
        Serial.print(F(","));
        Serial.print(raw_buf[i].y);
        Serial.print(F(","));
        Serial.println(raw_buf[i].z);
    }
    Serial.println(F("--- RAW DATA END ---"));
}

// =============================================================
// Binning — Compress raw samples into NUM_BINS time slices
// =============================================================

static GestureBins capture_to_bins(void) {
    GestureBins bins;
    memset(&bins, 0, sizeof(bins));

    if (raw_count == 0) return bins;

    float samples_per_bin = (float)raw_count / NUM_BINS;

    for (uint8_t b = 0; b < NUM_BINS; b++) {
        uint16_t start = (uint16_t)(b * samples_per_bin);
        uint16_t end   = (uint16_t)((b + 1) * samples_per_bin);
        if (end > raw_count) end = raw_count;
        if (start >= end) {
            if (b > 0) {
                bins.x[b] = bins.x[b - 1];
                bins.y[b] = bins.y[b - 1];
                bins.z[b] = bins.z[b - 1];
            }
            continue;
        }

        float sx = 0, sy = 0, sz = 0;
        uint16_t count = end - start;
        for (uint16_t i = start; i < end; i++) {
            sx += raw_buf[i].x;
            sy += raw_buf[i].y;
            sz += raw_buf[i].z;
        }
        bins.x[b] = sx / count;
        bins.y[b] = sy / count;
        bins.z[b] = sz / count;
    }

    return bins;
}

// =============================================================
// V2 Normalization — Subtract mean, then normalize by TOTAL energy
//
// Key difference from V1:
//   V1: divided each axis by its own std dev → destroyed shape
//   V2: divide ALL axes by the same global magnitude → preserves shape
//
// Example: Drawing "1" (mostly Y-axis) vs "—" (mostly X-axis)
//   V1: After per-axis norm, both look like [0,0,...,1,1,...,0,0]
//   V2: "1" keeps high Y, low X.  "—" keeps high X, low Y.
// =============================================================

static void normalize_bins(GestureBins *bins) {
    // --- Step 1: Subtract mean per axis (removes gravity offset) ---
    float mx = 0, my = 0, mz = 0;
    for (uint8_t i = 0; i < NUM_BINS; i++) {
        mx += bins->x[i];
        my += bins->y[i];
        mz += bins->z[i];
    }
    mx /= NUM_BINS;
    my /= NUM_BINS;
    mz /= NUM_BINS;

    for (uint8_t i = 0; i < NUM_BINS; i++) {
        bins->x[i] -= mx;
        bins->y[i] -= my;
        bins->z[i] -= mz;
    }

    // --- Step 2: Compute TOTAL energy across ALL axes ---
    float energy = 0;
    for (uint8_t i = 0; i < NUM_BINS; i++) {
        energy += bins->x[i] * bins->x[i];
        energy += bins->y[i] * bins->y[i];
        energy += bins->z[i] * bins->z[i];
    }
    float norm = sqrt(energy);

    // --- Step 3: Divide ALL axes by the SAME norm ---
    // This preserves the ratio between axes (the shape)
    // while normalizing for amplitude (how big you drew)
    float eps = 0.001f;
    if (norm > eps) {
        for (uint8_t i = 0; i < NUM_BINS; i++) {
            bins->x[i] /= norm;
            bins->y[i] /= norm;
            bins->z[i] /= norm;
        }
    }
}

// =============================================================
// Debug: Print binned features to Serial
// =============================================================
static void bins_dump_serial(const GestureBins *bins, uint8_t gesture_num) {
    Serial.print(F("  [Bins G"));
    Serial.print(gesture_num + 1);
    Serial.println(F("]"));
    for (uint8_t i = 0; i < NUM_BINS; i++) {
        Serial.print(F("    bin"));
        Serial.print(i);
        Serial.print(F(": x="));
        Serial.print(bins->x[i], 3);
        Serial.print(F(" y="));
        Serial.print(bins->y[i], 3);
        Serial.print(F(" z="));
        Serial.println(bins->z[i], 3);
    }
}

// =============================================================
// Comparison — Euclidean distance between normalized vectors
// =============================================================

static float gesture_distance(const GestureBins *a, const GestureBins *b) {
    float dist = 0;
    for (uint8_t i = 0; i < NUM_BINS; i++) {
        float dx = a->x[i] - b->x[i];
        float dy = a->y[i] - b->y[i];
        float dz = a->z[i] - b->z[i];
        dist += dx * dx + dy * dy + dz * dz;
    }
    return sqrt(dist);
}

static bool gesture_matches(const GestureBins *attempt, const GestureBins *stored) {
    float d = gesture_distance(attempt, stored);
    return d < MATCH_THRESHOLD;
}

// =============================================================
// Full Pipeline: capture → bin → normalize
// =============================================================

static GestureBins capture_finalize(void) {
    GestureBins bins = capture_to_bins();
    normalize_bins(&bins);
    return bins;
}

// =============================================================
// Motion Detection
// =============================================================

#define RAW_REST_MAG    8192.0f
#define RAW_MOTION_THR  1500.0f

static bool is_moving_raw(int16_t x, int16_t y, int16_t z) {
    float mag = sqrt((float)x * x + (float)y * y + (float)z * z);
    float dev = fabs(mag - RAW_REST_MAG);
    return dev > RAW_MOTION_THR;
}

#endif // GESTURE_H