#ifndef OOMWOO_STM32G473_SAFETY_H
#define OOMWOO_STM32G473_SAFETY_H

#include "oomwoo_cpu_watchdog.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OOMWOO I/O board mapping from
 * makerspet/oomwoo-io-board@5c275ad5edb199b84d87636823723d555adbd6dd:
 *   PD8  -> WDI (external RC watchdog input)
 *   PE10 -> ~VM-VBAT-EN (high disables the motor/fan battery rail)
 */
#define OOMWOO_STM32G473_WDI_PIN UINT32_C(8)
#define OOMWOO_STM32G473_VM_VBAT_EN_PIN UINT32_C(10)
#define OOMWOO_STM32G473_WDI_MASK \
  (UINT32_C(1) << OOMWOO_STM32G473_WDI_PIN)
#define OOMWOO_STM32G473_VM_VBAT_EN_MASK \
  (UINT32_C(1) << OOMWOO_STM32G473_VM_VBAT_EN_PIN)

typedef struct {
  volatile uint32_t *moder;
  volatile uint32_t *otyper;
  volatile uint32_t *ospeedr;
  volatile uint32_t *pupdr;
  volatile uint32_t *bsrr;
} oomwoo_stm32g473_gpio_registers_t;

typedef struct {
  volatile uint32_t *internal_wdi_bsrr;
  volatile uint32_t *internal_vm_vbat_en_bsrr;
  uint32_t internal_wdi_high;
  volatile uint32_t internal_last_stop_reason;
} oomwoo_stm32g473_safety_t;

/*
 * Configure PD8 and PE10 as low-speed, push-pull outputs with no internal pull.
 * Safe output values are loaded before output mode is selected, preventing an
 * enable glitch during initialization. GPIO port clocks must already be on.
 */
bool oomwoo_stm32g473_safety_init(
    oomwoo_stm32g473_safety_t *safety,
    const oomwoo_stm32g473_gpio_registers_t *gpiod,
    const oomwoo_stm32g473_gpio_registers_t *gpioe);

/* CPU-watchdog callback. Directly drives PE10 high and performs no calls. */
void oomwoo_stm32g473_safety_hard_stop_isr(
    void *context, oomwoo_cpu_stop_reason_t reason);

/*
 * Toggle PD8 from foreground control-loop context only. Do not call this from
 * a timer/PWM ISR: a blocked task must stop feeding the external watchdog.
 */
void oomwoo_stm32g473_safety_toggle_wdi_foreground(
    oomwoo_stm32g473_safety_t *safety);

oomwoo_cpu_stop_reason_t oomwoo_stm32g473_safety_last_stop_reason(
    const oomwoo_stm32g473_safety_t *safety);

#ifdef __cplusplus
}
#endif

#endif
