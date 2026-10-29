#include "oomwoo_stm32g473_safety.h"

#include <assert.h>
#include <stdio.h>

typedef struct {
  uint32_t moder;
  uint32_t otyper;
  uint32_t ospeedr;
  uint32_t pupdr;
  uint32_t bsrr;
} fake_gpio_t;

static oomwoo_stm32g473_gpio_registers_t registers_for(fake_gpio_t *gpio) {
  const oomwoo_stm32g473_gpio_registers_t registers = {
      &gpio->moder,
      &gpio->otyper,
      &gpio->ospeedr,
      &gpio->pupdr,
      &gpio->bsrr,
  };
  return registers;
}

static void test_init_is_fail_closed(void) {
  fake_gpio_t gpiod = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 0U};
  fake_gpio_t gpioe = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 0U};
  const oomwoo_stm32g473_gpio_registers_t d = registers_for(&gpiod);
  const oomwoo_stm32g473_gpio_registers_t e = registers_for(&gpioe);
  oomwoo_stm32g473_safety_t safety;

  assert(oomwoo_stm32g473_safety_init(&safety, &d, &e));
  assert(gpioe.bsrr == OOMWOO_STM32G473_VM_VBAT_EN_MASK);
  assert(gpiod.bsrr == (OOMWOO_STM32G473_WDI_MASK << 16U));
  assert(((gpioe.moder >> (OOMWOO_STM32G473_VM_VBAT_EN_PIN * 2U)) & 3U) ==
         1U);
  assert(((gpiod.moder >> (OOMWOO_STM32G473_WDI_PIN * 2U)) & 3U) == 1U);
  assert((gpioe.otyper & OOMWOO_STM32G473_VM_VBAT_EN_MASK) == 0U);
  assert((gpiod.otyper & OOMWOO_STM32G473_WDI_MASK) == 0U);
  assert(((gpioe.ospeedr >>
           (OOMWOO_STM32G473_VM_VBAT_EN_PIN * 2U)) &
          3U) == 0U);
  assert(((gpiod.pupdr >> (OOMWOO_STM32G473_WDI_PIN * 2U)) & 3U) == 0U);
}

static void test_init_rejects_incomplete_register_maps(void) {
  fake_gpio_t gpio = {0U, 0U, 0U, 0U, 0U};
  oomwoo_stm32g473_gpio_registers_t registers = registers_for(&gpio);
  oomwoo_stm32g473_safety_t safety;

  assert(!oomwoo_stm32g473_safety_init(NULL, &registers, &registers));
  assert(!oomwoo_stm32g473_safety_init(&safety, NULL, &registers));
  registers.bsrr = NULL;
  assert(!oomwoo_stm32g473_safety_init(&safety, &registers, &registers));
}

static void test_hard_stop_sets_active_low_enable_high(void) {
  fake_gpio_t gpiod = {0U, 0U, 0U, 0U, 0U};
  fake_gpio_t gpioe = {0U, 0U, 0U, 0U, 0U};
  const oomwoo_stm32g473_gpio_registers_t d = registers_for(&gpiod);
  const oomwoo_stm32g473_gpio_registers_t e = registers_for(&gpioe);
  oomwoo_stm32g473_safety_t safety;

  assert(oomwoo_stm32g473_safety_init(&safety, &d, &e));
  gpioe.bsrr = 0U;
  oomwoo_stm32g473_safety_hard_stop_isr(
      &safety, OOMWOO_CPU_STOP_HEARTBEAT_TIMEOUT);
  assert(gpioe.bsrr == OOMWOO_STM32G473_VM_VBAT_EN_MASK);
  assert(oomwoo_stm32g473_safety_last_stop_reason(&safety) ==
         OOMWOO_CPU_STOP_HEARTBEAT_TIMEOUT);
}

static void test_wdi_edges_are_foreground_driven(void) {
  fake_gpio_t gpiod = {0U, 0U, 0U, 0U, 0U};
  fake_gpio_t gpioe = {0U, 0U, 0U, 0U, 0U};
  const oomwoo_stm32g473_gpio_registers_t d = registers_for(&gpiod);
  const oomwoo_stm32g473_gpio_registers_t e = registers_for(&gpioe);
  oomwoo_stm32g473_safety_t safety;

  assert(oomwoo_stm32g473_safety_init(&safety, &d, &e));
  oomwoo_stm32g473_safety_toggle_wdi_foreground(&safety);
  assert(gpiod.bsrr == OOMWOO_STM32G473_WDI_MASK);
  oomwoo_stm32g473_safety_toggle_wdi_foreground(&safety);
  assert(gpiod.bsrr == (OOMWOO_STM32G473_WDI_MASK << 16U));
}

int main(void) {
  test_init_is_fail_closed();
  test_init_rejects_incomplete_register_maps();
  test_hard_stop_sets_active_low_enable_high();
  test_wdi_edges_are_foreground_driven();
  puts("4 STM32G473 safety HAL tests passed");
  return 0;
}
