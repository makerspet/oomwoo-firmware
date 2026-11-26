/* Generated copy of mcu/common/nec.h (tools/sync_arduino_lib.py). Do not edit here. */
/* NEC IR frame decoder for the dock beacon (38 kHz receivers, active-low
 * output). Fed with the timestamps of falling edges only (one EXTI edge per
 * receiver), so it decodes from the falling-to-falling intervals:
 * leader 13.5 ms, repeat 11.25 ms, bit 0 1.125 ms, bit 1 2.25 ms.
 * Pure logic, host-tested. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t last_us;
  uint32_t bits;
  uint8_t nbits;
  bool in_frame;
  bool have_edge;
  /* last result */
  uint32_t frame;      /* raw 32 bits, LSB first as sent */
  uint32_t frame_us;   /* timestamp of the frame's last edge */
  uint32_t frames;     /* complete frames decoded */
  uint32_t repeats;
  uint32_t errors;
} nec_t;

void nec_init(nec_t *d);
/* Returns true when a complete 32-bit frame was just decoded. */
bool nec_falling_edge(nec_t *d, uint32_t t_us);
/* Standard NEC check (command byte followed by its inverse). */
bool nec_frame_valid(uint32_t frame, uint16_t *address, uint8_t *command);

#ifdef __cplusplus
}
#endif
