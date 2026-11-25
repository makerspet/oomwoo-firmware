/* Host tests for the portable logic shared by U13 and U28 (mcu/common). */
#include <stdio.h>
#include <string.h>

#include "charger_sm.h"
#include "drive_gate.h"
#include "fmt.h"
#include "mcu_link.h"
#include "nec.h"
#include "oomwoo_protocol.h"
#include "power_sm.h"

static int failures;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      ++failures;                                                   \
    }                                                               \
  } while (0)

static void step_n(power_sm_t *sm, unsigned ms, int pwr, int home, int ack) {
  for (unsigned i = 0; i < ms; ++i) power_sm_step(sm, 1u, pwr != 0, home != 0, ack != 0);
}

static void test_power_sm(void) {
  power_sm_t sm;
  power_sm_init(&sm);
  CHECK(sm.hold && sm.state == MCU_LINK_PWR_BOOT);
  /* the press that turned the robot on is still held: no shutdown */
  step_n(&sm, 5000, 1, 0, 0);
  CHECK(sm.state == MCU_LINK_PWR_BOOT && sm.hold);
  step_n(&sm, POWER_SM_RELEASE_MS, 0, 0, 0);
  CHECK(sm.state == MCU_LINK_PWR_RUN);
  /* short press: nothing */
  step_n(&sm, 2000, 1, 0, 0);
  CHECK(sm.state == MCU_LINK_PWR_RUN && sm.feedback);
  step_n(&sm, 10, 0, 0, 0);
  CHECK(!sm.feedback);
  /* factory reset: power + home for 3 s, stays on */
  step_n(&sm, POWER_SM_OFF_PRESS_MS, 1, 1, 0);
  CHECK(sm.state == MCU_LINK_PWR_RUN && sm.factory_reset && sm.hold);
  step_n(&sm, 2000, 1, 1, 0); /* still held: consumed, no second action */
  CHECK(sm.state == MCU_LINK_PWR_RUN);
  power_sm_clear_factory_reset(&sm);
  step_n(&sm, 10, 0, 0, 0);
  /* 3 s press: off request, then ack -> latch released */
  step_n(&sm, POWER_SM_OFF_PRESS_MS, 1, 0, 0);
  CHECK(sm.state == MCU_LINK_PWR_OFF_REQUEST && sm.hold);
  step_n(&sm, 10, 1, 0, 1);
  CHECK(sm.state == MCU_LINK_PWR_OFF && !sm.hold);
  /* still held after off (rail kept up by the dock): must not turn back on */
  step_n(&sm, 1000, 1, 0, 0);
  CHECK(sm.state == MCU_LINK_PWR_OFF && !sm.hold);
  /* release and press again while docked: back on */
  step_n(&sm, 10, 0, 0, 0);
  step_n(&sm, POWER_SM_WAKE_PRESS_MS, 1, 0, 0);
  CHECK(sm.state == MCU_LINK_PWR_RUN && sm.hold);
  /* no ack: timeout releases the latch anyway */
  step_n(&sm, 10, 0, 0, 0);
  step_n(&sm, POWER_SM_OFF_PRESS_MS, 1, 0, 0);
  step_n(&sm, POWER_SM_ACK_TIMEOUT_MS + 1, 1, 0, 0);
  CHECK(sm.state == MCU_LINK_PWR_OFF && !sm.hold);
}

static uint8_t chg_run(charger_sm_t *sm, unsigned ms, int docked, int chrg, int stdby, uint16_t mv) {
  uint8_t s = 0;
  for (unsigned i = 0; i < ms; ++i) s = charger_sm_step(sm, 1u, docked != 0, chrg != 0, stdby != 0, mv);
  return s;
}

static void test_charger_sm(void) {
  charger_sm_t sm;
  charger_sm_init(&sm);
  CHECK(chg_run(&sm, 100, 0, 1, 0, 15000) == MCU_LINK_CHG_OFF_DOCK);
  CHECK(chg_run(&sm, 1000, 1, 1, 0, 15000) == MCU_LINK_CHG_CHARGING);
  CHECK(chg_run(&sm, 1000, 1, 0, 1, 16600) == MCU_LINK_CHG_DONE);
  CHECK(chg_run(&sm, 1000, 1, 0, 1, 15000) == MCU_LINK_CHG_FAULT); /* standby at low voltage */
  CHECK(chg_run(&sm, 1000, 1, 0, 0, 15000) == MCU_LINK_CHG_FAULT); /* both high */
  /* ~CHRG toggling at 2 Hz: no battery */
  charger_sm_init(&sm);
  uint8_t s = 0;
  for (int i = 0; i < 12; ++i) s = chg_run(&sm, 250, 1, i & 1, 0, 0);
  CHECK(s == MCU_LINK_CHG_NO_BATTERY);
  CHECK(chg_run(&sm, 10, 0, 0, 0, 0) == MCU_LINK_CHG_OFF_DOCK);
}

static void nec_send(nec_t *d, uint32_t *t, uint32_t frame) {
  nec_falling_edge(d, *t);
  *t += 13500u;
  nec_falling_edge(d, *t);
  for (int i = 0; i < 32; ++i) {
    *t += ((frame >> i) & 1u) ? 2250u : 1125u;
    nec_falling_edge(d, *t);
  }
}

