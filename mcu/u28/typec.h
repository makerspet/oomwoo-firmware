/* USB-C baseline on U28: Type-C resistors only, no PD messages.
 *  SINK_DATA: Rd on (CC-RD-EN high, R236/R237 via Q42/Q43), no Rp, robot 5 V
 *    off. The phone sees a device, powers VBUS and enumerates U13 (USB HS).
 *  SOURCE_CHARGE: Rd off, Rp 1.5 A (180 uA) on both CC pins; once the phone's
 *    Rd is seen on one CC for 150 ms and VBUS is ~0 V, USB_PWR_EN turns the
 *    robot's 5 V onto VBUS. No data in this mode.
 * Switching modes is a detach and re-attach. The PD power-role swap (bonus)
 * builds on the WCH USBPD examples later. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

void typec_init(void);
/* Call every 1 ms with the requested mode (mcu_link_usb_mode_t) and VBUS in mV.
 * Returns mcu_link_usb_state_t. */
uint8_t typec_step(uint8_t mode, uint32_t vbus_mv);
uint8_t typec_cc_seen(void); /* 0 none, 1 CC1, 2 CC2 (source mode) */
