#include <cstring>

#include <unity.h>

#include "oomwoo_protocol.h"

namespace {

struct Capture {
  size_t count;
  uint16_t sequence;
  uint16_t message_type;
  uint16_t payload_length;
  uint8_t payload[16];
};

void capture_frame(const oomwoo_decoded_frame_t *frame, void *context) {
  auto *capture = static_cast<Capture *>(context);
  ++capture->count;
  capture->sequence = frame->sequence;
  capture->message_type = frame->message_type;
  capture->payload_length = frame->payload_length;
  const size_t copy_length =
      frame->payload_length < sizeof(capture->payload)
          ? frame->payload_length
          : sizeof(capture->payload);
  std::memcpy(capture->payload, frame->payload, copy_length);
}

size_t encode(uint16_t type, const uint8_t *payload, uint16_t payload_length,
              uint16_t sequence, uint8_t *output) {
  size_t output_length = 0u;
  TEST_ASSERT_EQUAL(
      OOMWOO_PROTOCOL_OK,
      oomwoo_encode_frame(type, payload, payload_length, sequence, 0u, output,
                          OOMWOO_PROTOCOL_MAX_FRAME_SIZE, &output_length));
  return output_length;
}

void test_crc_and_cross_language_golden_vector() {
  static const uint8_t input[] = "123456789";
  static const uint8_t payload[] = {0x78u, 0x56u, 0x34u, 0x12u, 0x01u};
  static const uint8_t expected[] = {
      0x4fu, 0x57u, 0x02u, 0x00u, 0x2au, 0x00u, 0x01u, 0x00u, 0x05u,
      0x00u, 0x78u, 0x56u, 0x34u, 0x12u, 0x01u, 0x0fu, 0x37u};
  uint8_t output[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  const size_t output_length =
      encode(0x0001u, payload, sizeof(payload), 0x002au, output);

  TEST_ASSERT_EQUAL_HEX16(0x29b1u,
                          oomwoo_crc16_ccitt_false(input, 9u));
  TEST_ASSERT_EQUAL_UINT32(sizeof(expected), output_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, output, sizeof(expected));
}

void test_stream_recovers_from_noise_and_split_input() {
  static const uint8_t payload[] = {0x01u, 0x02u, 0x03u};
  static const uint8_t noise[] = {0x00u, 0xffu, 0x4fu};
  uint8_t frame[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  oomwoo_stream_decoder_t decoder;
  Capture capture{};
  const size_t frame_length =
      encode(0x8000u, payload, sizeof(payload), 7u, frame);

  oomwoo_stream_decoder_init(&decoder);
  TEST_ASSERT_EQUAL_UINT32(
      0u, oomwoo_stream_decoder_feed(&decoder, noise, sizeof(noise),
                                     capture_frame, &capture));
  TEST_ASSERT_EQUAL_UINT32(
      0u, oomwoo_stream_decoder_feed(&decoder, frame + 1u, 4u, capture_frame,
                                     &capture));
  TEST_ASSERT_EQUAL_UINT32(
      1u, oomwoo_stream_decoder_feed(&decoder, frame + 5u, frame_length - 5u,
                                     capture_frame, &capture));

  TEST_ASSERT_EQUAL_UINT32(1u, capture.count);
  TEST_ASSERT_EQUAL_HEX16(0x8000u, capture.message_type);
  TEST_ASSERT_EQUAL_UINT16(7u, capture.sequence);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, capture.payload, sizeof(payload));
  TEST_ASSERT_EQUAL_UINT32(2u, decoder.stats.discarded_bytes);
}

void test_bad_crc_is_dropped_and_next_frame_decodes() {
  uint8_t bad[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  uint8_t good[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  oomwoo_stream_decoder_t decoder;
  Capture capture{};
  size_t bad_length = encode(0x7001u, nullptr, 0u, 1u, bad);
  const size_t good_length = encode(0x7002u, nullptr, 0u, 2u, good);

  bad[bad_length - 1u] ^= 0x01u;
  oomwoo_stream_decoder_init(&decoder);
  TEST_ASSERT_EQUAL_UINT32(
      0u, oomwoo_stream_decoder_feed(&decoder, bad, bad_length, capture_frame,
                                     &capture));
  TEST_ASSERT_EQUAL_UINT32(
      1u, oomwoo_stream_decoder_feed(&decoder, good, good_length,
                                     capture_frame, &capture));

  TEST_ASSERT_EQUAL_UINT32(1u, decoder.stats.crc_errors);
  TEST_ASSERT_EQUAL_UINT32(1u, capture.count);
  TEST_ASSERT_EQUAL_UINT16(2u, capture.sequence);
}

void test_wrong_version_is_counted_and_rejected() {
  uint8_t frame[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  oomwoo_stream_decoder_t decoder;
  Capture capture{};
  const size_t frame_length = encode(0x7001u, nullptr, 0u, 3u, frame);

  frame[2] = 1u;
  oomwoo_stream_decoder_init(&decoder);
  TEST_ASSERT_EQUAL_UINT32(
      0u, oomwoo_stream_decoder_feed(&decoder, frame, frame_length,
                                     capture_frame, &capture));
  TEST_ASSERT_EQUAL_UINT32(1u, decoder.stats.version_errors);
  TEST_ASSERT_EQUAL_UINT32(0u, capture.count);
}

void test_gap_reset_releases_corrupt_length() {
  uint8_t frame[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  oomwoo_stream_decoder_t decoder;
  Capture capture{};
  const size_t frame_length = encode(0x0001u, nullptr, 0u, 4u, frame);

  frame[8] = 0xf0u;
  frame[9] = 0x01u;
  oomwoo_stream_decoder_init(&decoder);
  TEST_ASSERT_EQUAL_UINT32(
      0u, oomwoo_stream_decoder_feed(&decoder, frame, frame_length,
                                     capture_frame, &capture));
  TEST_ASSERT_GREATER_THAN_UINT32(0u, decoder.buffered_bytes);
  oomwoo_stream_decoder_reset_incomplete(&decoder);
  TEST_ASSERT_EQUAL_UINT32(0u, decoder.buffered_bytes);
  TEST_ASSERT_EQUAL_UINT32(1u, decoder.stats.gap_resets);
}

void test_maximum_payload_round_trips() {
  uint8_t payload[OOMWOO_PROTOCOL_MAX_PAYLOAD_SIZE];
  uint8_t frame[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  oomwoo_decoded_frame_t decoded;

  std::memset(payload, 0xa5, sizeof(payload));
  const size_t frame_length =
      encode(0x8004u, payload, sizeof(payload), 5u, frame);
  TEST_ASSERT_EQUAL_UINT32(OOMWOO_PROTOCOL_MAX_FRAME_SIZE, frame_length);
  TEST_ASSERT_EQUAL(
      OOMWOO_PROTOCOL_OK,
      oomwoo_decode_frame(frame, frame_length, &decoded));
  TEST_ASSERT_EQUAL_UINT16(sizeof(payload), decoded.payload_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, decoded.payload, sizeof(payload));
}

}  // namespace

void setUp() {}

void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc_and_cross_language_golden_vector);
  RUN_TEST(test_stream_recovers_from_noise_and_split_input);
  RUN_TEST(test_bad_crc_is_dropped_and_next_frame_decodes);
  RUN_TEST(test_wrong_version_is_counted_and_rejected);
  RUN_TEST(test_gap_reset_releases_corrupt_length);
  RUN_TEST(test_maximum_payload_round_trips);
  return UNITY_END();
}
