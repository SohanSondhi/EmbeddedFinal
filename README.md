# Embedded Challenge Spring 2026

## Term Project

# KinetiKey

*"Old lock, New Twist"*

![KinetiKey image](Embedded%20Challenge%20S2026_media/media/image1.png)

## Objective

- Use the data collected from a single accelerometer and/or gyro to record a three-gesture sequence needed to generally "unlock" a resource. Similar to the old-school combination locks that required a specific 3-number combination, except your lock will require a three-gesture sequence instead.
- The recorded sequence must be saved on the microcontroller using a **Record Key** feature.
- The user must then replicate the key sequence within sufficient tolerances to unlock the resource.
- Every successful gesture should be indicated somehow on your development board, such as with LEDs.
- A successful unlock must be indicated by a visual signal, such as an LED or similar indicator.

## Restrictions

- This is a group project to be done by groups of no more than 3 students.
- Only one microcontroller and one accelerometer/gyro may be used, specifically the one integrated on your board.
- You must use PlatformIO as done throughout the class.
- You may use drivers/HAL functions available through the IDE.
- The accelerometer/gyro must be held in a closed fist of either hand while performing the mechanical sequence.
- There must be a way to invoke both **unlock** and **record** functionality so that the user knows when to start the unlocking and recording sequences, respectively.

## Grading Criteria

- Ability to successfully achieve the objectives (40%)
- Repeatability and robustness of unlock sequence via video demo (20%)
- Ease of use (10%)
- Creativity (10%)
- Well-written code (10%)
- Complexity (10%)
