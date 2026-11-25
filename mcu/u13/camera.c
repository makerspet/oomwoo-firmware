#include "camera.h"

#include <stddef.h>

#include "board.h"
#include "i2c.h"

static bool s_clk;

void camera_clock(bool on) {
  if (on) {
    /* PLL3 = HSE / PREDIV2 x PLL3MUL = 8 MHz / 2 x 6 = 24 MHz. PREDIV2 also
     * feeds PLL2, which is unused; the system PLL takes HSE directly. */
    RCC_PREDIV2Config(RCC_PREDIV2_Div2);
    RCC_PLL3Config(RCC_PLL3Mul_6);
    RCC_PLL3Cmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLL3RDY) == RESET) {
    }
    RCC_MCOConfig(RCC_MCO_PLL3CLK);
    pin_mode(PIN(PIN_CAM_OSC), PM_AF_PP);
  } else {
    pin_output(PIN(PIN_CAM_OSC), false, PM_OUT_PP);
    RCC_MCOConfig(RCC_MCO_NoClock);
    RCC_PLL3Cmd(DISABLE);
  }
  s_clk = on;
}

bool camera_clock_on(void) { return s_clk; }

void camera_power_down(bool pd) { pin_write(PIN(PIN_CAM_PWDN), pd); }

const char *camera_identify(uint16_t *id) {
  uint8_t hi = 0u, lo = 0u;
  /* GC2145 (and GC0308/GC2155 family): 7-bit address 0x3C, ID at 0xF0/0xF1 */
  if (i2c_read_reg8(I2C_CAM, 0x3C, 0xF0, &hi) && i2c_read_reg8(I2C_CAM, 0x3C, 0xF1, &lo)) {
    *id = (uint16_t)((hi << 8) | lo);
    return *id == 0x2145u ? "GC2145" : "GalaxyCore?";
  }
  /* OV2640: 0x30, register bank 1 (0xFF = 1), PID/VER at 0x0A/0x0B (0x2642) */
  if (i2c_write_reg8(I2C_CAM, 0x30, 0xFF, 0x01) && i2c_read_reg8(I2C_CAM, 0x30, 0x0A, &hi) &&
      i2c_read_reg8(I2C_CAM, 0x30, 0x0B, &lo)) {
    *id = (uint16_t)((hi << 8) | lo);
    return (hi == 0x26u) ? "OV2640" : "OmniVision?";
  }
  /* OV3660/OV5640: 0x3C with 16-bit registers 0x300A/0x300B */
  *id = 0u;
  return NULL;
}
