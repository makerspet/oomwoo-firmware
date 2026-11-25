/* U28 clock tree: no crystal (PD0/PD1 are spares on J1).
 *   HSI 8 MHz -> PLL x12 -> SYSCLK 96 MHz, HCLK 96, PCLK2 96, PCLK1 48 MHz
 *   USART2 (link, 1 Mbit/s) from PCLK1: divider 48 exactly. HSI is within
 *   about +/-1 % at room temperature; the link tolerates a few %, and U13
 *   (crystal) could trim HSITRIM from the measured frame timing if needed.
 *   ADC: PCLK2 / 8 = 12 MHz. */
#include "chip.h"

uint32_t SystemCoreClock = 96000000u;

void SystemInit(void) {
  RCC->CTLR |= RCC_HSION;
  RCC->CFGR0 &= 0x08FF0000u;
  RCC->CTLR &= 0xFEF6FFFBu;
  RCC->CTLR &= 0xFFFBFFFFu;
  RCC->CFGR0 &= 0xFF00FFFFu;
  RCC->INTR = 0x009F0000u;

  FLASH->ACTLR = FLASH_ACTLR_LATENCY_2;
  EXTEN->EXTEN_CTR |= EXTEN_PLL_HSI_PRE; /* PLL input = HSI, not HSI/2 */
  RCC->CFGR0 |= RCC_HPRE_DIV1 | RCC_PPRE2_DIV1 | RCC_PPRE1_DIV2 | RCC_ADCPRE_DIV8;
  RCC->CFGR0 &= ~(RCC_PLLSRC | RCC_PLLXTPRE | RCC_PLLMULL);
  RCC->CFGR0 |= RCC_PLLSRC_HSI_Div2 | RCC_PLLMULL12;
  RCC->CTLR |= RCC_PLLON;
  while ((RCC->CTLR & RCC_PLLRDY) == 0u) {
  }
  RCC->CFGR0 = (RCC->CFGR0 & ~RCC_SW) | RCC_SW_PLL;
  while ((RCC->CFGR0 & RCC_SWS) != 0x08u) {
  }
  SystemCoreClock = 96000000u;
}

void SystemCoreClockUpdate(void) { SystemCoreClock = 96000000u; }
