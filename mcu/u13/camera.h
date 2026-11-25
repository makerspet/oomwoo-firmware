/* Camera bring-up on J12: XCLK from MCO (PA8, PLL3 = 24 MHz), PWDN (PC10),
 * RESETB via U28 (link CONTROL flag), SCCB on the bit-banged CAM bus.
 * DVP capture and UVC streaming come later (see docs/firmware-plan.md). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

void camera_clock(bool on);
bool camera_clock_on(void);
void camera_power_down(bool pd); /* PWDN high = standby (GC2145/OV2640) */
/* Reads the sensor ID over SCCB: returns a short name or NULL. */
const char *camera_identify(uint16_t *id);
