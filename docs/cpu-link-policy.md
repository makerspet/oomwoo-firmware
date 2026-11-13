# CPU link safety policy

This document defines the liveness and return-channel rules confirmed in
[`makerspet/oomwoo-firmware#1`](https://github.com/makerspet/oomwoo-firmware/issues/1#issuecomment-5881016172).
They are normative for CPU/MCU integration. Hardware reaction-time claims still
require measurement on the production path.

## Heartbeat safe-stop

- The initial bring-up deadline is 150 ms, represented by
  `OOMWOO_CPU_WATCHDOG_INITIAL_TIMEOUT_TICKS_1KHZ` at a 1 kHz watchdog rate.
- Only a fully validated `STACK_HEALTHY` heartbeat refreshes the local MCU
  deadline. The MCU uses its local tick, not the untrusted CPU timestamp.
- A validated `DISARMED` heartbeat forces the actuator-safe state at the next
  watchdog tick.
- Missing the deadline forces all motors and actuators safe and invalidates
  motion-command latches. A later heartbeat cannot replay a stale command.
- The final production deadline may be tuned only after measuring worst-case
  ISR-to-electrical-cutoff latency under representative load.

## CPU reset supervision

The heartbeat safe-stop and CPU reset are independent mechanisms. Short
heartbeat loss must never reset the CPU.

CPU reset supervision uses a much longer timeout, initially approximately five
minutes, so normal boot and recovery have time to complete. Its implementation,
liveness criteria, reset-reason reporting, and hardware validation are outside
the heartbeat watchdog module and require separate review.

## Return channel while disarmed

Disarming outputs does not silence non-actuating MCU state. While disarmed, the
MCU continues to emit:

- `SAFETY_STATE` at its normal periodic and event-driven rate;
- `MCU_DIAGNOSTIC` after its payload is defined;
- `POWER_TELEMETRY` after its board-side fields and payload are defined.

These messages must not arm an output, replay a command, clear a latched fault,
or refresh the heartbeat deadline. Undefined diagnostics and power payloads
remain fail-closed until their contracts are reviewed.

## Implementation status

The identity service, validated CPU ingress, bounded ingress-to-watchdog bridge,
and compile-tested STM32G473 shutdown HAL are implemented. The production target
maps PD8 to the external `WDI`, maps PE10 to active-low `VM-VBAT` enable, runs the
heartbeat deadline from TIM7, and leaves the motor rail disabled. The watchdog
core, production HAL, and wire-v1 Nucleo HIL harness remain under safety review.
Disarmed telemetry scheduling, CPU reset supervision, physical fault injection,
command-latch integration, and measured electrical cutoff latency remain open
work.
