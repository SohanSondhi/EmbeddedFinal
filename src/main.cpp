#include <Arduino.h>

#include "pin_config.h"
#include "gpio_reg.h"
#include "gesture.h"
#include "spi_reg.h"

// States and Variables
enum State { 
    ST_IDLE, // Idle State
    ST_RECORD_WAIT,  // Waiting for user to perform the gesture after pressing the record button
    ST_RECORD_CAPTURE, // Actively capturing the gesture data
    ST_UNLOCK_WAIT, // Waiting for user to perform the gesture after pressing the unlock button
    ST_UNLOCK_CAPTURE, // Actively capturing the gesture data for unlock attempt
    ST_UNLOCKED, // Successfully unlocked
    ST_FAILED // Failed unlock attempt (but not locked out yet)
};

enum Mode { 
    MODE_RECORD, // Recording mode for setting the key
    MODE_UNLOCK // Unlocking mode for attempting to match the key
};

static State state = ST_IDLE; // set initial state to idle
static Mode mode = MODE_RECORD; // set initial mode to record
static uint8_t gesture_idx = 0; // Index of the current gesture being recorded or attempted (0, 1, or 2 for the three gestures in the combination)
static GestureBins stored_key[NUM_GESTURES]; // The stored key combination of gestures, saved only in RAM while the board stays powered on
static bool key_exists = false; // Indicates if a key has been recorded during the current power cycle
static unsigned long state_enter_time = 0, last_sample_ms = 0, last_motion_ms = 0; // Timing variables for managing state transitions and gesture capture timing
static bool saw_motion = false; //Indicate if any motion was detected during the capture phase (used to detect if the user is actually performing a gesture or just holding still)
static uint8_t anim_tick = 0, retries_left = MAX_RETRIES; // Tracks how many unlock attempts are left before lockout

// Function prototypes
static void read_accel_raw(int16_t &x, int16_t &y, int16_t &z) { // reads raw accelerometer values into x, y, z
    accel_spi_read_xyz(x, y, z); // Read the LIS3DH accelerometer directly through SPI (Function in spi.h)
}

static void go_to(State s) { // state transition helper
    state = s; // Update the current state to the new state 's'
    state_enter_time = millis(); // Record the time at which we entered this state (used for timing out if the user takes too long to perform a gesture)
    saw_motion = false; // Reset the motion detection flag whenever we enter a new state, so we can track if the user performs any motion during the gesture capture phase
    anim_tick = 0; // Reset the animation tick counter for LED feedback animations
}

static bool check_cancel(void) { // checks if both buttons are held to cancel the current operation, returns true if cancelled
    if (both_buttons_held()) { // If both buttons are held
        Serial.println(F("--- CANCELLED ---")); // Print cancellation message to serial monitor
        neo_cancel_feedback(); // Flash Neopixel animation to indicate cancellation
        neo_idle_indicator(); // Return to idle LED state
        go_to(ST_IDLE); // Transition back to idle state
        while (btn_left_raw() || btn_right_raw()) {
            _delay_ms(10); // Wait until both buttons are released before allowing any further actions
        }
        return true; // Indicate that the operation was cancelled
    }

    return false; // Indicate that the operation was not cancelled and can continue as normal
}

