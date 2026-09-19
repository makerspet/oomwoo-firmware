#include "oomwoo_cpu_watchdog.h"

#include <limits.h>
#include <stddef.h>

#define OOMWOO_WATCHDOG_STATE_INITIALIZED (UINT32_C(1) << 0)
#define OOMWOO_WATCHDOG_STATE_ARMED (UINT32_C(1) << 1)
#define OOMWOO_WATCHDOG_STATE_TIMEOUT_ACTIVE (UINT32_C(1) << 2)
#define OOMWOO_WATCHDOG_STATE_TIMEOUT_LATCHED (UINT32_C(1) << 3)

#define OOMWOO_WATCHDOG_MODE_MASK UINT32_C(1)
#define OOMWOO_WATCHDOG_SEQUENCE_MASK UINT32_C(0x7fffffff)

_Static_assert(
    (offsetof(oomwoo_cpu_watchdog_t, internal_mailbox_word) %
     sizeof(uint32_t)) == 0,
    "watchdog mailbox must be naturally aligned");

static bool watchdog_is_initialized(
    const oomwoo_cpu_watchdog_t *watchdog) {
  return watchdog != NULL &&
         (watchdog->internal_state & OOMWOO_WATCHDOG_STATE_INITIALIZED) != 0U;
}

static void invoke_hard_stop(oomwoo_cpu_watchdog_t *watchdog,
                             oomwoo_cpu_stop_reason_t reason,
                             uint32_t state) {
  watchdog->internal_state = state & ~OOMWOO_WATCHDOG_STATE_ARMED;
  watchdog->internal_last_stop_reason = (uint32_t)reason;
  watchdog->internal_hard_stop_isr(watchdog->internal_hard_stop_context,
                                   reason);
}

bool oomwoo_cpu_watchdog_init(
    oomwoo_cpu_watchdog_t *watchdog,
    const oomwoo_cpu_watchdog_config_t *config) {
  if (watchdog == NULL || config == NULL ||
      config->hard_stop_isr == NULL || config->timeout_ticks == 0U ||
      config->timeout_ticks > (UINT32_MAX / 2U)) {
    return false;
  }

  watchdog->internal_mailbox_word = 0U;
  watchdog->internal_producer_sequence = 0U;
  watchdog->internal_consumed_mailbox_word = 0U;
  watchdog->internal_last_healthy_tick = 0U;
  watchdog->internal_timeout_ticks = config->timeout_ticks;
  watchdog->internal_state = OOMWOO_WATCHDOG_STATE_INITIALIZED;
  watchdog->internal_accepted_heartbeat_count = 0U;
  watchdog->internal_timeout_count = 0U;
  watchdog->internal_last_stop_reason = (uint32_t)OOMWOO_CPU_STOP_BOOT;
  watchdog->internal_hard_stop_isr = config->hard_stop_isr;
  watchdog->internal_hard_stop_context = config->hard_stop_context;

  watchdog->internal_hard_stop_isr(watchdog->internal_hard_stop_context,
                                   OOMWOO_CPU_STOP_BOOT);
  return true;
}

bool oomwoo_cpu_watchdog_submit_heartbeat(
    oomwoo_cpu_watchdog_t *watchdog, oomwoo_cpu_mode_t mode) {
  uint32_t sequence;

  if (!watchdog_is_initialized(watchdog) ||
      (mode != OOMWOO_CPU_MODE_DISARMED &&
       mode != OOMWOO_CPU_MODE_STACK_HEALTHY)) {
    return false;
  }

  sequence = (watchdog->internal_producer_sequence + 1U) &
             OOMWOO_WATCHDOG_SEQUENCE_MASK;
  watchdog->internal_producer_sequence = sequence;
  watchdog->internal_mailbox_word =
      (sequence << 1) | ((uint32_t)mode & OOMWOO_WATCHDOG_MODE_MASK);
  return true;
}

