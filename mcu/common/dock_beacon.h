/* Dock IR beacon codes, shared by the dock (sender) and U13 (receiver).
 *
 * Standard NEC frames: address byte + its inverse, command byte + its inverse.
 * The address says which side of the dock the frame came from (the left and
 * right LED strings send in turn); the command carries the dock's power budget
 * at the contacts in 2 W steps. 0 means the dock refuses to charge (no 20 V,
 * no PD contract, or a fault), so the robot can still home on it. */
#pragma once

#include <stdint.h>

#define DOCK_BEACON_ADDR_LEFT 0xD1u
#define DOCK_BEACON_ADDR_RIGHT 0xD2u
#define DOCK_BEACON_W_PER_STEP 2u

static inline uint8_t dock_beacon_budget_code(uint32_t watts) {
  const uint32_t c = watts / DOCK_BEACON_W_PER_STEP;
  return (uint8_t)(c > 255u ? 255u : c);
}

static inline uint32_t dock_beacon_budget_w(uint8_t code) {
  return (uint32_t)code * DOCK_BEACON_W_PER_STEP;
}

/* NEC frame bits as sent LSB first: addr, ~addr, cmd, ~cmd */
static inline uint32_t dock_beacon_frame(uint8_t addr, uint8_t cmd) {
  return (uint32_t)addr | ((uint32_t)(uint8_t)~addr << 8) | ((uint32_t)cmd << 16) |
         ((uint32_t)(uint8_t)~cmd << 24);
}
