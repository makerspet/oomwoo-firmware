// The board link to U13: Serial is USART2 (PA2 TX, PA3 RX) at 1 Mbit/s.
// U13 sends CONTROL every 10 ms (its heartbeat); U28 answers with STATUS.
// Same "OW" framing and payloads as the plain-C firmware (mcu_link.h).
#include "u28.h"

static oomwoo_stream_decoder_t s_dec;
static mcu_link_control_t s_ctl;
static volatile uint32_t s_ctl_ms;
static volatile bool s_seen;
static uint16_t s_seq;

static void on_frame(const oomwoo_decoded_frame_t *f, void *) {
  mcu_link_control_t c;
  if (f->message_type == MCU_LINK_MSG_CONTROL &&
      mcu_link_control_unpack(f->payload, f->payload_length, &c)) {
    noInterrupts(); // the safety tick reads it
    s_ctl = c;
    s_ctl_ms = millis();
    s_seen = true;
    interrupts();
  }
}

void link_begin() {
  oomwoo_stream_decoder_init(&s_dec);
  Serial.begin(MCU_LINK_BAUD);
}

void link_poll() {
  uint8_t buf[32];
  size_t n = 0;
  while (Serial.available() > 0 && n < sizeof(buf)) buf[n++] = (uint8_t)Serial.read();
  if (n) oomwoo_stream_decoder_feed(&s_dec, buf, n, on_frame, nullptr);
}

bool link_alive() { return s_seen && millis() - s_ctl_ms < 500; }

mcu_link_control_t link_control() {
  noInterrupts();
  mcu_link_control_t c = s_ctl;
  interrupts();
  return c;
}

// The writer (on_frame, in loop()) masks interrupts, so the tick never sees a
// half-written copy; and it must not re-enable interrupts itself.
mcu_link_control_t link_control_from_isr() { return s_ctl; }

void link_send_status(uint8_t usb_state) {
  mcu_link_status_t s = {};
  s.uptime_ms = millis();
  s.vbat_mv = g_analog.vbat_mv;
  s.dc_in_mv = g_analog.dc_in_mv;
  s.vbus_mv = g_analog.vbus_mv;
  s.bat_ntc_raw = g_analog.ntc_raw;
  s.bat_id_raw = g_analog.id_raw;
  s.ldr_raw = g_analog.ldr_raw;
  s.hall_floor_l_raw = g_analog.floor_l_raw;
  s.hall_floor_r_raw = g_analog.floor_r_raw;
  s.buttons = (digitalRead(PIN_MCU2_CLEAN_BTN_N) == LOW ? MCU_LINK_BTN_CLEAN : 0) |
              (digitalRead(PIN_MCU2_HOME_BTN_N) == LOW ? MCU_LINK_BTN_HOME : 0) |
              (digitalRead(PIN_MCU2_PWR_BTN_SENSE) == LOW ? MCU_LINK_BTN_POWER : 0) |
              (safety_factory_reset() ? MCU_LINK_BTN_FACTORY_RESET : 0);
  s.flags = (digitalRead(PIN_MCU2_HALL_BIN) == LOW ? MCU_LINK_ST_HALL_BIN : 0) |
            (safety_docked() ? MCU_LINK_ST_DOCKED : 0) |
            (digitalRead(PIN_MCU2_CHG_I_HI) == HIGH ? MCU_LINK_ST_CHG_I_HI : 0) |
            (link_alive() ? 0 : MCU_LINK_ST_U13_LOST);
  s.power = safety_power_state();
  s.charge = safety_charge_state();
  s.usb = usb_state;
  s.fw_version = 1;

  uint8_t payload[MCU_LINK_STATUS_SIZE];
  uint8_t frame[OOMWOO_PROTOCOL_HEADER_SIZE + MCU_LINK_STATUS_SIZE + OOMWOO_PROTOCOL_CRC_SIZE];
  size_t len = 0;
  mcu_link_status_pack(&s, payload);
  if (oomwoo_encode_frame(MCU_LINK_MSG_STATUS, payload, MCU_LINK_STATUS_SIZE, s_seq++, 0, frame,
                          sizeof(frame), &len) == OOMWOO_PROTOCOL_OK) {
    Serial.write(frame, len);
  }
}
