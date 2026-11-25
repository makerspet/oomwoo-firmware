/* U28 power latch and power button state machine. Pure logic, host-tested.
 *
 * Hardware facts (oomwoo-pcb, sheet MCU/POWER):
 *  - The button (or dock power) turns the MCU rail on; R172 holds the latch
 *    while U28 boots, then U28 drives PWR-HOLD (PB13) high.
 *  - PWR-BTN-SENSE (PA12) is low while the button is pressed (D26 + R173).
 *  - A hardware timer drops the MCU rail after ~3.5-9.5 s of holding the
 *    button, whatever the firmware does: decide at 3 s and give LED feedback
 *    before that.
 *  - While docked the charger keeps the MCU rail on: "off" then means rails
 *    off and U13 idle, with PWR-HOLD low. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POWER_SM_RELEASE_MS 50u    /* button must be released this long after boot */
#define POWER_SM_OFF_PRESS_MS 3000u
#define POWER_SM_FEEDBACK_MS 1000u /* start fast-blinking the power LED */
#define POWER_SM_ACK_TIMEOUT_MS 500u
#define POWER_SM_WAKE_PRESS_MS 100u

typedef struct {
  uint8_t state;      /* mcu_link_power_t */
  bool hold;          /* PWR-HOLD output level */
  bool factory_reset; /* latched until power_sm_clear_factory_reset() */
  bool feedback;      /* long press in progress: show it on the LEDs */
  bool press_consumed;
  uint32_t press_ms;
  uint32_t release_ms;
  uint32_t state_ms;
} power_sm_t;

void power_sm_init(power_sm_t *sm);
void power_sm_step(power_sm_t *sm, uint32_t dt_ms, bool power_pressed, bool home_pressed,
                   bool rails_off_ack);
void power_sm_clear_factory_reset(power_sm_t *sm);

#ifdef __cplusplus
}
#endif
