#ifndef OOMWOO_CPU_WATCHDOG_H
#define OOMWOO_CPU_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Values intentionally match the draft CPU/MCU HEARTBEAT cpu_mode field.
 * Parse and validate the serial frame before calling this module.
 */
typedef enum {
  OOMWOO_CPU_MODE_DISARMED = 0,
  OOMWOO_CPU_MODE_STACK_HEALTHY = 1
} oomwoo_cpu_mode_t;

typedef enum {
  OOMWOO_CPU_STOP_NONE = 0,
  OOMWOO_CPU_STOP_BOOT = 1,
  OOMWOO_CPU_STOP_DISARMED = 2,
  OOMWOO_CPU_STOP_HEARTBEAT_TIMEOUT = 3
} oomwoo_cpu_stop_reason_t;

typedef void (*oomwoo_cpu_hard_stop_isr_fn)(
    void *context, oomwoo_cpu_stop_reason_t reason);

typedef struct {
  uint32_t timeout_ticks;
  oomwoo_cpu_hard_stop_isr_fn hard_stop_isr;
  void *hard_stop_context;
} oomwoo_cpu_watchdog_config_t;

/*
 * Statically allocated watchdog state.
 *
 * The fields are public only so firmware can allocate the object without a
 * heap. Access state through the functions below.
 */
typedef struct {
  volatile uint32_t internal_mailbox_word;
  uint32_t internal_producer_sequence;
  uint32_t internal_consumed_mailbox_word;
  uint32_t internal_last_healthy_tick;
  uint32_t internal_timeout_ticks;
  volatile uint32_t internal_state;
  volatile uint32_t internal_accepted_heartbeat_count;
  volatile uint32_t internal_timeout_count;
  volatile uint32_t internal_last_stop_reason;
  oomwoo_cpu_hard_stop_isr_fn internal_hard_stop_isr;
  void *internal_hard_stop_context;
} oomwoo_cpu_watchdog_t;

/*
 * Call once after the hard-stop GPIO/PWM path is configured and before the
 * watchdog timer IRQ is enabled. A successful init invokes hard_stop_isr with
 * OOMWOO_CPU_STOP_BOOT to establish the fail-safe output state.
 */
bool oomwoo_cpu_watchdog_init(
    oomwoo_cpu_watchdog_t *watchdog,
    const oomwoo_cpu_watchdog_config_t *config);

/*
 * Single-producer task API. The latest validated mode is transferred to the
 * ISR through one aligned 32-bit mailbox write.
 */
bool oomwoo_cpu_watchdog_submit_heartbeat(
    oomwoo_cpu_watchdog_t *watchdog, oomwoo_cpu_mode_t mode);

/*
 * Call from a fixed-rate timer ISR. now_tick is a wrapping uint32_t timer.
 * Returns true when hard_stop_isr was invoked during this call.
 */
bool oomwoo_cpu_watchdog_tick_isr(oomwoo_cpu_watchdog_t *watchdog,
                                  uint32_t now_tick);

/*
 * ISR-only diagnostic operation. An active timeout can only be cleared by a
 * fresh healthy heartbeat; its historical latch may then be acknowledged.
 */
bool oomwoo_cpu_watchdog_clear_timeout_latch_isr(
    oomwoo_cpu_watchdog_t *watchdog);

bool oomwoo_cpu_watchdog_motion_permitted(
    const oomwoo_cpu_watchdog_t *watchdog);
bool oomwoo_cpu_watchdog_timeout_active(
    const oomwoo_cpu_watchdog_t *watchdog);
bool oomwoo_cpu_watchdog_timeout_latched(
    const oomwoo_cpu_watchdog_t *watchdog);
uint32_t oomwoo_cpu_watchdog_accepted_heartbeat_count(
    const oomwoo_cpu_watchdog_t *watchdog);
uint32_t oomwoo_cpu_watchdog_timeout_count(
    const oomwoo_cpu_watchdog_t *watchdog);
oomwoo_cpu_stop_reason_t oomwoo_cpu_watchdog_last_stop_reason(
    const oomwoo_cpu_watchdog_t *watchdog);

#ifdef __cplusplus
}
#endif

#endif
