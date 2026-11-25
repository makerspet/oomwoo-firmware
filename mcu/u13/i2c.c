#include "i2c.h"

#include <stddef.h>

#include "board.h"
#include "systime.h"

#define I2C_TIMEOUT_US 2000u

/* ---------------------------------------------------------------- I2C2 */

static bool wait_flag(volatile uint16_t *reg, uint16_t mask, bool set) {
  const uint32_t t0 = systime_us();
  while (((*reg & mask) != 0u) != set) {
    if (I2C2->STAR1 & (I2C_STAR1_AF | I2C_STAR1_BERR | I2C_STAR1_ARLO)) return false;
    if (systime_us() - t0 > I2C_TIMEOUT_US) return false;
  }
  return true;
}

static void hw_stop_and_clear(void) {
  I2C2->CTLR1 |= I2C_CTLR1_STOP;
  I2C2->STAR1 = 0u; /* clear AF/BERR/ARLO */
}

static void hw_init(void) {
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
  I2C_DeInit(I2C2);
  I2C_InitTypeDef c = {0};
  c.I2C_ClockSpeed = 400000;
  c.I2C_Mode = I2C_Mode_I2C;
  c.I2C_DutyCycle = I2C_DutyCycle_2;
  c.I2C_OwnAddress1 = 0x02;
  c.I2C_Ack = I2C_Ack_Enable;
  c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
  I2C_Init(I2C2, &c);
  I2C_Cmd(I2C2, ENABLE);
  pin_mode(PIN(PIN_IMU_SCL), PM_AF_OD);
  pin_mode(PIN(PIN_IMU_SDA), PM_AF_OD);
}

static bool hw_xfer(uint8_t addr, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen) {
  if (!wait_flag(&I2C2->STAR2, I2C_STAR2_BUSY, false)) {
    i2c_recover(I2C_MAIN);
    return false;
  }
  if (wlen != 0u || rlen == 0u) {
    I2C2->CTLR1 |= I2C_CTLR1_START;
    if (!wait_flag(&I2C2->STAR1, I2C_STAR1_SB, true)) goto fail;
    I2C2->DATAR = (uint16_t)(addr << 1);
    if (!wait_flag(&I2C2->STAR1, I2C_STAR1_ADDR, true)) goto fail;
    (void)I2C2->STAR2;
    for (uint32_t i = 0; i < wlen; ++i) {
      if (!wait_flag(&I2C2->STAR1, I2C_STAR1_TXE, true)) goto fail;
      I2C2->DATAR = w[i];
    }
    if (wlen != 0u && !wait_flag(&I2C2->STAR1, I2C_STAR1_BTF, true)) goto fail;
  }
  if (rlen != 0u) {
    I2C2->CTLR1 |= I2C_CTLR1_START | I2C_CTLR1_ACK;
    if (!wait_flag(&I2C2->STAR1, I2C_STAR1_SB, true)) goto fail;
    I2C2->DATAR = (uint16_t)((addr << 1) | 1u);
    if (!wait_flag(&I2C2->STAR1, I2C_STAR1_ADDR, true)) goto fail;
    if (rlen == 1u) I2C2->CTLR1 &= ~I2C_CTLR1_ACK;
    __disable_irq(); /* ACK/STOP must be set before the last byte arrives */
    (void)I2C2->STAR2;
    if (rlen == 1u) I2C2->CTLR1 |= I2C_CTLR1_STOP;
    __enable_irq();
    for (uint32_t i = 0; i < rlen; ++i) {
      if (!wait_flag(&I2C2->STAR1, I2C_STAR1_RXNE, true)) goto fail;
      if (rlen > 1u && i == rlen - 2u) {
        I2C2->CTLR1 &= ~I2C_CTLR1_ACK;
        I2C2->CTLR1 |= I2C_CTLR1_STOP;
      }
      r[i] = (uint8_t)I2C2->DATAR;
    }
  } else {
    I2C2->CTLR1 |= I2C_CTLR1_STOP;
  }
  return true;
fail:
  hw_stop_and_clear();
  return false;
}

/* ---------------------------------------------------------------- bit-bang */
/* PC14/PC15 are low-drive pins (2 MHz, sink only): open drain is all they
 * need with the 2.2k pull-ups. ~100 kHz for bring-up; 400 kHz is a bench
 * test item (VL53L7 firmware upload, ~84 KB). */
#define BB_HALF_US 5u

static void scl(bool v) { pin_write(PIN(PIN_CAM_SCL), v); }
static void sda(bool v) { pin_write(PIN(PIN_CAM_SDA), v); }
static bool sda_in(void) { return pin_read(PIN(PIN_CAM_SDA)); }

static bool scl_high_wait(void) {
  scl(true);
  const uint32_t t0 = systime_us();
  while (!pin_read(PIN(PIN_CAM_SCL))) { /* clock stretching */
    if (systime_us() - t0 > I2C_TIMEOUT_US) return false;
  }
  return true;
}

