/* Generated copy of mcu/common/dock_sm.c (tools/sync_arduino_lib.py). Do not edit here. */
#include "dock_sm.h"

#include <string.h>

#include "dock_beacon.h"

#define PROBE_R_OHM 100000u   /* R1014 */
#define DIVIDER_OHM 1100000u  /* R1010 + R1011, the dock's own DOCK-SENSE load */
#define CONNECT_CHECK_MS 50u /* the readings are filtered (~8 ms) */
#define CONNECT_MARGIN_MV 1500u
#define CONTACT_DROP_MV 2000u
#define CONTACT_DROP_MS 5u
#define CHARGED_RESET_MS 60000u

void dock_sm_init(dock_sm_t *sm) {
  memset(sm, 0, sizeof(*sm));
  sm->p.vin_min_mv = 18500u;
  sm->p.vin_max_mv = 22000u;
  sm->p.r_min_ohm = 90000u;
  sm->p.r_max_ohm = 180000u;
  sm->p.settle_min_ms = 300u;
  sm->p.stable_ms = 1000u;
  sm->p.idle_ma = 5u;
  sm->p.idle_ms = 100u;
  sm->p.max_ma = 3200u;
  sm->p.bleed_mv = 3000u;
  sm->p.bleed_max_ms = 3000u;
  sm->p.fault_backoff_ms = 10000u;
  sm->p.fault_backoff_long_ms = 60000u;
  sm->p.power_ok_ms = 500u;
  sm->state = DOCK_NO_POWER;
  sm->r_ohm = UINT32_MAX;
}

uint32_t dock_probe_ohms(uint32_t vdd_mv, uint32_t vprobe_mv, uint32_t vdock_mv) {
  if (vprobe_mv >= vdd_mv) return UINT32_MAX;
  /* currents in nA: 1 mV across 100k = 10 nA */
  const uint64_t i_probe = (uint64_t)(vdd_mv - vprobe_mv) * 1000000u / PROBE_R_OHM;
  const uint64_t i_div = (uint64_t)vdock_mv * 1000000u / DIVIDER_OHM;
  if (i_probe <= i_div) return UINT32_MAX;
  const uint64_t r = (uint64_t)vdock_mv * 1000000u / (i_probe - i_div); /* mV / nA = MOhm */
  return r > 0xFFFFFFFEu ? 0xFFFFFFFEu : (uint32_t)r;
}

uint32_t dock_budget_w(const dock_in_t *in) {
  if (!in->pd_contract) return 0u;
  const uint32_t w = (20u * in->pd_limit_ma) / 1000u;
  return w > 1u ? w - 1u : 0u;
}

const char *dock_state_name(uint8_t s) {
  static const char *const n[] = {"no_power", "probing", "connecting", "charging", "bleed", "fault"};
  return s <= DOCK_FAULT ? n[s] : "?";
}

static void enter(dock_sm_t *sm, uint8_t state) {
  sm->state = state;
  sm->state_ms = 0u;
  sm->below_ms = sm->in_ms = 0u;
  sm->rose = false;
  sm->idle_cnt_ms = 0u;
}

static void fault(dock_sm_t *sm, uint8_t why) {
  sm->fault = why;
  if (sm->consecutive_faults < 255u) ++sm->consecutive_faults;
  enter(sm, DOCK_FAULT);
}

static bool power_ok(const dock_sm_t *sm, const dock_in_t *in) {
  return in->pd_contract && in->vin_mv >= sm->p.vin_min_mv && in->vin_mv <= sm->p.vin_max_mv;
}

