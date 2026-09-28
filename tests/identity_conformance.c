#include "oomwoo_identity.h"

#include "oomwoo_cpu_ingress.h"
#include "generated/oomwoo_golden_vectors_v1.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
  HEARTBEAT_VECTOR_INDEX = 0,
  IDENTIFY_VECTOR_INDEX = 3,
  MCU_HELLO_VECTOR_INDEX = 10
};

typedef struct {
  oomwoo_identity_t *identity;
  uint8_t response[OOMWOO_IDENTITY_HELLO_FRAME_SIZE];
  size_t response_length;
  size_t calls;
  oomwoo_identity_result_t result;
} identity_callback_context_t;

static void handle_identity(const oomwoo_decoded_frame_t *frame,
                            const oomwoo_message_t *message, void *context) {
  identity_callback_context_t *callback_context =
      (identity_callback_context_t *)context;

  (void)frame;
  ++callback_context->calls;
  callback_context->result = oomwoo_identity_handle_message(
      callback_context->identity, message, callback_context->response,
      sizeof(callback_context->response),
      &callback_context->response_length);
}

static void test_canonical_mcu_hello(void) {
  const oomwoo_golden_vector_v1_t *expected =
      &OOMWOO_GOLDEN_VECTORS_V1[MCU_HELLO_VECTOR_INDEX];
  oomwoo_identity_t identity;
  uint8_t output[OOMWOO_IDENTITY_HELLO_FRAME_SIZE];
  size_t output_length = 0u;

  oomwoo_identity_init(&identity, UINT16_C(1), UINT16_C(2),
                       UINT32_C(0x12345678), UINT16_C(11));
  assert(oomwoo_identity_emit_hello(&identity, output, sizeof(output),
                                    &output_length) == OOMWOO_IDENTITY_OK);
  assert(output_length == expected->frame_length);
  assert(memcmp(output, expected->frame, output_length) == 0);
  assert(identity.next_sequence == UINT16_C(12));
}

static void test_identify_request_through_ingress(void) {
  const oomwoo_golden_vector_v1_t *request =
      &OOMWOO_GOLDEN_VECTORS_V1[IDENTIFY_VECTOR_INDEX];
  const oomwoo_golden_vector_v1_t *expected =
      &OOMWOO_GOLDEN_VECTORS_V1[MCU_HELLO_VECTOR_INDEX];
  oomwoo_cpu_ingress_t ingress;
  oomwoo_identity_t identity;
  identity_callback_context_t context;
  oomwoo_decoded_frame_t decoded;

  oomwoo_identity_init(&identity, UINT16_C(1), UINT16_C(2),
                       UINT32_C(0x12345678), UINT16_C(11));
  memset(&context, 0, sizeof(context));
  context.identity = &identity;
  context.result = OOMWOO_IDENTITY_IGNORED;
  oomwoo_cpu_ingress_init(&ingress, handle_identity, &context);

  assert(oomwoo_cpu_ingress_feed(&ingress, request->frame,
                                 request->frame_length) == 1u);
  assert(context.calls == 1u);
  assert(context.result == OOMWOO_IDENTITY_OK);
  assert(context.response_length == expected->frame_length);
  assert(memcmp(context.response, expected->frame,
                context.response_length) == 0);
  assert(identity.next_sequence == UINT16_C(12));

  context.response_length = 0u;
  assert(oomwoo_cpu_ingress_feed(&ingress, request->frame,
                                 request->frame_length) == 1u);
  assert(context.calls == 2u);
  assert(context.result == OOMWOO_IDENTITY_OK);
  assert(context.response_length == expected->frame_length);
  assert(oomwoo_decode_frame(context.response, context.response_length,
                             &decoded) ==
         OOMWOO_PROTOCOL_OK);
  assert(decoded.sequence == UINT16_C(12));
  assert(identity.next_sequence == UINT16_C(13));
}

static void test_non_identify_message_is_ignored(void) {
  const oomwoo_golden_vector_v1_t *heartbeat =
      &OOMWOO_GOLDEN_VECTORS_V1[HEARTBEAT_VECTOR_INDEX];
  oomwoo_cpu_ingress_t ingress;
  oomwoo_identity_t identity;
  identity_callback_context_t context;

  oomwoo_identity_init(&identity, UINT16_C(1), UINT16_C(0), UINT32_C(0),
                       UINT16_C(7));
  memset(&context, 0, sizeof(context));
  context.identity = &identity;
  oomwoo_cpu_ingress_init(&ingress, handle_identity, &context);

  assert(oomwoo_cpu_ingress_feed(&ingress, heartbeat->frame,
                                 heartbeat->frame_length) == 1u);
  assert(context.calls == 1u);
  assert(context.result == OOMWOO_IDENTITY_IGNORED);
  assert(context.response_length == 0u);
  assert(identity.next_sequence == UINT16_C(7));
}