bool oomwoo_cpu_watchdog_tick_isr(oomwoo_cpu_watchdog_t *watchdog,
                                  uint32_t now_tick) {
  uint32_t mailbox_word;
  uint32_t state;
  bool stopped = false;

  if (!watchdog_is_initialized(watchdog)) {
    return false;
  }

  state = watchdog->internal_state;
  mailbox_word = watchdog->internal_mailbox_word;
  if (mailbox_word != watchdog->internal_consumed_mailbox_word) {
    watchdog->internal_consumed_mailbox_word = mailbox_word;

    if ((mailbox_word & OOMWOO_WATCHDOG_MODE_MASK) != 0U) {
      watchdog->internal_last_healthy_tick = now_tick;
      watchdog->internal_accepted_heartbeat_count++;
      state |= OOMWOO_WATCHDOG_STATE_ARMED;
      state &= ~OOMWOO_WATCHDOG_STATE_TIMEOUT_ACTIVE;
      watchdog->internal_state = state;
    } else {
      invoke_hard_stop(watchdog, OOMWOO_CPU_STOP_DISARMED, state);
      state = watchdog->internal_state;
      stopped = true;
    }
  }

  if ((state & OOMWOO_WATCHDOG_STATE_ARMED) != 0U &&
      (uint32_t)(now_tick - watchdog->internal_last_healthy_tick) >=
          watchdog->internal_timeout_ticks) {
    state |= OOMWOO_WATCHDOG_STATE_TIMEOUT_ACTIVE |
             OOMWOO_WATCHDOG_STATE_TIMEOUT_LATCHED;
    watchdog->internal_timeout_count++;
    invoke_hard_stop(watchdog, OOMWOO_CPU_STOP_HEARTBEAT_TIMEOUT, state);
    stopped = true;
  }

  return stopped;
}

bool oomwoo_cpu_watchdog_clear_timeout_latch_isr(
    oomwoo_cpu_watchdog_t *watchdog) {
  uint32_t state;

  if (!watchdog_is_initialized(watchdog)) {
    return false;
  }

  state = watchdog->internal_state;
  if ((state & OOMWOO_WATCHDOG_STATE_TIMEOUT_ACTIVE) != 0U) {
    return false;
  }

  watchdog->internal_state =
      state & ~OOMWOO_WATCHDOG_STATE_TIMEOUT_LATCHED;
  return true;
}

bool oomwoo_cpu_watchdog_motion_permitted(
    const oomwoo_cpu_watchdog_t *watchdog) {
  return watchdog_is_initialized(watchdog) &&
         (watchdog->internal_state & OOMWOO_WATCHDOG_STATE_ARMED) != 0U;
}

bool oomwoo_cpu_watchdog_timeout_active(
    const oomwoo_cpu_watchdog_t *watchdog) {
  return watchdog_is_initialized(watchdog) &&
         (watchdog->internal_state &
          OOMWOO_WATCHDOG_STATE_TIMEOUT_ACTIVE) != 0U;
}

bool oomwoo_cpu_watchdog_timeout_latched(
    const oomwoo_cpu_watchdog_t *watchdog) {
  return watchdog_is_initialized(watchdog) &&
         (watchdog->internal_state &
          OOMWOO_WATCHDOG_STATE_TIMEOUT_LATCHED) != 0U;
}

uint32_t oomwoo_cpu_watchdog_accepted_heartbeat_count(
    const oomwoo_cpu_watchdog_t *watchdog) {
  if (!watchdog_is_initialized(watchdog)) {
    return 0U;
  }
  return watchdog->internal_accepted_heartbeat_count;
}

uint32_t oomwoo_cpu_watchdog_timeout_count(
    const oomwoo_cpu_watchdog_t *watchdog) {
  if (!watchdog_is_initialized(watchdog)) {
    return 0U;
  }
  return watchdog->internal_timeout_count;
}

oomwoo_cpu_stop_reason_t oomwoo_cpu_watchdog_last_stop_reason(
    const oomwoo_cpu_watchdog_t *watchdog) {
  if (!watchdog_is_initialized(watchdog)) {
    return OOMWOO_CPU_STOP_NONE;
  }
  return (oomwoo_cpu_stop_reason_t)watchdog->internal_last_stop_reason;
}
