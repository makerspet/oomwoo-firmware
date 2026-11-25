#include "uart.h"

#define SR_ORE (1u << 3)
#define SR_RXNE (1u << 5)
#define SR_TXE (1u << 7)
#define CR1_RE (1u << 2)
#define CR1_TE (1u << 3)
#define CR1_RXNEIE (1u << 5)
#define CR1_TXEIE (1u << 7)
#define CR1_UE (1u << 13)

void uart_init(uart_t *u, USART_TypeDef *regs, uint32_t pclk_hz, uint32_t baud, IRQn_Type irq,
               uint8_t prio) {
  u->regs = regs;
  u->rx_head = u->rx_tail = 0u;
  u->tx_head = u->tx_tail = 0u;
  u->rx_overruns = 0u;
  u->rx_bytes = 0u;
  regs->CTLR1 = 0u;
  regs->CTLR2 = 0u;
  regs->CTLR3 = 0u;
  /* 16x oversampling: BRR = pclk / baud, rounded (mantissa:fraction 12.4) */
  regs->BRR = (uint16_t)((pclk_hz + baud / 2u) / baud);
  regs->CTLR1 = CR1_UE | CR1_TE | CR1_RE | CR1_RXNEIE;
  NVIC_SetPriority(irq, prio);
  NVIC_EnableIRQ(irq);
}

void uart_irq(uart_t *u) {
  USART_TypeDef *r = u->regs;
  uint32_t sr = r->STATR;
  if (sr & (SR_RXNE | SR_ORE)) {
    const uint8_t b = (uint8_t)r->DATAR; /* also clears ORE */
    if (sr & SR_ORE) ++u->rx_overruns;
    const uint16_t next = (uint16_t)((u->rx_head + 1u) & (UART_RX_SIZE - 1u));
    if (next == u->rx_tail) {
      ++u->rx_overruns;
    } else {
      u->rx[u->rx_head] = b;
      u->rx_head = next;
      ++u->rx_bytes;
    }
  }
  if ((r->CTLR1 & CR1_TXEIE) && (sr & SR_TXE)) {
    if (u->tx_tail == u->tx_head) {
      r->CTLR1 &= ~CR1_TXEIE;
    } else {
      r->DATAR = u->tx[u->tx_tail];
      u->tx_tail = (uint16_t)((u->tx_tail + 1u) & (UART_TX_SIZE - 1u));
    }
  }
}

int uart_getc(uart_t *u) {
  if (u->rx_tail == u->rx_head) return -1;
  const uint8_t b = u->rx[u->rx_tail];
  u->rx_tail = (uint16_t)((u->rx_tail + 1u) & (UART_RX_SIZE - 1u));
  return b;
}

uint32_t uart_rx_available(const uart_t *u) {
  return (uint32_t)((u->rx_head - u->rx_tail) & (UART_RX_SIZE - 1u));
}

uint32_t uart_tx_free(const uart_t *u) {
  return (UART_TX_SIZE - 1u) - ((uint32_t)(u->tx_head - u->tx_tail) & (UART_TX_SIZE - 1u));
}

bool uart_tx_idle(const uart_t *u) { return u->tx_head == u->tx_tail; }

uint32_t uart_write(uart_t *u, const void *data, uint32_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t n = 0u;
  for (; n < len; ++n) {
    const uint16_t next = (uint16_t)((u->tx_head + 1u) & (UART_TX_SIZE - 1u));
    if (next == u->tx_tail) break;
    u->tx[u->tx_head] = p[n];
    u->tx_head = next;
  }
  if (n != 0u) u->regs->CTLR1 |= CR1_TXEIE;
  return n;
}