static bool do_countdown(void) { // Shows a countdown animation on the NeoPixels and checks for cancellation. Returns true if countdown completes without cancellation, false if cancelled.
    uint32_t c = (mode == MODE_RECORD) ? COLOR_PURPLE : COLOR_YELLOW; // Set the color based on whether we're in record mode (purple) or unlock mode (yellow)
    uint32_t pc = (mode == MODE_RECORD) ? COLOR_PURPLE : COLOR_GREEN; // Set the progress color (used during countdown) based on mode (purple for record, green for unlock)
    
    Serial.print(F("  Ready G")); Serial.print(gesture_idx + 1); // Print which gesture number we're about to record or attempt (1, 2, or 3)
    if (mode == MODE_UNLOCK) { // If unlock mode, print how many retries are left
        Serial.print(F(" (")); 
        Serial.print(retries_left); 
        Serial.print(F(" tries)")); 
    }

    Serial.println();
    uint16_t step = COUNTDOWN_MS / NUM_PIXELS; // Calculate how long to wait between lighting each pixel during the countdown animation
    for (uint8_t i = 0; i < NUM_PIXELS; i++) { // For each pixel in the NeoPixel strip
        CircuitPlayground.strip.clear(); // Clear the strip before setting the next pixel
        for (uint8_t g = 0; g < gesture_idx; g++)
            for (uint8_t p = g*3; p < (g*3)+3 && p < NUM_PIXELS; p++) neo_set(p, pc); // Light up the pixels corresponding to already recorded gestures in progress color (e.g. if we're on gesture 2, light up pixels 0-5)
        neo_set(NUM_PIXELS - 1 - i, c); // Light up the next pixel in the countdown color (starting from the end of the strip and moving backwards)
        neo_show(); // Update the strip to show the new pixel states
        for (uint16_t t = 0; t < step/10; t++) { // Wait for the duration of this step, checking for cancellation every 10ms
            _delay_ms(10); 
            if (both_buttons_held()) { // If both buttons are held during the countdown, treat it as a cancellation
                check_cancel();
                return false; 
            } 
        }
    }

    neo_fill(c); // After countdown completes, fill the strip with the main color (purple for record, yellow for unlock) to indicate that we're now ready to capture the gesture
    neo_show(); // Update the strip to show the new state
    _delay_ms(300); // Briefly show the full color to indicate countdown completion
    neo_clear(); // Clear the strip to prepare for gesture capture animation

    Serial.print(F("  GO! G")); // Print "GO!" message to serial monitor along with the gesture number we're about to capture or attempt
    Serial.println(gesture_idx + 1);

    return true; // Indicate that the countdown completed successfully and we're ready to capture the gesture
}

// Setup function
void setup() {
    Serial.begin(9600); // Initialize serial communication at 9600 baud rate
    unsigned long t0 = millis(); // Record the start time to implement a timeout for waiting
    while (!Serial && millis() - t0 < 2000) {
        _delay_ms(10); // Wait briefly for Serial Monitor
    }

    gpio_init(); // Initialize GPIO pins and the CircuitPlayground board (Function in gpio_reg.h)
    neo_boot_animation(); // Play the boot animation on the NeoPixels (Function in gpio_reg.h)

    accel_spi_init(); // Initialize SPI and configure the LIS3DH accelerometer (Function in spi.h)
    if (accel_spi_ok()) { // Check the LIS3DH WHO_AM_I register to make sure the accelerometer is responding over SPI (Function in spi.h)
        Serial.println(F("LIS3DH SPI OK")); 
    }
    else { // If this message appears, the accelerometer was not detected correctly through SPI
        Serial.println(F("LIS3DH SPI check failed"));
        neo_flash(COLOR_ORANGE, 500); 
    }

    key_exists = false; // Start with no saved key after every reset/power-on
    Serial.println(F("No key saved yet. LEFT to record.")); // The user must record a new key each time the board is powered on
    neo_flash(COLOR_ORANGE, 500);

    neo_idle_indicator(); // Set the NeoPixels to the idle state (dim blue) to indicate that we're ready for user input
    go_to(ST_IDLE); // Transition to the idle state to start the main loop

    Serial.println(F("\n=== KinetiKey ===")); // Print the welcome message and instructions to the serial monitor
    Serial.print(F(" Retries=")); Serial.println(MAX_RETRIES);
    Serial.println(F("LEFT=Record | RIGHT=Unlock | LEFT3s=Clear RAM key | BOTH=Cancel\n"));
}

// State handling functions

