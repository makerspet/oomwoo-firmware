#include "oomwoo_messages.h"

#include <limits.h>
#include <string.h>

#define HEARTBEAT_SIZE UINT16_C(5)
#define ESTOP_SET_SIZE UINT16_C(3)
#define CLEAR_LATCHED_FAULT_SIZE UINT16_C(2)
#define IDENTIFY_REQUEST_SIZE UINT16_C(0)
#define DRIVE_SETPOINT_SIZE UINT16_C(6)
#define CLEANING_MOTORS_SET_SIZE UINT16_C(4)
#define LIDAR_MOTOR_SET_SIZE UINT16_C(1)
#define LED_SET_SIZE UINT16_C(3)
#define ACK_SIZE UINT16_C(4)
#define NACK_SIZE UINT16_C(4)
#define MCU_HELLO_SIZE UINT16_C(8)
#define FAST_TELEMETRY_SIZE UINT16_C(19)
#define SAFETY_EVENT_SIZE UINT16_C(5)
#define SAFETY_STATE_SIZE UINT16_C(8)

static uint16_t read_u16_le(const uint8_t *input) {
  return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8u));
}

static uint32_t read_u32_le(const uint8_t *input) {
  return (uint32_t)input[0] | ((uint32_t)input[1] << 8u) |
         ((uint32_t)input[2] << 16u) | ((uint32_t)input[3] << 24u);
}

static int16_t read_i16_le(const uint8_t *input) {
  const uint16_t value = read_u16_le(input);
  if (value <= (uint16_t)INT16_MAX) {
    return (int16_t)value;
  }
  return (int16_t)(-((int32_t)(UINT16_MAX - value)) - 1);
}

static int32_t read_i32_le(const uint8_t *input) {
  const uint32_t value = read_u32_le(input);
  if (value <= (uint32_t)INT32_MAX) {
    return (int32_t)value;
  }
  return -((int32_t)(UINT32_MAX - value)) - 1;
}

static void write_u16_le(uint8_t *output, uint16_t value) {
  output[0] = (uint8_t)(value & UINT16_C(0xff));
  output[1] = (uint8_t)((value >> 8u) & UINT16_C(0xff));
}

static void write_u32_le(uint8_t *output, uint32_t value) {
  output[0] = (uint8_t)(value & UINT32_C(0xff));
  output[1] = (uint8_t)((value >> 8u) & UINT32_C(0xff));
  output[2] = (uint8_t)((value >> 16u) & UINT32_C(0xff));
  output[3] = (uint8_t)((value >> 24u) & UINT32_C(0xff));
}

static void write_i16_le(uint8_t *output, int16_t value) {
  write_u16_le(output, (uint16_t)value);
}

static void write_i32_le(uint8_t *output, int32_t value) {
  write_u32_le(output, (uint32_t)value);
}

oomwoo_message_direction_t oomwoo_message_direction(uint16_t message_type) {
  switch (message_type) {
    case OOMWOO_MESSAGE_HEARTBEAT:
    case OOMWOO_MESSAGE_ESTOP_SET:
    case OOMWOO_MESSAGE_CLEAR_LATCHED_FAULT:
    case OOMWOO_MESSAGE_IDENTIFY_REQUEST:
    case OOMWOO_MESSAGE_DRIVE_SETPOINT:
    case OOMWOO_MESSAGE_CLEANING_MOTORS_SET:
    case OOMWOO_MESSAGE_LIDAR_MOTOR_SET:
    case OOMWOO_MESSAGE_LED_SET:
      return OOMWOO_MESSAGE_DIRECTION_CPU_TO_MCU;
    case OOMWOO_MESSAGE_ACK:
    case OOMWOO_MESSAGE_NACK:
      return OOMWOO_MESSAGE_DIRECTION_BOTH;
    case OOMWOO_MESSAGE_MCU_HELLO:
    case OOMWOO_MESSAGE_FAST_TELEMETRY:
    case OOMWOO_MESSAGE_SAFETY_EVENT:
    case OOMWOO_MESSAGE_POWER_TELEMETRY:
    case OOMWOO_MESSAGE_MCU_DIAGNOSTIC:
    case OOMWOO_MESSAGE_SAFETY_STATE:
      return OOMWOO_MESSAGE_DIRECTION_MCU_TO_CPU;
    default:
      return OOMWOO_MESSAGE_DIRECTION_UNKNOWN;
  }
}

