#include "systime.h"

#include "chip.h"

/* SysTick CTLR bits (QingKe V4 and V2A) */
#define STK_STE 0x01u   /* counter enable */
#define STK_STIE 0x02u  /* interrupt enable */
#define STK_STCLK 0x04u /* 1: HCLK, 0: HCLK/8 */

static uint32_t s_ticks_per_us;
static uint32_t s_ticks_per_ms;
static volatile uint32_t s_ms;
static volatile uint32_t s_pending;

void systime_init(uint32_t hclk_hz) {
  s_ticks_per_us = hclk_hz / 8000000u;
  s_ticks_per_ms = hclk_hz / 8000u;
  SysTick->CTLR = 0u;
  SysTick->SR = 0u;
  SysTick->CNT = 0u;
  SysTick->CMP = s_ticks_per_ms;
  NVIC_SetPriority(SysTick_IRQn, 0xC0); /* low: control loop work runs in main */
  NVIC_EnableIRQ(SysTick_IRQn);
  SysTick->CTLR = STK_STE | STK_STIE; /* HCLK/8, free running, no auto-reload */
}

#ifdef CH32V003
/* QingKe V2A (CH32V003): CNT and CMP are 32-bit and wrap every ~12 minutes
 * at 6 MHz. Extend them in software: s_base is the tick count of the last
 * compare match, which the interrupt advances every millisecond. */
static volatile uint64_t s_base;

void SysTick_Handler(void) ISR;
void SysTick_Handler(void) {
  SysTick->SR = 0u;
  s_base += s_ticks_per_ms;
  SysTick->CMP = (uint32_t)(s_base + s_ticks_per_ms);
  ++s_ms;
  ++s_pending;
}

uint64_t systime_ticks(void) {
  __disable_irq();
  const uint64_t base = s_base;
  const uint32_t cnt = SysTick->CNT;
  __enable_irq();
  return base + (uint32_t)(cnt - (uint32_t)base);
}
#else
void SysTick_Handler(void) ISR;
void SysTick_Handler(void) {
  SysTick->SR = 0u;
  SysTick->CMP += s_ticks_per_ms;
  ++s_ms;
  ++s_pending;
}

uint64_t systime_ticks(void) {
  /* CNT is 64-bit but read as two 32-bit halves: re-read on a carry. */
  volatile uint32_t *p = (volatile uint32_t *)&SysTick->CNT;
  uint32_t hi, lo;
  do {
    hi = p[1];
    lo = p[0];
  } while (hi != p[1]);
  return ((uint64_t)hi << 32) | lo;
}
#endif

uint32_t systime_us(void) { return (uint32_t)(systime_ticks() / s_ticks_per_us); }

uint32_t systime_ms(void) { return s_ms; }

void systime_delay_us(uint32_t us) {
  const uint64_t end = systime_ticks() + (uint64_t)us * s_ticks_per_us;
  while (systime_ticks() < end) {
  }
}

bool systime_tick_pending(void) {
  if (s_pending == 0u) return false;
  __disable_irq();
  --s_pending;
  __enable_irq();
  return true;
}
