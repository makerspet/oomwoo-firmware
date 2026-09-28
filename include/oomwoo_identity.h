#ifndef OOMWOO_IDENTITY_H
#define OOMWOO_IDENTITY_H

#include "oomwoo_messages.h"
#include "oomwoo_protocol.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OOMWOO_IDENTITY_HELLO_PAYLOAD_SIZE ((size_t)8)
#define OOMWOO_IDENTITY_HELLO_FRAME_SIZE                                  \
  (OOMWOO_PROTOCOL_HEADER_SIZE + OOMWOO_IDENTITY_HELLO_PAYLOAD_SIZE +      \
   OOMWOO_PROTOCOL_CRC_SIZE)

typedef enum {
  OOMWOO_IDENTITY_OK = 0,
  OOMWOO_IDENTITY_IGNORED,
  OOMWOO_IDENTITY_NULL_ARGUMENT,
  OOMWOO_IDENTITY_OUTPUT_TOO_SMALL,
  OOMWOO_IDENTITY_CODEC_ERROR
} oomwoo_identity_result_t;

typedef struct {
  uint16_t firmware_major;
  uint16_t firmware_minor;
  uint32_t build_id;
  uint16_t next_sequence;
} oomwoo_identity_t;

void oomwoo_identity_init(oomwoo_identity_t *identity,
                          uint16_t firmware_major,
                          uint16_t firmware_minor, uint32_t build_id,
                          uint16_t initial_sequence);

oomwoo_identity_result_t oomwoo_identity_emit_hello(
    oomwoo_identity_t *identity, uint8_t *output, size_t output_capacity,
    size_t *output_length);

oomwoo_identity_result_t oomwoo_identity_handle_message(
    oomwoo_identity_t *identity, const oomwoo_message_t *message,
    uint8_t *output, size_t output_capacity, size_t *output_length);

#ifdef __cplusplus
}
#endif

#endif
