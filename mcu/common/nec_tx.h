/* NEC frame timing for an IR sender: turns a 32-bit frame into alternating
 * mark (carrier on) / space (carrier off) durations in microseconds.
 * Pure logic, host-tested against the decoder in nec.h. */
#pragma once

#include <stdint.h>

#define NEC_TX_SEGMENTS 67u /* leader mark + space, 32 x (mark + space), stop mark */

/* Fills seg[0..66]; even indices are marks, odd indices spaces. */
void nec_tx_frame(uint32_t frame, uint16_t seg[NEC_TX_SEGMENTS]);