static void test_corrupt_identify_request_is_silent(void) {
  const oomwoo_golden_vector_v1_t *request =
      &OOMWOO_GOLDEN_VECTORS_V1[IDENTIFY_VECTOR_INDEX];
  oomwoo_cpu_ingress_t ingress;
  oomwoo_identity_t identity;
  identity_callback_context_t context;
  uint8_t corrupt[OOMWOO_GOLDEN_MAX_FRAME_SIZE];

  oomwoo_identity_init(&identity, UINT16_C(1), UINT16_C(0), UINT32_C(0),
                       UINT16_C(9));
  memset(&context, 0, sizeof(context));
  context.identity = &identity;
  oomwoo_cpu_ingress_init(&ingress, handle_identity, &context);
  memcpy(corrupt, request->frame, request->frame_length);
  corrupt[request->frame_length - 1u] ^= UINT8_C(1);

  assert(oomwoo_cpu_ingress_feed(&ingress, corrupt,
                                 request->frame_length) == 0u);
  assert(context.calls == 0u);
  assert(context.response_length == 0u);
  assert(identity.next_sequence == UINT16_C(9));
  assert(ingress.decoder.stats.crc_errors == 1u);
}

static void test_failed_encode_does_not_consume_sequence(void) {
  oomwoo_identity_t identity;
  uint8_t output[OOMWOO_IDENTITY_HELLO_FRAME_SIZE - 1u];
  size_t output_length = 123u;

  oomwoo_identity_init(&identity, UINT16_C(1), UINT16_C(0), UINT32_C(0),
                       UINT16_C(42));
  assert(oomwoo_identity_emit_hello(&identity, output, sizeof(output),
                                    &output_length) ==
         OOMWOO_IDENTITY_OUTPUT_TOO_SMALL);
  assert(output_length == 0u);
  assert(identity.next_sequence == UINT16_C(42));
}

static void test_sequence_wraps_after_success(void) {
  oomwoo_identity_t identity;
  oomwoo_decoded_frame_t decoded;
  uint8_t output[OOMWOO_IDENTITY_HELLO_FRAME_SIZE];
  size_t output_length = 0u;

  oomwoo_identity_init(&identity, UINT16_C(1), UINT16_C(0), UINT32_C(0),
                       UINT16_MAX);
  assert(oomwoo_identity_emit_hello(&identity, output, sizeof(output),
                                    &output_length) == OOMWOO_IDENTITY_OK);
  assert(oomwoo_decode_frame(output, output_length, &decoded) ==
         OOMWOO_PROTOCOL_OK);
  assert(decoded.sequence == UINT16_MAX);
  assert(identity.next_sequence == UINT16_C(0));
}

static void test_null_arguments(void) {
  oomwoo_identity_t identity;
  oomwoo_message_t message;
  uint8_t output[OOMWOO_IDENTITY_HELLO_FRAME_SIZE];
  size_t output_length = 123u;

  memset(&message, 0, sizeof(message));
  message.type = OOMWOO_MESSAGE_IDENTIFY_REQUEST;
  oomwoo_identity_init(&identity, UINT16_C(1), UINT16_C(0), UINT32_C(0),
                       UINT16_C(0));

  assert(oomwoo_identity_emit_hello(NULL, output, sizeof(output),
                                    &output_length) ==
         OOMWOO_IDENTITY_NULL_ARGUMENT);
  assert(output_length == 0u);
  assert(oomwoo_identity_emit_hello(&identity, NULL, sizeof(output),
                                    &output_length) ==
         OOMWOO_IDENTITY_NULL_ARGUMENT);
  assert(oomwoo_identity_emit_hello(&identity, output, sizeof(output), NULL) ==
         OOMWOO_IDENTITY_NULL_ARGUMENT);
  assert(oomwoo_identity_handle_message(NULL, &message, output, sizeof(output),
                                        &output_length) ==
         OOMWOO_IDENTITY_NULL_ARGUMENT);
  assert(oomwoo_identity_handle_message(&identity, NULL, output,
                                        sizeof(output), &output_length) ==
         OOMWOO_IDENTITY_NULL_ARGUMENT);
  assert(oomwoo_identity_handle_message(&identity, &message, output,
                                        sizeof(output), NULL) ==
         OOMWOO_IDENTITY_NULL_ARGUMENT);
  oomwoo_identity_init(NULL, UINT16_C(0), UINT16_C(0), UINT32_C(0),
                       UINT16_C(0));
}

int main(void) {
  test_canonical_mcu_hello();
  test_identify_request_through_ingress();
  test_non_identify_message_is_ignored();
  test_corrupt_identify_request_is_silent();
  test_failed_encode_does_not_consume_sequence();
  test_sequence_wraps_after_success();
  test_null_arguments();
  puts("OOMWOO identity handshake conformance: PASS");
  return 0;
}
