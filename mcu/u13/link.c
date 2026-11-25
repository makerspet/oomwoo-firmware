#include "link.h"

#include "board.h"
#include "oomwoo_protocol.h"
#include "systime.h"
#include "uart.h"

#define LINK_ALIVE_MS 100u

static uart_t s_uart;
static oomwoo_stream_decoder_t s_dec;
static mcu_link_status_t s_status;
static uint32_t s_last_ms;
static bool s_seen;
static uint16_t s_seq;
static uint8_t s_tx[OOMWOO_PROTOCOL_HEADER_SIZE + MCU_LINK_STATUS_SIZE + OOMWOO_PROTOCOL_CRC_SIZE];

void UART8_IRQHandler(void) ISR;
void UART8_IRQHandler(void) { uart_irq(&s_uart); }

void link_init(void) {
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART8, ENABLE);
  pin_output(PIN(PIN_MCU2_RX), true, PM_AF_PP); /* PE14 = UART8 TX -> U28 RX */
  pin_input_pullup(PIN(PIN_MCU2_TX));            /* PE15 = UART8 RX <- U28 TX */
  oomwoo_stream_decoder_init(&s_dec);
  uart_init(&s_uart, UART8, PCLK1_HZ, MCU_LINK_BAUD, UART8_IRQn, 0x40);
}

static void on_frame(const oomwoo_decoded_frame_t *f, void *ctx) {
  (void)ctx;
  if (f->message_type == MCU_LINK_MSG_STATUS &&
      mcu_link_status_unpack(f->payload, f->payload_length, &s_status)) {
    s_last_ms = systime_ms();
    s_seen = true;
  }
}

void link_poll(void) {
  uint8_t buf[32];
  for (;;) {
    uint32_t n = 0u;
    int c;
    while (n < sizeof(buf) && (c = uart_getc(&s_uart)) >= 0) buf[n++] = (uint8_t)c;
    if (n == 0u) break;
    oomwoo_stream_decoder_feed(&s_dec, buf, n, on_frame, NULL);
  }
}

void link_send(const mcu_link_control_t *c) {
  uint8_t payload[MCU_LINK_CONTROL_SIZE];
  size_t len = 0u;
  mcu_link_control_pack(c, payload);
  if (oomwoo_encode_frame(MCU_LINK_MSG_CONTROL, payload, MCU_LINK_CONTROL_SIZE, s_seq++, 0u, s_tx,
                          sizeof(s_tx), &len) == OOMWOO_PROTOCOL_OK &&
      uart_tx_free(&s_uart) >= len) {
    uart_write(&s_uart, s_tx, (uint32_t)len);
  }
}

bool link_alive(void) { return s_seen && (systime_ms() - s_last_ms) < LINK_ALIVE_MS; }

const mcu_link_status_t *link_status(void) { return &s_status; }

void link_stats(uint32_t *frames, uint32_t *crc_errors, uint32_t *overruns) {
  *frames = s_dec.stats.frames;
  *crc_errors = s_dec.stats.crc_errors;
  *overruns = s_uart.rx_overruns;
}
