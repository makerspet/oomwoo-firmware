/* Charging dock state machine (CH32V003, kicad/charging-dock). Pure logic,
 * host-tested; the hardware layer (mcu/dock) feeds it filtered readings every
 * millisecond and applies its outputs.
 *
 *  NO_POWER    charger missing, not at 20 V, or no PD contract: contacts off
 *  PROBING     PROBE-DRV high; measure the resistance across the contacts and
 *              wait for the robot's signature: ~130k, reached gradually as its
 *              ~30 uF input capacitance charges or discharges
 *  CONNECTING  CHARGE-EN high; VDOCK-20V must follow VIN within 50 ms
 *  CHARGING    off when the current stops (robot left) or on overcurrent
 *  BLEED       contacts off; wait for VDOCK-20V to fall before probing again
 *  FAULT       contacts off; retry after a back-off */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  DOCK_NO_POWER = 0,
  DOCK_PROBING,
  DOCK_CONNECTING,
  DOCK_CHARGING,
  DOCK_BLEED,
  DOCK_FAULT,
} dock_state_t;

typedef enum {
  DOCK_FAULT_NONE = 0,
  DOCK_FAULT_NO_RISE,     /* VDOCK-20V did not follow VIN: short or switch failure */
  DOCK_FAULT_OVERCURRENT,
  DOCK_FAULT_CONTACT,     /* VDOCK-20V collapsed while charging */
} dock_fault_t;

typedef enum {
  DOCK_LED_OFF = 0,
  DOCK_LED_ON,
  DOCK_LED_BLIP,         /* short flash every 2 s: waiting for a robot */
  DOCK_LED_DOUBLE_BLINK, /* charger unsuitable */
  DOCK_LED_FAST,         /* fault */
} dock_led_t;

typedef struct {
  uint32_t vin_min_mv, vin_max_mv;   /* 18500, 22000 */
  uint32_t r_min_ohm, r_max_ohm;     /* signature window, 90k-180k */
  uint32_t settle_min_ms;            /* time spent near the window before entering it */
  uint32_t stable_ms;                /* time inside the window before connecting */
  uint32_t idle_ma, idle_ms;         /* off when below idle_ma for idle_ms */
  uint32_t max_ma;                   /* overcurrent trip (also limited to the PD limit + 10 %) */
  uint32_t bleed_mv, bleed_max_ms;
  uint32_t fault_backoff_ms, fault_backoff_long_ms;
  uint32_t power_ok_ms;              /* VIN/PD good this long before probing */
} dock_params_t;

typedef struct {
  uint32_t vin_mv;    /* VIN-SENSE x 11 */
  uint32_t vdock_mv;  /* DOCK-SENSE x 11 */
  uint32_t vprobe_mv; /* PROBE-SENSE */
  uint32_t vdd_mv;    /* the probe drive level (PROBE-DRV high = VDD) */
  int32_t i_ma;       /* I-SENSE minus its zero offset */
  bool pd_contract;   /* CH224A 0x09 bit 3 */
  uint16_t pd_limit_ma; /* CH224A 0x50 x 50 mA */
  bool trip;          /* the hardware overcurrent watchdog fired */
} dock_in_t;

typedef struct {
  bool charge_en;
  bool probe_drv;
  uint8_t led;        /* dock_led_t */
  uint8_t budget_code; /* beacon command byte (dock_beacon.h) */
} dock_out_t;

typedef struct {
  dock_params_t p;
  uint8_t state;
  uint8_t fault;
  uint8_t consecutive_faults;
  uint32_t state_ms;
  uint32_t ok_ms;
  uint32_t below_ms, in_ms;
  bool rose;          /* approached the window from below for >= settle_min_ms */
  uint32_t idle_cnt_ms;
  uint32_t r_ohm;     /* last probe result, for logs */
  uint32_t charged_ms;
} dock_sm_t;

void dock_sm_init(dock_sm_t *sm);
void dock_sm_step(dock_sm_t *sm, uint32_t dt_ms, const dock_in_t *in, dock_out_t *out);
/* Resistance across the contacts from the probe readings; UINT32_MAX if open. */
uint32_t dock_probe_ohms(uint32_t vdd_mv, uint32_t vprobe_mv, uint32_t vdock_mv);
/* Watts at the contacts the dock can offer: 20 V x PD limit, minus ~1 W for itself. */
uint32_t dock_budget_w(const dock_in_t *in);
const char *dock_state_name(uint8_t s);

#ifdef __cplusplus
}
#endif
