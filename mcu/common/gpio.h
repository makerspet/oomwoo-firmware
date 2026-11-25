/* GPIO helpers shared by U13 (CH32V307) and U28 (CH32L103).
 * Both chips have the same F1-style port layout: CFGLR/CFGHR hold a 4-bit
 * CNF:MODE field per pin, BSHR/BCR set and clear outputs atomically. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "chip.h"

typedef struct {
  GPIO_TypeDef *port;
  uint8_t num;
} pin_t;

/* PIN(PIN_WDI) -> {GPIOB, 5}, from the generated include/oomwoo_pins.h */
#define PIN(name) ((pin_t){name##_PORT, name##_NUM})
/* Same, for static initializers */
#define PIN_INIT(name) {name##_PORT, name##_NUM}

/* CNF[1:0] << 2 | MODE[1:0] */
enum {
  PM_ANALOG = 0x0,      /* analog input (ADC, OPA, PD CC) */
  PM_IN_FLOAT = 0x4,    /* digital input, no pull */
  PM_IN_PULL = 0x8,     /* digital input, pull set by the output bit */
  PM_OUT_PP = 0x3,      /* push-pull output, 50 MHz */
  PM_OUT_OD = 0x7,      /* open-drain output, 50 MHz */
  PM_AF_PP = 0xB,       /* alternate function push-pull, 50 MHz */
  PM_AF_OD = 0xF,       /* alternate function open-drain, 50 MHz */
  PM_OUT_PP_2M = 0x2,   /* PC13-PC15 only support 2 MHz outputs */
  PM_OUT_OD_2M = 0x6,
};

static inline void pin_mode(pin_t p, uint8_t mode) {
  volatile uint32_t *cfg = p.num < 8 ? &p.port->CFGLR : &p.port->CFGHR;
  const uint32_t shift = (uint32_t)(p.num & 7u) * 4u;
  *cfg = (*cfg & ~(0xFu << shift)) | ((uint32_t)mode << shift);
}

static inline void pin_high(pin_t p) { p.port->BSHR = 1u << p.num; }
static inline void pin_low(pin_t p) { p.port->BCR = 1u << p.num; }
static inline void pin_write(pin_t p, bool v) {
  if (v) {
    pin_high(p);
  } else {
    pin_low(p);
  }
}
static inline bool pin_read(pin_t p) { return (p.port->INDR >> p.num) & 1u; }
static inline bool pin_out_state(pin_t p) { return (p.port->OUTDR >> p.num) & 1u; }

/* Output with a defined level: set the level first, then the mode, so the
 * pin never glitches to the wrong level when it stops floating. */
static inline void pin_output(pin_t p, bool level, uint8_t mode) {
  pin_write(p, level);
  pin_mode(p, mode);
}

static inline void pin_input_pullup(pin_t p) {
  pin_high(p);
  pin_mode(p, PM_IN_PULL);
}

static inline void pin_input_pulldown(pin_t p) {
  pin_low(p);
  pin_mode(p, PM_IN_PULL);
}
