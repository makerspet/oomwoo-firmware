#include <Arduino.h>
#include <HardwareTimer.h>

#include "oomwoo_cpu_watchdog_bridge.h"

#include <stdint.h>

namespace {

constexpr uint32_t kWatchdogFrequencyHz = 1000U;
constexpr uint32_t kWatchdogTimeoutTicks =
    OOMWOO_CPU_WATCHDOG_INITIAL_TIMEOUT_TICKS_1KHZ;
constexpr uint32_t kFramingGapMs = 50U;
constexpr uint8_t kBlockForegroundControl = static_cast<uint8_t>('B');
constexpr uint8_t kStatusControl = static_cast<uint8_t>('S');
constexpr uint32_t kMotorEnablePin = PA8;  // Nucleo D7
constexpr uint32_t kHeartbeatMarkerPin = PA9;  // Nucleo D8

oomwoo_cpu_watchdog_t g_watchdog;
oomwoo_cpu_watchdog_bridge_t g_watchdog_bridge;
oomwoo_cpu_ingress_t g_ingress;
HardwareTimer g_watchdog_timer(TIM7);
volatile uint32_t g_tick_count = 0U;
volatile uint32_t g_motor_command_latched = 0U;
volatile uint32_t g_last_stop_reason = OOMWOO_CPU_STOP_NONE;
uint32_t g_last_rx_ms = 0U;
bool g_marker_high = false;

void set_motor_enable_direct(bool enabled) {
  GPIOA->BSRR = enabled
                    ? GPIO_PIN_8
                    : (static_cast<uint32_t>(GPIO_PIN_8) << 16U);
}

void toggle_heartbeat_marker() {
  g_marker_high = !g_marker_high;
  GPIOA->BSRR = g_marker_high
                    ? GPIO_PIN_9
                    : (static_cast<uint32_t>(GPIO_PIN_9) << 16U);
}

void hard_stop_isr(void *context, oomwoo_cpu_stop_reason_t reason) {
  (void)context;
  GPIOA->BSRR = static_cast<uint32_t>(GPIO_PIN_8) << 16U;
  g_motor_command_latched = 0U;
  g_last_stop_reason = static_cast<uint32_t>(reason);
}

void watchdog_tick_isr() {
  g_tick_count++;
  (void)oomwoo_cpu_watchdog_tick_isr(&g_watchdog, g_tick_count);
}

bool request_motor_enable() {
  const uint32_t previous_primask = __get_PRIMASK();
  __disable_irq();

  const bool permitted = oomwoo_cpu_watchdog_motion_permitted(&g_watchdog);
  if (permitted) {
    g_motor_command_latched = 1U;
    set_motor_enable_direct(true);
  }

  if (previous_primask == 0U) {
    __enable_irq();
  }
  return permitted;
}

void print_status() {
  Serial.print("STATUS tick=");
  Serial.print(g_tick_count);
  Serial.print(" permitted=");
  Serial.print(oomwoo_cpu_watchdog_motion_permitted(&g_watchdog) ? 1 : 0);
  Serial.print(" motor=");
  Serial.print(g_motor_command_latched);
  Serial.print(" timeout_active=");
  Serial.print(oomwoo_cpu_watchdog_timeout_active(&g_watchdog) ? 1 : 0);
  Serial.print(" timeout_latched=");
  Serial.print(oomwoo_cpu_watchdog_timeout_latched(&g_watchdog) ? 1 : 0);
  Serial.print(" stop_reason=");
  Serial.print(g_last_stop_reason);
  Serial.print(" submitted_heartbeats=");
  Serial.print(g_watchdog_bridge.submitted_heartbeats);
  Serial.print(" crc_errors=");
  Serial.print(g_ingress.decoder.stats.crc_errors);
  Serial.print(" value_errors=");
  Serial.println(g_ingress.stats.value_out_of_range);
}

void print_help() {
  Serial.println(
      "OOMWOO watchdog HIL: binary frames; B=block S=status");
}

void block_foreground_forever() {
  Serial.println("BLOCKING foreground; timer ISR must still stop D7");
  Serial.flush();
  for (;;) {
    __NOP();
  }
}

void handle_message(const oomwoo_decoded_frame_t *,
                    const oomwoo_message_t *message, void *) {
  const oomwoo_cpu_watchdog_bridge_result_t bridge_result =
      oomwoo_cpu_watchdog_bridge_handle_message(&g_watchdog_bridge, message);

  if (message->type == OOMWOO_MESSAGE_HEARTBEAT) {
    if (bridge_result != OOMWOO_CPU_WATCHDOG_BRIDGE_SUBMITTED) {
      Serial.println("HEARTBEAT rejected");
    } else if (message->payload.heartbeat.cpu_mode ==
               OOMWOO_CPU_MODE_STACK_HEALTHY) {
      toggle_heartbeat_marker();
      Serial.println("HEARTBEAT queued");
    } else {
      Serial.println("DISARM queued");
    }
  } else if (message->type == OOMWOO_MESSAGE_DRIVE_SETPOINT) {
    Serial.println(request_motor_enable() ? "MOTOR enabled"
                                         : "MOTOR rejected");
  }
}

}  // namespace

void setup() {
  pinMode(kMotorEnablePin, OUTPUT);
  pinMode(kHeartbeatMarkerPin, OUTPUT);
  set_motor_enable_direct(false);
  GPIOA->BSRR = static_cast<uint32_t>(GPIO_PIN_9) << 16U;

  Serial.begin(115200);

  const oomwoo_cpu_watchdog_config_t config = {
      kWatchdogTimeoutTicks,
      hard_stop_isr,
      nullptr,
  };
  if (!oomwoo_cpu_watchdog_init(&g_watchdog, &config)) {
    Serial.println("FATAL watchdog init failed");
    for (;;) {
      __WFI();
    }
  }
  if (!oomwoo_cpu_watchdog_bridge_init(&g_watchdog_bridge, &g_watchdog)) {
    Serial.println("FATAL watchdog bridge init failed");
    for (;;) {
      __WFI();
    }
  }
  oomwoo_cpu_ingress_init(&g_ingress, handle_message, nullptr);

  g_watchdog_timer.setOverflow(kWatchdogFrequencyHz, HERTZ_FORMAT);
  g_watchdog_timer.attachInterrupt(watchdog_tick_isr);
  g_watchdog_timer.resume();

  print_help();
  print_status();
}

void loop() {
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value >= 0) {
      const uint8_t byte = static_cast<uint8_t>(value);
      if (g_ingress.decoder.buffered_bytes == 0U &&
          byte == kBlockForegroundControl) {
        print_status();
        block_foreground_forever();
      }
      if (g_ingress.decoder.buffered_bytes == 0U &&
          byte == kStatusControl) {
        print_status();
        continue;
      }
      g_last_rx_ms = millis();
      (void)oomwoo_cpu_ingress_feed(&g_ingress, &byte, 1U);
    }
  }

  if (g_ingress.decoder.buffered_bytes != 0U &&
      static_cast<uint32_t>(millis() - g_last_rx_ms) > kFramingGapMs) {
    oomwoo_cpu_ingress_reset_incomplete(&g_ingress);
  }
}