oomwoo_message_result_t oomwoo_message_payload_size(uint16_t message_type,
                                                    uint16_t *payload_size) {
  if (payload_size == NULL) {
    return OOMWOO_MESSAGE_NULL_ARGUMENT;
  }
  *payload_size = UINT16_C(0);

  switch (message_type) {
    case OOMWOO_MESSAGE_HEARTBEAT:
      *payload_size = HEARTBEAT_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_ESTOP_SET:
      *payload_size = ESTOP_SET_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_CLEAR_LATCHED_FAULT:
      *payload_size = CLEAR_LATCHED_FAULT_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_IDENTIFY_REQUEST:
      *payload_size = IDENTIFY_REQUEST_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_DRIVE_SETPOINT:
      *payload_size = DRIVE_SETPOINT_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_CLEANING_MOTORS_SET:
      *payload_size = CLEANING_MOTORS_SET_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_LIDAR_MOTOR_SET:
      *payload_size = LIDAR_MOTOR_SET_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_LED_SET:
      *payload_size = LED_SET_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_ACK:
      *payload_size = ACK_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_NACK:
      *payload_size = NACK_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_MCU_HELLO:
      *payload_size = MCU_HELLO_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_FAST_TELEMETRY:
      *payload_size = FAST_TELEMETRY_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_SAFETY_EVENT:
      *payload_size = SAFETY_EVENT_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_SAFETY_STATE:
      *payload_size = SAFETY_STATE_SIZE;
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_POWER_TELEMETRY:
    case OOMWOO_MESSAGE_MCU_DIAGNOSTIC:
      return OOMWOO_MESSAGE_PAYLOAD_UNDEFINED;
    default:
      return OOMWOO_MESSAGE_UNKNOWN_TYPE;
  }
}

bool oomwoo_message_is_known_cpu_mode(uint8_t cpu_mode) {
  return cpu_mode == (uint8_t)OOMWOO_CPU_MODE_DISARMED ||
         cpu_mode == (uint8_t)OOMWOO_CPU_MODE_STACK_HEALTHY;
}

