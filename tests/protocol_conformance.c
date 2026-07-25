#include "oomwoo_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  size_t count;
  uint16_t sequence;
  uint16_t message_type;
} capture_t;

static void capture_frame(const oomwoo_decoded_frame_t *frame, void *context) {
  capture_t *capture = (capture_t *)context;
  ++capture->count;
  capture->sequence = frame->sequence;
  capture->message_type = frame->message_type;
}

static size_t encode(uint16_t type, uint16_t sequence, uint8_t *output) {
  size_t length = 0u;
  assert(oomwoo_encode_frame(type, NULL, 0u, sequence, 0u, output,
                             OOMWOO_PROTOCOL_MAX_FRAME_SIZE, &length) ==
         OOMWOO_PROTOCOL_OK);
  return length;
}

static void test_golden_heartbeat(void) {
  static const uint8_t payload[] = {0x78u, 0x56u, 0x34u, 0x12u, 0x01u};
  static const uint8_t expected[] = {
      0x4fu, 0x57u, 0x02u, 0x00u, 0x2au, 0x00u, 0x01u, 0x00u, 0x05u,
      0x00u, 0x78u, 0x56u, 0x34u, 0x12u, 0x01u, 0x0fu, 0x37u};
  uint8_t output[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  size_t output_length = 0u;
  oomwoo_decoded_frame_t decoded;

  assert(oomwoo_crc16_ccitt_false((const uint8_t *)"123456789", 9u) ==
         0x29b1u);
  assert(oomwoo_encode_frame(0x0001u, payload, sizeof(payload), 0x002au, 0u,
                             output, sizeof(output), &output_length) ==
         OOMWOO_PROTOCOL_OK);
  assert(output_length == sizeof(expected));
  assert(memcmp(output, expected, sizeof(expected)) == 0);
  assert(oomwoo_decode_frame(output, output_length, &decoded) ==
         OOMWOO_PROTOCOL_OK);
  assert(decoded.sequence == 0x002au);
  assert(decoded.payload_length == sizeof(payload));
}

static void test_stream_fault_recovery(void) {
  uint8_t bad[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  uint8_t good[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  uint8_t input[OOMWOO_PROTOCOL_MAX_FRAME_SIZE * 2u + 3u];
  size_t bad_length = encode(0x7001u, 1u, bad);
  size_t good_length = encode(0x7002u, 2u, good);
  size_t input_length;
  oomwoo_stream_decoder_t decoder;
  capture_t capture = {0u, 0u, 0u};

  bad[bad_length - 1u] ^= 0x01u;
  memcpy(input, bad, bad_length);
  input[bad_length] = 0x00u;
  input[bad_length + 1u] = 0xffu;
  input[bad_length + 2u] = 0x4fu;
  memcpy(input + bad_length + 3u, good + 1u, good_length - 1u);
  input_length = bad_length + good_length + 2u;

  oomwoo_stream_decoder_init(&decoder);
  assert(oomwoo_stream_decoder_feed(&decoder, input, input_length,
                                    capture_frame, &capture) == 1u);
  assert(capture.count == 1u);
  assert(capture.sequence == 2u);
  assert(capture.message_type == 0x7002u);
  assert(decoder.stats.crc_errors == 1u);
  assert(decoder.stats.discarded_bytes >= bad_length);
}

static void test_corrupt_length_gap_reset(void) {
  uint8_t frame[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  size_t frame_length = encode(0x0001u, 3u, frame);
  oomwoo_stream_decoder_t decoder;

  frame[8] = 0xf0u;
  frame[9] = 0x01u;
  oomwoo_stream_decoder_init(&decoder);
  assert(oomwoo_stream_decoder_feed(&decoder, frame, frame_length, NULL,
                                    NULL) == 0u);
  assert(decoder.buffered_bytes != 0u);
  oomwoo_stream_decoder_reset_incomplete(&decoder);
  assert(decoder.buffered_bytes == 0u);
  assert(decoder.stats.gap_resets == 1u);
}

int main(void) {
  test_golden_heartbeat();
  test_stream_fault_recovery();
  test_corrupt_length_gap_reset();
  puts("OOMWOO firmware protocol conformance: PASS");
  return 0;
}
