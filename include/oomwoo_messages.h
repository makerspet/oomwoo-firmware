#ifndef OOMWOO_MESSAGES_H
#define OOMWOO_MESSAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OOMWOO_MAX_LINEAR_MM_S INT16_C(500)
#define OOMWOO_MAX_ANGULAR_MRAD_S INT16_C(4000)
#define OOMWOO_MAX_SETPOINT_DURATION_MS UINT16_C(250)
#define OOMWOO_MAX_PERCENT UINT8_C(100)

typedef enum {
  OOMWOO_MESSAGE_HEARTBEAT = 0x0001,
  OOMWOO_MESSAGE_ESTOP_SET = 0x0002,
  OOMWOO_MESSAGE_CLEAR_LATCHED_FAULT = 0x0003,
  OOMWOO_MESSAGE_IDENTIFY_REQUEST = 0x0004,
  OOMWOO_MESSAGE_DRIVE_SETPOINT = 0x0101,
  OOMWOO_MESSAGE_CLEANING_MOTORS_SET = 0x0102,
  OOMWOO_MESSAGE_LIDAR_MOTOR_SET = 0x0103,
  OOMWOO_MESSAGE_LED_SET = 0x0104,
  OOMWOO_MESSAGE_ACK = 0x7001,
  OOMWOO_MESSAGE_NACK = 0x7002,
  OOMWOO_MESSAGE_MCU_HELLO = 0x8000,
  OOMWOO_MESSAGE_FAST_TELEMETRY = 0x8001,
  OOMWOO_MESSAGE_SAFETY_EVENT = 0x8002,
  OOMWOO_MESSAGE_POWER_TELEMETRY = 0x8003,
  OOMWOO_MESSAGE_MCU_DIAGNOSTIC = 0x8004,
  OOMWOO_MESSAGE_SAFETY_STATE = 0x8005
} oomwoo_message_type_t;

typedef enum {
  OOMWOO_MESSAGE_DIRECTION_UNKNOWN = 0,
  OOMWOO_MESSAGE_DIRECTION_CPU_TO_MCU,
  OOMWOO_MESSAGE_DIRECTION_MCU_TO_CPU,
  OOMWOO_MESSAGE_DIRECTION_BOTH
} oomwoo_message_direction_t;

typedef enum {
  OOMWOO_MESSAGE_OK = 0,
  OOMWOO_MESSAGE_NULL_ARGUMENT,
  OOMWOO_MESSAGE_UNKNOWN_TYPE,
  OOMWOO_MESSAGE_PAYLOAD_UNDEFINED,
  OOMWOO_MESSAGE_WRONG_PAYLOAD_LENGTH,
  OOMWOO_MESSAGE_OUTPUT_TOO_SMALL,
  OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE
} oomwoo_message_result_t;

typedef enum {
  OOMWOO_CPU_MODE_DISARMED = 0,
  OOMWOO_CPU_MODE_STACK_HEALTHY = 1
} oomwoo_cpu_mode_t;

typedef enum {
  OOMWOO_SAFETY_BUMPER_LEFT = 1,
  OOMWOO_SAFETY_BUMPER_RIGHT = 2,
  OOMWOO_SAFETY_CLIFF_LEFT = 3,
  OOMWOO_SAFETY_CLIFF_RIGHT = 4,
  OOMWOO_SAFETY_WHEEL_DROP_LEFT = 5,
  OOMWOO_SAFETY_WHEEL_DROP_RIGHT = 6,
  OOMWOO_SAFETY_BRUSH_OVERCURRENT = 7,
  OOMWOO_SAFETY_FAN_OVERCURRENT = 8,
  OOMWOO_SAFETY_CPU_HEARTBEAT_TIMEOUT = 9,
  OOMWOO_SAFETY_ESTOP = 10
} oomwoo_safety_event_code_t;

typedef struct {
  uint32_t cpu_time_ms;
  uint8_t cpu_mode;
} oomwoo_heartbeat_t;

typedef struct {
  uint8_t active;
  uint16_t reason;
} oomwoo_estop_set_t;

typedef struct {
  uint16_t fault_mask;
} oomwoo_clear_latched_fault_t;

typedef struct {
  int16_t linear_mm_s;
  int16_t angular_mrad_s;
  uint16_t duration_ms;
} oomwoo_drive_setpoint_t;

typedef struct {
  uint8_t main_brush_pct;
  uint8_t side_brush_pct;
  uint8_t fan_pct;
  uint8_t pump_pct;
} oomwoo_cleaning_motors_set_t;

typedef struct {
  uint8_t pwm_pct;
} oomwoo_lidar_motor_set_t;

typedef struct {
  uint8_t led_id;
  uint8_t mode;
  uint8_t brightness_pct;
} oomwoo_led_set_t;

typedef struct {
  uint16_t acked_sequence;
  uint16_t status;
} oomwoo_ack_t;

typedef struct {
  uint16_t rejected_sequence;
  uint16_t error_code;
} oomwoo_nack_t;

typedef struct {
  uint16_t firmware_major;
  uint16_t firmware_minor;
  uint32_t build_id;
} oomwoo_mcu_hello_t;

typedef struct {
  uint32_t timestamp_ms;
  int32_t left_ticks;
  int32_t right_ticks;
  uint8_t bumper_flags;
  uint8_t cliff_flags;
  uint8_t wheel_drop_flags;
  uint8_t dock_flags;
  uint8_t safety_latched_flags;
  uint16_t battery_mv;
} oomwoo_fast_telemetry_t;

typedef struct {
  uint16_t event;
  uint8_t active;
  uint16_t detail;
} oomwoo_safety_event_t;

typedef struct {
  uint32_t timestamp_ms;
  uint16_t active_flags;
  uint16_t latched_flags;
} oomwoo_safety_state_t;

typedef struct {
  uint16_t type;
  union {
    oomwoo_heartbeat_t heartbeat;
    oomwoo_estop_set_t estop_set;
    oomwoo_clear_latched_fault_t clear_latched_fault;
    oomwoo_drive_setpoint_t drive_setpoint;
    oomwoo_cleaning_motors_set_t cleaning_motors_set;
    oomwoo_lidar_motor_set_t lidar_motor_set;
    oomwoo_led_set_t led_set;
    oomwoo_ack_t ack;
    oomwoo_nack_t nack;
    oomwoo_mcu_hello_t mcu_hello;
    oomwoo_fast_telemetry_t fast_telemetry;
    oomwoo_safety_event_t safety_event;
    oomwoo_safety_state_t safety_state;
  } payload;
} oomwoo_message_t;

oomwoo_message_direction_t oomwoo_message_direction(uint16_t message_type);

oomwoo_message_result_t oomwoo_message_payload_size(uint16_t message_type,
                                                    uint16_t *payload_size);

oomwoo_message_result_t oomwoo_message_validate(
    const oomwoo_message_t *message);

oomwoo_message_result_t oomwoo_message_decode_payload(
    uint16_t message_type, const uint8_t *payload, uint16_t payload_length,
    oomwoo_message_t *output);

oomwoo_message_result_t oomwoo_message_encode_payload(
    const oomwoo_message_t *message, uint8_t *output, size_t output_capacity,
    uint16_t *output_length);

bool oomwoo_message_is_known_cpu_mode(uint8_t cpu_mode);

#ifdef __cplusplus
}
#endif

#endif