// 1) Idle state: wait for user to press either the record button (left) or the unlock button (right)
static void do_idle(void) {
    if (btn_left_pressed()) { // If the left button is pressed, we want to start the process of recording a new key
        unsigned long hs = millis(); // Record the time at which the left button was pressed to implement a long-press detection for erasing the key
        uint8_t lp = 0; // Track how many pixels to light up

        while (btn_left_raw()) { // While the left button is still being held down, check if it's a long press for erasing the key
            unsigned long held = millis() - hs; // Calculate how long the button has been held down
            uint8_t px = (held * NUM_PIXELS) / ERASE_HOLD_MS; // Calculate how many pixels to light up based on how long the button has been held (progressively light up more pixels the longer it's held, up to the total number of pixels when it reaches the erase hold time)
            if (px > NUM_PIXELS) px = NUM_PIXELS; // Cap the number of pixels at the total number of pixels in the strip
            if (px != lp) { // If the number of pixels to light up has changed since the last check, update the NeoPixel strip to reflect this
                CircuitPlayground.strip.clear();
                for (uint8_t p = 0; p < px; p++)
                neo_set(p, COLOR_RED);
                neo_show();
                lp = px; 
            }
            if (held >= ERASE_HOLD_MS) { // If the button has been held for long enough to trigger a key erase (3 seconds), perform the erase operation
                Serial.println(F("*** KEY ERASED ***"));
                key_exists = false; // Clear the recorded key from RAM. No EEPROM erase is needed because the key is not saved after power-off
                neo_erase_animation(); // Play the key erase animation on the NeoPixels (Function in gpio_reg.h)
                neo_idle_indicator(); // Return to the idle LED state after erasing
                while (btn_left_raw()) {
                    _delay_ms(10); // Wait until the left button is released before allowing any further actions
                }
                return; // return to main loop
            }
            _delay_ms(10); // Check every 10ms for button release or long-press completion
        }

        neo_clear(); // Clear the NeoPixel strip after the button is released (whether it was a short press or a long press)
        _delay_ms(100);

        Serial.println(F("\n=== RECORD ===")); // Print the record mode message to the serial monitor
        gesture_idx = 0; // Reset the gesture index to 0 to start recording from the first gesture in the combination
        mode = MODE_RECORD; // Set the mode to record so that the rest of the program knows we're in recording mode (used for determining colors and behavior in other functions)
        neo_flash(COLOR_PURPLE, 300); // Flash the NeoPixels purple to indicate that we've entered record mode and are ready to start recording gestures
        if (!do_countdown()) return; // Show the countdown animation and if it returns false, it means the user cancelled during the countdown, so we return early to stop the recording process
        go_to(ST_RECORD_WAIT); // Transition to the state where we wait for the user to perform the first gesture to record
        return; // Return to main loop to start handling the new state
    }
    if (btn_right_pressed()) { // If right button is pressed, we want to start the unlock process
        if (!key_exists) { // If there is no key currently stored, we can't unlock
            Serial.println(F("No key! LEFT to record."));
            neo_no_key_warning(); // Flash a warning animation on the NeoPixels to indicate that there is no key stored (Function in gpio_reg.h)
            neo_idle_indicator(); // Return to idle LED state after showing the warning
            return; 
        }

        Serial.println(F("\n=== UNLOCK ===")); // Print the unlock mode message to the serial monitor
        gesture_idx = 0; // Reset the gesture index to 0 to start attempting to match from the first gesture in the combination
        retries_left = MAX_RETRIES; // Reset the retries left counter to the maximum number of retries allowed before lockout
        mode = MODE_UNLOCK; // Set the mode to unlock so that the rest of the program knows we're in unlocking mode (used for determining colors and behavior in other functions)
        neo_flash(COLOR_YELLOW, 300); // Flash the NeoPixels yellow to indicate that we've entered unlock mode and are ready to start attempting to match gestures
        while (btn_right_raw()) {
            _delay_ms(10);// Wait until the right button is released before starting the countdown and capture process
        }
        if (!do_countdown()) return; // Show the countdown animation and if it returns false, it means the user cancelled during the countdown, so we return early to stop the unlock attempt process
        go_to(ST_UNLOCK_WAIT); 
        return;
    }
}

