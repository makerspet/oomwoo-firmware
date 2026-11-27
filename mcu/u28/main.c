/* U28 (CH32L103C8T6) starter firmware: power latch and button, button LEDs,
 * slow analog inputs, charger status, torch, camera reset, side ToF XSHUT,
 * USB-C baseline roles, and the link to U13. See docs/firmware-plan.md.
 *
 * U28 obeys U13's CONTROL frames for everything that touches the sensor
 * rails (torch, camera reset, XSHUT): those pins stay low unless U13 says
 * the rails are up and has been heard within the last 500 ms. */
#include <string.h>

#include "board.h"
#include "charger_sm.h"
#include "mcu_link.h"
#include "oomwoo_protocol.h"
#include "power_sm.h"
#include "systime.h"
#include "typec.h"
#include "uart.h"

#define LINK_LOST_MS 500u
#define DOCKED_MV 15000u /* DC-IN-20V; the dock's signature probe is ~1.5 V */

static uart_t s_uart;
static oomwoo_stream_decoder_t s_dec;
static mcu_link_control_t s_ctl;
static uint32_t s_ctl_ms;
static bool s_ctl_seen;
static uint16_t s_seq;
static power_sm_t s_pwr;
static charger_sm_t s_chg;
static uint8_t s_usb_state;
static uint32_t s_factory_ms;

void USART2_IRQHandler(void) ISR;
void USART2_IRQHandler(void) { uart_irq(&s_uart); }

static void on_frame(const oomwoo_decoded_frame_t *f, void *ctx) {
  (void)ctx;
  mcu_link_control_t c;
  if (f->message_type == MCU_LINK_MSG_CONTROL &&
      mcu_link_control_unpack(f->payload, f->payload_length, &c)) {
    s_ctl = c;
    s_ctl_ms = systime_ms();
    s_ctl_seen = true;
  }
}

static void link_init(void) {
  RCC_PB1PeriphClockCmd(RCC_PB1Periph_USART2, ENABLE);
  pin_output(PIN(PIN_MCU2_MCU2_TX), true, PM_AF_PP); /* PA2 USART2 TX -> U13 */
  pin_input_pullup(PIN(PIN_MCU2_MCU2_RX));            /* PA3 USART2 RX <- U13 */
  oomwoo_stream_decoder_init(&s_dec);
  uart_init(&s_uart, USART2, PCLK1_HZ, MCU_LINK_BAUD, USART2_IRQn, 0x40);
}

static void link_poll(void) {
  uint8_t buf[32];
  for (;;) {
    uint32_t n = 0u;
    int c;
    while (n < sizeof(buf) && (c = uart_getc(&s_uart)) >= 0) buf[n++] = (uint8_t)c;
    if (n == 0u) break;
    oomwoo_stream_decoder_feed(&s_dec, buf, n, on_frame, NULL);
  }
}

static bool u13_alive(void) { return s_ctl_seen && systime_ms() - s_ctl_ms < LINK_LOST_MS; }

static void send_status(void) {
  mcu_link_status_t s = {0};
  s.uptime_ms = systime_ms();
  s.vbat_mv = (uint16_t)(board_adc_mv(U28_ADC_BAT_SENSE) * 11u);
  s.dc_in_mv = (uint16_t)(board_adc_mv(U28_ADC_DC_IN) * 11u);
  s.vbus_mv = (uint16_t)(board_adc_mv(U28_ADC_VBUS) * 147u / 47u);
  s.bat_ntc_raw = board_adc_raw(U28_ADC_BAT_NTC);
  s.bat_id_raw = board_adc_raw(U28_ADC_BAT_ID);
  s.ldr_raw = board_adc_raw(U28_ADC_LDR);
  s.hall_floor_l_raw = board_adc_raw(U28_ADC_HALL_FLOOR_L);
  s.hall_floor_r_raw = board_adc_raw(U28_ADC_HALL_FLOOR_R);
  s.mcu_temp_c10 = (int16_t)board_temp_c10();
  s.buttons = (uint8_t)((clean_button_pressed() ? MCU_LINK_BTN_CLEAN : 0u) |
                        (home_button_pressed() ? MCU_LINK_BTN_HOME : 0u) |
                        (pwr_button_pressed() ? MCU_LINK_BTN_POWER : 0u) |
                        (s_pwr.factory_reset ? MCU_LINK_BTN_FACTORY_RESET : 0u));
  const bool docked = s.dc_in_mv > DOCKED_MV;
  s.flags = (uint8_t)((hall_bin_present() ? MCU_LINK_ST_HALL_BIN : 0u) |
                      (docked ? MCU_LINK_ST_DOCKED : 0u) |
                      (pin_out_state(PIN(PIN_MCU2_CHG_I_HI)) ? MCU_LINK_ST_CHG_I_HI : 0u) |
                      (u13_alive() ? 0u : MCU_LINK_ST_U13_LOST));
  s.power = s_pwr.state;
  s.charge = s_chg.state;
  s.usb = s_usb_state;
  s.fw_version = (uint8_t)FW_VERSION;

  uint8_t payload[MCU_LINK_STATUS_SIZE];
  uint8_t frame[OOMWOO_PROTOCOL_HEADER_SIZE + MCU_LINK_STATUS_SIZE + OOMWOO_PROTOCOL_CRC_SIZE];
  size_t len = 0u;
  mcu_link_status_pack(&s, payload);
  if (oomwoo_encode_frame(MCU_LINK_MSG_STATUS, payload, MCU_LINK_STATUS_SIZE, s_seq++, 0u, frame,
                          sizeof(frame), &len) == OOMWOO_PROTOCOL_OK &&
      uart_tx_free(&s_uart) >= len) {
    uart_write(&s_uart, frame, (uint32_t)len);
  }
}

