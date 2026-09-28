/* Cross-repo command-gate conformance.
 *
 * Runs the vendored accept/reject corpus (authored in makerspet/oomwoo) against
 * this repo's oomwoo_cpu_ingress_validate_frame. Each vector's expected result
 * is the reason authored upstream from the firmware rules; if the C gate ever
 * disagrees, this test fails -- keeping the host oracle and the firmware gate
 * in lock-step.
 *
 * Scope: the decoded-frame command gate only (no wire framing or CRC).
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "generated/oomwoo_command_gate_vectors_v1.h"
#include "oomwoo_cpu_ingress.h"
#include "oomwoo_messages.h"
#include "oomwoo_protocol.h"

int main(void) {
  size_t i;

  for (i = 0u; i < OOMWOO_COMMAND_GATE_VECTOR_COUNT; ++i) {
    const oomwoo_command_gate_vector_t *v = &OOMWOO_COMMAND_GATE_VECTORS[i];
    oomwoo_decoded_frame_t frame;
    oomwoo_message_t output;
    oomwoo_cpu_ingress_result_t got;

    memset(&frame, 0, sizeof frame);
    memset(&output, 0, sizeof output);
    frame.version = v->version;
    frame.message_type = v->message_type;
    frame.payload_length = v->payload_length;
    frame.payload = v->payload;

    got = oomwoo_cpu_ingress_validate_frame(&frame, &output);
    if (got != v->expected) {
      fprintf(stderr,
              "command gate MISMATCH: vector \"%s\" (type 0x%04x) -> got %d, "
              "expected %d\n",
              v->name, (unsigned)v->message_type, (int)got, (int)v->expected);
      return 1;
    }
  }

  if (OOMWOO_COMMAND_GATE_VECTOR_COUNT != 57u) {
    fprintf(stderr, "expected 57 vectors, compiled %u\n",
            (unsigned)OOMWOO_COMMAND_GATE_VECTOR_COUNT);
    return 1;
  }

  printf("OOMWOO command gate conformance: %u vectors PASS\n",
         (unsigned)OOMWOO_COMMAND_GATE_VECTOR_COUNT);
  return 0;
}
