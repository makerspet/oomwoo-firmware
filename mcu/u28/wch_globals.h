/* Factory calibration values the WCH CH32L103 peripheral library reads from
 * globals (defined in ch32l103_gpio.c, declared in the SDK's debug.h, which
 * this build does not use). board_safe_init() loads them. Force-included
 * into the vendor sources by the Makefile. */
#pragma once
#include <stdint.h>

extern uint16_t ADC_Trim;
extern uint32_t TS_Val;
extern uint32_t CHIPID;
extern uint16_t USBPD_CFG;
extern uint32_t OPA_Trim;
