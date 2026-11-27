// U28 safety core: runs from a 1 kHz hardware-timer interrupt, so a sketch
// that blocks, delay()s or crashes cannot stop it. Changes here need review
// (power latch, charge current, lines into the sensor rail).
//
//  - Power latch: PWR-HOLD high first thing; the power-button state machine
//    (3 s = off request, + Home = factory reset) drops it only after U13
//    acknowledges, or after a 500 ms timeout.
//  - Outputs into sensor-rail parts (camera reset, VL53L0X XSHUT) and the
//    2.2 A charge current stay off unless U13 has been heard in the last
//    500 ms and asks for them.
//  - Watchdog: fed here only while loop() is also alive, so a stuck sketch
//    resets U28 (R172 holds the power latch through the reset).
#include "u28.h"

volatile U28Analog g_analog;

static HardwareTimer *s_timer;
static power_sm_t s_pwr;
static charger_sm_t s_chg;
static volatile uint32_t s_loop_ms;

static const uint16_t DOCKED_MV = 15000; // DC-IN-20V; the dock's probe is ~1.5 V

static void tick() {
  const uint32_t now = millis();
  const bool alive = link_alive();
  const mcu_link_control_t c = link_control_from_isr();
  const bool sensors_on = alive && (c.flags & MCU_LINK_CTL_SENSORS_ON);

  power_sm_step(&s_pwr, 1, digitalRead(PIN_MCU2_PWR_BTN_SENSE) == LOW,
                digitalRead(PIN_MCU2_HOME_BTN_N) == LOW,
                alive && (c.flags & MCU_LINK_CTL_RAILS_OFF));
  digitalWrite(PIN_MCU2_PWR_HOLD, s_pwr.hold ? HIGH : LOW);

  const bool docked = g_analog.dc_in_mv > DOCKED_MV;
  charger_sm_step(&s_chg, 1, docked, digitalRead(PIN_MCU2_CHRG_N) == LOW,
                  digitalRead(PIN_MCU2_STDBY_N) == LOW, g_analog.vbat_mv);

  digitalWrite(PIN_MCU2_CAM_RESET, sensors_on && (c.flags & MCU_LINK_CTL_CAM_RUN) ? HIGH : LOW);
  digitalWrite(PIN_MCU2_SIDE_PROXI_CE_R,
               sensors_on && (c.flags & MCU_LINK_CTL_SIDE_TOF_ON) ? HIGH : LOW);
  digitalWrite(PIN_MCU2_CHG_I_HI, docked && alive && (c.flags & MCU_LINK_CTL_CHG_I_HI) ? HIGH : LOW);

  if (now - s_loop_ms < 100) IWDG_ReloadCounter();
}

void safety_begin() {
  // First: hold the power latch (R172 holds it only until here).
  digitalWrite(PIN_MCU2_PWR_HOLD, HIGH);
  pinMode(PIN_MCU2_PWR_HOLD, OUTPUT);

  // Everything else off.
  const uint32_t low_outputs[] = {PIN_MCU2_TORCH, PIN_MCU2_CAM_RESET, PIN_MCU2_SIDE_PROXI_CE_R,
                                  PIN_MCU2_USB_PWR_EN, PIN_MCU2_CHG_I_HI, PIN_MCU2_LED_HOME,
                                  PIN_MCU2_LED_PWR, PIN_MCU2_LED_CLEAN};
  for (uint32_t p : low_outputs) {
    digitalWrite(p, LOW);
    pinMode(p, OUTPUT);
  }
  digitalWrite(PIN_MCU2_CC_RD_EN, HIGH); // Rd on: the phone sees a sink
  pinMode(PIN_MCU2_CC_RD_EN, OUTPUT);
  pinMode(PIN_MCU2_CLEAN_BTN_N, INPUT_PULLUP);
  pinMode(PIN_MCU2_HOME_BTN_N, INPUT_PULLUP);
#ifdef OOMWOO_EVT
  pinMode(PIN_MCU2_PWR_BTN_SENSE, INPUT_PULLUP); // evaluation board: no R173
#else
  pinMode(PIN_MCU2_PWR_BTN_SENSE, INPUT);         // R173 pull-up
#endif
  pinMode(PIN_MCU2_HALL_BIN, INPUT_PULLUP);
  pinMode(PIN_MCU2_CHRG_N, INPUT_PULLUP);
  pinMode(PIN_MCU2_STDBY_N, INPUT_PULLUP);

  power_sm_init(&s_pwr);
  charger_sm_init(&s_chg);
  s_loop_ms = millis();

  // Independent watchdog: ~260 ms, frozen while a debugger halts the core.
  DBGMCU_Config(DBGMCU_IWDG_STOP, ENABLE);
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_32);
  IWDG_SetReload(325);
  IWDG_ReloadCounter();
  IWDG_Enable();

  s_timer = new HardwareTimer(TIM2);
  s_timer->setOverflow(1000, HERTZ_FORMAT);
  s_timer->attachInterrupt(tick);
  s_timer->resume();
}

void safety_loop_alive() { s_loop_ms = millis(); }
uint8_t safety_power_state() { return s_pwr.state; }
uint8_t safety_charge_state() { return s_chg.state; }
bool safety_factory_reset() { return s_pwr.factory_reset; }
bool safety_docked() { return g_analog.dc_in_mv > DOCKED_MV; }
bool safety_long_press() { return s_pwr.feedback || s_pwr.state == MCU_LINK_PWR_OFF_REQUEST; }