static void bb_start(void) {
  sda(true);
  scl_high_wait();
  systime_delay_us(BB_HALF_US);
  sda(false);
  systime_delay_us(BB_HALF_US);
  scl(false);
}

static void bb_stop(void) {
  sda(false);
  systime_delay_us(BB_HALF_US);
  scl_high_wait();
  systime_delay_us(BB_HALF_US);
  sda(true);
  systime_delay_us(BB_HALF_US);
}

static bool bb_write(uint8_t b) {
  for (int i = 7; i >= 0; --i) {
    sda((b >> i) & 1u);
    systime_delay_us(BB_HALF_US);
    if (!scl_high_wait()) return false;
    systime_delay_us(BB_HALF_US);
    scl(false);
  }
  sda(true);
  systime_delay_us(BB_HALF_US);
  if (!scl_high_wait()) return false;
  const bool ack = !sda_in();
  systime_delay_us(BB_HALF_US);
  scl(false);
  return ack;
}

static uint8_t bb_read(bool ack) {
  uint8_t b = 0u;
  sda(true);
  for (int i = 0; i < 8; ++i) {
    systime_delay_us(BB_HALF_US);
    scl_high_wait();
    b = (uint8_t)((b << 1) | (sda_in() ? 1u : 0u));
    systime_delay_us(BB_HALF_US);
    scl(false);
  }
  sda(!ack);
  systime_delay_us(BB_HALF_US);
  scl_high_wait();
  systime_delay_us(BB_HALF_US);
  scl(false);
  sda(true);
  return b;
}

static bool bb_xfer(uint8_t addr, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen) {
  bool ok = true;
  if (wlen != 0u || rlen == 0u) {
    bb_start();
    ok = bb_write((uint8_t)(addr << 1));
    for (uint32_t i = 0; ok && i < wlen; ++i) ok = bb_write(w[i]);
  }
  if (ok && rlen != 0u) {
    bb_start(); /* repeated start (SCCB devices also accept it) */
    ok = bb_write((uint8_t)((addr << 1) | 1u));
    for (uint32_t i = 0; ok && i < rlen; ++i) r[i] = bb_read(i + 1u < rlen);
  }
  bb_stop();
  return ok;
}

static void bb_init(void) {
  pin_output(PIN(PIN_CAM_SCL), true, PM_OUT_OD_2M);
  pin_output(PIN(PIN_CAM_SDA), true, PM_OUT_OD_2M);
}

/* ---------------------------------------------------------------- API */

void i2c_init(void) {
  hw_init();
  bb_init();
}

void i2c_release(void) {
  I2C_Cmd(I2C2, DISABLE);
  pin_mode(PIN(PIN_IMU_SCL), PM_IN_FLOAT);
  pin_mode(PIN(PIN_IMU_SDA), PM_IN_FLOAT);
  pin_mode(PIN(PIN_CAM_SCL), PM_IN_FLOAT);
  pin_mode(PIN(PIN_CAM_SDA), PM_IN_FLOAT);
}

bool i2c_xfer(i2c_bus_t bus, uint8_t addr, const uint8_t *w, uint32_t wlen, uint8_t *r,
              uint32_t rlen) {
  return bus == I2C_MAIN ? hw_xfer(addr, w, wlen, r, rlen) : bb_xfer(addr, w, wlen, r, rlen);
}

bool i2c_probe(i2c_bus_t bus, uint8_t addr) { return i2c_xfer(bus, addr, NULL, 0u, NULL, 0u); }

bool i2c_read_reg8(i2c_bus_t bus, uint8_t addr, uint8_t reg, uint8_t *val) {
  return i2c_xfer(bus, addr, &reg, 1u, val, 1u);
}

bool i2c_write_reg8(i2c_bus_t bus, uint8_t addr, uint8_t reg, uint8_t val) {
  const uint8_t b[2] = {reg, val};
  return i2c_xfer(bus, addr, b, 2u, NULL, 0u);
}

void i2c_recover(i2c_bus_t bus) {
  const pin_t scl_pin = bus == I2C_MAIN ? PIN(PIN_IMU_SCL) : PIN(PIN_CAM_SCL);
  const pin_t sda_pin = bus == I2C_MAIN ? PIN(PIN_IMU_SDA) : PIN(PIN_CAM_SDA);
  const uint8_t od = bus == I2C_MAIN ? PM_OUT_OD : PM_OUT_OD_2M;
  if (bus == I2C_MAIN) I2C_Cmd(I2C2, DISABLE);
  pin_output(sda_pin, true, od);
  pin_output(scl_pin, true, od);
  for (int i = 0; i < 9; ++i) {
    pin_low(scl_pin);
    systime_delay_us(5u);
    pin_high(scl_pin);
    systime_delay_us(5u);
  }
  pin_low(sda_pin); /* STOP */
  systime_delay_us(5u);
  pin_high(sda_pin);
  if (bus == I2C_MAIN) {
    I2C_SoftwareResetCmd(I2C2, ENABLE);
    I2C_SoftwareResetCmd(I2C2, DISABLE);
    hw_init();
  }
}
