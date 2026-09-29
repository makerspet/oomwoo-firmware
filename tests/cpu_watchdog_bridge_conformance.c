#include "oomwoo_cpu_watchdog_bridge.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  uint32_t calls;
  oomwoo_cpu_stop_reason_t last_reason;
} stop_probe_t;

typedef struct {
  oomwoo_cpu_watchdog_t watchdog;
  oomwoo_cpu_watchdog_bridge_t bridge;
  oomwoo_cpu_ingress_t ingress;
  stop_probe_t stop_probe;
} fixture_t;

static void record_stop(void *context, oomwoo_cpu_stop_reason_t reason) {
  stop_probe_t *probe = (stop_probe_t *)context;

  ++probe->calls;
  probe->last_reason = reason;
}

static void fixture_init(fixture_t *fixture) {
  const oomwoo_cpu_watchdog_config_t config = {
      .timeout_ticks = OOMWOO_CPU_WATCHDOG_INITIAL_TIMEOUT_TICKS_1KHZ,
      .hard_stop_isr = record_stop,
      .hard_stop_context = &fixture->stop_probe,
  };

  memset(fixture, 0, sizeof(*fixture));
  assert(oomwoo_cpu_watchdog_init(&fixture->watchdog, &config));
  assert(oomwoo_cpu_watchdog_bridge_init(&fixture->bridge,
                                         &fixture->watchdog));
  oomwoo_cpu_ingress_init(
      &fixture->ingress,
      oomwoo_cpu_watchdog_bridge_ingress_callback,
      &fixture->bridge);
}

