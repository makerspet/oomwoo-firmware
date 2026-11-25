#include "nec_tx.h"

void nec_tx_frame(uint32_t frame, uint16_t seg[NEC_TX_SEGMENTS]) {
  unsigned n = 0;
  seg[n++] = 9000u; /* leader mark */
  seg[n++] = 4500u; /* leader space */
  for (unsigned i = 0; i < 32u; ++i) {
    seg[n++] = 562u;
    seg[n++] = ((frame >> i) & 1u) ? 1688u : 563u;
  }
  seg[n] = 562u; /* stop mark: ends the last bit's space */
}