// 2) Record wait state: after the user presses the record button, we wait for them to perform the gesture. If they take too long, we timeout and return to idle. If they perform motion, we transition to the record capture state.
static void do_record_wait(void) {
    if (check_cancel()) return; // Check if the user has cancelled the operation by holding both buttons, and if so, return early to stop the recording process and go back to idle

    if (millis() - state_enter_time > GESTURE_TIMEOUT_MS) { // If the user takes too long to perform the gesture (exceeds the gesture timeout), we consider it a timeout and return to idle
        Serial.println(F("Timeout.")); // Print a timeout message
        neo_flash(COLOR_ORANGE, 300); // Flash the NeoPixels orange to indicate that a timeout occurred
        neo_idle_indicator(); // Return to the idle LED state after the timeout
        go_to(ST_IDLE); // Transition back to the idle state
        return; 
    }

    if ((millis()/400)%2==0) neo_waiting_pulse(COLOR_PURPLE, gesture_idx); // While waiting for the user to perform the gesture, show a pulsing animation on the NeoPixels to indicate that we're waiting for input. The color is purple since we're in record mode, and the number of pixels lit corresponds to how many gestures have already been recorded in this session.
    else neo_show_progress(gesture_idx, COLOR_PURPLE); // Alternate between the pulsing animation and a static progress display to create a blinking effect while waiting

    int16_t x, y, z;
    read_accel_raw(x, y, z); // Read the raw accelerometer values into x, y, z variables

    if (is_moving_raw(x, y, z)) { // If we detect motion from the accelerometer readings (Function in geture.h)
        capture_reset(); // Reset the gesture capture buffer and counters to prepare for capturing a new gesture (Function in gesture.h)
        capture_add(x, y, z); // Add the current accelerometer reading to the gesture capture buffer (Function in gesture.h)
        saw_motion = true; // Set the flag to indicate that we've seen motion, which is used to determine if the user is actually performing a gesture or just holding still
        last_motion_ms = millis(); // Record the time at which we detected motion to manage timing for when the user stops moving
        last_sample_ms = millis(); // Record the time at which we took this sample to manage the sampling rate during the capture phase

        Serial.print(F("  Capturing G")); // Print a message to the serial monitor indicating that we're now capturing a gesture, along with the gesture number (1, 2, or 3)
        Serial.println(gesture_idx + 1);
        go_to(ST_RECORD_CAPTURE); // Transition to capture state to start actively capturing the gesture data
        saw_motion = true;
    }
}

