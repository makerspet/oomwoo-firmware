/* U13 (CH32V307VCT6) board layer: safe pin states, rails, AFIO remaps.
 * Pin names come from the generated include/oomwoo_pins.h. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gpio.h"
#include "oomwoo_pins.h"

#define HCLK_HZ 144000000u
#define PCLK1_HZ 72000000u
#define PCLK2_HZ 72000000u
#define TIMCLK_HZ 144000000u /* both APB prescalers are /2, so timers run at 2 x PCLK */

typedef enum {
  RAIL_SENS_3V3 = 0, /* ~VSENS_3V3_EN (PB15), low = on: sensors, camera DOVDD, front/side boards */
  RAIL_SENS_5V = 1,  /* VSENS-5V-USB-EN (PB14), high = on: cliff/bumper LEDs, torch, phone charging */
  RAIL_MOTOR = 2,    /* ~VMOT-14V4-EN (PB12), low = on, AND WD_OK: motors, fan, LiDAR */
  RAIL_COUNT
} rail_t;

/* First thing in main(): every output to its off level, before any rail. */
void board_safe_init(void);
/* AFIO remaps for the timers and UARTs the board uses (after board_safe_init). */
void board_remap(void);

void rail_set(rail_t r, bool on);
bool rail_on(rail_t r);
const char *rail_name(rail_t r);

/* RC watchdog (WDI, PB5): toggle only from the healthy control loop. */
void wdi_toggle(void);

/* Independent watchdog: ~260 ms, frozen while the debugger halts the core. */
void iwdg_start(void);
void iwdg_feed(void);
bool board_reset_was_iwdg(void);

/* U28 control lines: MCU2-NRST (PC13, open drain) and MCU2-BOOT (PD4). */
void u28_reset_pulse(bool to_bootloader);