static void test_nec(void) {
  nec_t d;
  nec_init(&d);
  uint32_t t = 0xFFFF0000u; /* across the 32-bit wrap */
  const uint32_t frame = 0x00u | (0xFFu << 8) | (0x2Au << 16) | ((uint32_t)(uint8_t)~0x2Au << 24);
  nec_send(&d, &t, frame);
  CHECK(d.frames == 1u && d.frame == frame);
  uint16_t a = 0;
  uint8_t c = 0;
  CHECK(nec_frame_valid(d.frame, &a, &c) && a == 0x00u && c == 0x2Au);
  t += 40000u; /* gap, then a repeat code */
  nec_falling_edge(&d, t);
  t += 11250u;
  nec_falling_edge(&d, t);
  CHECK(d.repeats == 1u);
  CHECK(!nec_frame_valid(0x2A2AFF00u, NULL, NULL)); /* command byte not followed by its inverse */
}

static void test_gate(void) {
  gate_inputs_t in;
  memset(&in, 0, sizeof(in));
  gate_out_t g = drive_gate_eval(&in);
  CHECK(g.reasons & GATE_NOT_ARMED);
  CHECK(!g.wheels_forward && !g.wheels_reverse && !g.cleaning_motors);
  in.armed = in.motor_rail_on = in.u28_alive = in.cmd_valid = true;
  g = drive_gate_eval(&in);
  CHECK(g.reasons == 0u && g.wheels_forward && g.wheels_reverse && g.cleaning_motors);
  in.bumper = true;
  g = drive_gate_eval(&in);
  CHECK(!g.wheels_forward && g.wheels_reverse && g.cleaning_motors);
  CHECK(drive_gate_wheel(&g, 500) == 0 && drive_gate_wheel(&g, -500) == -500);
  in.bumper = false;
  in.cliff_rear = true;
  g = drive_gate_eval(&in);
  CHECK(g.wheels_forward && !g.wheels_reverse);
  in.cliff_rear = false;
  in.wheel_drop = true;
  g = drive_gate_eval(&in);
  CHECK(!g.wheels_forward && !g.wheels_reverse && !g.cleaning_motors);
  CHECK(drive_gate_wheel(&g, 2000) == 0);
  in.wheel_drop = false;
  g = drive_gate_eval(&in);
  CHECK(drive_gate_wheel(&g, 2000) == 1000 && drive_gate_wheel(&g, -2000) == -1000);
}

static void test_link(void) {
  mcu_link_status_t s, r;
  memset(&s, 0, sizeof(s));
  s.uptime_ms = 0x12345678u;
  s.vbat_mv = 16123u;
  s.dc_in_mv = 20111u;
  s.vbus_mv = 5050u;
  s.mcu_temp_c10 = -45;
  s.buttons = MCU_LINK_BTN_HOME;
  s.flags = MCU_LINK_ST_DOCKED;
  s.power = MCU_LINK_PWR_RUN;
  s.charge = MCU_LINK_CHG_CHARGING;
  s.usb = MCU_LINK_USB_SOURCING;
  s.fw_version = 7;
  uint8_t buf[MCU_LINK_STATUS_SIZE];
  mcu_link_status_pack(&s, buf);
  CHECK(mcu_link_status_unpack(buf, sizeof(buf), &r));
  CHECK(memcmp(&s, &r, sizeof(s)) == 0);
  CHECK(!mcu_link_status_unpack(buf, sizeof(buf) - 1u, &r));
  buf[24] = 9; /* bad power state */
  CHECK(!mcu_link_status_unpack(buf, sizeof(buf), &r));

  mcu_link_control_t c = {MCU_LINK_CTL_CAM_RUN | MCU_LINK_CTL_SENSORS_ON, 40, MCU_LINK_LED_BREATHE,
                          MCU_LINK_LED_ON, MCU_LINK_USB_SOURCE_CHARGE, 200};
  mcu_link_control_t rc;
  uint8_t cb[MCU_LINK_CONTROL_SIZE];
  mcu_link_control_pack(&c, cb);
  CHECK(mcu_link_control_unpack(cb, sizeof(cb), &rc) && memcmp(&c, &rc, sizeof(c)) == 0);
  cb[1] = 101; /* torch over 100 % */
  CHECK(!mcu_link_control_unpack(cb, sizeof(cb), &rc));

  /* through the real framing */
  uint8_t frame[64];
  size_t len = 0;
  mcu_link_control_pack(&c, cb);
  CHECK(oomwoo_encode_frame(MCU_LINK_MSG_CONTROL, cb, MCU_LINK_CONTROL_SIZE, 1u, 0u, frame,
                            sizeof(frame), &len) == OOMWOO_PROTOCOL_OK);
  oomwoo_decoded_frame_t f;
  CHECK(oomwoo_decode_frame(frame, len, &f) == OOMWOO_PROTOCOL_OK);
  CHECK(f.message_type == MCU_LINK_MSG_CONTROL && f.payload_length == MCU_LINK_CONTROL_SIZE);
}

static void test_fmt(void) {
  char b[64];
  fmt_snprintf(b, sizeof(b), "%d|%5u|%-4s|%04x|%ld|%c|%%", -42, 7u, "ab", 0xBEu, -2147483647L - 1, 'z');
  CHECK(strcmp(b, "-42|    7|ab  |00be|-2147483648|z|%") == 0);
  fmt_snprintf(b, 4, "%s", "truncate");
  CHECK(strcmp(b, "tru") == 0);
}

int main(void) {
  test_power_sm();
  test_charger_sm();
  test_nec();
  test_gate();
  test_link();
  test_fmt();
  if (failures != 0) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("mcu logic tests passed\n");
  return 0;
}