// 3) Record capture state: Actively capturing gesture data
static void do_record_capture(void) {
    if (check_cancel()) return; // Check if the user has cancelled the operation by holding both buttons, and if so, return early to stop the recording process and go back to idle

    unsigned long now = millis(); // Get the current time to manage sampling rate and detect when the user has stopped moving

    if (now - last_sample_ms < SAMPLE_RATE_MS) return; // If it's not time to take the next sample yet (based on the defined sample rate), return early and wait until the next loop iteration to check again.

    last_sample_ms = now; // Update the last sample time to the current time
    anim_tick++; // Increment the animation tick counter for the LED feedback animation during capture

    neo_capturing_tick(COLOR_PURPLE, gesture_idx, anim_tick); // Update the NeoPixel animation to indicate that we're actively capturing a gesture. The color is purple since we're in record mode, and the animation may show which gesture number we're on and provide some visual feedback during the capture process (Function in gpio_reg.h)
    int16_t x, y, z;
    read_accel_raw(x, y, z); // Read the raw accelerometer values into x, y, z variables to capture the current state of motion for this sample
    bool moving = is_moving_raw(x, y, z); // Determine if the user is currently moving based on the accelerometer readings (Function in gesture.h). 

    if (moving) { // If we detect that the user is still moving
        capture_add(x, y, z); // Add the current accelerometer reading to the gesture capture buffer (Function in gesture.h)
        last_motion_ms = now; // Update the last motion time to the current time since we detected movement
        saw_motion = true; // Set the flag to indicate that we've seen motion
    }

    else { // If we detect that the user has stopped moving
        capture_add(x, y, z); // Add the current accelerometer reading to the gesture capture buffer (Function in gesture.h) to capture the final state of the gesture when the user stops moving
        if (saw_motion && (now - last_motion_ms > STILLNESS_MS)) { // If we've seen motion before and it's been long enough since we last detected motion (indicating that the user has likely finished performing the gesture), we proceed to finalize the capture of this gesture
            if (!capture_valid()) { // If the captured gesture data is not valid (e.g. too short, not enough samples; Function in gesture.h), we consider this a failed capture attempt and prompt the user to try again without advancing to the next gesture
                Serial.println(F("  Too short!")); // Print message indicating that the captured gesture was too short or invalid
                neo_flash(COLOR_ORANGE, 300); // Flash the NeoPixels orange to indicate that the capture was unsuccessful and the user needs to try again
                if (!do_countdown()) return; // Show the countdown animation again to prompt the user to perform the gesture, and if they cancel during the countdown, return early to stop the recording process
                go_to(ST_RECORD_WAIT); return; 
            }
            capture_dump_serial(); // Write the raw captured gesture data to the serial monitor for debugging purposes
            GestureBins bins = capture_finalize(); // Process the raw captured gesture data to compute the downsampled and normalized gesture bins that will be stored as part of the key (Function in gesture.h)
            stored_key[gesture_idx] = bins; // Store the computed gesture bins in the key array
            bins_dump_serial(&bins, gesture_idx); // Write the computed gesture bins to the serial monitor for debugging purposes

            gesture_idx++; // Increment the gesture index to move on to the next gesture in the combination for recording
            Serial.print(F("  G")); // Print a message indicating which gesture number was just recorded out of the total number of gestures in the combination (e.g. "G1/3 recorded!", "G2/3 recorded!", etc.)
            Serial.print(gesture_idx);
            Serial.println(F("/3 recorded!"));

            if (gesture_idx >= NUM_GESTURES) { // If all gestures in the combination have been recorded, finalize the key saving process
                key_exists = true; // Mark that a key now exists in RAM for the current power cycle
                Serial.println(F("\n*** KEY SAVED ***\n")); // Print a message to the serial monitor indicating that the key has been successfully saved
                neo_show_progress(NUM_GESTURES, COLOR_PURPLE); // Show a progress animation with all pixels lit in purple to indicate that all gestures have been recorded
                for (uint16_t i = 0; i < 100; i++) // Briefly show the completed progress animation before transitioning back to idle
                    _delay_ms(10);
                    neo_saved_animation(); // Play the saved key animation on the NeoPixels to indicate that the key has been saved (Function in gpio_reg.h)
                    neo_idle_indicator(); 
                    go_to(ST_IDLE);
            } 

            else { // If there are more gestures to record in the combination, prompt the user to perform the next gesture
                neo_show_progress(gesture_idx, COLOR_PURPLE); // Update the progress animation to show how many gestures have been recorded so far in purple
                for (uint16_t i = 0; i < 200; i++) _delay_ms(10);
                if (!do_countdown()) return; 
                go_to(ST_RECORD_WAIT); 
            }

            return;
        }
    }
    if (raw_count >= MAX_SAMPLES) { // If we've reached the maximum number of samples for this gesture capture, we need to finalize the capture even if the user is still moving, since we can't capture any more data for this gesture
        if (!capture_valid()) { // If the captured gesture data is not valid (Function in gesture.h), consider this a failed capture attempt
            neo_flash(COLOR_ORANGE, 200); // Flash the NeoPixels orange to indicate that the capture was unsuccessful and the user needs to try again
            if (!do_countdown()) return; // Show the countdown animation again to prompt the user to perform the gesture, and if they cancel during the countdown, return early to stop the recording process
            go_to(ST_RECORD_WAIT); // Transition back to the state where we wait for the user to perform the gesture again since this capture attempt was not valid
            return; 
        }
        capture_dump_serial();
        GestureBins bins = capture_finalize();
        stored_key[gesture_idx] = bins; 
        gesture_idx++;

        if (gesture_idx >= NUM_GESTURES) { 
            key_exists = true; // Mark that a key now exists in RAM for the current power cycle 
            Serial.println(F("*** KEY SAVED ***")); 
            neo_saved_animation(); 
            neo_idle_indicator(); 
            go_to(ST_IDLE); 
        }
        else { 
            neo_show_progress(gesture_idx, COLOR_PURPLE); 
            for (uint16_t i = 0; i < 200; i++) _delay_ms(10); 
            if (!do_countdown()) return; 
            go_to(ST_RECORD_WAIT); 
        }
    }
}

