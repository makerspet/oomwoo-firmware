#include "oomwoo_cpu_watchdog.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                       \
      (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,  \
                    #condition);                                              \
      return false;                                                           \
    }                                                                         \
  } while (0)

typedef struct {
  uint32_t count;
  oomwoo_cpu_stop_reason_t last_reason;
} stop_probe_t;

static void record_stop(void *context, oomwoo_cpu_stop_reason_t reason) {
  stop_probe_t *probe = (stop_probe_t *)context;
  probe->count++;
  probe->last_reason = reason;
}

static bool init_watchdog(oomwoo_cpu_watchdog_t *watchdog,
                          stop_probe_t *probe, uint32_t timeout_ticks) {
  const oomwoo_cpu_watchdog_config_t config = {
      .timeout_ticks = timeout_ticks,
      .hard_stop_isr = record_stop,
      .hard_stop_context = probe,
  };
  return oomwoo_cpu_watchdog_init(watchdog, &config);
}

static bool test_init_fails_safe(void) {
  oomwoo_cpu_watchdog_t watchdog;
  stop_probe_t probe = {0U, OOMWOO_CPU_STOP_NONE};
  oomwoo_cpu_watchdog_config_t config = {
      .timeout_ticks = 150U,
      .hard_stop_isr = record_stop,
      .hard_stop_context = &probe,
  };

  CHECK(!oomwoo_cpu_watchdog_init(NULL, &config));
  CHECK(!oomwoo_cpu_watchdog_init(&watchdog, NULL));
  config.hard_stop_isr = NULL;
  CHECK(!oomwoo_cpu_watchdog_init(&watchdog, &config));
  config.hard_stop_isr = record_stop;
  config.timeout_ticks = 0U;
  CHECK(!oomwoo_cpu_watchdog_init(&watchdog, &config));
  config.timeout_ticks = UINT32_C(0x80000000);
  CHECK(!oomwoo_cpu_watchdog_init(&watchdog, &config));
  CHECK(probe.count == 0U);

  CHECK(init_watchdog(&watchdog, &probe, 150U));
  CHECK(probe.count == 1U);
  CHECK(probe.last_reason == OOMWOO_CPU_STOP_BOOT);
  CHECK(!oomwoo_cpu_watchdog_motion_permitted(&watchdog));
  CHECK(!oomwoo_cpu_watchdog_timeout_active(&watchdog));
  CHECK(!oomwoo_cpu_watchdog_timeout_latched(&watchdog));
  CHECK(oomwoo_cpu_watchdog_last_stop_reason(&watchdog) ==
        OOMWOO_CPU_STOP_BOOT);
  return true;
}

static bool test_timeout_boundary_and_single_stop(void) {
  oomwoo_cpu_watchdog_t watchdog;
  stop_probe_t probe = {0U, OOMWOO_CPU_STOP_NONE};

  CHECK(init_watchdog(&watchdog, &probe, 150U));
  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_STACK_HEALTHY));
  CHECK(!oomwoo_cpu_watchdog_motion_permitted(&watchdog));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, 100U));
  CHECK(oomwoo_cpu_watchdog_motion_permitted(&watchdog));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, 249U));
  CHECK(oomwoo_cpu_watchdog_tick_isr(&watchdog, 250U));
  CHECK(!oomwoo_cpu_watchdog_motion_permitted(&watchdog));
  CHECK(oomwoo_cpu_watchdog_timeout_active(&watchdog));
  CHECK(oomwoo_cpu_watchdog_timeout_latched(&watchdog));
  CHECK(oomwoo_cpu_watchdog_timeout_count(&watchdog) == 1U);
  CHECK(probe.count == 2U);
  CHECK(probe.last_reason == OOMWOO_CPU_STOP_HEARTBEAT_TIMEOUT);

  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, 251U));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, 1000U));
  CHECK(oomwoo_cpu_watchdog_timeout_count(&watchdog) == 1U);
  CHECK(probe.count == 2U);
  return true;
}

static bool test_recovery_requires_fresh_heartbeat(void) {
  oomwoo_cpu_watchdog_t watchdog;
  stop_probe_t probe = {0U, OOMWOO_CPU_STOP_NONE};

  CHECK(init_watchdog(&watchdog, &probe, 10U));
  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_STACK_HEALTHY));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, 10U));
  CHECK(oomwoo_cpu_watchdog_tick_isr(&watchdog, 20U));
  CHECK(!oomwoo_cpu_watchdog_clear_timeout_latch_isr(&watchdog));

  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_STACK_HEALTHY));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, 21U));
  CHECK(oomwoo_cpu_watchdog_motion_permitted(&watchdog));
  CHECK(!oomwoo_cpu_watchdog_timeout_active(&watchdog));
  CHECK(oomwoo_cpu_watchdog_timeout_latched(&watchdog));
  CHECK(oomwoo_cpu_watchdog_clear_timeout_latch_isr(&watchdog));
  CHECK(!oomwoo_cpu_watchdog_timeout_latched(&watchdog));
  CHECK(oomwoo_cpu_watchdog_accepted_heartbeat_count(&watchdog) == 2U);
  return true;
}

static bool test_fresh_heartbeat_wins_at_deadline(void) {
  oomwoo_cpu_watchdog_t watchdog;
  stop_probe_t probe = {0U, OOMWOO_CPU_STOP_NONE};

  CHECK(init_watchdog(&watchdog, &probe, 10U));
  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_STACK_HEALTHY));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, 100U));

  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_STACK_HEALTHY));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, 110U));
  CHECK(oomwoo_cpu_watchdog_motion_permitted(&watchdog));
  CHECK(oomwoo_cpu_watchdog_timeout_count(&watchdog) == 0U);
  CHECK(probe.count == 1U);

  CHECK(oomwoo_cpu_watchdog_tick_isr(&watchdog, 120U));
  CHECK(oomwoo_cpu_watchdog_timeout_count(&watchdog) == 1U);
  return true;
}

