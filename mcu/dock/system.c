/* Dock clock: HSI 24 MHz x 2 (PLL) = 48 MHz, one flash wait state.
 * HCLK = PCLK = 48 MHz; ADC clock PCLK2 / 8 = 6 MHz. No crystal. */
#include "chip.h"

uint32_t SystemCoreClock = 48000000u;

void SystemInit(void) {
  RCC->CTLR |= RCC_HSION;
  RCC->CFGR0 &= 0xF8FF0000u;
  RCC->CTLR &= 0xFEF6FFFFu;
  RCC->CTLR &= 0xFFFBFFFFu;
  RCC->CFGR0 &= 0xFFFEFFFFu;
  RCC->INTR = 0x009F0000u;

  FLASH->ACTLR = (FLASH->ACTLR & ~FLASH_ACTLR_LATENCY) | FLASH_ACTLR_LATENCY_1;
  RCC->CFGR0 |= RCC_HPRE_DIV1 | RCC_ADCPRE_DIV8;
  RCC->CFGR0 &= ~RCC_PLLSRC; /* PLL input = HSI; the PLL multiplies by 2 */
  RCC->CTLR |= RCC_PLLON;
  while ((RCC->CTLR & RCC_PLLRDY) == 0u) {
  }
  RCC->CFGR0 = (RCC->CFGR0 & ~RCC_SW) | RCC_SW_PLL;
  while ((RCC->CFGR0 & RCC_SWS) != 0x08u) {
  }
  SystemCoreClock = 48000000u;
}

void SystemCoreClockUpdate(void) { SystemCoreClock = 48000000u; }