// 4) Unlock wait state: after the user presses the unlock button, we wait for them to perform the gesture. 
static void do_unlock_wait(void) {
    if (check_cancel()) return; // Check if the user has cancelled the operation by holding both buttons, and if so, return early to stop the unlock attempt process and go back to idle

    if (millis() - state_enter_time > GESTURE_TIMEOUT_MS) { // If the user takes too long to perform the gesture (exceeds the gesture timeout), we consider it a timeout and return to idle
        Serial.println(F("Timeout.")); 
        neo_flash(COLOR_ORANGE, 300); 
        neo_idle_indicator(); 
        go_to(ST_IDLE); 
        return; 
    }

    if ((millis()/400)%2==0) neo_waiting_pulse(COLOR_YELLOW, gesture_idx); // While waiting for the user to perform the gesture, show a pulsing animation on the NeoPixels to indicate that we're waiting for input. The color is yellow since we're in unlock mode, and the number of pixels lit corresponds to how many gestures have already been successfully matched in this unlock attempt.
    else neo_show_progress(gesture_idx, COLOR_GREEN); // Alternate between the pulsing animation and a static progress display to create a blinking effect while waiting. The progress display uses green pixels to indicate how many gestures have been successfully matched so far in this unlock attempt.

    int16_t x, y, z;
    read_accel_raw(x, y, z); // Read the raw accelerometer values into x, y, z variables to capture the current state of motion for this sample

    if (is_moving_raw(x, y, z)) { // If we detect motion from the accelerometer readings, we assume the user is starting to perform the gesture and transition to the capture state to start actively capturing the gesture data for this unlock attempt
        capture_reset(); // Reset the gesture capture buffer and counters to prepare for capturing a new gesture attempt (Function in gesture.h)
        capture_add(x, y, z); // Add the current accelerometer reading to the gesture capture buffer to capture the initial state of the gesture when we first detect motion (Function in gesture.h)
        saw_motion = true; // Set the flag to indicate that we've seen motion, which is used to determine if the user is actually performing a gesture or just holding still
        last_motion_ms = millis(); // Record the time at which we detected motion to manage timing for when the user stops moving
        last_sample_ms = millis(); // Record the time at which we took this sample to manage the sampling rate during the capture phase
        
        Serial.print(F("  Capturing G")); 
        Serial.println(gesture_idx + 1); 

        go_to(ST_UNLOCK_CAPTURE); // Transition to capture state to start actively capturing the gesture data for this unlock attempt
        saw_motion = true; // Set the flag to indicate that we've seen motion, which is used to determine if the user is actually performing a gesture or just holding still
    }
}

