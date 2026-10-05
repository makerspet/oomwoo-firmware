#include "oomwoo_stm32g473_safety.h"

#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
#define OOMWOO_NOINLINE_USED __attribute__((noinline, used))
#else
#define OOMWOO_NOINLINE_USED
#endif

static bool gpio_registers_valid(
    const oomwoo_stm32g473_gpio_registers_t *gpio) {
  return gpio != NULL && gpio->moder != NULL && gpio->otyper != NULL &&
         gpio->ospeedr != NULL && gpio->pupdr != NULL && gpio->bsrr != NULL;
}

static void configure_output(
    const oomwoo_stm32g473_gpio_registers_t *gpio, uint32_t pin) {
  const uint32_t mode_shift = pin * UINT32_C(2);
  const uint32_t mode_mask = UINT32_C(3) << mode_shift;

  *gpio->moder = (*gpio->moder & ~mode_mask) | (UINT32_C(1) << mode_shift);
  *gpio->otyper &= ~(UINT32_C(1) << pin);
  *gpio->ospeedr &= ~mode_mask;
  *gpio->pupdr &= ~mode_mask;
}

bool oomwoo_stm32g473_safety_init(
    oomwoo_stm32g473_safety_t *safety,
    const oomwoo_stm32g473_gpio_registers_t *gpiod,
    const oomwoo_stm32g473_gpio_registers_t *gpioe) {
  if (safety == NULL || !gpio_registers_valid(gpiod) ||
      !gpio_registers_valid(gpioe)) {
    return false;
  }

  safety->internal_wdi_bsrr = gpiod->bsrr;
  safety->internal_vm_vbat_en_bsrr = gpioe->bsrr;
  safety->internal_wdi_high = 0U;
  safety->internal_last_stop_reason = OOMWOO_CPU_STOP_BOOT;

  *gpioe->bsrr = OOMWOO_STM32G473_VM_VBAT_EN_MASK;
  *gpiod->bsrr = OOMWOO_STM32G473_WDI_MASK << UINT32_C(16);
  configure_output(gpioe, OOMWOO_STM32G473_VM_VBAT_EN_PIN);
  configure_output(gpiod, OOMWOO_STM32G473_WDI_PIN);
  return true;
}

OOMWOO_NOINLINE_USED void oomwoo_stm32g473_safety_hard_stop_isr(
    void *context, oomwoo_cpu_stop_reason_t reason) {
  oomwoo_stm32g473_safety_t *const safety =
      (oomwoo_stm32g473_safety_t *)context;

  if (safety != NULL && safety->internal_vm_vbat_en_bsrr != NULL) {
    *safety->internal_vm_vbat_en_bsrr =
        OOMWOO_STM32G473_VM_VBAT_EN_MASK;
    safety->internal_last_stop_reason = (uint32_t)reason;
  }
}

void oomwoo_stm32g473_safety_toggle_wdi_foreground(
    oomwoo_stm32g473_safety_t *safety) {
  if (safety == NULL || safety->internal_wdi_bsrr == NULL) {
    return;
  }

  safety->internal_wdi_high ^= UINT32_C(1);
  *safety->internal_wdi_bsrr =
      safety->internal_wdi_high != 0U
          ? OOMWOO_STM32G473_WDI_MASK
          : (OOMWOO_STM32G473_WDI_MASK << UINT32_C(16));
}

oomwoo_cpu_stop_reason_t oomwoo_stm32g473_safety_last_stop_reason(
    const oomwoo_stm32g473_safety_t *safety) {
  if (safety == NULL) {
    return OOMWOO_CPU_STOP_NONE;
  }
  return (oomwoo_cpu_stop_reason_t)safety->internal_last_stop_reason;
}