oomwoo_message_result_t oomwoo_message_validate(
    const oomwoo_message_t *message) {
  uint16_t ignored_size;
  oomwoo_message_result_t result;

  if (message == NULL) {
    return OOMWOO_MESSAGE_NULL_ARGUMENT;
  }

  result = oomwoo_message_payload_size(message->type, &ignored_size);
  if (result != OOMWOO_MESSAGE_OK) {
    return result;
  }

  switch (message->type) {
    case OOMWOO_MESSAGE_HEARTBEAT:
      return oomwoo_message_is_known_cpu_mode(
                 message->payload.heartbeat.cpu_mode)
                 ? OOMWOO_MESSAGE_OK
                 : OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE;
    case OOMWOO_MESSAGE_ESTOP_SET:
      return message->payload.estop_set.active <= UINT8_C(1)
                 ? OOMWOO_MESSAGE_OK
                 : OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE;
    case OOMWOO_MESSAGE_DRIVE_SETPOINT:
      if (message->payload.drive_setpoint.linear_mm_s <
              -OOMWOO_MAX_LINEAR_MM_S ||
          message->payload.drive_setpoint.linear_mm_s >
              OOMWOO_MAX_LINEAR_MM_S ||
          message->payload.drive_setpoint.angular_mrad_s <
              -OOMWOO_MAX_ANGULAR_MRAD_S ||
          message->payload.drive_setpoint.angular_mrad_s >
              OOMWOO_MAX_ANGULAR_MRAD_S ||
          message->payload.drive_setpoint.duration_ms == UINT16_C(0) ||
          message->payload.drive_setpoint.duration_ms >
              OOMWOO_MAX_SETPOINT_DURATION_MS) {
        return OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE;
      }
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_CLEANING_MOTORS_SET:
      if (message->payload.cleaning_motors_set.main_brush_pct >
              OOMWOO_MAX_PERCENT ||
          message->payload.cleaning_motors_set.side_brush_pct >
              OOMWOO_MAX_PERCENT ||
          message->payload.cleaning_motors_set.fan_pct >
              OOMWOO_MAX_PERCENT ||
          message->payload.cleaning_motors_set.pump_pct >
              OOMWOO_MAX_PERCENT) {
        return OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE;
      }
      return OOMWOO_MESSAGE_OK;
    case OOMWOO_MESSAGE_LIDAR_MOTOR_SET:
      return message->payload.lidar_motor_set.pwm_pct <= OOMWOO_MAX_PERCENT
                 ? OOMWOO_MESSAGE_OK
                 : OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE;
    case OOMWOO_MESSAGE_LED_SET:
      return message->payload.led_set.brightness_pct <= OOMWOO_MAX_PERCENT
                 ? OOMWOO_MESSAGE_OK
                 : OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE;
    case OOMWOO_MESSAGE_SAFETY_EVENT:
      if (message->payload.safety_event.event == UINT16_C(0) ||
          message->payload.safety_event.event > UINT16_C(16) ||
          message->payload.safety_event.active > UINT8_C(1)) {
        return OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE;
      }
      return OOMWOO_MESSAGE_OK;
    default:
      return OOMWOO_MESSAGE_OK;
  }
}

