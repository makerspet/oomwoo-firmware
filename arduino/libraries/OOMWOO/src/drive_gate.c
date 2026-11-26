/* Generated copy of mcu/common/drive_gate.c (tools/sync_arduino_lib.py). Do not edit here. */
#include "drive_gate.h"

gate_out_t drive_gate_eval(const gate_inputs_t *in) {
  gate_out_t g = {0u, false, false, false};
  if (!in->armed) g.reasons |= GATE_NOT_ARMED;
  if (!in->motor_rail_on) g.reasons |= GATE_RAIL_OFF;
  if (!in->u28_alive) g.reasons |= GATE_LINK_LOST;
  if (in->power_off_requested) g.reasons |= GATE_POWER_OFF;
  if (!in->cmd_valid) g.reasons |= GATE_CMD_EXPIRED;
  if (in->wheel_drop) g.reasons |= GATE_WHEEL_DROP;
  if (in->cliff_front) g.reasons |= GATE_CLIFF_FRONT;
  if (in->cliff_rear) g.reasons |= GATE_CLIFF_REAR;
  if (in->bumper) g.reasons |= GATE_BUMPER;
  if (in->overcurrent) g.reasons |= GATE_OVERCURRENT;

  const uint16_t hard = GATE_NOT_ARMED | GATE_RAIL_OFF | GATE_LINK_LOST | GATE_POWER_OFF |
                        GATE_CMD_EXPIRED | GATE_WHEEL_DROP | GATE_OVERCURRENT;
  if ((g.reasons & hard) != 0u) return g;

  g.cleaning_motors = true;
  g.wheels_forward = (g.reasons & (GATE_CLIFF_FRONT | GATE_BUMPER)) == 0u;
  g.wheels_reverse = (g.reasons & GATE_CLIFF_REAR) == 0u;
  return g;
}

int16_t drive_gate_wheel(const gate_out_t *g, int16_t cmd) {
  if (cmd > 1000) cmd = 1000;
  if (cmd < -1000) cmd = -1000;
  if (cmd > 0 && !g->wheels_forward) return 0;
  if (cmd < 0 && !g->wheels_reverse) return 0;
  return cmd;
}
