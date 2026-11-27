/* Generated copy of mcu/common/mcu_link.c (tools/sync_arduino_lib.py). Do not edit here. */
#include "mcu_link.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
  put16(p, (uint16_t)v);
  put16(p + 2, (uint16_t)(v >> 16));
}

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t get32(const uint8_t *p) {
  return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16);
}

void mcu_link_status_pack(const mcu_link_status_t *s, uint8_t out[MCU_LINK_STATUS_SIZE]) {
  put32(out + 0, s->uptime_ms);
  put16(out + 4, s->vbat_mv);
  put16(out + 6, s->dc_in_mv);
  put16(out + 8, s->vbus_mv);
  put16(out + 10, s->bat_ntc_raw);
  put16(out + 12, s->bat_id_raw);
  put16(out + 14, s->ldr_raw);
  put16(out + 16, s->hall_floor_l_raw);
  put16(out + 18, s->hall_floor_r_raw);
  put16(out + 20, (uint16_t)s->mcu_temp_c10);
  out[22] = s->buttons;
  out[23] = s->flags;
  out[24] = s->power;
  out[25] = s->charge;
  out[26] = s->usb;
  out[27] = s->fw_version;
  out[28] = 0u; /* reserved */
  out[29] = 0u;
}

bool mcu_link_status_unpack(const uint8_t *in, size_t len, mcu_link_status_t *s) {
  memset(s, 0, sizeof(*s));
  if (in == NULL || len != MCU_LINK_STATUS_SIZE) return false;
  s->uptime_ms = get32(in + 0);
  s->vbat_mv = get16(in + 4);
  s->dc_in_mv = get16(in + 6);
  s->vbus_mv = get16(in + 8);
  s->bat_ntc_raw = get16(in + 10);
  s->bat_id_raw = get16(in + 12);
  s->ldr_raw = get16(in + 14);
  s->hall_floor_l_raw = get16(in + 16);
  s->hall_floor_r_raw = get16(in + 18);
  s->mcu_temp_c10 = (int16_t)get16(in + 20);
  s->buttons = in[22];
  s->flags = in[23];
  s->power = in[24];
  s->charge = in[25];
  s->usb = in[26];
  s->fw_version = in[27];
  if (s->power > MCU_LINK_PWR_OFF || s->charge > MCU_LINK_CHG_NO_BATTERY ||
      s->usb > MCU_LINK_USB_BLOCKED) {
    memset(s, 0, sizeof(*s));
    return false;
  }
  return true;
}

void mcu_link_control_pack(const mcu_link_control_t *c, uint8_t out[MCU_LINK_CONTROL_SIZE]) {
  out[0] = c->flags;
  out[1] = c->torch_pct;
  out[2] = c->led_home;
  out[3] = c->led_power;
  out[4] = c->usb_mode;
  out[5] = c->seq;
  out[6] = c->led_clean;
}

bool mcu_link_control_unpack(const uint8_t *in, size_t len, mcu_link_control_t *c) {
  memset(c, 0, sizeof(*c));
  if (in == NULL || len != MCU_LINK_CONTROL_SIZE) return false;
  c->flags = in[0];
  c->torch_pct = in[1];
  c->led_home = in[2];
  c->led_power = in[3];
  c->usb_mode = in[4];
  c->seq = in[5];
  c->led_clean = in[6];
  if (c->torch_pct > 100u || c->led_home > MCU_LINK_LED_BREATHE ||
      c->led_power > MCU_LINK_LED_BREATHE || c->led_clean > MCU_LINK_LED_BREATHE || c->usb_mode > MCU_LINK_USB_SOURCE_CHARGE ||
      (c->flags & 0xE0u) != 0u) {
    memset(c, 0, sizeof(*c));
    return false;
  }
  return true;
}
