#include "charger_sm.h"

#include <string.h>

#include "mcu_link.h"

void charger_sm_init(charger_sm_t *sm) {
  memset(sm, 0, sizeof(*sm));
  sm->state = MCU_LINK_CHG_OFF_DOCK;
}

uint8_t charger_sm_step(charger_sm_t *sm, uint32_t dt_ms, bool docked, bool chrg_low,
                        bool stdby_low, uint16_t vbat_mv) {
  if (!docked) {
    charger_sm_init(sm);
    return sm->state;
  }
  sm->docked_ms += dt_ms;

  if (chrg_low != sm->last_chrg_low) ++sm->edges;
  if (chrg_low != sm->last_chrg_low || stdby_low != sm->last_stdby_low) {
    sm->stable_ms = 0u;
  } else {
    sm->stable_ms += dt_ms;
  }
  sm->last_chrg_low = chrg_low;
  sm->last_stdby_low = stdby_low;

  sm->window_ms += dt_ms;
  if (sm->window_ms >= CHARGER_SM_WINDOW_MS) {
    sm->prev_window_edges = sm->edges;
    sm->edges = 0u;
    sm->window_ms = 0u;
  }

  if (sm->docked_ms < CHARGER_SM_SETTLE_MS) {
    sm->state = MCU_LINK_CHG_CHARGING; /* provisional until the pins settle */
    return sm->state;
  }
  if (sm->prev_window_edges >= CHARGER_SM_TOGGLE_EDGES || sm->edges >= CHARGER_SM_TOGGLE_EDGES) {
    sm->state = MCU_LINK_CHG_NO_BATTERY;
    return sm->state;
  }
  if (sm->stable_ms < CHARGER_SM_STABLE_MS) return sm->state;

  if (stdby_low) {
    sm->state = (vbat_mv >= CHARGER_SM_DONE_MV) ? MCU_LINK_CHG_DONE : MCU_LINK_CHG_FAULT;
  } else if (chrg_low) {
    sm->state = MCU_LINK_CHG_CHARGING;
  } else {
    sm->state = MCU_LINK_CHG_FAULT;
  }
  return sm->state;
}
