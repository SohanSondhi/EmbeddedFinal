#ifndef GESTURE_H
#define GESTURE_H

#include <math.h>
#include "pin_config.h"

// =============================================================
// Gesture Engine
// Records accelerometer data, extracts features, compares
// =============================================================

// --- Feature vector for one gesture ---
// Instead of storing raw samples, we extract key features.
// This saves EEPROM space and makes comparison simpler.
struct GestureFeatures {
    float x_mean;       // average X acceleration
    float y_mean;       // average Y acceleration
    float z_mean;       // average Z acceleration
    float x_peak;       // max absolute X
    float y_peak;       // max absolute Y
    float z_peak;       // max absolute Z
    float magnitude;    // average total magnitude
    uint16_t duration;  // gesture duration in ms
};

// --- Raw sample buffer (temporary, used during capture) ---
struct RawSample {
    float x, y, z;
};

static RawSample sample_buf[MAX_SAMPLES];
static uint16_t sample_count = 0;

// --- Stored key (3 gestures) ---
static GestureFeatures stored_key[NUM_GESTURES];
static bool key_loaded = false;

// =============================================================
// Feature Extraction
// =============================================================

// Extract features from raw sample buffer
GestureFeatures extract_features(RawSample *samples, uint16_t count) {
    GestureFeatures f;
    f.x_mean = 0; f.y_mean = 0; f.z_mean = 0;
    f.x_peak = 0; f.y_peak = 0; f.z_peak = 0;
    f.magnitude = 0;
    f.duration = count * SAMPLE_RATE_MS;

    if (count == 0) return f;

    for (uint16_t i = 0; i < count; i++) {
        f.x_mean += samples[i].x;
        f.y_mean += samples[i].y;
        f.z_mean += samples[i].z;

        float ax = fabs(samples[i].x);
        float ay = fabs(samples[i].y);
        float az = fabs(samples[i].z);

        if (ax > f.x_peak) f.x_peak = ax;
        if (ay > f.y_peak) f.y_peak = ay;
        if (az > f.z_peak) f.z_peak = az;

        f.magnitude += sqrt(
            samples[i].x * samples[i].x +
            samples[i].y * samples[i].y +
            samples[i].z * samples[i].z
        );
    }

    f.x_mean /= count;
    f.y_mean /= count;
    f.z_mean /= count;
    f.magnitude /= count;

    return f;
}

// =============================================================
// Gesture Comparison
// =============================================================

// Returns a "distance" between two gestures. Lower = more similar.
float gesture_distance(GestureFeatures *a, GestureFeatures *b) {
    float dist = 0;

    // Weighted differences of each feature
    dist += (a->x_mean - b->x_mean) * (a->x_mean - b->x_mean);
    dist += (a->y_mean - b->y_mean) * (a->y_mean - b->y_mean);
    dist += (a->z_mean - b->z_mean) * (a->z_mean - b->z_mean);

    dist += 0.5f * (a->x_peak - b->x_peak) * (a->x_peak - b->x_peak);
    dist += 0.5f * (a->y_peak - b->y_peak) * (a->y_peak - b->y_peak);
    dist += 0.5f * (a->z_peak - b->z_peak) * (a->z_peak - b->z_peak);

    dist += 0.3f * (a->magnitude - b->magnitude) * (a->magnitude - b->magnitude);

    return sqrt(dist);
}

// Check if a gesture matches the stored one
bool gesture_matches(GestureFeatures *attempt, GestureFeatures *stored) {
    float dist = gesture_distance(attempt, stored);
    return (dist < MATCH_THRESHOLD);
}

// =============================================================
// Capture Helpers
// =============================================================

// Reset sample buffer before capture
void capture_reset() {
    sample_count = 0;
}

// Add a sample to the buffer. Returns false if buffer full.
bool capture_add_sample(float x, float y, float z) {
    if (sample_count >= MAX_SAMPLES) return false;
    sample_buf[sample_count].x = x;
    sample_buf[sample_count].y = y;
    sample_buf[sample_count].z = z;
    sample_count++;
    return true;
}

// Finalize capture and extract features
GestureFeatures capture_finalize() {
    return extract_features(sample_buf, sample_count);
}

// =============================================================
// Motion Detection (to know when a gesture starts/stops)
// =============================================================

// Detect if there's significant motion (above resting gravity)
bool is_moving(float x, float y, float z) {
    float mag = sqrt(x * x + y * y + z * z);
    // At rest, magnitude ~ 9.8 m/s². Motion threshold above/below rest.
    return (mag > 12.0f || mag < 7.0f);
}

#endif // GESTURE_H
