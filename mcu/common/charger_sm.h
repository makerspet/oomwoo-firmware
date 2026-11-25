/* SLM6900 charge status decoding (U28: ~CHRG PB4, ~STDBY PB3, open drain,
 * internal pull-ups). Pure logic, host-tested. Rules from the PCB SPEC:
 *  - decode only while docked (DC-IN-20V present);
 *  - ~CHRG low, ~STDBY high: charging;
 *  - ~STDBY low: done if the battery reads >= ~16.4 V, otherwise a fault
 *    (over-voltage, shorted battery, battery too hot or cold);
 *  - both high: fault;
 *  - ~CHRG toggling at 1-4 Hz: no battery. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHARGER_SM_SETTLE_MS 500u   /* ignore the pins right after docking */
#define CHARGER_SM_STABLE_MS 200u   /* a pin state must hold this long */
#define CHARGER_SM_WINDOW_MS 2000u  /* ~CHRG edge-count window */
#define CHARGER_SM_TOGGLE_EDGES 3u  /* edges per window that mean "no battery" */
#define CHARGER_SM_DONE_MV 16400u

typedef struct {
  uint8_t state; /* mcu_link_charge_t */
  bool last_chrg_low;
  bool last_stdby_low;
  uint32_t docked_ms;
  uint32_t stable_ms;
  uint32_t window_ms;
  uint8_t edges;
  uint8_t prev_window_edges;
} charger_sm_t;

void charger_sm_init(charger_sm_t *sm);
uint8_t charger_sm_step(charger_sm_t *sm, uint32_t dt_ms, bool docked, bool chrg_low,
                        bool stdby_low, uint16_t vbat_mv);

#ifdef __cplusplus
}
#endif
