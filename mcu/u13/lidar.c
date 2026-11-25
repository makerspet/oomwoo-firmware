#include "lidar.h"

#include "board.h"
#include "uart.h"

#define LIDAR_BAUD 230400u

static uart_t s_uart;
static bool s_on;
static uint32_t s_packets;
static uint8_t s_prev;

void UART5_IRQHandler(void) ISR;
void UART5_IRQHandler(void) { uart_irq(&s_uart); }

void lidar_uart_enable(bool on) {
  if (on == s_on) return;
  s_on = on;
  if (on) {
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART5, ENABLE);
    pin_output(PIN(PIN_LIDAR_RXD), true, PM_AF_PP); /* PC12 UART5 TX */
    pin_mode(PIN(PIN_LIDAR_TXD), PM_IN_FLOAT);        /* PD2 UART5 RX */
    uart_init(&s_uart, UART5, PCLK1_HZ, LIDAR_BAUD, UART5_IRQn, 0x60);
  } else {
    NVIC_DisableIRQ(UART5_IRQn);
    USART_Cmd(UART5, DISABLE);
    /* 100 R series resistors only: drive low so the unpowered LiDAR is not
     * back-powered through its RX pin. */
    pin_output(PIN(PIN_LIDAR_RXD), false, PM_OUT_PP);
  }
}

void lidar_poll(void) {
  if (!s_on) return;
  int c;
  while ((c = uart_getc(&s_uart)) >= 0) {
    if (s_prev == 0x54u && c == 0x2Cu) ++s_packets;
    s_prev = (uint8_t)c;
  }
}

void lidar_stats(uint32_t *bytes, uint32_t *packets, uint32_t *overruns) {
  *bytes = s_uart.rx_bytes;
  *packets = s_packets;
  *overruns = s_uart.rx_overruns;
}
