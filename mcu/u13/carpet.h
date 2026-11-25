/* Ultrasonic carpet sensor: 290 kHz burst on CARPET-SENSOR-LO (PC6, TIM8
 * CH1, one-pulse mode with the repetition counter for an exact cycle count),
 * echo amplified by OPA3 (+ PB13, - PB2, out PE7; gain set by R239/R240) and
 * peak-detected into ECHO-ENV (PA1), sampled by ADC2 alone. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t idle_mv;   /* envelope before the burst */
  uint16_t peak_mv;   /* largest envelope after the blanking time */
  uint16_t peak_us;   /* time of the peak after the burst start */
  uint16_t samples;
} carpet_echo_t;

void carpet_init(void);
/* Blocking, ~1.5 ms. Needs VSENS-5V-USB on: it enables the MT3608 boost (U5)
 * that makes VUSONIC-14V4. */
bool carpet_ping(uint8_t cycles, carpet_echo_t *out);