oomwoo_message_result_t oomwoo_message_decode_payload(
    uint16_t message_type, const uint8_t *payload, uint16_t payload_length,
    oomwoo_message_t *output) {
  uint16_t expected_length;
  oomwoo_message_result_t result;

  if (output == NULL || (payload == NULL && payload_length != UINT16_C(0))) {
    return OOMWOO_MESSAGE_NULL_ARGUMENT;
  }

  memset(output, 0, sizeof(*output));
  result = oomwoo_message_payload_size(message_type, &expected_length);
  if (result != OOMWOO_MESSAGE_OK) {
    return result;
  }
  if (payload_length != expected_length) {
    return OOMWOO_MESSAGE_WRONG_PAYLOAD_LENGTH;
  }

  output->type = message_type;

  switch (message_type) {
    case OOMWOO_MESSAGE_HEARTBEAT:
      output->payload.heartbeat.cpu_time_ms = read_u32_le(payload);
      output->payload.heartbeat.cpu_mode = payload[4];
      break;
    case OOMWOO_MESSAGE_ESTOP_SET:
      output->payload.estop_set.active = payload[0];
      output->payload.estop_set.reason = read_u16_le(payload + 1u);
      break;
    case OOMWOO_MESSAGE_CLEAR_LATCHED_FAULT:
      output->payload.clear_latched_fault.fault_mask = read_u16_le(payload);
      break;
    case OOMWOO_MESSAGE_IDENTIFY_REQUEST:
      break;
    case OOMWOO_MESSAGE_DRIVE_SETPOINT:
      output->payload.drive_setpoint.linear_mm_s = read_i16_le(payload);
      output->payload.drive_setpoint.angular_mrad_s = read_i16_le(payload + 2u);
      output->payload.drive_setpoint.duration_ms = read_u16_le(payload + 4u);
      break;
    case OOMWOO_MESSAGE_CLEANING_MOTORS_SET:
      output->payload.cleaning_motors_set.main_brush_pct = payload[0];
      output->payload.cleaning_motors_set.side_brush_pct = payload[1];
      output->payload.cleaning_motors_set.fan_pct = payload[2];
      output->payload.cleaning_motors_set.pump_pct = payload[3];
      break;
    case OOMWOO_MESSAGE_LIDAR_MOTOR_SET:
      output->payload.lidar_motor_set.pwm_pct = payload[0];
      break;
    case OOMWOO_MESSAGE_LED_SET:
      output->payload.led_set.led_id = payload[0];
      output->payload.led_set.mode = payload[1];
      output->payload.led_set.brightness_pct = payload[2];
      break;
    case OOMWOO_MESSAGE_ACK:
      output->payload.ack.acked_sequence = read_u16_le(payload);
      output->payload.ack.status = read_u16_le(payload + 2u);
      break;
    case OOMWOO_MESSAGE_NACK:
      output->payload.nack.rejected_sequence = read_u16_le(payload);
      output->payload.nack.error_code = read_u16_le(payload + 2u);
      break;
    case OOMWOO_MESSAGE_MCU_HELLO:
      output->payload.mcu_hello.firmware_major = read_u16_le(payload);
      output->payload.mcu_hello.firmware_minor = read_u16_le(payload + 2u);
      output->payload.mcu_hello.build_id = read_u32_le(payload + 4u);
      break;
    case OOMWOO_MESSAGE_FAST_TELEMETRY:
      output->payload.fast_telemetry.timestamp_ms = read_u32_le(payload);
      output->payload.fast_telemetry.left_ticks = read_i32_le(payload + 4u);
      output->payload.fast_telemetry.right_ticks = read_i32_le(payload + 8u);
      output->payload.fast_telemetry.bumper_flags = payload[12];
      output->payload.fast_telemetry.cliff_flags = payload[13];
      output->payload.fast_telemetry.wheel_drop_flags = payload[14];
      output->payload.fast_telemetry.dock_flags = payload[15];
      output->payload.fast_telemetry.safety_latched_flags = payload[16];
      output->payload.fast_telemetry.battery_mv = read_u16_le(payload + 17u);
      break;
    case OOMWOO_MESSAGE_SAFETY_EVENT:
      output->payload.safety_event.event = read_u16_le(payload);
      output->payload.safety_event.active = payload[2];
      output->payload.safety_event.detail = read_u16_le(payload + 3u);
      break;
    case OOMWOO_MESSAGE_SAFETY_STATE:
      output->payload.safety_state.timestamp_ms = read_u32_le(payload);
      output->payload.safety_state.active_flags = read_u16_le(payload + 4u);
      output->payload.safety_state.latched_flags = read_u16_le(payload + 6u);
      break;
    default:
      return OOMWOO_MESSAGE_UNKNOWN_TYPE;
  }

  result = oomwoo_message_validate(output);
  if (result != OOMWOO_MESSAGE_OK) {
    memset(output, 0, sizeof(*output));
  }
  return result;
}

