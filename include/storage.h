#ifndef STORAGE_H
#define STORAGE_H

#include <avr/eeprom.h>
#include "pin_config.h"
#include "gesture.h"

#define EEPROM_MAGIC_ADDR   0
#define EEPROM_CSUM_ADDR    1
#define EEPROM_DATA_ADDR    2
#define EEPROM_MAGIC_VAL    0xAB

static uint8_t compute_checksum(const void *data, uint16_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint8_t csum = 0;
    for (uint16_t i = 0; i < len; i++) csum ^= p[i];
    return csum;
}

static void storage_save(const GestureBins key[NUM_GESTURES]) {
    uint16_t sz = sizeof(GestureBins) * NUM_GESTURES;
    eeprom_update_byte((uint8_t *)EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
    eeprom_update_byte((uint8_t *)EEPROM_CSUM_ADDR, compute_checksum(key, sz));
    eeprom_update_block((const void *)key, (void *)EEPROM_DATA_ADDR, sz);
}

static bool storage_load(GestureBins key[NUM_GESTURES]) {
    if (eeprom_read_byte((const uint8_t *)EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VAL)
        return false;
    uint8_t stored = eeprom_read_byte((const uint8_t *)EEPROM_CSUM_ADDR);
    uint16_t sz = sizeof(GestureBins) * NUM_GESTURES;
    eeprom_read_block((void *)key, (const void *)EEPROM_DATA_ADDR, sz);
    return compute_checksum(key, sz) == stored;
}

static void storage_erase(void) {
    eeprom_update_byte((uint8_t *)EEPROM_MAGIC_ADDR, 0x00);
}

#endif