static bool test_disarmed_mode_forces_stop(void) {
  oomwoo_cpu_watchdog_t watchdog;
  stop_probe_t probe = {0U, OOMWOO_CPU_STOP_NONE};

  CHECK(init_watchdog(&watchdog, &probe, 150U));
  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_STACK_HEALTHY));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, 0U));
  CHECK(oomwoo_cpu_watchdog_motion_permitted(&watchdog));

  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_DISARMED));
  CHECK(oomwoo_cpu_watchdog_tick_isr(&watchdog, 1U));
  CHECK(!oomwoo_cpu_watchdog_motion_permitted(&watchdog));
  CHECK(probe.count == 2U);
  CHECK(probe.last_reason == OOMWOO_CPU_STOP_DISARMED);

  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_DISARMED));
  CHECK(oomwoo_cpu_watchdog_tick_isr(&watchdog, 2U));
  CHECK(probe.count == 3U);
  return true;
}

static bool test_latest_mailbox_value_wins(void) {
  oomwoo_cpu_watchdog_t watchdog;
  stop_probe_t probe = {0U, OOMWOO_CPU_STOP_NONE};

  CHECK(init_watchdog(&watchdog, &probe, 150U));
  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_STACK_HEALTHY));
  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_DISARMED));
  CHECK(oomwoo_cpu_watchdog_tick_isr(&watchdog, 5U));
  CHECK(!oomwoo_cpu_watchdog_motion_permitted(&watchdog));
  CHECK(oomwoo_cpu_watchdog_accepted_heartbeat_count(&watchdog) == 0U);
  CHECK(probe.last_reason == OOMWOO_CPU_STOP_DISARMED);

  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_DISARMED));
  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_STACK_HEALTHY));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, 6U));
  CHECK(oomwoo_cpu_watchdog_motion_permitted(&watchdog));
  CHECK(oomwoo_cpu_watchdog_accepted_heartbeat_count(&watchdog) == 1U);

  CHECK(!oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, (oomwoo_cpu_mode_t)2));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, 7U));
  return true;
}

static bool test_mailbox_sequence_wraparound(void) {
  oomwoo_cpu_watchdog_t watchdog;
  stop_probe_t probe = {0U, OOMWOO_CPU_STOP_NONE};

  CHECK(init_watchdog(&watchdog, &probe, 150U));
  watchdog.internal_producer_sequence = UINT32_C(0x7ffffffe);

  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_STACK_HEALTHY));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, 0U));
  CHECK(watchdog.internal_mailbox_word == UINT32_MAX);
  CHECK(oomwoo_cpu_watchdog_motion_permitted(&watchdog));

  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_DISARMED));
  CHECK(watchdog.internal_mailbox_word == 0U);
  CHECK(oomwoo_cpu_watchdog_tick_isr(&watchdog, 1U));
  CHECK(!oomwoo_cpu_watchdog_motion_permitted(&watchdog));
  return true;
}

static bool test_tick_wraparound(void) {
  oomwoo_cpu_watchdog_t watchdog;
  stop_probe_t probe = {0U, OOMWOO_CPU_STOP_NONE};
  const uint32_t start = UINT32_MAX - 50U;

  CHECK(init_watchdog(&watchdog, &probe, 150U));
  CHECK(oomwoo_cpu_watchdog_submit_heartbeat(
      &watchdog, OOMWOO_CPU_MODE_STACK_HEALTHY));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog, start));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(&watchdog,
                                      (uint32_t)(start + 149U)));
  CHECK(oomwoo_cpu_watchdog_tick_isr(&watchdog,
                                     (uint32_t)(start + 150U)));
  CHECK(oomwoo_cpu_watchdog_timeout_count(&watchdog) == 1U);
  return true;
}

static bool test_null_queries_are_fail_safe(void) {
  CHECK(!oomwoo_cpu_watchdog_submit_heartbeat(
      NULL, OOMWOO_CPU_MODE_STACK_HEALTHY));
  CHECK(!oomwoo_cpu_watchdog_tick_isr(NULL, 0U));
  CHECK(!oomwoo_cpu_watchdog_clear_timeout_latch_isr(NULL));
  CHECK(!oomwoo_cpu_watchdog_motion_permitted(NULL));
  CHECK(!oomwoo_cpu_watchdog_timeout_active(NULL));
  CHECK(!oomwoo_cpu_watchdog_timeout_latched(NULL));
  CHECK(oomwoo_cpu_watchdog_accepted_heartbeat_count(NULL) == 0U);
  CHECK(oomwoo_cpu_watchdog_timeout_count(NULL) == 0U);
  CHECK(oomwoo_cpu_watchdog_last_stop_reason(NULL) ==
        OOMWOO_CPU_STOP_NONE);
  return true;
}

typedef bool (*test_fn_t)(void);

int main(void) {
  static const test_fn_t tests[] = {
      test_init_fails_safe,
      test_timeout_boundary_and_single_stop,
      test_recovery_requires_fresh_heartbeat,
      test_fresh_heartbeat_wins_at_deadline,
      test_disarmed_mode_forces_stop,
      test_latest_mailbox_value_wins,
      test_mailbox_sequence_wraparound,
      test_tick_wraparound,
      test_null_queries_are_fail_safe,
  };
  uint32_t index;

  for (index = 0U; index < (uint32_t)(sizeof(tests) / sizeof(tests[0]));
       index++) {
    if (!tests[index]()) {
      return 1;
    }
  }

  (void)printf("%u watchdog tests passed\n", index);
  return 0;
}
