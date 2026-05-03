#ifndef SPI_H
#define SPI_H

#include "SPI.h"
#include "pin_config.h"

static inline void accel_cs_low(void) { // Select the LIS3DH accelerometer so the microcontroller can communicate with it over SPI
    ACCEL_CS_PORT &= ~(1 << ACCEL_CS_BIT); // Pull CS low
}

static inline void accel_cs_high(void) { // Deselect the LIS3DH accelerometer when the SPI transfer is finished
    ACCEL_CS_PORT |= (1 << ACCEL_CS_BIT); // Pull CS high
}

static void accel_write_reg(uint8_t reg, uint8_t value) { // Write one byte of configuration data to a LIS3DH register
    accel_cs_low(); // Begin SPI communication with the accelerometer
    SPI.transfer(reg); // Send the register address to write to
    SPI.transfer(value); // Send the value that should be written into that register
    accel_cs_high(); // End SPI communication with the accelerometer
}

static uint8_t accel_read_reg(uint8_t reg) { // Read one byte from a LIS3DH register
    accel_cs_low(); // Begin SPI communication with the accelerometer
    SPI.transfer(reg | READ_REG); // Send the register address with the read bit added
    uint8_t value = SPI.transfer(0xFF); // Send a dummy byte so the accelerometer can send the register value back
    accel_cs_high(); // End SPI communication with the accelerometer
    return value; // Return the value read from the register
}

static void accel_spi_init(void) { // Initialize SPI communication and configure the LIS3DH accelerometer
    ACCEL_CS_DDR |= (1 << ACCEL_CS_BIT); // Set the accelerometer CS pin as an output
    accel_cs_high(); // Keep CS high when the accelerometer is not being accessed

    SPI.begin(); // Start SPI communication so the microcontroller can transfer bytes to/from the LIS3DH

    accel_write_reg(CFG1_REG, 0b01000111); // 50 Hz data rate with X, Y, and Z axes enabled
    accel_write_reg(CFG4_REG, 0b00010000); // Set the accelerometer range to +/- 4g
}

static bool accel_spi_ok(void) { // Check that SPI communication is reaching the LIS3DH accelerometer
    return accel_read_reg(WHO_AM_I) == 0x33; // The LIS3DH WHO_AM_I register should return 0x33
}

static void accel_spi_read_xyz(int16_t &x, int16_t &y, int16_t &z) { // Read raw x, y, z accelerometer values directly through SPI
    accel_cs_low(); // Begin SPI communication with the accelerometer
    SPI.transfer(AX_REG | READ_REG | INC_REG); // Start reading at the X-axis output register and auto-increment through all 6 data bytes

    uint8_t xl = SPI.transfer(0xFF); // Read X low byte
    uint8_t xh = SPI.transfer(0xFF); // Read X high byte
    uint8_t yl = SPI.transfer(0xFF); // Read Y low byte
    uint8_t yh = SPI.transfer(0xFF); // Read Y high byte
    uint8_t zl = SPI.transfer(0xFF); // Read Z low byte
    uint8_t zh = SPI.transfer(0xFF); // Read Z high byte

    accel_cs_high(); // End SPI communication with the accelerometer

    x = (int16_t)(((uint16_t)xh << 8) | xl); // Combine high and low bytes into one signed 16-bit X value
    y = (int16_t)(((uint16_t)yh << 8) | yl); // Combine high and low bytes into one signed 16-bit Y value
    z = (int16_t)(((uint16_t)zh << 8) | zl); // Combine high and low bytes into one signed 16-bit Z value
}

#endif