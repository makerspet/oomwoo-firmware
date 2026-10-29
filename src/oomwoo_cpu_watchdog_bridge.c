#include "oomwoo_cpu_watchdog_bridge.h"

bool oomwoo_cpu_watchdog_bridge_init(
    oomwoo_cpu_watchdog_bridge_t *bridge,
    oomwoo_cpu_watchdog_t *watchdog) {
  if (bridge == NULL) {
    return false;
  }

  bridge->watchdog = watchdog;
  bridge->submitted_heartbeats = 0U;
  bridge->ignored_messages = 0U;
  bridge->submission_failures = 0U;
  return watchdog != NULL;
}

oomwoo_cpu_watchdog_bridge_result_t
oomwoo_cpu_watchdog_bridge_handle_message(
    oomwoo_cpu_watchdog_bridge_t *bridge,
    const oomwoo_message_t *message) {
  if (bridge == NULL) {
    return OOMWOO_CPU_WATCHDOG_BRIDGE_REJECTED;
  }
  if (message == NULL) {
    ++bridge->submission_failures;
    return OOMWOO_CPU_WATCHDOG_BRIDGE_REJECTED;
  }
  if (message->type != OOMWOO_MESSAGE_HEARTBEAT) {
    ++bridge->ignored_messages;
    return OOMWOO_CPU_WATCHDOG_BRIDGE_IGNORED;
  }
  if (bridge->watchdog == NULL ||
      !oomwoo_cpu_watchdog_submit_heartbeat(
          bridge->watchdog,
          (oomwoo_cpu_mode_t)message->payload.heartbeat.cpu_mode)) {
    ++bridge->submission_failures;
    return OOMWOO_CPU_WATCHDOG_BRIDGE_REJECTED;
  }

  ++bridge->submitted_heartbeats;
  return OOMWOO_CPU_WATCHDOG_BRIDGE_SUBMITTED;
}

void oomwoo_cpu_watchdog_bridge_ingress_callback(
    const oomwoo_decoded_frame_t *frame,
    const oomwoo_message_t *message, void *context) {
  (void)frame;
  (void)oomwoo_cpu_watchdog_bridge_handle_message(
      (oomwoo_cpu_watchdog_bridge_t *)context, message);
}
