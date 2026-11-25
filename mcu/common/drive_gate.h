/* U13 motion gate: decides, every control tick, which motors may run.
 * Pure logic, host-tested. Policy (starter, to be reviewed with the hazard note):
 *  - nothing runs unless armed, the motor rail is on, U28 is alive and the
 *    last motion command has not expired;
 *  - a wheel drop stops everything (robot lifted: brushes and fan too);
 *  - a front cliff or a bumper blocks forward wheel motion only, so the
 *    robot can still back away; a rear cliff blocks reverse;
 *  - a latched overcurrent stops that motor until re-armed. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  GATE_NOT_ARMED = 1u << 0,
  GATE_RAIL_OFF = 1u << 1,
  GATE_LINK_LOST = 1u << 2,
  GATE_CMD_EXPIRED = 1u << 3,
  GATE_WHEEL_DROP = 1u << 4,
  GATE_CLIFF_FRONT = 1u << 5,
  GATE_CLIFF_REAR = 1u << 6,
  GATE_BUMPER = 1u << 7,
  GATE_OVERCURRENT = 1u << 8,
  GATE_POWER_OFF = 1u << 9,
};

typedef struct {
  bool armed;
  bool motor_rail_on;
  bool u28_alive;
  bool power_off_requested;
  bool cmd_valid;    /* a motion command exists and has not expired */
  bool wheel_drop;   /* either wheel */
  bool cliff_front;  /* any of LF, RF, LM, RM */
  bool cliff_rear;   /* LR, RR */
  bool bumper;       /* either bumper */
  bool overcurrent;  /* latched */
} gate_inputs_t;

typedef struct {
  uint16_t reasons;      /* GATE_* bits currently active */
  bool wheels_forward;   /* positive wheel commands allowed */
  bool wheels_reverse;   /* negative wheel commands allowed */
  bool cleaning_motors;  /* brushes and fan */
} gate_out_t;

gate_out_t drive_gate_eval(const gate_inputs_t *in);
/* Clamp one wheel command (-1000..1000) through the gate. */
int16_t drive_gate_wheel(const gate_out_t *g, int16_t cmd);

#ifdef __cplusplus
}
#endif
