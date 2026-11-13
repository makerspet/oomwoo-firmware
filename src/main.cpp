#include <Arduino.h>

#include "oomwoo_cpu_ingress.h"
#include "oomwoo_identity.h"

#if defined(OOMWOO_IO_BOARD_STM32G473)
#include <HardwareTimer.h>

#include "oomwoo_cpu_watchdog_bridge.h"
#include "oomwoo_stm32g473_safety.h"
#endif

#ifndef OOMWOO_FIRMWARE_VERSION_MAJOR
#define OOMWOO_FIRMWARE_VERSION_MAJOR 0u
#endif

#ifndef OOMWOO_FIRMWARE_VERSION_MINOR
#define OOMWOO_FIRMWARE_VERSION_MINOR 1u
#endif

#ifndef OOMWOO_FIRMWARE_BUILD_ID
#define OOMWOO_FIRMWARE_BUILD_ID 0u
#endif

namespace {

constexpr uint32_t kFramingGapMs = 50u;

#if defined(OOMWOO_IO_BOARD_STM32G473)
constexpr uint32_t kWatchdogFrequencyHz = 1000U;
constexpr uint32_t kWdiHalfPeriodMs = 5U;
#endif

oomwoo_cpu_ingress_t ingress;
oomwoo_identity_t identity;
uint8_t tx_buffer[OOMWOO_IDENTITY_HELLO_FRAME_SIZE];
uint32_t last_rx_ms = 0u;

#if defined(OOMWOO_IO_BOARD_STM32G473)
oomwoo_cpu_watchdog_t cpu_watchdog;
oomwoo_cpu_watchdog_bridge_t cpu_watchdog_bridge;
oomwoo_stm32g473_safety_t board_safety;
HardwareTimer watchdog_timer(TIM7);
HardwareSerial cpu_serial(PC5, PC4);  // USART1 RX/TX on the OOMWOO schematic
volatile uint32_t watchdog_tick_count = 0U;
uint32_t last_wdi_toggle_ms = 0U;
#define OOMWOO_CPU_SERIAL cpu_serial
#else
#define OOMWOO_CPU_SERIAL Serial
#endif

void send_hello() {
  size_t frame_length = 0u;

  if (oomwoo_identity_emit_hello(&identity, tx_buffer, sizeof(tx_buffer),
                                 &frame_length) == OOMWOO_IDENTITY_OK) {
    OOMWOO_CPU_SERIAL.write(tx_buffer, frame_length);
  }
}

void handle_message(const oomwoo_decoded_frame_t *,
                    const oomwoo_message_t *message, void *) {
  size_t frame_length = 0u;

#if defined(OOMWOO_IO_BOARD_STM32G473)
  (void)oomwoo_cpu_watchdog_bridge_handle_message(&cpu_watchdog_bridge,
                                                   message);
#endif

  if (oomwoo_identity_handle_message(&identity, message, tx_buffer,
                                     sizeof(tx_buffer), &frame_length) ==
      OOMWOO_IDENTITY_OK) {
    OOMWOO_CPU_SERIAL.write(tx_buffer, frame_length);
  }
}

#if defined(OOMWOO_IO_BOARD_STM32G473)

void watchdog_tick_isr() {
  ++watchdog_tick_count;
  (void)oomwoo_cpu_watchdog_tick_isr(&cpu_watchdog,
                                     watchdog_tick_count);
}

bool init_board_safety() {
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  const oomwoo_stm32g473_gpio_registers_t gpiod = {
      &GPIOD->MODER,
      &GPIOD->OTYPER,
      &GPIOD->OSPEEDR,
      &GPIOD->PUPDR,
      &GPIOD->BSRR,
  };
  const oomwoo_stm32g473_gpio_registers_t gpioe = {
      &GPIOE->MODER,
      &GPIOE->OTYPER,
      &GPIOE->OSPEEDR,
      &GPIOE->PUPDR,
      &GPIOE->BSRR,
  };
  if (!oomwoo_stm32g473_safety_init(&board_safety, &gpiod, &gpioe)) {
    return false;
  }

  const oomwoo_cpu_watchdog_config_t config = {
      OOMWOO_CPU_WATCHDOG_INITIAL_TIMEOUT_TICKS_1KHZ,
      oomwoo_stm32g473_safety_hard_stop_isr,
      &board_safety,
  };
  if (!oomwoo_cpu_watchdog_init(&cpu_watchdog, &config) ||
      !oomwoo_cpu_watchdog_bridge_init(&cpu_watchdog_bridge,
                                       &cpu_watchdog)) {
    return false;
  }

  watchdog_timer.setOverflow(kWatchdogFrequencyHz, HERTZ_FORMAT);
  watchdog_timer.attachInterrupt(watchdog_tick_isr);
  watchdog_timer.resume();
  return true;
}

void service_external_watchdog_foreground(uint32_t now_ms) {
  if (static_cast<uint32_t>(now_ms - last_wdi_toggle_ms) >=
      kWdiHalfPeriodMs) {
    last_wdi_toggle_ms = now_ms;
    oomwoo_stm32g473_safety_toggle_wdi_foreground(&board_safety);
  }
}

#endif

}  // namespace

void setup() {
#if defined(OOMWOO_IO_BOARD_STM32G473)
  if (!init_board_safety()) {
    for (;;) {
      __WFI();
    }
  }
#endif

  oomwoo_cpu_ingress_init(&ingress, handle_message, nullptr);
  oomwoo_identity_init(
      &identity, static_cast<uint16_t>(OOMWOO_FIRMWARE_VERSION_MAJOR),
      static_cast<uint16_t>(OOMWOO_FIRMWARE_VERSION_MINOR),
      static_cast<uint32_t>(OOMWOO_FIRMWARE_BUILD_ID), UINT16_C(0));
  OOMWOO_CPU_SERIAL.begin(115200);
  send_hello();
}

void loop() {
  const uint32_t now_ms = millis();

  while (OOMWOO_CPU_SERIAL.available() > 0) {
    const int value = OOMWOO_CPU_SERIAL.read();
    if (value >= 0) {
      const uint8_t byte = static_cast<uint8_t>(value);
      last_rx_ms = now_ms;
      oomwoo_cpu_ingress_feed(&ingress, &byte, 1u);
    }
  }

  if (ingress.decoder.buffered_bytes != 0u &&
      static_cast<uint32_t>(now_ms - last_rx_ms) > kFramingGapMs) {
    oomwoo_cpu_ingress_reset_incomplete(&ingress);
  }

#if defined(OOMWOO_IO_BOARD_STM32G473)
  service_external_watchdog_foreground(now_ms);
#endif
}