void dock_sm_step(dock_sm_t *sm, uint32_t dt_ms, const dock_in_t *in, dock_out_t *out) {
  sm->state_ms += dt_ms;
  const bool pwr = power_ok(sm, in);
  sm->ok_ms = pwr ? sm->ok_ms + dt_ms : 0u;

  /* Losing the charger ends everything, whatever the state. */
  if (!pwr && sm->state != DOCK_NO_POWER && sm->state != DOCK_FAULT) enter(sm, DOCK_NO_POWER);

  switch (sm->state) {
    case DOCK_NO_POWER:
      /* via BLEED: the contacts may still be charged if the charger dropped mid-charge */
      if (sm->ok_ms >= sm->p.power_ok_ms) enter(sm, DOCK_BLEED);
      break;

    case DOCK_PROBING: {
      const uint32_t r = dock_probe_ohms(in->vdd_mv, in->vprobe_mv, in->vdock_mv);
      sm->r_ohm = r;
      /* A robot's ~30 uF input capacitance makes the reading drift into the
       * window over seconds: from below when its capacitors start empty, from
       * above when they are still charged (after a charge cut). A plain
       * resistor jumps from open straight into the window and is refused.
       * below_ms counts time spent "near" the window (below it, or up to 4x
       * above it) right before entering. */
      if (r >= sm->p.r_min_ohm && r <= sm->p.r_max_ohm) {
        if (sm->below_ms >= sm->p.settle_min_ms) sm->rose = true;
        sm->below_ms = 0u;
        sm->in_ms += dt_ms;
      } else if (r < sm->p.r_min_ohm || r <= 4u * sm->p.r_max_ohm) {
        sm->below_ms += dt_ms;
        sm->in_ms = 0u;
        sm->rose = false;
      } else {
        sm->below_ms = sm->in_ms = 0u;
        sm->rose = false;
      }
      if (sm->rose && sm->in_ms >= sm->p.stable_ms) enter(sm, DOCK_CONNECTING);
      break;
    }

    case DOCK_CONNECTING:
      if (in->trip) {
        fault(sm, DOCK_FAULT_OVERCURRENT);
      } else if (in->vdock_mv + CONNECT_MARGIN_MV >= in->vin_mv) {
        enter(sm, DOCK_CHARGING);
      } else if (sm->state_ms >= CONNECT_CHECK_MS) {
        fault(sm, DOCK_FAULT_NO_RISE);
      }
      break;

    case DOCK_CHARGING: {
      uint32_t max_ma = sm->p.max_ma;
      const uint32_t pd = (uint32_t)in->pd_limit_ma + in->pd_limit_ma / 10u;
      if (in->pd_limit_ma != 0u && pd < max_ma) max_ma = pd;
      sm->charged_ms += dt_ms;
      if (sm->charged_ms >= CHARGED_RESET_MS) sm->consecutive_faults = 0u;
      if (in->trip || in->i_ma > (int32_t)max_ma) {
        fault(sm, DOCK_FAULT_OVERCURRENT);
        break;
      }
      if (in->vdock_mv + CONTACT_DROP_MV < in->vin_mv) {
        sm->below_ms += dt_ms;
        if (sm->below_ms >= CONTACT_DROP_MS) {
          fault(sm, DOCK_FAULT_CONTACT);
          break;
        }
      } else {
        sm->below_ms = 0u;
      }
      /* The robot keeps its MCU powered while it charges, so ~0 mA means it left. */
      sm->idle_cnt_ms = in->i_ma < (int32_t)sm->p.idle_ma ? sm->idle_cnt_ms + dt_ms : 0u;
      if (sm->idle_cnt_ms >= sm->p.idle_ms) enter(sm, DOCK_BLEED);
      break;
    }

    case DOCK_BLEED:
      if (in->vdock_mv <= sm->p.bleed_mv || sm->state_ms >= sm->p.bleed_max_ms) {
        sm->charged_ms = 0u;
        enter(sm, DOCK_PROBING);
      }
      break;

    case DOCK_FAULT:
    default: {
      const uint32_t wait =
          sm->consecutive_faults >= 3u ? sm->p.fault_backoff_long_ms : sm->p.fault_backoff_ms;
      if (sm->state_ms >= wait) {
        sm->charged_ms = 0u;
        enter(sm, pwr ? DOCK_BLEED : DOCK_NO_POWER);
      }
      break;
    }
  }

  out->charge_en = sm->state == DOCK_CONNECTING || sm->state == DOCK_CHARGING;
  out->probe_drv = sm->state == DOCK_PROBING;
  switch (sm->state) {
    case DOCK_NO_POWER: out->led = DOCK_LED_DOUBLE_BLINK; break;
    case DOCK_PROBING: out->led = DOCK_LED_BLIP; break;
    case DOCK_CHARGING: out->led = DOCK_LED_ON; break;
    case DOCK_FAULT: out->led = DOCK_LED_FAST; break;
    default: out->led = DOCK_LED_OFF; break;
  }
  const bool offering = pwr && sm->state != DOCK_FAULT;
  out->budget_code = offering ? dock_beacon_budget_code(dock_budget_w(in)) : 0u;
}
