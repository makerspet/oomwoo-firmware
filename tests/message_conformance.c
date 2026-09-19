#include "oomwoo_messages.h"
#include "oomwoo_protocol.h"

#include "generated/oomwoo_golden_vectors_v1.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  uint16_t type;
  uint16_t size;
} message_shape_t;

static const message_shape_t MESSAGE_SHAPES[] = {
    {OOMWOO_MESSAGE_HEARTBEAT, 5u},
    {OOMWOO_MESSAGE_ESTOP_SET, 3u},
    {OOMWOO_MESSAGE_CLEAR_LATCHED_FAULT, 2u},
    {OOMWOO_MESSAGE_IDENTIFY_REQUEST, 0u},
    {OOMWOO_MESSAGE_DRIVE_SETPOINT, 6u},
    {OOMWOO_MESSAGE_CLEANING_MOTORS_SET, 4u},
    {OOMWOO_MESSAGE_LIDAR_MOTOR_SET, 1u},
    {OOMWOO_MESSAGE_LED_SET, 3u},
    {OOMWOO_MESSAGE_ACK, 4u},
    {OOMWOO_MESSAGE_NACK, 4u},
    {OOMWOO_MESSAGE_MCU_HELLO, 8u},
    {OOMWOO_MESSAGE_FAST_TELEMETRY, 19u},
    {OOMWOO_MESSAGE_SAFETY_EVENT, 5u},
    {OOMWOO_MESSAGE_SAFETY_STATE, 8u},
};

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

static void assert_selected_fields(const oomwoo_decoded_frame_t *frame,
                                   const oomwoo_message_t *message) {
  switch (message->type) {
    case OOMWOO_MESSAGE_HEARTBEAT:
      assert(message->payload.heartbeat.cpu_time_ms == UINT32_C(0x12345678));
      assert(message->payload.heartbeat.cpu_mode ==
             (uint8_t)OOMWOO_CPU_MODE_STACK_HEALTHY);
      break;
    case OOMWOO_MESSAGE_DRIVE_SETPOINT:
      assert(message->payload.drive_setpoint.linear_mm_s == -120);
      assert(message->payload.drive_setpoint.angular_mrad_s == 500);
      assert(message->payload.drive_setpoint.duration_ms == 100u);
      break;
    case OOMWOO_MESSAGE_MCU_HELLO:
      assert(message->payload.mcu_hello.firmware_major == 1u);
      assert(message->payload.mcu_hello.firmware_minor == 2u);
      assert(message->payload.mcu_hello.build_id == UINT32_C(0x12345678));
      break;
    case OOMWOO_MESSAGE_FAST_TELEMETRY:
      assert(message->payload.fast_telemetry.timestamp_ms == 20u);
      assert(message->payload.fast_telemetry.left_ticks == -10);
      assert(message->payload.fast_telemetry.right_ticks == 11);
      assert(message->payload.fast_telemetry.dock_flags == 3u);
      assert(message->payload.fast_telemetry.battery_mv == 15100u);
      break;
    case OOMWOO_MESSAGE_SAFETY_EVENT:
      assert(message->payload.safety_event.event >= 1u);
      assert(message->payload.safety_event.event <= 10u);
      assert(frame->sequence ==
             (uint16_t)(message->payload.safety_event.event + 12u));
      break;
    case OOMWOO_MESSAGE_SAFETY_STATE:
      assert(message->payload.safety_state.timestamp_ms == 151u);
      assert(message->payload.safety_state.active_flags == 0x0100u);
      assert(message->payload.safety_state.latched_flags == 0x0300u);
      break;
    default:
      break;
  }
}

