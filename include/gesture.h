#ifndef GESTURE_H
#define GESTURE_H

#include <math.h>
#include <string.h>
#include "pin_config.h"

struct GestureBins { // Downsampled gesture data
    int16_t x[DTW_SAMPLES];
    int16_t y[DTW_SAMPLES];
    int16_t z[DTW_SAMPLES];
};

struct RawSample { int16_t x, y, z; }; // Raw accelerometer sample (before downsampling and binning)
static RawSample raw_buf[MAX_SAMPLES]; // Buffer to hold raw accelerometer samples during gesture capture. 
static uint16_t  raw_count = 0; // Counter to keep track of how many raw accelerometer samples have been captured so far for the current gesture. This is used to determine when we've reached the maximum number of samples for a gesture capture, and also to determine if we have enough samples to consider the capture valid.

// function prototypes for gesture capture and processing
static void capture_reset(void) { raw_count = 0; } // Reset the raw sample buffer and counter to start a new gesture capture

static bool capture_add(int16_t x, int16_t y, int16_t z) { // Add a new raw accelerometer sample to the capture buffer. Returns false if we've already reached the maximum number of samples for this gesture capture, true otherwise.
    if (raw_count >= MAX_SAMPLES) return false;
    raw_buf[raw_count].x = x;
    raw_buf[raw_count].y = y;
    raw_buf[raw_count].z = z;
    raw_count++;
    return true;
}

static bool capture_valid(void) { return raw_count >= MIN_SAMPLES; } // Check if the captured gesture data is valid by ensuring that we have at least the minimum number of raw samples required for a valid capture. This is used to filter out captures that are too short or don't have enough data to be considered a real gesture.

static void capture_dump_serial(void) { // Debug function to print the raw captured gesture data to the serial monitor. 
    Serial.println(F("--- RAW START ---"));
    for (uint16_t i = 0; i < raw_count; i++) { // Loop through all the raw samples
        // Print the index and the x, y, z values of each raw sample to the serial monitor for debugging purposes. 
        Serial.print(i); Serial.print(','); 
        Serial.print(raw_buf[i].x); Serial.print(',');
        Serial.print(raw_buf[i].y); Serial.print(',');
        Serial.println(raw_buf[i].z);
    }
    Serial.println(F("--- RAW END ---"));
}
/* Downsampling is used to convert a gesture with many raw samples 
   into a fixed number of points, so all gestures have the same length.
   In this process, DTW is used to compare two gestures even if they are performed
   at slightly different speeds. It aligns the sequences in time. Together, downsampling
   and DTW make gesture matching more consistent.*/

// Downsample raw_buf
static GestureBins downsample(void) { // Process the raw captured gesture data to compute the downsampled and normalized gesture bins for this unlock attempt. 
    GestureBins g;
    memset(&g, 0, sizeof(g)); // Initialize the gesture bins structure to zero before filling it with the downsampled data. 
    if (raw_count == 0) return g; // If there are no raw samples, return the empty gesture bins structure. 
    for (uint8_t i = 0; i < DTW_SAMPLES; i++) { // Loop through the number of downsampled points we want to compute (DTW_SAMPLES) and fill in the gesture bins by selecting the appropriate raw samples from the raw_buf based on their index. 
        uint16_t idx = (uint16_t)((uint32_t)i * (raw_count - 1) / (DTW_SAMPLES - 1));
        if (idx >= raw_count) idx = raw_count - 1;
        g.x[i] = raw_buf[idx].x;
        g.y[i] = raw_buf[idx].y;
        g.z[i] = raw_buf[idx].z;
    }
    return g;
}

// Compute magnitude sequence and normalize (zero mean, unit std)
static void compute_normalized_mags(const GestureBins *g, float *mags) {
    // Compute raw magnitudes
    for (uint8_t i = 0; i < DTW_SAMPLES; i++) {
        mags[i] = sqrt((float)g->x[i] * g->x[i] +
                        (float)g->y[i] * g->y[i] +
                        (float)g->z[i] * g->z[i]);
    }

    // Compute mean
    float mean = 0;
    for (uint8_t i = 0; i < DTW_SAMPLES; i++) mean += mags[i];
    mean /= DTW_SAMPLES;

    // Subtract mean from magnitude
    for (uint8_t i = 0; i < DTW_SAMPLES; i++) mags[i] -= mean;

    // Compute std dev
    float var = 0;
    for (uint8_t i = 0; i < DTW_SAMPLES; i++) var += mags[i] * mags[i];
    float std = sqrt(var / DTW_SAMPLES);

    // Divide by std (avoid div by zero)
    if (std > 0.01f) {
        for (uint8_t i = 0; i < DTW_SAMPLES; i++) mags[i] /= std;
    }
}

