#include <Arduino.h>
#include <HardwareTimer.h>

#include "oomwoo_cpu_watchdog.h"

#include <stdint.h>

namespace {

constexpr uint32_t kWatchdogFrequencyHz = 1000U;
constexpr uint32_t kWatchdogTimeoutTicks = 150U;
constexpr uint32_t kMotorEnablePin = PA8;  // Nucleo D7
constexpr uint32_t kHeartbeatMarkerPin = PA9;  // Nucleo D8

oomwoo_cpu_watchdog_t g_watchdog;
HardwareTimer g_watchdog_timer(TIM7);
volatile uint32_t g_tick_count = 0U;
volatile uint32_t g_motor_command_latched = 0U;
volatile uint32_t g_last_stop_reason = OOMWOO_CPU_STOP_NONE;
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
  Serial.println(g_last_stop_reason);
}

void print_help() {
  Serial.println(
      "OOMWOO watchdog HIL: H=healthy M=motor D=disarm B=block S=status ?=help");
}

void block_foreground_forever() {
  Serial.println("BLOCKING foreground; timer ISR must still stop D7");
  Serial.flush();
  for (;;) {
    __NOP();
  }
}

void handle_command(char command) {
  switch (command) {
    case 'H':
    case 'h':
      toggle_heartbeat_marker();
      Serial.println(oomwoo_cpu_watchdog_submit_heartbeat(
                         &g_watchdog, OOMWOO_CPU_MODE_STACK_HEALTHY)
                         ? "HEARTBEAT queued"
                         : "HEARTBEAT rejected");
      break;
    case 'M':
    case 'm':
      Serial.println(request_motor_enable() ? "MOTOR enabled" : "MOTOR rejected");
      break;
    case 'D':
    case 'd':
      Serial.println(oomwoo_cpu_watchdog_submit_heartbeat(
                         &g_watchdog, OOMWOO_CPU_MODE_DISARMED)
                         ? "DISARM queued"
                         : "DISARM rejected");
      break;
    case 'B':
    case 'b':
      block_foreground_forever();
      break;
    case 'S':
    case 's':
      print_status();
      break;
    case '?':
      print_help();
      break;
    case '\r':
    case '\n':
    case ' ':
    case '\t':
      break;
    default:
      Serial.println("UNKNOWN command");
      break;
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

  g_watchdog_timer.setOverflow(kWatchdogFrequencyHz, HERTZ_FORMAT);
  g_watchdog_timer.attachInterrupt(watchdog_tick_isr);
  g_watchdog_timer.resume();

  print_help();
  print_status();
}

void loop() {
  while (Serial.available() > 0) {
    handle_command(static_cast<char>(Serial.read()));
  }
}
