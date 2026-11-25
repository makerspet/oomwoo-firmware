/* Interrupt-driven UART with RX and TX ring buffers (both MCUs: the USART
 * register layout is the same). Each target's IRQ handler calls uart_irq(). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "chip.h"

#ifndef UART_RX_SIZE
#define UART_RX_SIZE 512u /* power of two; the dock (2 KB RAM) sets smaller ones */
#endif
#ifndef UART_TX_SIZE
#define UART_TX_SIZE 512u
#endif

typedef struct {
  USART_TypeDef *regs;
  volatile uint16_t rx_head, rx_tail;
  volatile uint16_t tx_head, tx_tail;
  volatile uint32_t rx_overruns; /* bytes lost: ring full or hardware overrun */
  volatile uint32_t rx_bytes;
  uint8_t rx[UART_RX_SIZE];
  uint8_t tx[UART_TX_SIZE];
} uart_t;

/* Configures the USART (8N1, RX interrupt) and enables its IRQ. The pins and
 * the peripheral clock are set up by the caller. */
void uart_init(uart_t *u, USART_TypeDef *regs, uint32_t pclk_hz, uint32_t baud, IRQn_Type irq,
               uint8_t prio);
void uart_irq(uart_t *u);
int uart_getc(uart_t *u); /* -1 if empty */
uint32_t uart_rx_available(const uart_t *u);
/* Queues bytes; drops what does not fit and returns the number queued. */
uint32_t uart_write(uart_t *u, const void *data, uint32_t len);
uint32_t uart_tx_free(const uart_t *u);
bool uart_tx_idle(const uart_t *u);