static size_t feed_typed_message(fixture_t *fixture,
                                 const oomwoo_message_t *message,
                                 uint16_t sequence) {
  uint8_t payload[OOMWOO_PROTOCOL_MAX_PAYLOAD_SIZE];
  uint8_t frame[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  uint16_t payload_length = 0U;
  size_t frame_length = 0U;

  assert(oomwoo_message_encode_payload(message, payload, sizeof(payload),
                                       &payload_length) == OOMWOO_MESSAGE_OK);
  assert(oomwoo_encode_frame(message->type, payload, payload_length, sequence,
                             0U, frame, sizeof(frame), &frame_length) ==
         OOMWOO_PROTOCOL_OK);
  return oomwoo_cpu_ingress_feed(&fixture->ingress, frame, frame_length);
}

static size_t feed_raw_heartbeat(fixture_t *fixture, uint8_t mode,
                                 uint16_t sequence, bool corrupt_crc) {
  static const uint8_t timestamp[] = {0x78U, 0x56U, 0x34U, 0x12U};
  uint8_t payload[5U];
  uint8_t frame[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  size_t frame_length = 0U;

  memcpy(payload, timestamp, sizeof(timestamp));
  payload[4] = mode;
  assert(oomwoo_encode_frame(OOMWOO_MESSAGE_HEARTBEAT, payload,
                             (uint16_t)sizeof(payload), sequence, 0U, frame,
                             sizeof(frame), &frame_length) ==
         OOMWOO_PROTOCOL_OK);
  if (corrupt_crc) {
    frame[frame_length - 1U] ^= UINT8_C(0x01);
  }
  return oomwoo_cpu_ingress_feed(&fixture->ingress, frame, frame_length);
}

static void test_valid_heartbeat_is_the_only_deadline_refresh(void) {
  fixture_t fixture;
  oomwoo_message_t heartbeat;
  oomwoo_message_t identify;

  fixture_init(&fixture);
  memset(&heartbeat, 0, sizeof(heartbeat));
  heartbeat.type = OOMWOO_MESSAGE_HEARTBEAT;
  heartbeat.payload.heartbeat.cpu_time_ms = UINT32_C(0x12345678);
  heartbeat.payload.heartbeat.cpu_mode = OOMWOO_CPU_MODE_STACK_HEALTHY;
  assert(feed_typed_message(&fixture, &heartbeat, 1U) == 1U);
  assert(fixture.bridge.submitted_heartbeats == 1U);
  assert(!oomwoo_cpu_watchdog_tick_isr(&fixture.watchdog, 100U));
  assert(oomwoo_cpu_watchdog_motion_permitted(&fixture.watchdog));

  memset(&identify, 0, sizeof(identify));
  identify.type = OOMWOO_MESSAGE_IDENTIFY_REQUEST;
  assert(feed_typed_message(&fixture, &identify, 2U) == 1U);
  assert(fixture.bridge.ignored_messages == 1U);
  assert(!oomwoo_cpu_watchdog_tick_isr(&fixture.watchdog, 249U));
  assert(oomwoo_cpu_watchdog_tick_isr(&fixture.watchdog, 250U));
  assert(!oomwoo_cpu_watchdog_motion_permitted(&fixture.watchdog));
  assert(fixture.stop_probe.last_reason ==
         OOMWOO_CPU_STOP_HEARTBEAT_TIMEOUT);
}

static void test_corrupt_and_invalid_frames_never_refresh(void) {
  fixture_t fixture;
  oomwoo_message_t drive;

  fixture_init(&fixture);
  assert(feed_raw_heartbeat(&fixture, OOMWOO_CPU_MODE_STACK_HEALTHY, 1U,
                            false) == 1U);
  assert(!oomwoo_cpu_watchdog_tick_isr(&fixture.watchdog, 10U));

  assert(feed_raw_heartbeat(&fixture, OOMWOO_CPU_MODE_STACK_HEALTHY, 2U,
                            true) == 0U);
  assert(feed_raw_heartbeat(&fixture, UINT8_C(2), 3U, false) == 0U);

  memset(&drive, 0, sizeof(drive));
  drive.type = OOMWOO_MESSAGE_DRIVE_SETPOINT;
  drive.payload.drive_setpoint.linear_mm_s = 100;
  drive.payload.drive_setpoint.duration_ms = 100U;
  assert(feed_typed_message(&fixture, &drive, 4U) == 1U);

  assert(fixture.ingress.decoder.stats.crc_errors == 1U);
  assert(fixture.ingress.stats.value_out_of_range == 1U);
  assert(fixture.bridge.submitted_heartbeats == 1U);
  assert(fixture.bridge.ignored_messages == 1U);
  assert(!oomwoo_cpu_watchdog_tick_isr(&fixture.watchdog, 159U));
  assert(oomwoo_cpu_watchdog_tick_isr(&fixture.watchdog, 160U));
  assert(fixture.stop_probe.last_reason ==
         OOMWOO_CPU_STOP_HEARTBEAT_TIMEOUT);
}

static void test_disarmed_frame_forces_stop(void) {
  fixture_t fixture;

  fixture_init(&fixture);
  assert(feed_raw_heartbeat(&fixture, OOMWOO_CPU_MODE_STACK_HEALTHY, 1U,
                            false) == 1U);
  assert(!oomwoo_cpu_watchdog_tick_isr(&fixture.watchdog, 50U));
  assert(oomwoo_cpu_watchdog_motion_permitted(&fixture.watchdog));

  assert(feed_raw_heartbeat(&fixture, OOMWOO_CPU_MODE_DISARMED, 2U,
                            false) == 1U);
  assert(oomwoo_cpu_watchdog_tick_isr(&fixture.watchdog, 51U));
  assert(!oomwoo_cpu_watchdog_motion_permitted(&fixture.watchdog));
  assert(fixture.stop_probe.last_reason == OOMWOO_CPU_STOP_DISARMED);
}

static void test_fragmented_frame_and_direct_fail_closed(void) {
  fixture_t fixture;
  oomwoo_cpu_watchdog_bridge_t unbound_bridge;
  oomwoo_message_t invalid_heartbeat;
  uint8_t payload[] = {0x78U, 0x56U, 0x34U, 0x12U,
                       OOMWOO_CPU_MODE_STACK_HEALTHY};
  uint8_t frame[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
  size_t frame_length = 0U;
  size_t index;

  fixture_init(&fixture);
  assert(oomwoo_encode_frame(OOMWOO_MESSAGE_HEARTBEAT, payload,
                             (uint16_t)sizeof(payload), 1U, 0U, frame,
                             sizeof(frame), &frame_length) ==
         OOMWOO_PROTOCOL_OK);
  for (index = 0U; index < frame_length; ++index) {
    const size_t accepted =
        oomwoo_cpu_ingress_feed(&fixture.ingress, &frame[index], 1U);
    assert(accepted == (index + 1U == frame_length ? 1U : 0U));
  }
  assert(fixture.bridge.submitted_heartbeats == 1U);

  memset(&invalid_heartbeat, 0, sizeof(invalid_heartbeat));
  invalid_heartbeat.type = OOMWOO_MESSAGE_HEARTBEAT;
  invalid_heartbeat.payload.heartbeat.cpu_mode = UINT8_C(2);
  assert(oomwoo_cpu_watchdog_bridge_handle_message(
             &fixture.bridge, &invalid_heartbeat) ==
         OOMWOO_CPU_WATCHDOG_BRIDGE_REJECTED);
  assert(fixture.bridge.submission_failures == 1U);
  assert(oomwoo_cpu_watchdog_bridge_handle_message(&fixture.bridge, NULL) ==
         OOMWOO_CPU_WATCHDOG_BRIDGE_REJECTED);
  assert(fixture.bridge.submission_failures == 2U);
  oomwoo_cpu_watchdog_bridge_ingress_callback(NULL, NULL, NULL);

  assert(!oomwoo_cpu_watchdog_bridge_init(NULL, &fixture.watchdog));
  assert(!oomwoo_cpu_watchdog_bridge_init(&unbound_bridge, NULL));
  invalid_heartbeat.payload.heartbeat.cpu_mode =
      OOMWOO_CPU_MODE_STACK_HEALTHY;
  assert(oomwoo_cpu_watchdog_bridge_handle_message(
             &unbound_bridge, &invalid_heartbeat) ==
         OOMWOO_CPU_WATCHDOG_BRIDGE_REJECTED);
  assert(unbound_bridge.submission_failures == 1U);
}

int main(void) {
  test_valid_heartbeat_is_the_only_deadline_refresh();
  test_corrupt_and_invalid_frames_never_refresh();
  test_disarmed_frame_forces_stop();
  test_fragmented_frame_and_direct_fail_closed();
  (void)printf("OOMWOO watchdog ingress bridge: 4 tests PASS\n");
  return 0;
}
