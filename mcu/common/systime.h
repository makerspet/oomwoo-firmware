/* System time on the QingKe V4 SysTick: a free-running 64-bit counter at
 * HCLK/8 (U13: 18 MHz, U28: 12 MHz) for microsecond timestamps, and a
 * 1 kHz compare interrupt for the millisecond tick and the control loop. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

void systime_init(uint32_t hclk_hz);
uint64_t systime_ticks(void);
uint32_t systime_us(void);   /* wraps every ~71 minutes; use differences */
uint32_t systime_ms(void);
void systime_delay_us(uint32_t us);
/* True once per elapsed millisecond tick since the last call (control loop pacing). */
bool systime_tick_pending(void);