// Banded DTW on normalized magnitude sequences
static float dtw_on_mags(const float *a, const float *b) {
    uint8_t N = DTW_SAMPLES; // Length of the sequences we're comparing (after downsampling)
    float prev[DTW_SAMPLES + 1]; // Previous row of the DTW matrix
    float curr[DTW_SAMPLES + 1]; // Current row of the DTW matrix

    for (uint8_t j = 0; j <= N; j++) prev[j] = 1e9f; // Initialize the first row of the DTW matrix to a large value (infinity) to represent the cost of aligning an empty sequence with the first j points of the other sequence.
    prev[0] = 0;

    for (uint8_t i = 1; i <= N; i++) { // Loop through each point in the first sequence
        for (uint8_t j = 0; j <= N; j++) curr[j] = 1e9f; // Initialize the current row of the DTW matrix to a large value (infinity) for the same reason as above, since we haven't computed any costs for aligning points yet.

        uint8_t j_start = (i > DTW_BAND) ? (i - DTW_BAND) : 1; // 
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

// Compute distance between two stored gestures
static float gesture_distance(const GestureBins *a, const GestureBins *b) {
    // Gesture b = stored key, Gesture a = attempt. We compute the distance between the two gestures by first computing their normalized magnitude sequences and then using DTW to compute the distance between these sequences. 
    float mags_a[DTW_SAMPLES]; // Fill in normalized magnitude sequences for gesture a
    float mags_b[DTW_SAMPLES]; // Fill in normalized magnitude sequences for gesture b
    compute_normalized_mags(a, mags_a); // Compute the normalized magnitude sequence for gesture a by calling the compute_normalized_mags function
    compute_normalized_mags(b, mags_b); // Compute the normalized magnitude sequence for gesture b by calling the compute_normalized_mags function
    return dtw_on_mags(mags_a, mags_b); // Compute the distance between the two gestures by calling the dtw_on_mags function to compute the DTW distance between the two normalized magnitude sequences, and return this distance as a measure of how similar the two gestures are. A smaller distance indicates a closer match between the gestures.
}

static bool gesture_matches(const GestureBins *a, const GestureBins *b) { // Determine if the captured gesture bins for this unlock attempt match closely enough to the corresponding gesture bins in the stored key by computing the distance between the two gestures using the gesture_distance function and comparing it to a defined distance threshold (MATCH_THRESHOLD).
    return gesture_distance(a, b) < MATCH_THRESHOLD; // If the distance is less than the threshold, we consider this a successful match for this gesture in the combination and return true; otherwise, we return false to indicate that this attempt does not match this gesture in the stored key closely enough.
}

static GestureBins capture_finalize(void) { // This function returns the final processed gesture bins for the current unlock attempt
    return downsample();
}

// Debug print with magnitudes
static void bins_dump_serial(const GestureBins *g, uint8_t gnum) {
    Serial.print(F("  [G")); Serial.print(gnum + 1); // Print the gesture number for this unlock attempt to the serial monitor for debugging purposes. We add 1 to gnum because it's zero-indexed.
    Serial.print(F(" ")); Serial.print(DTW_SAMPLES); Serial.println(F("pts]")); // Print the number of downsampled points in the gesture bins to the serial monitor for debugging purposes.
    float mags[DTW_SAMPLES];

    compute_normalized_mags(g, mags); // Compute the normalized magnitude sequence for this gesture
    for (uint8_t i = 0; i < DTW_SAMPLES; i++) { // Loop through each point in the downsampled gesture bins and print the normalized magnitude for each point to the serial monitor for debugging purposes. 
        Serial.print(F("  ")); Serial.print(i);
        Serial.print(F(": mag_norm=")); Serial.println(mags[i], 3);
    }
}

// Motion detection
static bool is_moving_raw(int16_t x, int16_t y, int16_t z) { // Determine if the user is currently moving based on the raw accelerometer readings
    float mag = sqrt((float)x * x + (float)y * y + (float)z * z); // Compute the magnitude of the accelerometer vector from its x, y, z components. This gives us a single value that represents the overall acceleration regardless of direction.
    return fabs(mag - RAW_REST_MAG) > RAW_MOTION_THR; // Compare the computed magnitude to a predefined rest magnitude (RAW_REST_MAG) and a motion threshold (RAW_MOTION_THR) to determine if the user is currently moving. If the absolute difference between the current magnitude and the rest magnitude is greater than the motion threshold, we consider this as movement and return true; otherwise, we return false to indicate that the user is likely still.
}

#endif