static void test_all_canonical_vectors(void) {
  size_t index;

  assert(OOMWOO_PROTOCOL_VERSION == 1u);
  assert(OOMWOO_GOLDEN_VECTOR_V1_COUNT == 23u);
  assert(strlen(OOMWOO_GOLDEN_SOURCE_COMMIT) == 40u);

  for (index = 0u; index < OOMWOO_GOLDEN_VECTOR_V1_COUNT; ++index) {
    const oomwoo_golden_vector_v1_t *vector =
        &OOMWOO_GOLDEN_VECTORS_V1[index];
    oomwoo_decoded_frame_t frame;
    oomwoo_message_t message;
    uint8_t payload[OOMWOO_GOLDEN_MAX_PAYLOAD_SIZE];
    uint8_t encoded_frame[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
    uint16_t payload_length = 0u;
    size_t frame_length = 0u;

    assert(oomwoo_decode_frame(vector->frame, vector->frame_length, &frame) ==
           OOMWOO_PROTOCOL_OK);
    assert(frame.message_type == vector->message_type);
    assert(frame.sequence == vector->sequence);
    assert(frame.flags == vector->flags);
    assert(frame.payload_length == vector->payload_length);
    assert(memcmp(frame.payload, vector->payload, vector->payload_length) == 0);

    assert(oomwoo_message_decode_payload(
               frame.message_type, frame.payload, frame.payload_length,
               &message) == OOMWOO_MESSAGE_OK);
    assert(message.type == vector->message_type);
    assert_selected_fields(&frame, &message);

    assert(oomwoo_message_encode_payload(&message, payload, sizeof(payload),
                                         &payload_length) ==
           OOMWOO_MESSAGE_OK);
    assert(payload_length == vector->payload_length);
    assert(memcmp(payload, vector->payload, payload_length) == 0);

    assert(oomwoo_encode_frame(message.type, payload, payload_length,
                               frame.sequence, frame.flags, encoded_frame,
                               sizeof(encoded_frame), &frame_length) ==
           OOMWOO_PROTOCOL_OK);
    assert(frame_length == vector->frame_length);
    assert(memcmp(encoded_frame, vector->frame, frame_length) == 0);
  }
}

static void test_payload_shapes_and_fail_closed_output(void) {
  uint8_t payload[32] = {0u};
  size_t index;

  for (index = 0u; index < sizeof(MESSAGE_SHAPES) / sizeof(MESSAGE_SHAPES[0]);
       ++index) {
    const message_shape_t *shape = &MESSAGE_SHAPES[index];
    oomwoo_message_t output;
    uint16_t size = UINT16_MAX;

    assert(oomwoo_message_payload_size(shape->type, &size) ==
           OOMWOO_MESSAGE_OK);
    assert(size == shape->size);

    memset(&output, 0xa5, sizeof(output));
    assert(oomwoo_message_decode_payload(
               shape->type, payload, (uint16_t)(shape->size + 1u), &output) ==
           OOMWOO_MESSAGE_WRONG_PAYLOAD_LENGTH);
    assert(memory_is_zero(&output, sizeof(output)));

    if (shape->size != 0u) {
      memset(&output, 0xa5, sizeof(output));
      assert(oomwoo_message_decode_payload(
                 shape->type, payload, (uint16_t)(shape->size - 1u),
                 &output) == OOMWOO_MESSAGE_WRONG_PAYLOAD_LENGTH);
      assert(memory_is_zero(&output, sizeof(output)));
    }
  }
}

static void test_semantic_boundaries(void) {
  oomwoo_message_t message;
  uint8_t output[32];
  uint16_t output_length = UINT16_MAX;

  memset(&message, 0, sizeof(message));
  message.type = OOMWOO_MESSAGE_DRIVE_SETPOINT;
  message.payload.drive_setpoint.linear_mm_s = -OOMWOO_MAX_LINEAR_MM_S;
  message.payload.drive_setpoint.angular_mrad_s =
      OOMWOO_MAX_ANGULAR_MRAD_S;
  message.payload.drive_setpoint.duration_ms =
      OOMWOO_MAX_SETPOINT_DURATION_MS;
  assert(oomwoo_message_encode_payload(&message, output, sizeof(output),
                                       &output_length) == OOMWOO_MESSAGE_OK);
  assert(output_length == 6u);

  message.payload.drive_setpoint.duration_ms = 0u;
  assert(oomwoo_message_encode_payload(&message, output, sizeof(output),
                                       &output_length) ==
         OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE);
  assert(output_length == 0u);

  message.payload.drive_setpoint.duration_ms = 100u;
  message.payload.drive_setpoint.linear_mm_s = 501;
  assert(oomwoo_message_validate(&message) ==
         OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE);

  message.payload.drive_setpoint.linear_mm_s = -500;
  message.payload.drive_setpoint.angular_mrad_s = -4001;
  assert(oomwoo_message_validate(&message) ==
         OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE);

  memset(&message, 0, sizeof(message));
  message.type = OOMWOO_MESSAGE_CLEANING_MOTORS_SET;
  message.payload.cleaning_motors_set.fan_pct = 101u;
  assert(oomwoo_message_validate(&message) ==
         OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE);

  memset(&message, 0, sizeof(message));
  message.type = OOMWOO_MESSAGE_HEARTBEAT;
  message.payload.heartbeat.cpu_mode = 2u;
  assert(oomwoo_message_validate(&message) ==
         OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE);

  memset(&message, 0, sizeof(message));
  message.type = OOMWOO_MESSAGE_SAFETY_EVENT;
  message.payload.safety_event.event = 17u;
  message.payload.safety_event.active = 1u;
  assert(oomwoo_message_validate(&message) ==
         OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE);
}

static void test_direction_open_and_unknown_messages(void) {
  oomwoo_message_t message;
  oomwoo_message_t output;
  uint8_t encoded[8];
  uint16_t encoded_length = UINT16_MAX;
  uint16_t size = 0u;

  assert(oomwoo_message_direction(OOMWOO_MESSAGE_HEARTBEAT) ==
         OOMWOO_MESSAGE_DIRECTION_CPU_TO_MCU);
  assert(oomwoo_message_direction(OOMWOO_MESSAGE_SAFETY_STATE) ==
         OOMWOO_MESSAGE_DIRECTION_MCU_TO_CPU);
  assert(oomwoo_message_direction(OOMWOO_MESSAGE_ACK) ==
         OOMWOO_MESSAGE_DIRECTION_BOTH);
  assert(oomwoo_message_direction(0xffffu) ==
         OOMWOO_MESSAGE_DIRECTION_UNKNOWN);

  assert(oomwoo_message_payload_size(OOMWOO_MESSAGE_POWER_TELEMETRY, &size) ==
         OOMWOO_MESSAGE_PAYLOAD_UNDEFINED);
  assert(size == 0u);
  assert(oomwoo_message_decode_payload(OOMWOO_MESSAGE_MCU_DIAGNOSTIC, NULL, 0u,
                                       &output) ==
         OOMWOO_MESSAGE_PAYLOAD_UNDEFINED);
  assert(memory_is_zero(&output, sizeof(output)));

  memset(&message, 0, sizeof(message));
  message.type = OOMWOO_MESSAGE_POWER_TELEMETRY;
  assert(oomwoo_message_encode_payload(&message, encoded, sizeof(encoded),
                                       &encoded_length) ==
         OOMWOO_MESSAGE_PAYLOAD_UNDEFINED);
  assert(encoded_length == 0u);

  size = UINT16_MAX;
  assert(oomwoo_message_payload_size(0xffffu, &size) ==
         OOMWOO_MESSAGE_UNKNOWN_TYPE);
  assert(size == 0u);
}

static void test_signed_telemetry_extrema(void) {
  oomwoo_message_t input;
  oomwoo_message_t decoded;
  uint8_t payload[19];
  uint16_t payload_length = 0u;

  memset(&input, 0, sizeof(input));
  input.type = OOMWOO_MESSAGE_FAST_TELEMETRY;
  input.payload.fast_telemetry.left_ticks = INT32_MIN;
  input.payload.fast_telemetry.right_ticks = INT32_MAX;

  assert(oomwoo_message_encode_payload(&input, payload, sizeof(payload),
                                       &payload_length) == OOMWOO_MESSAGE_OK);
  assert(payload_length == sizeof(payload));
  assert(oomwoo_message_decode_payload(input.type, payload, payload_length,
                                       &decoded) == OOMWOO_MESSAGE_OK);
  assert(decoded.payload.fast_telemetry.left_ticks == INT32_MIN);
  assert(decoded.payload.fast_telemetry.right_ticks == INT32_MAX);
}

static void test_null_and_capacity_errors(void) {
  oomwoo_message_t identify;
  oomwoo_message_t heartbeat;
  uint8_t output[4];
  uint16_t output_length = UINT16_MAX;

  memset(&identify, 0, sizeof(identify));
  identify.type = OOMWOO_MESSAGE_IDENTIFY_REQUEST;
  assert(oomwoo_message_encode_payload(&identify, NULL, 0u, &output_length) ==
         OOMWOO_MESSAGE_OK);
  assert(output_length == 0u);

  memset(&heartbeat, 0, sizeof(heartbeat));
  heartbeat.type = OOMWOO_MESSAGE_HEARTBEAT;
  heartbeat.payload.heartbeat.cpu_mode = OOMWOO_CPU_MODE_DISARMED;
  assert(oomwoo_message_encode_payload(&heartbeat, output, sizeof(output),
                                       &output_length) ==
         OOMWOO_MESSAGE_OUTPUT_TOO_SMALL);
  assert(output_length == 0u);
  assert(oomwoo_message_encode_payload(NULL, output, sizeof(output),
                                       &output_length) ==
         OOMWOO_MESSAGE_NULL_ARGUMENT);
  assert(output_length == 0u);
  assert(oomwoo_message_decode_payload(OOMWOO_MESSAGE_HEARTBEAT, NULL, 5u,
                                       &heartbeat) ==
         OOMWOO_MESSAGE_NULL_ARGUMENT);
}

int main(void) {
  test_all_canonical_vectors();
  test_payload_shapes_and_fail_closed_output();
  test_semantic_boundaries();
  test_direction_open_and_unknown_messages();
  test_signed_telemetry_extrema();
  test_null_and_capacity_errors();
  puts("OOMWOO typed message conformance: 23 vectors PASS");
  return 0;
}