oomwoo_message_result_t oomwoo_message_encode_payload(
    const oomwoo_message_t *message, uint8_t *output, size_t output_capacity,
    uint16_t *output_length) {
  uint16_t required_length;
  oomwoo_message_result_t result;

  if (output_length == NULL) {
    return OOMWOO_MESSAGE_NULL_ARGUMENT;
  }
  *output_length = UINT16_C(0);
  if (message == NULL) {
    return OOMWOO_MESSAGE_NULL_ARGUMENT;
  }

  result = oomwoo_message_validate(message);
  if (result != OOMWOO_MESSAGE_OK) {
    return result;
  }
  result = oomwoo_message_payload_size(message->type, &required_length);
  if (result != OOMWOO_MESSAGE_OK) {
    return result;
  }
  if (output_capacity < (size_t)required_length) {
    return OOMWOO_MESSAGE_OUTPUT_TOO_SMALL;
  }
  if (required_length != UINT16_C(0) && output == NULL) {
    return OOMWOO_MESSAGE_NULL_ARGUMENT;
  }

  switch (message->type) {
    case OOMWOO_MESSAGE_HEARTBEAT:
      write_u32_le(output, message->payload.heartbeat.cpu_time_ms);
      output[4] = message->payload.heartbeat.cpu_mode;
      break;
    case OOMWOO_MESSAGE_ESTOP_SET:
      output[0] = message->payload.estop_set.active;
      write_u16_le(output + 1u, message->payload.estop_set.reason);
      break;
    case OOMWOO_MESSAGE_CLEAR_LATCHED_FAULT:
      write_u16_le(output, message->payload.clear_latched_fault.fault_mask);
      break;
    case OOMWOO_MESSAGE_IDENTIFY_REQUEST:
      break;
    case OOMWOO_MESSAGE_DRIVE_SETPOINT:
      write_i16_le(output, message->payload.drive_setpoint.linear_mm_s);
      write_i16_le(output + 2u,
                   message->payload.drive_setpoint.angular_mrad_s);
      write_u16_le(output + 4u, message->payload.drive_setpoint.duration_ms);
      break;
    case OOMWOO_MESSAGE_CLEANING_MOTORS_SET:
      output[0] = message->payload.cleaning_motors_set.main_brush_pct;
      output[1] = message->payload.cleaning_motors_set.side_brush_pct;
      output[2] = message->payload.cleaning_motors_set.fan_pct;
      output[3] = message->payload.cleaning_motors_set.pump_pct;
      break;
    case OOMWOO_MESSAGE_LIDAR_MOTOR_SET:
      output[0] = message->payload.lidar_motor_set.pwm_pct;
      break;
    case OOMWOO_MESSAGE_LED_SET:
      output[0] = message->payload.led_set.led_id;
      output[1] = message->payload.led_set.mode;
      output[2] = message->payload.led_set.brightness_pct;
      break;
    case OOMWOO_MESSAGE_ACK:
      write_u16_le(output, message->payload.ack.acked_sequence);
      write_u16_le(output + 2u, message->payload.ack.status);
      break;
    case OOMWOO_MESSAGE_NACK:
      write_u16_le(output, message->payload.nack.rejected_sequence);
      write_u16_le(output + 2u, message->payload.nack.error_code);
      break;
    case OOMWOO_MESSAGE_MCU_HELLO:
      write_u16_le(output, message->payload.mcu_hello.firmware_major);
      write_u16_le(output + 2u, message->payload.mcu_hello.firmware_minor);
      write_u32_le(output + 4u, message->payload.mcu_hello.build_id);
      break;
    case OOMWOO_MESSAGE_FAST_TELEMETRY:
      write_u32_le(output, message->payload.fast_telemetry.timestamp_ms);
      write_i32_le(output + 4u, message->payload.fast_telemetry.left_ticks);
      write_i32_le(output + 8u, message->payload.fast_telemetry.right_ticks);
      output[12] = message->payload.fast_telemetry.bumper_flags;
      output[13] = message->payload.fast_telemetry.cliff_flags;
      output[14] = message->payload.fast_telemetry.wheel_drop_flags;
      output[15] = message->payload.fast_telemetry.dock_flags;
      output[16] = message->payload.fast_telemetry.safety_latched_flags;
      write_u16_le(output + 17u, message->payload.fast_telemetry.battery_mv);
      break;
    case OOMWOO_MESSAGE_SAFETY_EVENT:
      write_u16_le(output, message->payload.safety_event.event);
      output[2] = message->payload.safety_event.active;
      write_u16_le(output + 3u, message->payload.safety_event.detail);
      break;
    case OOMWOO_MESSAGE_SAFETY_STATE:
      write_u32_le(output, message->payload.safety_state.timestamp_ms);
      write_u16_le(output + 4u, message->payload.safety_state.active_flags);
      write_u16_le(output + 6u, message->payload.safety_state.latched_flags);
      break;
    default:
      return OOMWOO_MESSAGE_UNKNOWN_TYPE;
  }

  *output_length = required_length;
  return OOMWOO_MESSAGE_OK;
}
