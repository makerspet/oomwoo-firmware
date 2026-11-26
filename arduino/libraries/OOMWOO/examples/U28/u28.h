// Shared declarations for the U28 sketch tabs.
#pragma once

#include <Arduino.h>
#include <OOMWOO.h>

// Slow analog readings, refreshed by loop() (analogRead is too slow for an
// interrupt). Millivolts at the board net, after the dividers.
struct U28Analog {
  uint16_t vbat_mv;   // BAT_SENSE x 11
  uint16_t dc_in_mv;  // DC_IN_SENSE x 11
  uint16_t vbus_mv;   // USB_VBUS_SENSE x 147/47
  uint16_t ntc_raw, id_raw, ldr_raw, floor_l_raw, floor_r_raw;
};
extern volatile U28Analog g_analog;

// ---- safety.cpp: runs from a 1 kHz timer interrupt, not from loop() ----
void safety_begin();          // safe pins (PWR-HOLD first), 1 kHz timer, watchdog
void safety_loop_alive();     // call once per loop(); a stalled loop() resets U28
uint8_t safety_power_state(); // mcu_link_power_t
uint8_t safety_charge_state();// mcu_link_charge_t
bool safety_factory_reset();
bool safety_docked();
bool safety_long_press();     // power button held > 1 s: show it on the LEDs

// ---- link.cpp: the board link to U13 on Serial (USART2) ----
void link_begin();
void link_poll();             // parse incoming CONTROL frames
void link_send_status(uint8_t usb_state);
bool link_alive();            // CONTROL heard in the last 500 ms
mcu_link_control_t link_control(); // last CONTROL (zeros if never heard), from loop()
mcu_link_control_t link_control_from_isr(); // the same, from the safety tick

// ---- typec.cpp: USB-C baseline roles ----
void typec_begin();
uint8_t typec_step(uint8_t mode, uint32_t vbus_mv); // call every ~1 ms; returns mcu_link_usb_state_t
