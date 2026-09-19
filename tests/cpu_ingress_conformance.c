#include "oomwoo_cpu_ingress.h"

#include "generated/oomwoo_golden_vectors_v1.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  size_t calls;
  uint16_t last_type;
  uint16_t last_sequence;
  int16_t drive_linear_mm_s;
} callback_record_t;

static bool memory_is_zero(const void *memory, size_t size) {
  const uint8_t *bytes = (const uint8_t *)memory;
  size_t index;

  for (index = 0u; index < size; ++index) {
    if (bytes[index] != 0u) {
      return false;
    }
  }
  return true;
}

static void record_message(const oomwoo_decoded_frame_t *frame,
                           const oomwoo_message_t *message, void *context) {
  callback_record_t *record = (callback_record_t *)context;

  ++record->calls;
  record->last_type = message->type;
  record->last_sequence = frame->sequence;
  if (message->type == OOMWOO_MESSAGE_DRIVE_SETPOINT) {
    record->drive_linear_mm_s =
        message->payload.drive_setpoint.linear_mm_s;
  }
}

static void test_canonical_cpu_ingress(void) {
  oomwoo_cpu_ingress_t ingress;
  callback_record_t record;
  size_t index;

  memset(&record, 0, sizeof(record));
  oomwoo_cpu_ingress_init(&ingress, record_message, &record);

  for (index = 0u; index < OOMWOO_GOLDEN_VECTOR_V1_COUNT; ++index) {
    const oomwoo_golden_vector_v1_t *vector =
        &OOMWOO_GOLDEN_VECTORS_V1[index];
    const size_t accepted =
        oomwoo_cpu_ingress_feed(&ingress, vector->frame, vector->frame_length);
    const oomwoo_message_direction_t direction =
        oomwoo_message_direction(vector->message_type);

    if (direction == OOMWOO_MESSAGE_DIRECTION_CPU_TO_MCU ||
        direction == OOMWOO_MESSAGE_DIRECTION_BOTH) {
      assert(accepted == 1u);
    } else {
      assert(accepted == 0u);
    }
  }

  assert(ingress.decoder.stats.frames == 23u);
  assert(ingress.stats.accepted_messages == 10u);
  assert(ingress.stats.wrong_direction == 13u);
  assert(record.calls == 10u);
  assert(record.last_type == OOMWOO_MESSAGE_NACK);
  assert(record.last_sequence == 10u);
  assert(record.drive_linear_mm_s == -120);
}

static oomwoo_decoded_frame_t make_frame(uint16_t message_type,
                                         const uint8_t *payload,
                                         uint16_t payload_length) {
  oomwoo_decoded_frame_t frame;

  memset(&frame, 0, sizeof(frame));
  frame.version = OOMWOO_PROTOCOL_VERSION;
  frame.message_type = message_type;
  frame.payload = payload;
  frame.payload_length = payload_length;
  return frame;
}

static void test_direct_validation_fail_closed(void) {
  static const uint8_t valid_heartbeat[] = {0u, 0u, 0u, 0u, 1u};
  static const uint8_t invalid_drive[] = {0u, 0u, 0u, 0u, 0u, 0u};
  oomwoo_decoded_frame_t frame;
  oomwoo_message_t output;

  frame = make_frame(OOMWOO_MESSAGE_HEARTBEAT, valid_heartbeat,
                     (uint16_t)sizeof(valid_heartbeat));
  assert(oomwoo_cpu_ingress_validate_frame(&frame, &output) ==
         OOMWOO_CPU_INGRESS_OK);
  assert(output.type == OOMWOO_MESSAGE_HEARTBEAT);

  memset(&output, 0xa5, sizeof(output));
  frame.version = 2u;
  assert(oomwoo_cpu_ingress_validate_frame(&frame, &output) ==
         OOMWOO_CPU_INGRESS_BAD_VERSION);
  assert(memory_is_zero(&output, sizeof(output)));

  memset(&output, 0xa5, sizeof(output));
  frame = make_frame(OOMWOO_MESSAGE_SAFETY_STATE, NULL, 0u);
  assert(oomwoo_cpu_ingress_validate_frame(&frame, &output) ==
         OOMWOO_CPU_INGRESS_WRONG_DIRECTION);
  assert(memory_is_zero(&output, sizeof(output)));

  memset(&output, 0xa5, sizeof(output));
  frame = make_frame(UINT16_C(0xffff), NULL, 0u);
  assert(oomwoo_cpu_ingress_validate_frame(&frame, &output) ==
         OOMWOO_CPU_INGRESS_UNKNOWN_TYPE);
  assert(memory_is_zero(&output, sizeof(output)));

  memset(&output, 0xa5, sizeof(output));
  frame = make_frame(OOMWOO_MESSAGE_HEARTBEAT, NULL, 0u);
  assert(oomwoo_cpu_ingress_validate_frame(&frame, &output) ==
         OOMWOO_CPU_INGRESS_WRONG_PAYLOAD_LENGTH);
  assert(memory_is_zero(&output, sizeof(output)));

  memset(&output, 0xa5, sizeof(output));
  frame = make_frame(OOMWOO_MESSAGE_DRIVE_SETPOINT, invalid_drive,
                     (uint16_t)sizeof(invalid_drive));
  assert(oomwoo_cpu_ingress_validate_frame(&frame, &output) ==
         OOMWOO_CPU_INGRESS_VALUE_OUT_OF_RANGE);
  assert(memory_is_zero(&output, sizeof(output)));

  frame = make_frame(OOMWOO_MESSAGE_IDENTIFY_REQUEST, NULL, 0u);
  assert(oomwoo_cpu_ingress_validate_frame(&frame, &output) ==
         OOMWOO_CPU_INGRESS_OK);
  assert(output.type == OOMWOO_MESSAGE_IDENTIFY_REQUEST);

  assert(oomwoo_cpu_ingress_validate_frame(NULL, &output) ==
         OOMWOO_CPU_INGRESS_NULL_ARGUMENT);
  assert(oomwoo_cpu_ingress_validate_frame(&frame, NULL) ==
         OOMWOO_CPU_INGRESS_NULL_ARGUMENT);
}

