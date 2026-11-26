/* Generated copy of mcu/common/nec.c (tools/sync_arduino_lib.py). Do not edit here. */
#include "nec.h"

#include <string.h>

/* Bits: +/-20 % windows. Leader (13.5 ms) and repeat (11.25 ms) are only
 * 18 % apart, so those use +/-10 %. */
static bool near(uint32_t v, uint32_t nominal) {
  return v >= nominal - nominal / 5u && v <= nominal + nominal / 5u;
}

static bool near10(uint32_t v, uint32_t nominal) {
  return v >= nominal - nominal / 10u && v <= nominal + nominal / 10u;
}

void nec_init(nec_t *d) { memset(d, 0, sizeof(*d)); }

bool nec_falling_edge(nec_t *d, uint32_t t_us) {
  if (!d->have_edge) {
    d->have_edge = true;
    d->last_us = t_us;
    return false;
  }
  const uint32_t dt = t_us - d->last_us; /* wraps correctly */
  d->last_us = t_us;

  if (near10(dt, 13500u)) { /* leader */
    d->in_frame = true;
    d->bits = 0u;
    d->nbits = 0u;
    return false;
  }
  if (near10(dt, 11250u)) { /* repeat code */
    d->in_frame = false;
    ++d->repeats;
    return false;
  }
  if (!d->in_frame) return false;

  if (near(dt, 1125u)) {
    /* bit 0 */
  } else if (near(dt, 2250u)) {
    d->bits |= 1ul << d->nbits;
  } else {
    d->in_frame = false;
    ++d->errors;
    return false;
  }
  if (++d->nbits == 32u) {
    d->in_frame = false;
    d->frame = d->bits;
    d->frame_us = t_us;
    ++d->frames;
    return true;
  }
  return false;
}

bool nec_frame_valid(uint32_t frame, uint16_t *address, uint8_t *command) {
  const uint8_t cmd = (uint8_t)(frame >> 16);
  const uint8_t ncmd = (uint8_t)(frame >> 24);
  if ((uint8_t)~cmd != ncmd) return false;
  if (address != NULL) {
    const uint8_t a = (uint8_t)frame;
    const uint8_t na = (uint8_t)(frame >> 8);
    *address = ((uint8_t)~a == na) ? a : (uint16_t)(frame & 0xFFFFu); /* extended NEC */
  }
  if (command != NULL) *command = cmd;
  return true;
}
