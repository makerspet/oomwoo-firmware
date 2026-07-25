#ifndef OOMWOO_PROTOCOL_H
#define OOMWOO_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OOMWOO_PROTOCOL_MAGIC_0 ((uint8_t)'O')
#define OOMWOO_PROTOCOL_MAGIC_1 ((uint8_t)'W')

#ifndef OOMWOO_PROTOCOL_VERSION
#define OOMWOO_PROTOCOL_VERSION ((uint8_t)2)
#endif

#define OOMWOO_PROTOCOL_HEADER_SIZE ((size_t)10)
#define OOMWOO_PROTOCOL_CRC_SIZE ((size_t)2)
#define OOMWOO_PROTOCOL_MAX_PAYLOAD_SIZE ((size_t)512)
#define OOMWOO_PROTOCOL_MAX_FRAME_SIZE                                      \
  (OOMWOO_PROTOCOL_HEADER_SIZE + OOMWOO_PROTOCOL_MAX_PAYLOAD_SIZE +         \
   OOMWOO_PROTOCOL_CRC_SIZE)

typedef enum {
  OOMWOO_PROTOCOL_OK = 0,
  OOMWOO_PROTOCOL_NULL_ARGUMENT,
  OOMWOO_PROTOCOL_OUTPUT_TOO_SMALL,
  OOMWOO_PROTOCOL_FRAME_TOO_SHORT,
  OOMWOO_PROTOCOL_BAD_MAGIC,
  OOMWOO_PROTOCOL_BAD_VERSION,
  OOMWOO_PROTOCOL_PAYLOAD_TOO_LARGE,
  OOMWOO_PROTOCOL_LENGTH_MISMATCH,
  OOMWOO_PROTOCOL_BAD_CRC
} oomwoo_protocol_result_t;

typedef struct {
  uint8_t version;
  uint8_t flags;
  uint16_t sequence;
  uint16_t message_type;
  uint16_t payload_length;
  const uint8_t *payload;
} oomwoo_decoded_frame_t;

typedef struct {
  uint32_t frames;
  uint32_t discarded_bytes;
  uint32_t crc_errors;
  uint32_t version_errors;
  uint32_t length_errors;
  uint32_t gap_resets;
} oomwoo_decoder_stats_t;

typedef struct {
  uint8_t buffer[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  size_t buffered_bytes;
  oomwoo_decoder_stats_t stats;
} oomwoo_stream_decoder_t;

typedef void (*oomwoo_frame_callback_t)(
    const oomwoo_decoded_frame_t *frame, void *context);

uint16_t oomwoo_crc16_ccitt_false(const uint8_t *data, size_t length);

oomwoo_protocol_result_t oomwoo_encode_frame(
    uint16_t message_type, const uint8_t *payload, uint16_t payload_length,
    uint16_t sequence, uint8_t flags, uint8_t *output, size_t output_capacity,
    size_t *output_length);

oomwoo_protocol_result_t
oomwoo_decode_frame(const uint8_t *data, size_t length,
                    oomwoo_decoded_frame_t *output);

void oomwoo_stream_decoder_init(oomwoo_stream_decoder_t *decoder);

size_t oomwoo_stream_decoder_feed(oomwoo_stream_decoder_t *decoder,
                                  const uint8_t *data, size_t length,
                                  oomwoo_frame_callback_t callback,
                                  void *context);

void oomwoo_stream_decoder_reset_incomplete(oomwoo_stream_decoder_t *decoder);

#ifdef __cplusplus
}
#endif

#endif
