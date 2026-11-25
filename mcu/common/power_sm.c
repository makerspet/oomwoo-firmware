#include "power_sm.h"

#include <string.h>

#include "mcu_link.h"

static void enter(power_sm_t *sm, uint8_t state) {
  sm->state = state;
  sm->state_ms = 0u;
}

void power_sm_init(power_sm_t *sm) {
  memset(sm, 0, sizeof(*sm));
  sm->hold = true;
  enter(sm, MCU_LINK_PWR_BOOT);
}

void power_sm_clear_factory_reset(power_sm_t *sm) { sm->factory_reset = false; }

void power_sm_step(power_sm_t *sm, uint32_t dt_ms, bool power_pressed, bool home_pressed,
                   bool rails_off_ack) {
  sm->state_ms += dt_ms;
  if (power_pressed) {
    sm->press_ms += dt_ms;
    sm->release_ms = 0u;
  } else {
    sm->press_ms = 0u;
    sm->release_ms += dt_ms;
    sm->press_consumed = false;
  }
  sm->feedback = false;

  switch (sm->state) {
    case MCU_LINK_PWR_BOOT:
      /* The press that turned the robot on must end before it counts again. */
      sm->hold = true;
      if (sm->release_ms >= POWER_SM_RELEASE_MS) enter(sm, MCU_LINK_PWR_RUN);
      break;

    case MCU_LINK_PWR_RUN:
      sm->hold = true;
      if (power_pressed && !sm->press_consumed) {
        sm->feedback = sm->press_ms >= POWER_SM_FEEDBACK_MS;
        if (sm->press_ms >= POWER_SM_OFF_PRESS_MS) {
          sm->press_consumed = true;
          if (home_pressed) {
            sm->factory_reset = true; /* and stay on */
          } else {
            enter(sm, MCU_LINK_PWR_OFF_REQUEST);
          }
        }
      }
      break;

    case MCU_LINK_PWR_OFF_REQUEST:
      sm->hold = true;
      if (rails_off_ack || sm->state_ms >= POWER_SM_ACK_TIMEOUT_MS) {
        sm->hold = false;
        enter(sm, MCU_LINK_PWR_OFF);
      }
      break;

    case MCU_LINK_PWR_OFF:
    default:
      /* Never release PWR-HOLD to an input while shutting down: keep it low.
       * We only get here with the rail still up when the dock holds it on. */
      sm->hold = false;
      if (power_pressed && !sm->press_consumed && sm->press_ms >= POWER_SM_WAKE_PRESS_MS) {
        sm->press_consumed = true;
        sm->hold = true;
        enter(sm, MCU_LINK_PWR_RUN);
      }
      break;
  }
}
