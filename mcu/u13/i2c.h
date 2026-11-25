/* The two I2C buses on U13:
 *  - I2C_MAIN: hardware I2C2, PB10/PB11 (IMU-SCL/SDA): right VL53L7 (J5),
 *    side VL53L0X, IMU LSM6DS3TR-C (0x6A). R5/R6 2.2k to VSENS-3V3.
 *  - I2C_CAM: bit-banged on PC14/PC15 (CAM-SCL/SDA): left VL53L7 (J6) and the
 *    camera SCCB (GC2145 0x3C, OV2640 0x30). R246/R247 2.2k to VSENS-3V3.
 *    U28's I2C2 is wired in parallel; only one master drives at a time.
 * Blocking, with timeouts; addresses are 7-bit. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { I2C_MAIN = 0, I2C_CAM = 1 } i2c_bus_t;

void i2c_init(void);          /* call after VSENS-3V3 is up */
void i2c_release(void);       /* pins back to inputs before VSENS-3V3 goes down */
bool i2c_xfer(i2c_bus_t bus, uint8_t addr, const uint8_t *w, uint32_t wlen, uint8_t *r,
              uint32_t rlen);
bool i2c_probe(i2c_bus_t bus, uint8_t addr);
bool i2c_read_reg8(i2c_bus_t bus, uint8_t addr, uint8_t reg, uint8_t *val);
bool i2c_write_reg8(i2c_bus_t bus, uint8_t addr, uint8_t reg, uint8_t val);
void i2c_recover(i2c_bus_t bus); /* 9 clocks + STOP */
