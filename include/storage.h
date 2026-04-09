#ifndef STORAGE_H
#define STORAGE_H

#include <avr/eeprom.h>
#include "pin_config.h"
#include "gesture.h"

// =============================================================
// EEPROM Storage for Gesture Key
// ATmega32u4 has 1024 bytes of EEPROM
// =============================================================

// Magic number to verify valid data is stored
#define STORAGE_MAGIC       0xAB
#define STORAGE_MAGIC_ADDR  0

// Gesture data starts at address 1
// Each GestureFeatures is ~32 bytes, 3 gestures = ~96 bytes
// Well within 1024 byte EEPROM limit
#define STORAGE_DATA_ADDR   1

// =============================================================
// Save / Load
// =============================================================

// Save gesture key to EEPROM
void storage_save_key(GestureFeatures key[NUM_GESTURES]) {
    // Write magic byte to indicate valid data
    eeprom_update_byte((uint8_t *)STORAGE_MAGIC_ADDR, STORAGE_MAGIC);

    // Write gesture data
    eeprom_update_block(
        (const void *)key,
        (void *)STORAGE_DATA_ADDR,
        sizeof(GestureFeatures) * NUM_GESTURES
    );
}

// Load gesture key from EEPROM. Returns true if valid data found.
bool storage_load_key(GestureFeatures key[NUM_GESTURES]) {
    // Check magic byte
    uint8_t magic = eeprom_read_byte((const uint8_t *)STORAGE_MAGIC_ADDR);
    if (magic != STORAGE_MAGIC) {
        return false;  // no valid key stored
    }

    // Read gesture data
    eeprom_read_block(
        (void *)key,
        (const void *)STORAGE_DATA_ADDR,
        sizeof(GestureFeatures) * NUM_GESTURES
    );
    return true;
}

// Check if a key exists in EEPROM
bool storage_has_key() {
    return eeprom_read_byte((const uint8_t *)STORAGE_MAGIC_ADDR) == STORAGE_MAGIC;
}

// Erase stored key
void storage_erase_key() {
    eeprom_update_byte((uint8_t *)STORAGE_MAGIC_ADDR, 0x00);
}

#endif // STORAGE_H
