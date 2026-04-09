#ifndef GESTURE_H
#define GESTURE_H

#include <math.h>
#include <string.h>
#include "pin_config.h"

// =============================================================
// Gesture Engine for Air-Drawn Numbers
//
// How it works:
//   1. CAPTURE: Record raw accelerometer samples while user draws
//   2. BIN:     Compress variable-length samples into fixed NUM_BINS
//               time slices (this normalizes for speed differences)
//   3. NORMALIZE: Remove gravity offset and scale to unit variance
//               (this normalizes for amplitude/orientation differences)
//   4. COMPARE: Euclidean distance between two normalized bin vectors
//
// Memory layout:
//   Raw buffer:  int16_t[MAX_SAMPLES][3] = 600 bytes (temporary)
//   Bin vector:  float[NUM_BINS][3] = 120 bytes per gesture
//   Stored key:  3 × 120 = 360 bytes (in EEPROM)
// =============================================================

// --- Binned feature vector for one gesture ---
struct GestureBins {
    float x[NUM_BINS];
    float y[NUM_BINS];
    float z[NUM_BINS];
};

// --- Raw sample storage (int16_t to save RAM) ---
// These are the raw LIS3DH register values (lis.x, lis.y, lis.z)
struct RawSample {
    int16_t x, y, z;
};

// Global raw buffer — reused for every gesture capture
static RawSample raw_buf[MAX_SAMPLES];
static uint16_t  raw_count = 0;

// =============================================================
// Capture Functions
// =============================================================

// Reset before starting a new capture
static void capture_reset(void) {
    raw_count = 0;
}

// Add one raw sample. Returns false if buffer is full.
static bool capture_add(int16_t x, int16_t y, int16_t z) {
    if (raw_count >= MAX_SAMPLES) return false;
    raw_buf[raw_count].x = x;
    raw_buf[raw_count].y = y;
    raw_buf[raw_count].z = z;
    raw_count++;
    return true;
}

// Check if we have enough samples for a valid gesture
static bool capture_valid(void) {
    return raw_count >= MIN_SAMPLES;
}

// =============================================================
// Binning — Compress raw samples into NUM_BINS time slices
//
// If we have 80 raw samples and NUM_BINS=10, each bin gets ~8
// samples averaged together. This makes the feature vector
// independent of gesture duration (speed normalization).
// =============================================================

static GestureBins capture_to_bins(void) {
    GestureBins bins;
    memset(&bins, 0, sizeof(bins));

    if (raw_count == 0) return bins;

    // How many raw samples per bin (can be fractional)
    float samples_per_bin = (float)raw_count / NUM_BINS;

    for (uint8_t b = 0; b < NUM_BINS; b++) {
        // Calculate which raw samples fall into this bin
        uint16_t start = (uint16_t)(b * samples_per_bin);
        uint16_t end   = (uint16_t)((b + 1) * samples_per_bin);
        if (end > raw_count) end = raw_count;
        if (start >= end) {
            // Edge case: copy previous bin or leave as zero
            if (b > 0) {
                bins.x[b] = bins.x[b - 1];
                bins.y[b] = bins.y[b - 1];
                bins.z[b] = bins.z[b - 1];
            }
            continue;
        }

        // Average the samples in this bin
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
// Normalization — Remove DC offset and scale to unit variance
//
// Why: When you draw "3" while holding the board tilted vs flat,
// the gravity component shifts all readings. Subtracting the mean
// per axis removes this. Dividing by standard deviation makes the
// comparison independent of how "hard" you drew.
// =============================================================

static void normalize_bins(GestureBins *bins) {
    // --- Compute mean per axis ---
    float mx = 0, my = 0, mz = 0;
    for (uint8_t i = 0; i < NUM_BINS; i++) {
        mx += bins->x[i];
        my += bins->y[i];
        mz += bins->z[i];
    }
    mx /= NUM_BINS;
    my /= NUM_BINS;
    mz /= NUM_BINS;

    // --- Subtract mean ---
    for (uint8_t i = 0; i < NUM_BINS; i++) {
        bins->x[i] -= mx;
        bins->y[i] -= my;
        bins->z[i] -= mz;
    }

    // --- Compute standard deviation per axis ---
    float vx = 0, vy = 0, vz = 0;
    for (uint8_t i = 0; i < NUM_BINS; i++) {
        vx += bins->x[i] * bins->x[i];
        vy += bins->y[i] * bins->y[i];
        vz += bins->z[i] * bins->z[i];
    }
    float sx = sqrt(vx / NUM_BINS);
    float sy = sqrt(vy / NUM_BINS);
    float sz = sqrt(vz / NUM_BINS);

    // --- Divide by std dev (avoid div by zero) ---
    float eps = 0.001f;
    if (sx > eps) { for (uint8_t i = 0; i < NUM_BINS; i++) bins->x[i] /= sx; }
    if (sy > eps) { for (uint8_t i = 0; i < NUM_BINS; i++) bins->y[i] /= sy; }
    if (sz > eps) { for (uint8_t i = 0; i < NUM_BINS; i++) bins->z[i] /= sz; }
}

// =============================================================
// Comparison — Euclidean distance between two normalized vectors
// =============================================================

static float gesture_distance(const GestureBins *a, const GestureBins *b) {
    float dist = 0;
    for (uint8_t i = 0; i < NUM_BINS; i++) {
        float dx = a->x[i] - b->x[i];
        float dy = a->y[i] - b->y[i];
        float dz = a->z[i] - b->z[i];
        dist += dx * dx + dy * dy + dz * dz;
    }
    return sqrt(dist / NUM_BINS);  // RMS distance per bin
}

static bool gesture_matches(const GestureBins *attempt, const GestureBins *stored) {
    float d = gesture_distance(attempt, stored);
    return d < MATCH_THRESHOLD;
}

// =============================================================
// Full Pipeline: raw buffer → bins → normalize → ready
// Call after capture is done.
// =============================================================

static GestureBins capture_finalize(void) {
    GestureBins bins = capture_to_bins();
    normalize_bins(&bins);
    return bins;
}

// =============================================================
// Motion Detection
//
// Uses the magnitude of raw accelerometer values.
// At rest with LIS3DH at ±4g range, raw values for 1g ≈ 8192.
// Magnitude at rest ≈ 8192 (pure gravity).
// If magnitude deviates significantly, the board is moving.
// =============================================================

#define RAW_REST_MAG    8192.0f     // ~1g in raw units at ±4g range
#define RAW_MOTION_THR  2500.0f     // deviation from rest to count as moving

static bool is_moving_raw(int16_t x, int16_t y, int16_t z) {
    float mag = sqrt((float)x * x + (float)y * y + (float)z * z);
    float dev = fabs(mag - RAW_REST_MAG);
    return dev > RAW_MOTION_THR;
}

#endif // GESTURE_H
