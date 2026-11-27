// OOMWOO main board, U28 (CH32L103C8T6): the power/UI companion MCU.
//
// Board: "OOMWOO main board U28" (see arduino/README.md). U28 handles the
// power button and latch, the clean/home/power buttons and their LEDs, the charger
// status, the slow analog inputs, the torch, USB-C roles, and the link to U13.
//
// The parts that keep the robot safe run from a timer interrupt in safety.cpp,
// so loop() is yours to change: LED patterns, extra uses for the spare pins
// (PB12, PC15, PD0, PD1), new readings to report. If loop() stops
// running for 100 ms, the watchdog resets U28.
#include "u28.h"

// ---------------------------------------------------------------- LEDs
// Brightness 0-255. Edit these to change how the robot looks.
static uint8_t breathe(uint32_t ms) {
  const uint32_t t = ms % 2000;
  const uint32_t tri = t < 1000 ? t : 2000 - t; // 0..1000
  return (uint8_t)(tri * tri * 255 / 1000000);
}
static uint8_t blink(uint32_t ms, uint32_t period) { return (ms % period) < period / 2 ? 255 : 0; }

static uint8_t pattern(uint8_t p, uint32_t ms) {
  switch (p) {
    case MCU_LINK_LED_OFF: return 0;
    case MCU_LINK_LED_ON: return 255;
    case MCU_LINK_LED_BLINK_SLOW: return blink(ms, 1000);
    case MCU_LINK_LED_BLINK_FAST: return blink(ms, 200);
    case MCU_LINK_LED_BREATHE: return breathe(ms);
    default: return 0;
  }
}

static void update_leds(uint32_t now) {
  uint8_t home = MCU_LINK_LED_OFF;
  uint8_t power = MCU_LINK_LED_ON;
  uint8_t clean = digitalRead(PIN_MCU2_CLEAN_BTN_N) == LOW ? MCU_LINK_LED_ON : MCU_LINK_LED_OFF; // press feedback
  const uint8_t pwr = safety_power_state();
  if (pwr == MCU_LINK_PWR_OFF) power = MCU_LINK_LED_OFF;
  if (!link_alive() && pwr == MCU_LINK_PWR_RUN) power = MCU_LINK_LED_BLINK_SLOW; // U13 silent
  switch (safety_charge_state()) {
    case MCU_LINK_CHG_CHARGING: home = MCU_LINK_LED_BREATHE; break;
    case MCU_LINK_CHG_DONE: home = MCU_LINK_LED_ON; break;
    case MCU_LINK_CHG_FAULT:
    case MCU_LINK_CHG_NO_BATTERY: home = MCU_LINK_LED_BLINK_FAST; break;
    default: break;
  }
  const mcu_link_control_t c = link_control();
  if (link_alive() && pwr == MCU_LINK_PWR_RUN) { // U13 (or the phone) may override
    if (c.led_home != MCU_LINK_LED_AUTO) home = c.led_home;
    if (c.led_power != MCU_LINK_LED_AUTO) power = c.led_power;
    if (c.led_clean != MCU_LINK_LED_AUTO) clean = c.led_clean;
  }
  if (safety_long_press()) power = MCU_LINK_LED_BLINK_FAST; // "let go now"
  analogWrite(PIN_MCU2_LED_HOME, pattern(home, now));
  analogWrite(PIN_MCU2_LED_PWR, pattern(power, now));
  analogWrite(PIN_MCU2_LED_CLEAN, pattern(clean, now));
}

// ---------------------------------------------------------------- analog
static uint32_t mv(uint32_t pin) { return (uint32_t)analogRead(pin) * 3300u / 4095u; }

static void read_analog() {
  g_analog.vbat_mv = (uint16_t)(mv(PIN_MCU2_BAT_SENSE) * 11);
  g_analog.dc_in_mv = (uint16_t)(mv(PIN_MCU2_DC_IN_SENSE) * 11);
  g_analog.vbus_mv = (uint16_t)(mv(PIN_MCU2_USB_VBUS_SENSE) * 147 / 47);
  g_analog.ntc_raw = analogRead(PIN_MCU2_BAT_NTC);
  g_analog.id_raw = analogRead(PIN_MCU2_BATTERY_CHARGER_ID);
  g_analog.ldr_raw = analogRead(PIN_MCU2_LDR);
  g_analog.floor_l_raw = analogRead(PIN_MCU2_HALL_FLOOR_L);
  g_analog.floor_r_raw = analogRead(PIN_MCU2_HALL_FLOOR_R);
}

// ---------------------------------------------------------------- main
void setup() {
  safety_begin(); // power latch first, then the safe pin states and the 1 kHz tick
  analogReadResolution(12);
  analogWriteFrequency(25000); // LEDs and torch; fast enough not to band the camera
  link_begin();
  typec_begin();
}

void loop() {
  static uint32_t last_ms;
  static uint8_t usb_state;
  safety_loop_alive();
  link_poll();

  const uint32_t now = millis();
  if (now == last_ms) return;
  last_ms = now;

  // Once per millisecond
  const mcu_link_control_t c = link_control();
  const bool sensors_on = link_alive() && (c.flags & MCU_LINK_CTL_SENSORS_ON);
  usb_state = typec_step(link_alive() ? c.usb_mode : (uint8_t)MCU_LINK_USB_SINK_DATA, g_analog.vbus_mv);
  // The torch LEDs run from VSENS-5V-USB: only while U13 says that rail is up.
  static int torch = -1;
  const int t = sensors_on ? c.torch_pct * 255 / 100 : 0;
  if (t != torch) analogWrite(PIN_MCU2_TORCH, torch = t);

  if (now % 10 == 0) read_analog();
  if (now % 10 == 5) link_send_status(usb_state); // offset from U13's CONTROL slot
  if (now % 20 == 0) update_leds(now);
}
