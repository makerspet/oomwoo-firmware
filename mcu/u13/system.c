/* U13 clock tree (called from the WCH startup before main):
 *   X3 8 MHz crystal (HSE) -> PLL x18 -> SYSCLK 144 MHz
 *   HCLK 144 MHz, PCLK1 72 MHz, PCLK2 72 MHz (timers on both buses run at 144 MHz)
 *   ADC clock: PCLK2 / 6 = 12 MHz (14 MHz max)
 * USB high speed needs the crystal: its PHY PLL takes HSE / 2 = 4 MHz (set up
 * by the USB driver). The camera XCLK (MCO, PA8) comes from PLL3: HSE / 2 x 6
 * = 24 MHz (camera.c). If the crystal does not start, run from HSI x 18 and
 * report it (no USB HS then). */
#include "chip.h"

uint32_t SystemCoreClock = 144000000u;
uint8_t SystemClockFromHse = 0u;

#define HSE_STARTUP_LOOPS 0x10000u

void SystemInit(void) {
  /* Reset the clock configuration to HSI, as after a power-on reset. */
  RCC->CTLR |= RCC_HSION;
  RCC->CFGR0 &= 0xF0FF0000u;
  RCC->CTLR &= 0xFEF6FFFFu; /* HSEON, CSSON, PLLON off */
  RCC->CTLR &= 0xFFFBFFFFu; /* HSEBYP off */
  RCC->CFGR0 &= 0xFF00FFFFu;
  RCC->CTLR &= 0xEBFFFFFFu; /* PLL2ON, PLL3ON off */
  RCC->INTR = 0x00FF0000u;
  RCC->CFGR2 = 0u;

  RCC->CTLR |= RCC_HSEON;
  uint32_t n = 0u;
  while ((RCC->CTLR & RCC_HSERDY) == 0u && n < HSE_STARTUP_LOOPS) ++n;
  SystemClockFromHse = (RCC->CTLR & RCC_HSERDY) != 0u;

  RCC->CFGR0 |= RCC_HPRE_DIV1 | RCC_PPRE2_DIV2 | RCC_PPRE1_DIV2 | RCC_ADCPRE_DIV6;
  RCC->CFGR0 &= ~(RCC_PLLSRC | RCC_PLLXTPRE | RCC_PLLMULL);
  if (SystemClockFromHse) {
    RCC->CFGR0 |= RCC_PLLSRC_HSE | RCC_PLLXTPRE_HSE | RCC_PLLMULL18_EXTEN;
  } else {
    RCC->CTLR &= ~RCC_HSEON;
    EXTEN->EXTEN_CTR |= EXTEN_PLL_HSI_PRE; /* PLL input = HSI, not HSI/2 */
    RCC->CFGR0 |= RCC_PLLSRC_HSI_Div2 | RCC_PLLMULL18_EXTEN;
  }
  RCC->CTLR |= RCC_PLLON;
  while ((RCC->CTLR & RCC_PLLRDY) == 0u) {
  }
  RCC->CFGR0 = (RCC->CFGR0 & ~RCC_SW) | RCC_SW_PLL;
  while ((RCC->CFGR0 & RCC_SWS) != 0x08u) {
  }
  SystemCoreClock = 144000000u;
}

void SystemCoreClockUpdate(void) { SystemCoreClock = 144000000u; }
