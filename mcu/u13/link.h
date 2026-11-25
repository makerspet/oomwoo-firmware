/* U13 side of the board-internal link to U28 (UART8, PE14 TX / PE15 RX). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mcu_link.h"

void link_init(void);
void link_poll(void);                         /* drain RX, decode frames */
void link_send(const mcu_link_control_t *c);  /* call every 10 ms */
bool link_alive(void);                        /* STATUS seen in the last 100 ms */
const mcu_link_status_t *link_status(void);
void link_stats(uint32_t *frames, uint32_t *crc_errors, uint32_t *overruns);
