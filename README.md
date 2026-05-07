
# Embedded final project

A gesture-based combination lock using an accelerometer and Dynamic Time Warping. Hold the board in a closed fist, perform three distinct gestures to set a key, then replicate them to unlock.

Team Members: Daksh Mehta, Sohan Sondhi, Sichen Liang, Arial Lee

# Hardware

Adafruit Circuit Playground Classic (ATmega32u4 @ 8MHz)
1) Onboard LIS3DH 3-axis accelerometer (SPI)
2) Onboard 10x NeoPixel ring for visual feedback
3) Onboard push buttons and LEDs


# Recording a Key
1. Short press LEFT for Record mode
2. LED countdown (2s) then it will flash as GO signal
3. Perform gesture while holding the board in a closed fist and hold still for around 0.5s for gesture to be captured
4. Repeat for all 3 gestures
5. Green flash tells the user that the key is saved to RAM

# Unlocking
Press the RIGHT button for Unlock mode and replicate the same 3 gestures in order. Each correct gesture will give a green progress LEDs. You will have up to 3 retries per gesture and if all 3 are correct then there is a green flash animation which tells it is UNLOCKED

# Controls
Record new key: Short press LEFT button
Unlock: Press RIGHT button
Erase stored key: Hold LEFT button for 3 seconds
Cancel any operation: Short press BOTH buttons

# Gesture Recognition

We use Dynamic Time Warping (DTW) on normalized acceleration magnitude. The board records XYZ acceleration at 50Hz, computes the magnitude sqrt(x^2 + y^2 + z^2), normalizes it, and compares the resulting sequence against the saved template using DTW. If the average DTW cost is below the match threshold, the gesture passes.

This makes matching tolerant to gesture speed (DTW warps the time axis), grip orientation (magnitude is direction-independent), and gesture size (normalization removes scale).


# Build & Flash
1. Compile: pio run
2. Double-click RESET on the board (red LED pulses = bootloader mode)
3. Upload: `pio run -t upload`
4. Serial monitor at 9600 baud: `pio device monitor`

# LED Feedback

Two blue dots: Idle
White dots: Capturing gesture
Purple progress LEDs: Completed gesture progress (3 LEDs per gesture)
Green progress LEDs: Unlock progress
Green Flash animation: UNLOCKED
Red flash x3: Failed (all retries used)
Orange blink: Retry as gesture is too short

# Testing

1. Record a 3-number-gesture sequence and unlock with the same gestures — should pass consistently
2. Try unlocking with different gestures — should fail
3. Hold LEFT for 3 seconds to erase, then verify with RIGHT (should report no key)
4. Run `pio device monitor` while testing to see DTW distances and match decisions

# Result
1. Tested with 3 gestures drawing a number (0-9)
2. Achieve a good success rate although sometimes gets confused from some similar patterns (Better to have bigger motion when drawing number)
3. Demo-Video Link: https://drive.google.com/file/d/1VzubHkgXRPi2iQStQGSspEWrZTPNSykI/view?usp=sharing
