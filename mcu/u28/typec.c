#include "typec.h"

#include "board.h"
#include "ch32l103_usbpd.h"
#include "mcu_link.h"

#define VBUS_PRESENT_MV 4000u
#define VBUS_SAFE0_MV 800u
#define CC_DEBOUNCE_MS 150u /* tCCDebounce 100-200 ms */
#define DETACH_MS 10u

static uint8_t s_mode = MCU_LINK_USB_SINK_DATA;
static uint8_t s_state = MCU_LINK_USB_DETACHED;
static uint8_t s_cc;
static uint32_t s_seen_ms, s_gone_ms;

void typec_init(void) {
  RCC_HBPeriphClockCmd(RCC_HBPeriph_USBPD, ENABLE);
  AFIO->CR |= AFIO_CR_USBPD_IN_HVT;
  USBPD->CONFIG = PD_FILT_ED;
  USBPD->PORT_CC1 = 0u; /* no Rp, no internal pull-down (LQFP-48 has no Rd) */
  USBPD->PORT_CC2 = 0u;
}

/* Voltage on a CC pin above the comparator threshold? (Rp current into Rd) */
static bool cc_above(volatile uint16_t *port, uint16_t cmp) {
  *port = (uint16_t)((*port & ~(CC_CE | PA_CC_AI)) | cmp);
  for (volatile int i = 0; i < 64; ++i) { /* ~2 us settle */
  }
  return (*port & PA_CC_AI) != 0u;
}

/* With Rp = 180 uA: Rd 5.1k -> ~0.9 V, Ra 1k -> ~0.2 V, open -> pulled to VDD.
 * "Rd present" = above 0.45 V and below the digital high threshold. */
static bool rd_on(volatile uint16_t *port, pin_t pin) {
  return cc_above(port, CC_CMP_45) && !pin_read(pin);
}

static void source_off(void) {
  pin_low(PIN(PIN_MCU2_USB_PWR_EN));
}

static void enter_sink(void) {
  source_off();
  USBPD->PORT_CC1 = 0u;
  USBPD->PORT_CC2 = 0u;
  pin_high(PIN(PIN_MCU2_CC_RD_EN)); /* Rd on */
  s_cc = 0u;
}

static void enter_source(void) {
  source_off();
  pin_low(PIN(PIN_MCU2_CC_RD_EN)); /* our Rd off, so the phone does not see a sink */
  USBPD->PORT_CC1 = CC_PU_180 | CC_CMP_45;
  USBPD->PORT_CC2 = CC_PU_180 | CC_CMP_45;
  s_cc = 0u;
  s_seen_ms = s_gone_ms = 0u;
}

uint8_t typec_step(uint8_t mode, uint32_t vbus_mv) {
  if (mode != s_mode) {
    /* Leaving source: wait for VBUS to discharge before presenting Rd. */
    source_off();
    if (s_mode == MCU_LINK_USB_SOURCE_CHARGE && vbus_mv > VBUS_SAFE0_MV) return s_state;
    s_mode = mode;
    s_state = MCU_LINK_USB_DETACHED;
    if (mode == MCU_LINK_USB_SOURCE_CHARGE) {
      enter_source();
    } else {
      enter_sink();
    }
  }

  if (s_mode == MCU_LINK_USB_SINK_DATA) {
    s_state = vbus_mv > VBUS_PRESENT_MV ? MCU_LINK_USB_ATTACHED : MCU_LINK_USB_DETACHED;
    return s_state;
  }

  const bool cc1 = rd_on(&USBPD->PORT_CC1, PIN(PIN_MCU2_USB_CC1));
  const bool cc2 = rd_on(&USBPD->PORT_CC2, PIN(PIN_MCU2_USB_CC2));
  const uint8_t cc = cc1 ? 1u : (cc2 ? 2u : 0u);

  switch (s_state) {
    case MCU_LINK_USB_DETACHED:
    case MCU_LINK_USB_BLOCKED:
      if (cc != 0u) {
        if (++s_seen_ms >= CC_DEBOUNCE_MS) {
          s_cc = cc;
          if (vbus_mv > VBUS_SAFE0_MV) {
            s_state = MCU_LINK_USB_BLOCKED; /* someone else drives VBUS */
          } else {
            pin_high(PIN(PIN_MCU2_USB_PWR_EN));
            s_state = MCU_LINK_USB_SOURCING;
            s_gone_ms = 0u;
          }
        }
      } else {
        s_seen_ms = 0u;
        s_state = MCU_LINK_USB_DETACHED;
      }
      break;
    case MCU_LINK_USB_SOURCING:
    default:
      if (cc == 0u) {
        if (++s_gone_ms >= DETACH_MS) {
          source_off();
          s_state = MCU_LINK_USB_DETACHED;
          s_seen_ms = 0u;
          s_cc = 0u;
        }
      } else {
        s_gone_ms = 0u;
      }
      break;
  }
  return s_state;
}

uint8_t typec_cc_seen(void) { return s_cc; }
