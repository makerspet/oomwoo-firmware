/* LiDAR UART (UART5: RX PD2 from the LiDAR's TX, TX PC12 to its RX).
 * LDROBOT LD14P: 230400 8N1, 47-byte packets starting 0x54 0x2C. The packet
 * parser and timestamping (at packet start) come later; this counts bytes and
 * headers so the link can be checked on the bench. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

void lidar_uart_enable(bool on); /* follows the motor rail (the LiDAR is on it) */
void lidar_poll(void);
void lidar_stats(uint32_t *bytes, uint32_t *packets, uint32_t *overruns);
