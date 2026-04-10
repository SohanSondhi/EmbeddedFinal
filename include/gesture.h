#ifndef GESTURE_H
#define GESTURE_H

#include <math.h>
#include <string.h>
#include "pin_config.h"

// =============================================================
// Gesture Engine V7.2 — DTW on Normalized Magnitude
//
// V7.1 compared raw magnitudes. PROBLEM: drawing bigger = higher
// magnitudes → same gesture at different sizes fails.
//
// V7.2: Before running DTW, compute magnitude sequence for both
// gestures, then NORMALIZE (subtract mean, divide by std dev).
// This makes comparison invariant to:
//   - Orientation (magnitude is scalar)
//   - Size/force (normalization removes scale)
//   - Speed (DTW handles time warping)
// =============================================================

struct GestureBins {
    int16_t x[DTW_SAMPLES];
    int16_t y[DTW_SAMPLES];
    int16_t z[DTW_SAMPLES];
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
    Serial.println(F("--- RAW START ---"));
    for (uint16_t i = 0; i < raw_count; i++) {
        Serial.print(i); Serial.print(',');
        Serial.print(raw_buf[i].x); Serial.print(',');
        Serial.print(raw_buf[i].y); Serial.print(',');
        Serial.println(raw_buf[i].z);
    }
    Serial.println(F("--- RAW END ---"));
}

// Downsample raw_buf to DTW_SAMPLES points
static GestureBins downsample(void) {
    GestureBins g;
    memset(&g, 0, sizeof(g));
    if (raw_count == 0) return g;
    for (uint8_t i = 0; i < DTW_SAMPLES; i++) {
        uint16_t idx = (uint16_t)((uint32_t)i * (raw_count - 1) / (DTW_SAMPLES - 1));
        if (idx >= raw_count) idx = raw_count - 1;
        g.x[i] = raw_buf[idx].x;
        g.y[i] = raw_buf[idx].y;
        g.z[i] = raw_buf[idx].z;
    }
    return g;
}

// =============================================================
// Compute magnitude sequence and normalize (zero mean, unit std)
// =============================================================
static void compute_normalized_mags(const GestureBins *g, float *mags) {
    // Step 1: compute raw magnitudes
    for (uint8_t i = 0; i < DTW_SAMPLES; i++) {
        mags[i] = sqrt((float)g->x[i] * g->x[i] +
                        (float)g->y[i] * g->y[i] +
                        (float)g->z[i] * g->z[i]);
    }

    // Step 2: compute mean
    float mean = 0;
    for (uint8_t i = 0; i < DTW_SAMPLES; i++) mean += mags[i];
    mean /= DTW_SAMPLES;

    // Step 3: subtract mean
    for (uint8_t i = 0; i < DTW_SAMPLES; i++) mags[i] -= mean;

    // Step 4: compute std dev
    float var = 0;
    for (uint8_t i = 0; i < DTW_SAMPLES; i++) var += mags[i] * mags[i];
    float std = sqrt(var / DTW_SAMPLES);

    // Step 5: divide by std (avoid div by zero)
    if (std > 0.01f) {
        for (uint8_t i = 0; i < DTW_SAMPLES; i++) mags[i] /= std;
    }
}

// =============================================================
// Banded DTW on normalized magnitude sequences
// =============================================================
static float dtw_on_mags(const float *a, const float *b) {
    uint8_t N = DTW_SAMPLES;
    float prev[DTW_SAMPLES + 1];
    float curr[DTW_SAMPLES + 1];

    for (uint8_t j = 0; j <= N; j++) prev[j] = 1e9f;
    prev[0] = 0;

    for (uint8_t i = 1; i <= N; i++) {
        for (uint8_t j = 0; j <= N; j++) curr[j] = 1e9f;

        uint8_t j_start = (i > DTW_BAND) ? (i - DTW_BAND) : 1;
        uint8_t j_end   = (i + DTW_BAND < N) ? (i + DTW_BAND) : N;

        for (uint8_t j = j_start; j <= j_end; j++) {
            float cost = fabs(a[i - 1] - b[j - 1]);

            float m = prev[j - 1];
            if (prev[j] < m) m = prev[j];
            if (curr[j - 1] < m) m = curr[j - 1];

            curr[j] = cost + m;
        }

        for (uint8_t j = 0; j <= N; j++) prev[j] = curr[j];
    }

    return prev[N] / N;
}

// =============================================================
// Public interface: compute distance between two stored gestures
// =============================================================
static float gesture_distance(const GestureBins *a, const GestureBins *b) {
    float mags_a[DTW_SAMPLES];
    float mags_b[DTW_SAMPLES];
    compute_normalized_mags(a, mags_a);
    compute_normalized_mags(b, mags_b);
    return dtw_on_mags(mags_a, mags_b);
}

static bool gesture_matches(const GestureBins *a, const GestureBins *b) {
    return gesture_distance(a, b) < MATCH_THRESHOLD;
}

// Full pipeline
static GestureBins capture_finalize(void) {
    return downsample();
}

// Debug print with magnitudes
static void bins_dump_serial(const GestureBins *g, uint8_t gnum) {
    Serial.print(F("  [G")); Serial.print(gnum + 1);
    Serial.print(F(" ")); Serial.print(DTW_SAMPLES); Serial.println(F("pts]"));
    float mags[DTW_SAMPLES];
    compute_normalized_mags(g, mags);
    for (uint8_t i = 0; i < DTW_SAMPLES; i++) {
        Serial.print(F("  ")); Serial.print(i);
        Serial.print(F(": mag_norm=")); Serial.println(mags[i], 3);
    }
}

// Motion detection
static bool is_moving_raw(int16_t x, int16_t y, int16_t z) {
    float mag = sqrt((float)x * x + (float)y * y + (float)z * z);
    return fabs(mag - RAW_REST_MAG) > RAW_MOTION_THR;
}

#endif