/* ---------------------------------------------------------------- LEDs */

static uint16_t pattern(uint8_t p, uint32_t ms) {
  switch (p) {
    case MCU_LINK_LED_OFF: return 0u;
    case MCU_LINK_LED_ON: return 1000u;
    case MCU_LINK_LED_BLINK_SLOW: return (ms % 1000u) < 500u ? 1000u : 0u;
    case MCU_LINK_LED_BLINK_FAST: return (ms % 200u) < 100u ? 1000u : 0u;
    case MCU_LINK_LED_BREATHE: {
      const uint32_t t = ms % 2000u;
      const uint32_t tri = t < 1000u ? t : 2000u - t; /* 0..1000 */
      return (uint16_t)(tri * tri / 1000u);
    }
    default: return 0u;
  }
}

static void leds_update(uint32_t now, bool alive) {
  uint8_t home = MCU_LINK_LED_OFF;
  uint8_t power = MCU_LINK_LED_ON;
  uint8_t clean = clean_button_pressed() ? MCU_LINK_LED_ON : MCU_LINK_LED_OFF; /* press feedback */

  if (s_pwr.state == MCU_LINK_PWR_OFF) power = MCU_LINK_LED_OFF;
  if (!alive && s_pwr.state == MCU_LINK_PWR_RUN) power = MCU_LINK_LED_BLINK_SLOW; /* U13 silent */
  switch (s_chg.state) {
    case MCU_LINK_CHG_CHARGING: home = MCU_LINK_LED_BREATHE; break;
    case MCU_LINK_CHG_DONE: home = MCU_LINK_LED_ON; break;
    case MCU_LINK_CHG_FAULT:
    case MCU_LINK_CHG_NO_BATTERY: home = MCU_LINK_LED_BLINK_FAST; break;
    default: break;
  }
  if (alive && s_pwr.state == MCU_LINK_PWR_RUN) {
    if (s_ctl.led_home != MCU_LINK_LED_AUTO) home = s_ctl.led_home;
    if (s_ctl.led_power != MCU_LINK_LED_AUTO) power = s_ctl.led_power;
    if (s_ctl.led_clean != MCU_LINK_LED_AUTO) clean = s_ctl.led_clean;
  }
  if (s_pwr.feedback || s_pwr.state == MCU_LINK_PWR_OFF_REQUEST) power = MCU_LINK_LED_BLINK_FAST;
  led_home_set(pattern(home, now));
  led_power_set(pattern(power, now));
  led_clean_set(pattern(clean, now));
}

/* ---------------------------------------------------------------- tick */

static void tick_1ms(void) {
  const uint32_t now = systime_ms();
  link_poll();
  const bool alive = u13_alive();
  const bool sensors_on = alive && (s_ctl.flags & MCU_LINK_CTL_SENSORS_ON) != 0u;

  power_sm_step(&s_pwr, 1u, pwr_button_pressed(), home_button_pressed(),
                alive && (s_ctl.flags & MCU_LINK_CTL_RAILS_OFF) != 0u);
  pwr_hold(s_pwr.hold);
  if (s_pwr.factory_reset && ++s_factory_ms > 2000u) {
    power_sm_clear_factory_reset(&s_pwr);
    s_factory_ms = 0u;
  }

  const uint32_t dc_in_mv = board_adc_mv(U28_ADC_DC_IN) * 11u;
  const bool docked = dc_in_mv > DOCKED_MV;
  charger_sm_step(&s_chg, 1u, docked, chrg_low(), stdby_low(),
                  (uint16_t)(board_adc_mv(U28_ADC_BAT_SENSE) * 11u));

  /* Outputs that drive into sensor-rail parts: only while U13 says the rails are up. */
  torch_set(sensors_on ? s_ctl.torch_pct : 0u);
  pin_write(PIN(PIN_MCU2_CAM_RESET), sensors_on && (s_ctl.flags & MCU_LINK_CTL_CAM_RUN));
  pin_write(PIN(PIN_MCU2_SIDE_PROXI_CE_R), sensors_on && (s_ctl.flags & MCU_LINK_CTL_SIDE_TOF_ON));
  /* 2.2 A charging only on the dock and only on U13's request (U13 knows the
   * dock's power budget from the IR beacon). */
  pin_write(PIN(PIN_MCU2_CHG_I_HI), docked && alive && (s_ctl.flags & MCU_LINK_CTL_CHG_I_HI));

  /* USB-C: charge the phone only when U13 asks (docked, motors off). */
  const uint8_t usb_mode = alive ? s_ctl.usb_mode : MCU_LINK_USB_SINK_DATA;
  s_usb_state = typec_step(usb_mode, board_adc_mv(U28_ADC_VBUS) * 147u / 47u);

  leds_update(now, alive);
  if (now % 10u == 5u) send_status(); /* offset from U13's CONTROL slot */
}

int main(void) {
  board_safe_init(); /* PWR-HOLD high first */
  systime_init(HCLK_HZ);
  iwdg_start();
  power_sm_init(&s_pwr);
  charger_sm_init(&s_chg);
  board_pwm_init();
  board_adc_start();
  typec_init();
  link_init();

  for (;;) {
    iwdg_feed();
    while (systime_tick_pending()) tick_1ms();
    __WFI();
  }
}
