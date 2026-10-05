#ifndef OOMWOO_CPU_WATCHDOG_BRIDGE_H
#define OOMWOO_CPU_WATCHDOG_BRIDGE_H

#include "oomwoo_cpu_ingress.h"
#include "oomwoo_cpu_watchdog.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  OOMWOO_CPU_WATCHDOG_BRIDGE_IGNORED = 0,
  OOMWOO_CPU_WATCHDOG_BRIDGE_SUBMITTED,
  OOMWOO_CPU_WATCHDOG_BRIDGE_REJECTED
} oomwoo_cpu_watchdog_bridge_result_t;

typedef struct {
  oomwoo_cpu_watchdog_t *watchdog;
  uint32_t submitted_heartbeats;
  uint32_t ignored_messages;
  uint32_t submission_failures;
} oomwoo_cpu_watchdog_bridge_t;

bool oomwoo_cpu_watchdog_bridge_init(
    oomwoo_cpu_watchdog_bridge_t *bridge,
    oomwoo_cpu_watchdog_t *watchdog);

/*
 * Call only with a message accepted by oomwoo_cpu_ingress. Non-heartbeat
 * messages are counted and ignored; they cannot refresh the deadline.
 */
oomwoo_cpu_watchdog_bridge_result_t
oomwoo_cpu_watchdog_bridge_handle_message(
    oomwoo_cpu_watchdog_bridge_t *bridge,
    const oomwoo_message_t *message);

/*
 * Direct ingress callback for watchdog-only consumers and tests. A dispatcher
 * with additional command handlers should call handle_message as one fan-out
 * step so ignored messages still reach their owning subsystem.
 */
void oomwoo_cpu_watchdog_bridge_ingress_callback(
    const oomwoo_decoded_frame_t *frame,
    const oomwoo_message_t *message, void *context);

#ifdef __cplusplus
}
#endif

#endif