static void test_stream_rejections_are_counted(void) {
  static const uint8_t invalid_drive[] = {0u, 0u, 0u, 0u, 0u, 0u};
  oomwoo_cpu_ingress_t ingress;
  callback_record_t record;
  uint8_t frame[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  size_t frame_length = 0u;

  memset(&record, 0, sizeof(record));
  oomwoo_cpu_ingress_init(&ingress, record_message, &record);

  assert(oomwoo_encode_frame(OOMWOO_MESSAGE_DRIVE_SETPOINT, invalid_drive,
                             (uint16_t)sizeof(invalid_drive), 1u, 0u, frame,
                             sizeof(frame), &frame_length) ==
         OOMWOO_PROTOCOL_OK);
  assert(oomwoo_cpu_ingress_feed(&ingress, frame, frame_length) == 0u);
  assert(ingress.stats.value_out_of_range == 1u);

  assert(oomwoo_encode_frame(UINT16_C(0x0060), NULL, 0u, 2u, 0u, frame,
                             sizeof(frame), &frame_length) ==
         OOMWOO_PROTOCOL_OK);
  assert(oomwoo_cpu_ingress_feed(&ingress, frame, frame_length) == 0u);
  assert(ingress.stats.unknown_type == 1u);

  assert(oomwoo_encode_frame(OOMWOO_MESSAGE_HEARTBEAT, NULL, 0u, 3u, 0u,
                             frame, sizeof(frame), &frame_length) ==
         OOMWOO_PROTOCOL_OK);
  assert(oomwoo_cpu_ingress_feed(&ingress, frame, frame_length) == 0u);
  assert(ingress.stats.wrong_payload_length == 1u);
  assert(ingress.stats.accepted_messages == 0u);
  assert(record.calls == 0u);
  assert(ingress.decoder.stats.frames == 3u);
}

static void test_framing_rejection_never_reaches_message_layer(void) {
  oomwoo_cpu_ingress_t ingress;
  callback_record_t record;
  uint8_t corrupt[OOMWOO_GOLDEN_MAX_FRAME_SIZE];
  const oomwoo_golden_vector_v1_t *heartbeat =
      &OOMWOO_GOLDEN_VECTORS_V1[0];

  memset(&record, 0, sizeof(record));
  oomwoo_cpu_ingress_init(&ingress, record_message, &record);
  memcpy(corrupt, heartbeat->frame, heartbeat->frame_length);
  corrupt[heartbeat->frame_length - 1u] ^= UINT8_C(0x01);

  assert(oomwoo_cpu_ingress_feed(&ingress, corrupt,
                                 heartbeat->frame_length) == 0u);
  assert(ingress.decoder.stats.crc_errors == 1u);
  assert(ingress.decoder.stats.frames == 0u);
  assert(ingress.stats.accepted_messages == 0u);
  assert(record.calls == 0u);
}

static void test_fragmented_stream_recovers_from_noise(void) {
  oomwoo_cpu_ingress_t ingress;
  callback_record_t record;
  const oomwoo_golden_vector_v1_t *heartbeat =
      &OOMWOO_GOLDEN_VECTORS_V1[0];
  static const uint8_t noise[] = {0xa5u, 0x00u, 'O'};
  size_t index;

  memset(&record, 0, sizeof(record));
  oomwoo_cpu_ingress_init(&ingress, record_message, &record);
  assert(oomwoo_cpu_ingress_feed(&ingress, noise, sizeof(noise)) == 0u);

  for (index = 0u; index < heartbeat->frame_length; ++index) {
    const size_t accepted =
        oomwoo_cpu_ingress_feed(&ingress, &heartbeat->frame[index], 1u);
    assert(accepted == (index + 1u == heartbeat->frame_length ? 1u : 0u));
  }

  assert(record.calls == 1u);
  assert(record.last_type == OOMWOO_MESSAGE_HEARTBEAT);
  assert(ingress.stats.accepted_messages == 1u);
  assert(ingress.decoder.stats.discarded_bytes == sizeof(noise));
}

static void test_gap_reset_and_null_inputs(void) {
  oomwoo_cpu_ingress_t ingress;
  static const uint8_t partial[] = {'O', 'W', 1u};

  oomwoo_cpu_ingress_init(&ingress, NULL, NULL);
  assert(oomwoo_cpu_ingress_feed(&ingress, partial, sizeof(partial)) == 0u);
  assert(ingress.decoder.buffered_bytes == sizeof(partial));
  oomwoo_cpu_ingress_reset_incomplete(&ingress);
  assert(ingress.decoder.buffered_bytes == 0u);
  assert(ingress.decoder.stats.gap_resets == 1u);

  assert(oomwoo_cpu_ingress_feed(NULL, partial, sizeof(partial)) == 0u);
  assert(oomwoo_cpu_ingress_feed(&ingress, NULL, 1u) == 0u);
  oomwoo_cpu_ingress_init(NULL, NULL, NULL);
  oomwoo_cpu_ingress_reset_incomplete(NULL);
}

int main(void) {
  test_canonical_cpu_ingress();
  test_direct_validation_fail_closed();
  test_stream_rejections_are_counted();
  test_framing_rejection_never_reaches_message_layer();
  test_fragmented_stream_recovers_from_noise();
  test_gap_reset_and_null_inputs();
  puts("OOMWOO CPU ingress conformance: PASS");
  return 0;
}
