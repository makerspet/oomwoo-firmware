/* U13 <-> U28 board-internal link (U13 UART8 PE14/PE15 <-> U28 USART2 PA3/PA2).
 *
 * Framing reuses the CPU protocol's "OW" frames (oomwoo_protocol.h: header,
 * CRC-16/CCITT-FALSE, bounded stream decoder) with message types in the
 * 0xC000 block, which never appear on the phone link. Payloads are fixed-size,
 * little-endian and versioned by their message type. Pure C, no hardware:
 * built into both MCUs and the host tests. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MCU_LINK_BAUD 1000000u

enum {
  MCU_LINK_MSG_STATUS = 0xC001,  /* U28 -> U13, every 10 ms */
  MCU_LINK_MSG_CONTROL = 0xC002, /* U13 -> U28, every 10 ms; doubles as U13's heartbeat */
};

/* U28 power state machine, reported in STATUS */
typedef enum {
  MCU_LINK_PWR_BOOT = 0,        /* latch held, waiting for the power button release */
  MCU_LINK_PWR_RUN = 1,
  MCU_LINK_PWR_OFF_REQUEST = 2, /* 3 s press: U13 must turn its rails off and ack */
  MCU_LINK_PWR_OFF = 3,         /* PWR-HOLD released; still powered only while charging */
} mcu_link_power_t;

/* SLM6900 charge status, decoded on U28 */
typedef enum {
  MCU_LINK_CHG_OFF_DOCK = 0,
  MCU_LINK_CHG_CHARGING = 1,
  MCU_LINK_CHG_DONE = 2,
  MCU_LINK_CHG_FAULT = 3,
  MCU_LINK_CHG_NO_BATTERY = 4,
} mcu_link_charge_t;

/* USB-C role on U28 (baseline: Type-C resistors only, no PD messages) */
typedef enum {
  MCU_LINK_USB_SINK_DATA = 0,     /* Rd: the phone is host and powers VBUS */
  MCU_LINK_USB_SOURCE_CHARGE = 1, /* Rp + robot 5 V on VBUS: charges the phone, no data */
} mcu_link_usb_mode_t;

typedef enum {
  MCU_LINK_USB_DETACHED = 0,
  MCU_LINK_USB_ATTACHED = 1,  /* sink: VBUS from the phone; source: phone's Rd seen */
  MCU_LINK_USB_SOURCING = 2,  /* source: USB_PWR_EN on */
  MCU_LINK_USB_BLOCKED = 3,   /* source requested but VBUS already present */
} mcu_link_usb_state_t;

/* STATUS.buttons */
#define MCU_LINK_BTN_CLEAN 0x01u
#define MCU_LINK_BTN_HOME 0x02u
#define MCU_LINK_BTN_POWER 0x04u
#define MCU_LINK_BTN_FACTORY_RESET 0x08u /* latched: power + home held 3 s */

/* STATUS.flags */
#define MCU_LINK_ST_HALL_BIN 0x01u  /* dust bin magnet seen (bin present) */
#define MCU_LINK_ST_DOCKED 0x02u    /* DC-IN-20V above threshold */
#define MCU_LINK_ST_CHG_I_HI 0x04u  /* 2.2 A charge current selected */
#define MCU_LINK_ST_U13_LOST 0x08u  /* no CONTROL frame for > 500 ms */

typedef struct {
  uint32_t uptime_ms;
  uint16_t vbat_mv;     /* BAT_SENSE x11 */
  uint16_t dc_in_mv;    /* DC_IN_SENSE x11 */
  uint16_t vbus_mv;     /* USB_VBUS_SENSE x147/47 */
  uint16_t bat_ntc_raw; /* 12-bit ADC counts; conversion depends on DOCKED */
  uint16_t bat_id_raw;
  uint16_t ldr_raw;
  uint16_t hall_floor_l_raw;
  uint16_t hall_floor_r_raw;
  int16_t mcu_temp_c10; /* U28 internal sensor, 0.1 C */
  uint8_t buttons;
  uint8_t flags;
  uint8_t power;  /* mcu_link_power_t */
  uint8_t charge; /* mcu_link_charge_t */
  uint8_t usb;    /* mcu_link_usb_state_t */
  uint8_t fw_version;
} mcu_link_status_t;

#define MCU_LINK_STATUS_SIZE 30u

/* CONTROL.flags */
#define MCU_LINK_CTL_CAM_RUN 0x01u      /* release CAM-RESET (U28 PB9) */
#define MCU_LINK_CTL_SIDE_TOF_ON 0x02u  /* drive SIDE-PROXI-CE-R (VL53L0X XSHUT) high */
#define MCU_LINK_CTL_CHG_I_HI 0x04u     /* request 2.2 A charging (U28 also checks the dock budget) */
#define MCU_LINK_CTL_RAILS_OFF 0x08u    /* ack of OFF_REQUEST: U13's rails are off */
#define MCU_LINK_CTL_SENSORS_ON 0x10u   /* VSENS-3V3 is up: U28 may drive pins into sensor-rail parts */

/* LED patterns for CONTROL.led_home / led_power / led_clean */
typedef enum {
  MCU_LINK_LED_AUTO = 0, /* U28 decides (power on, charge state, errors) */
  MCU_LINK_LED_OFF = 1,
  MCU_LINK_LED_ON = 2,
  MCU_LINK_LED_BLINK_SLOW = 3,
  MCU_LINK_LED_BLINK_FAST = 4,
  MCU_LINK_LED_BREATHE = 5,
} mcu_link_led_t;

typedef struct {
  uint8_t flags;
  uint8_t torch_pct; /* 0-100, PWM duty on TORCH (U28 PB8) */
  uint8_t led_home;  /* mcu_link_led_t */
  uint8_t led_power; /* mcu_link_led_t */
  uint8_t usb_mode;  /* mcu_link_usb_mode_t */
  uint8_t seq;       /* incremented by U13 on every frame */
  uint8_t led_clean; /* mcu_link_led_t (byte 6, added 2026-11-27) */
} mcu_link_control_t;

#define MCU_LINK_CONTROL_SIZE 7u

void mcu_link_status_pack(const mcu_link_status_t *s, uint8_t out[MCU_LINK_STATUS_SIZE]);
bool mcu_link_status_unpack(const uint8_t *in, size_t len, mcu_link_status_t *s);
void mcu_link_control_pack(const mcu_link_control_t *c, uint8_t out[MCU_LINK_CONTROL_SIZE]);
bool mcu_link_control_unpack(const uint8_t *in, size_t len, mcu_link_control_t *c);

#ifdef __cplusplus
}
#endif
