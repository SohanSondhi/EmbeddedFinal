#ifndef STORAGE_H
#define STORAGE_H

#include <avr/eeprom.h>
#include <string.h>
#include "pin_config.h"
#include "gesture.h"

// =============================================================
// EEPROM Storage for Gesture Key
//
// ATmega32u4 has 1024 bytes of EEPROM.
// Our data: 3 gestures × 120 bytes = 360 bytes + header = ~370 bytes.
// Plenty of room.
//
// Layout:
//   Byte 0:       Magic number (0xAB) — indicates valid data
//   Byte 1:       Checksum (XOR of all gesture data bytes)
//   Byte 2..361:  GestureBins[3] (360 bytes)
// =============================================================

#define EEPROM_MAGIC_ADDR   0
#define EEPROM_CSUM_ADDR    1
#define EEPROM_DATA_ADDR    2
#define EEPROM_MAGIC_VAL    0xAB

// Compute simple XOR checksum over a byte array
static uint8_t compute_checksum(const void *data, uint16_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint8_t csum = 0;
    for (uint16_t i = 0; i < len; i++) {
        csum ^= p[i];
    }
    return csum;
}

// Save 3 gesture bins to EEPROM
static void storage_save(const GestureBins key[NUM_GESTURES]) {
    uint16_t data_size = sizeof(GestureBins) * NUM_GESTURES;

    // Compute checksum
    uint8_t csum = compute_checksum(key, data_size);

    // Write magic
    eeprom_update_byte((uint8_t *)EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);

    // Write checksum
    eeprom_update_byte((uint8_t *)EEPROM_CSUM_ADDR, csum);

    // Write gesture data
    eeprom_update_block(
        (const void *)key,
        (void *)EEPROM_DATA_ADDR,
        data_size
    );
}

// Load 3 gesture bins from EEPROM. Returns true if valid.
static bool storage_load(GestureBins key[NUM_GESTURES]) {
    // Check magic
    if (eeprom_read_byte((const uint8_t *)EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VAL) {
        return false;
    }

    // Read stored checksum
    uint8_t stored_csum = eeprom_read_byte((const uint8_t *)EEPROM_CSUM_ADDR);

    // Read gesture data
    uint16_t data_size = sizeof(GestureBins) * NUM_GESTURES;
    eeprom_read_block(
        (void *)key,
        (const void *)EEPROM_DATA_ADDR,
        data_size
    );

    // Verify checksum
    uint8_t actual_csum = compute_checksum(key, data_size);
    if (actual_csum != stored_csum) {
        return false;  // Data corrupted
    }

    return true;
}

// Check if valid key exists without loading it
static bool storage_has_key(void) {
    return eeprom_read_byte((const uint8_t *)EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VAL;
}

// Erase stored key
static void storage_erase(void) {
    eeprom_update_byte((uint8_t *)EEPROM_MAGIC_ADDR, 0x00);
}

#endif // STORAGE_H
