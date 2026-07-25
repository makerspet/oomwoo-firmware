# CPU heartbeat watchdog core

Status: safety-review draft. Do not connect motor loads until the hard-stop path
and reaction time have been validated on the target board.

## Safety contract

The watchdog is a dependency-free C module intended for the STM32G473 timer-ISR
layer. It does not parse serial frames and does not depend on Arduino, FreeRTOS,
ROS 2, or dynamic allocation.

The serial task may submit a heartbeat only after the frame version, length,
CRC, message type, payload length, and `cpu_mode` have been validated. The
watchdog uses the MCU's local tick; the untrusted `cpu_time_ms` value cannot
extend the deadline.

At a 1 kHz timer rate, configure `timeout_ticks = 150` for the current 150 ms
bench proposal. This value remains subject to the decision in firmware issue
#1.

The states are:

| Input/state | ISR behavior |
|---|---|
| Boot | Invoke the hard-stop callback and remain unarmed. |
| `STACK_HEALTHY` | Record the local tick, clear an active timeout, and arm the health gate. |
| `DISARMED` | Invoke the hard-stop callback at the next watchdog tick and unarm. |
| No healthy heartbeat for `timeout_ticks` | Latch the timeout, invoke hard stop once, and unarm. |
| Fresh heartbeat after a timeout | Clear the active condition, but retain the diagnostic latch. |

A healthy heartbeat only opens the watchdog's health gate. It never restores
PWM or replays an old command. The hard-stop callback must invalidate all
motion command latches, so recovery also requires a fresh, bounded setpoint.

## Context and ownership

Call `oomwoo_cpu_watchdog_init` after configuring the safe GPIO/PWM state and
before enabling the timer IRQ. Successful initialization immediately invokes
the callback with `OOMWOO_CPU_STOP_BOOT`.

Call `oomwoo_cpu_watchdog_submit_heartbeat` from exactly one producer context.
It writes the latest validated mode through one naturally aligned 32-bit
mailbox. The STM32 Cortex-M4 performs that load/store atomically.

Call `oomwoo_cpu_watchdog_tick_isr` from one fixed-rate timer ISR.
`oomwoo_cpu_watchdog_clear_timeout_latch_isr` is also ISR-only. This ownership
keeps all safety-state transitions in the ISR.

The hard-stop callback runs inside the timer ISR. It must be bounded,
non-blocking, allocation-free, and limited to direct motor-power/PWM shutdown,
command invalidation, and ISR-safe diagnostic flag updates. It must not log,
wait on a lock, use a queue that can block, or call Arduino APIs.

## Timing and wraparound

Timeout comparison uses unsigned subtraction, so a wrapping 32-bit tick is
handled correctly. `timeout_ticks` must be in the range `1..UINT32_MAX/2`.

With a 1 ms timer and no higher-priority ISR delay, hard stop is requested on
the first watchdog ISR for which:

```text
(uint32_t)(now_tick - last_healthy_tick) >= 150
```

This is a code-level bound, not a measured hardware reaction time. Before motor
loads are enabled, toggle a spare GPIO in the heartbeat-receive path and in the
hard-stop callback, then measure worst-case latency under interrupt and task
load. Add the measured ISR jitter and the electrical motor-power/PWM shutdown
delay to the hazard record.

## Hazard note

| Hazard | Mitigation in this module | Integration evidence still required |
|---|---|---|
| Linux/ROS 2 hangs while motors move | Local MCU deadline; stop is ISR-owned. | Fault-inject a CPU/serial hang under maximum firmware load. |
| Arduino or FreeRTOS task stalls | Timer ISR owns expiry and direct stop callback. | Deliberately hang the task layer and measure stop latency. |
| Stale command resumes after reconnect | Stop callback must invalidate commands; heartbeat alone never writes PWM. | Verify a new setpoint is required after recovery. |
| Corrupt frame refreshes watchdog | Only the validated decoder may submit a mode. | CRC, length, version, and invalid-mode integration tests. |
| Tick counter wraps | Unsigned elapsed-time comparison. | Host wraparound test and Cortex-M4 build. |
| Stop callback blocks or misses an output | Callback contract is ISR-safe and direct. | Maintainer HAL review plus GPIO/PWM and motor-power bench tests. |

The module is not a substitute for cliff, wheel-drop, bumper, overcurrent, or
MCU independent-watchdog protection. Those remain separate MCU-owned safety
channels.