// 5) Unlock capture state: Actively capturing gesture data for unlock attempt and checking for matches against the stored key
static void do_unlock_capture(void) {
    if (check_cancel()) return; // Check if the user has cancelled the operation by holding both buttons, and if so, return early to stop the unlock attempt process and go back to idle
    unsigned long now = millis(); // Get the current time to manage sampling rate and detect when the user has stopped moving

    if (now - last_sample_ms < SAMPLE_RATE_MS) return; // If it's not time to take the next sample yet (based on the defined sample rate), return early and wait until the next loop iteration to check again.
    last_sample_ms = now; // Update the last sample time to the current time
    anim_tick++; // Increment the animation tick counter for the LED feedback animation during capture
    neo_capturing_tick(COLOR_YELLOW, gesture_idx, anim_tick); // Update the NeoPixel animation to indicate that we're actively capturing a gesture for the unlock attempt. The color is yellow since we're in unlock mode, and the animation may show which gesture number we're on and provide some visual feedback during the capture process (Function in gpio_reg.h)
    
    int16_t x, y, z; 
    read_accel_raw(x, y, z); // Read the raw accelerometer values into x, y, z variables to capture the current state of motion for this sample
    bool moving = is_moving_raw(x, y, z); // Determine if the user is currently moving based on the accelerometer readings (Function in gesture.h).

    if (moving) { 
        capture_add(x, y, z); // Add the current accelerometer reading to the gesture capture buffer (Function in gesture.h)
        last_motion_ms = now; // Update the last motion time to the current time since we detected movement
        saw_motion = true; // Set the flag to indicate that we've seen motion, which is used to determine if the user is actually performing a gesture or just holding still
    }
    else {
        capture_add(x, y, z); // Add the current accelerometer reading to the gesture capture buffer (Function in gesture.h) to capture the final state of the gesture when the user stops moving

        if (saw_motion && (now - last_motion_ms > STILLNESS_MS)) { // If we've seen motion before and it's been long enough since we last detected motion (indicating that the user has likely finished performing the gesture), we proceed to finalize the capture of this gesture and check if it matches the corresponding gesture in the stored key
            if (!capture_valid()) { // If the captured gesture data is not valid (e.g. too short, not enough samples; Function in gesture.h), we consider it a failed capture attempt and prompt the user to try again without advancing to the next gesture
                Serial.println(F("  Too short!")); 
                neo_flash(COLOR_ORANGE, 300); 
                if (!do_countdown()) 
                    return;
                go_to(ST_UNLOCK_WAIT);
                return; 
            }

            capture_dump_serial(); // Write the raw captured gesture data to the serial monitor for debugging purposes
            GestureBins attempt = capture_finalize(); // Process the raw captured gesture data to compute the downsampled and normalized gesture bins for this unlock attempt (Function in gesture.h)
            bins_dump_serial(&attempt, gesture_idx); // Write the computed gesture bins for this unlock attempt to the serial monitor for debugging purposes
            float dist = gesture_distance(&attempt, &stored_key[gesture_idx]); // Compute the distance between the captured gesture bins for this unlock attempt and the corresponding gesture bins in the stored key to determine how close of a match this attempt is (Function in gesture.h)
            Serial.print(F("  G")); 
            Serial.print(gesture_idx + 1); 
            Serial.print(F(" dist=")); 
            Serial.print(dist, 3);
            Serial.print(F(" thr=")); 
            Serial.print(MATCH_THRESHOLD, 3);

            if (gesture_matches(&attempt, &stored_key[gesture_idx])) { // If the captured gesture bins for this unlock attempt match closely enough to the corresponding gesture bins in the stored key (based on a defined distance threshold), we consider this a successful match for this gesture in the combination (Function in gesture.h)
                Serial.println(F(" MATCH!"));
                gesture_idx++; 
                retries_left = MAX_RETRIES;

                if (gesture_idx >= NUM_GESTURES) { // If all gestures in the combination have been successfully matched, we consider the unlock attempt successful and transition to the unlocked state
                    Serial.println(F("\n***** UNLOCKED! *****\n")); 
                    go_to(ST_UNLOCKED); // Transition to the unlocked state to play the success animation
                }
                else { // If there are more gestures to attempt in the combination, prompt the user to perform the next gesture
                    neo_show_progress(gesture_idx, COLOR_GREEN); // Update the progress animation to show how many gestures have been successfully matched so far in green
                    for (uint16_t i = 0; i < 200; i++) _delay_ms(10); 
                    if (!do_countdown()) return; 
                    go_to(ST_UNLOCK_WAIT); 
                }

            } else { // If the captured gesture bins for this unlock attempt do not match closely enough to the corresponding gesture bins in the stored key, we consider this a failed match for this gesture in the combination
                retries_left--; // Decrement the retries left counter since this was an unsuccessful attempt at matching this gesture
                Serial.print(F(" NO MATCH (")); 
                Serial.print(retries_left); // Print how many retries are left for this gesture before lockout
                Serial.println(F(" left)"));

                if (retries_left == 0) { // If there are no retries left for this gesture, we consider the unlock attempt failed and transition to the failed state
                    Serial.println(F("\n*** ALL RETRIES USED ***\n")); 
                    go_to(ST_FAILED); // Transition to the failed state to play the failure animation
                }
                else { // If there are still retries left for this gesture, prompt the user to try performing this gesture again
                    neo_flash(COLOR_ORANGE, 400); // Flash the NeoPixels orange to indicate that the attempt to match this gesture was unsuccessful and the user needs to try again
                    neo_show_progress(gesture_idx, COLOR_GREEN); // Update the progress animation to show how many gestures have been successfully matched so far in green, which remains the same since this attempt was unsuccessful
                    for (uint16_t i = 0; i < 100; i++) _delay_ms(10); 
                    if (!do_countdown()) return; 
                    go_to(ST_UNLOCK_WAIT); // Transition back to the state where we wait for the user to perform this gesture again since this attempt was not successful
                }
            }
            return;
        }
    }
    if (raw_count >= MAX_SAMPLES) { // If we've reached the maximum number of samples for this gesture capture, we need to finalize the capture and check for a match against the stored key even if the user is still moving, since we can't capture any more data for this gesture
        
        if (!capture_valid()) { // If the captured gesture data is not valid (Function in gesture.h), consider this a failed capture attempt
            neo_flash(COLOR_ORANGE, 200); 
            if (!do_countdown()) return; 
            go_to(ST_UNLOCK_WAIT); // Transition back to the state where we wait for the user to perform this gesture again since this capture attempt was not valid
            return; 
        }

        capture_dump_serial(); // Write the raw captured gesture data to the serial monitor for debugging purposes
        GestureBins attempt = capture_finalize(); // Process the raw captured gesture data to compute the downsampled and normalized gesture bins for this unlock attempt (Function in gesture.h)
        float dist = gesture_distance(&attempt, &stored_key[gesture_idx]); // Compute the distance between the captured gesture bins for this unlock attempt and the corresponding gesture bins in the stored key to determine how close of a match this attempt is (Function in gesture.h)
        Serial.print(F("  dist=")); 
        Serial.print(dist, 3);

        if (gesture_matches(&attempt, &stored_key[gesture_idx])) { // If the captured gesture bins for this unlock attempt match closely enough to the corresponding gesture bins in the stored key (based on a defined distance threshold), we consider this a successful match for this gesture in the combination (Function in gesture.h)
            Serial.println(F(" MATCH!")); 
            gesture_idx++; 
            retries_left = MAX_RETRIES;
            if (gesture_idx >= NUM_GESTURES) {  // If all gestures in the combination have been successfully matched, we consider the unlock attempt successful and transition to the unlocked state
                Serial.println(F("\n***** UNLOCKED! *****\n"));
                go_to(ST_UNLOCKED); 
            }
            else { // If there are more gestures to attempt in the combination, prompt the user to perform the next gesture
                neo_show_progress(gesture_idx, COLOR_GREEN); 
                for (uint16_t i = 0; i < 200; i++) _delay_ms(10); 
                if (!do_countdown()) return;
                go_to(ST_UNLOCK_WAIT); // Transition back to the state where we wait for the user to perform the next gesture since this attempt was successful and we need to move on to the next one
            }
        } else { // If the captured gesture bins for this unlock attempt do not match closely enough to the corresponding gesture bins in the stored key, we consider this a failed match for this gesture in the combination
            retries_left--; // Decrement the retries left counter since this was an unsuccessful attempt at matching this gesture
            Serial.print(F(" NO MATCH (")); 
            Serial.print(retries_left); 
            Serial.println(F(" left)"));

            if (retries_left == 0) { // If there are no retries left for this gesture, we consider the unlock attempt failed and transition to the failed state
                Serial.println(F("\n*** ALL RETRIES USED ***\n"));
                go_to(ST_FAILED); 
            } else { // If there are still retries left for this gesture, prompt the user to try performing this gesture again
                Serial.println(F(" TRY AGAIN"));
                neo_flash(COLOR_ORANGE, 400);
                if (!do_countdown()) return;
                go_to(ST_UNLOCK_WAIT); // Transition back to the state where we wait for the user to perform this gesture again since this attempt was not successful
            } 
        }
    }
}

// 6) Unlocked state: play success animation
static void do_unlocked(void) { 
    neo_success_animation(); // Play the success animation on the NeoPixels to indicate that the unlock attempt was successful (Function in gpio_reg.h)
    Serial.println(F("Locked. Returning to idle.\n")); // Print a message to the serial monitor indicating that we're now returning to the idle state after a successful unlock attempt
    neo_idle_indicator(); // Return to the idle LED state after showing the success animation
    go_to(ST_IDLE); // Transition back to the idle state after a successful unlock attempt
}

// 7) Failed state: play failure animation and return to idle
static void do_failed(void) { 
    neo_fail_animation(); // Play the failure animation on the NeoPixels to indicate that the unlock attempt has failed (Function in gpio_reg.h)
    Serial.println(F("Failed. RIGHT to retry.\n")); // Print a message to the serial monitor indicating that the unlock attempt has failed and prompt the user to press the right button to retry
    neo_idle_indicator(); // Return to the idle LED state after showing the failure animation
    go_to(ST_IDLE); // Transition back to the idle state after a failed unlock attempt
}

// Loop function to handle state transitions
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
