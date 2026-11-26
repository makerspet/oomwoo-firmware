// USB-C baseline on U28 (Type-C resistors only, no PD messages):
//  SINK_DATA: Rd on (CC-RD-EN high), no Rp, no robot 5 V. The phone powers
//    VBUS and enumerates U13 over USB high speed.
//  SOURCE_CHARGE: Rd off, Rp 1.5 A on both CC pins; after the phone's Rd has
//    been seen for 150 ms with VBUS at ~0 V, USB_PWR_EN puts the robot's
//    5 V on VBUS. Off again 10 ms after the Rd disappears.
#include "u28.h"
#include "ch32l103_usbpd.h"

static uint8_t s_mode = MCU_LINK_USB_SINK_DATA;
static uint8_t s_state = MCU_LINK_USB_DETACHED;
static uint32_t s_seen_ms, s_gone_ms;

void typec_begin() {
  RCC_HBPeriphClockCmd(RCC_HBPeriph_USBPD, ENABLE);
  AFIO->CR |= AFIO_CR_USBPD_IN_HVT;
  USBPD->CONFIG = PD_FILT_ED;
  USBPD->PORT_CC1 = 0;
  USBPD->PORT_CC2 = 0;
  pinMode(PIN_MCU2_USB_CC1, INPUT);
  pinMode(PIN_MCU2_USB_CC2, INPUT);
}

// Rp 180 uA into the phone's Rd (5.1k) gives ~0.9 V; open floats to VDD.
static bool rd_seen(volatile uint16_t *port, uint32_t pin) {
  *port = (uint16_t)((*port & ~(CC_CE | PA_CC_AI)) | CC_CMP_45);
  delayMicroseconds(2);
  return (*port & PA_CC_AI) && digitalRead(pin) == LOW;
}

uint8_t typec_step(uint8_t mode, uint32_t vbus_mv) {
  if (mode != s_mode) {
    digitalWrite(PIN_MCU2_USB_PWR_EN, LOW);
    if (s_mode == MCU_LINK_USB_SOURCE_CHARGE && vbus_mv > 800) return s_state; // let VBUS fall
    s_mode = mode;
    s_state = MCU_LINK_USB_DETACHED;
    s_seen_ms = s_gone_ms = 0;
    if (mode == MCU_LINK_USB_SOURCE_CHARGE) {
      digitalWrite(PIN_MCU2_CC_RD_EN, LOW);
      USBPD->PORT_CC1 = CC_PU_180 | CC_CMP_45;
      USBPD->PORT_CC2 = CC_PU_180 | CC_CMP_45;
    } else {
      USBPD->PORT_CC1 = 0;
      USBPD->PORT_CC2 = 0;
      digitalWrite(PIN_MCU2_CC_RD_EN, HIGH);
    }
  }
  if (s_mode == MCU_LINK_USB_SINK_DATA) {
    s_state = vbus_mv > 4000 ? MCU_LINK_USB_ATTACHED : MCU_LINK_USB_DETACHED;
    return s_state;
  }
  const bool rd = rd_seen(&USBPD->PORT_CC1, PIN_MCU2_USB_CC1) ||
                  rd_seen(&USBPD->PORT_CC2, PIN_MCU2_USB_CC2);
  if (s_state == MCU_LINK_USB_SOURCING) {
    s_gone_ms = rd ? 0 : s_gone_ms + 1;
    if (s_gone_ms >= 10) {
      digitalWrite(PIN_MCU2_USB_PWR_EN, LOW);
      s_state = MCU_LINK_USB_DETACHED;
      s_seen_ms = 0;
    }
  } else if (rd) {
    if (++s_seen_ms >= 150) {
      if (vbus_mv > 800) {
        s_state = MCU_LINK_USB_BLOCKED; // someone else drives VBUS
      } else {
        digitalWrite(PIN_MCU2_USB_PWR_EN, HIGH);
        s_state = MCU_LINK_USB_SOURCING;
        s_gone_ms = 0;
      }
    }
  } else {
    s_seen_ms = 0;
    s_state = MCU_LINK_USB_DETACHED;
  }
  return s_state;
}
