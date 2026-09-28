#include "oomwoo_identity.h"

#include <string.h>

void oomwoo_identity_init(oomwoo_identity_t *identity,
                          uint16_t firmware_major,
                          uint16_t firmware_minor, uint32_t build_id,
                          uint16_t initial_sequence) {
  if (identity == NULL) {
    return;
  }

  identity->firmware_major = firmware_major;
  identity->firmware_minor = firmware_minor;
  identity->build_id = build_id;
  identity->next_sequence = initial_sequence;
}

oomwoo_identity_result_t oomwoo_identity_emit_hello(
    oomwoo_identity_t *identity, uint8_t *output, size_t output_capacity,
    size_t *output_length) {
  oomwoo_message_t message;
  uint8_t payload[OOMWOO_IDENTITY_HELLO_PAYLOAD_SIZE];
  uint16_t payload_length = UINT16_C(0);
  oomwoo_message_result_t message_result;
  oomwoo_protocol_result_t protocol_result;

  if (output_length == NULL) {
    return OOMWOO_IDENTITY_NULL_ARGUMENT;
  }
  *output_length = 0u;

  if (identity == NULL || output == NULL) {
    return OOMWOO_IDENTITY_NULL_ARGUMENT;
  }
  if (output_capacity < OOMWOO_IDENTITY_HELLO_FRAME_SIZE) {
    return OOMWOO_IDENTITY_OUTPUT_TOO_SMALL;
  }

  memset(&message, 0, sizeof(message));
  message.type = OOMWOO_MESSAGE_MCU_HELLO;
  message.payload.mcu_hello.firmware_major = identity->firmware_major;
  message.payload.mcu_hello.firmware_minor = identity->firmware_minor;
  message.payload.mcu_hello.build_id = identity->build_id;

  message_result = oomwoo_message_encode_payload(
      &message, payload, sizeof(payload), &payload_length);
  if (message_result != OOMWOO_MESSAGE_OK) {
    return OOMWOO_IDENTITY_CODEC_ERROR;
  }

  protocol_result = oomwoo_encode_frame(
      message.type, payload, payload_length, identity->next_sequence,
      UINT8_C(0), output, output_capacity, output_length);
  if (protocol_result == OOMWOO_PROTOCOL_OUTPUT_TOO_SMALL) {
    *output_length = 0u;
    return OOMWOO_IDENTITY_OUTPUT_TOO_SMALL;
  }
  if (protocol_result != OOMWOO_PROTOCOL_OK) {
    *output_length = 0u;
    return OOMWOO_IDENTITY_CODEC_ERROR;
  }

  identity->next_sequence = (uint16_t)(identity->next_sequence + UINT16_C(1));
  return OOMWOO_IDENTITY_OK;
}

oomwoo_identity_result_t oomwoo_identity_handle_message(
    oomwoo_identity_t *identity, const oomwoo_message_t *message,
    uint8_t *output, size_t output_capacity, size_t *output_length) {
  if (output_length == NULL) {
    return OOMWOO_IDENTITY_NULL_ARGUMENT;
  }
  *output_length = 0u;

  if (identity == NULL || message == NULL) {
    return OOMWOO_IDENTITY_NULL_ARGUMENT;
  }
  if (message->type != OOMWOO_MESSAGE_IDENTIFY_REQUEST) {
    return OOMWOO_IDENTITY_IGNORED;
  }

  return oomwoo_identity_emit_hello(identity, output, output_capacity,
                                    output_length);
}
