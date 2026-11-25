#pragma once
#include <stdint.h>

extern uint32_t SystemCoreClock;
extern uint8_t SystemClockFromHse; /* 0: the 8 MHz crystal failed, running from HSI */

void SystemInit(void);
void SystemCoreClockUpdate(void);
