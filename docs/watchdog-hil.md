# Nucleo G474RE watchdog HIL harness

Status: bench harness for the safety-review draft. It is not the production
OOMWOO board HAL and must not be connected to a motor driver or motor load.

## Purpose

This harness moves the CPU-watchdog evidence from host-only tests onto the
target MCU family. It verifies that:

- TIM7 invokes the watchdog from a real 1 kHz interrupt;
- heartbeat loss cuts an active-high test output after the configured 150 ticks;
- the cutoff still occurs if the Arduino foreground loop is permanently blocked;
- `DISARMED` cuts the output at the next watchdog tick;
- a fresh heartbeat after a stop opens only the health gate and never replays
  the invalidated motor command.

The 150 ms value remains a bring-up proposal pending firmware issue #1. This
harness provides a repeatable way to measure it; it does not make it final.

## Pins and equipment

Use a Nucleo G474RE, its ST-Link virtual serial port, and a two-channel logic
analyzer or oscilloscope sharing board ground.

| Signal | Nucleo pin | MCU pin | Meaning |
|---|---|---|---|
| `MOTOR_ENABLE_TEST` | D7 | PA8 | Active-high simulated motor-enable output; hard stop drives it low. |
| `HEARTBEAT_MARKER` | D8 | PA9 | Toggles when each healthy heartbeat is submitted. |
| Ground | GND | - | Logic-analyzer reference. |

PA8 and PA9 are test points only. The stop callback writes PA8 through the
STM32 `BSRR` register and performs no Arduino, serial, allocation, queue, or
locking operation. CI also disassembles the linked ARM image and fails if the
callback contains an ARM `bl` or `blx` call instruction.

## Build and flash

```bash
python3 -m pip install platformio==6.1.19
pio run -c platformio-watchdog-hil.ini -e nucleo_g474re_watchdog_hil
pio run -c platformio-watchdog-hil.ini -e nucleo_g474re_watchdog_hil -t upload
```

To repeat the leaf-function check locally after building:

```bash
~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-objdump \
  -d -C build/platformio-watchdog-hil/nucleo_g474re_watchdog_hil/firmware.elf \
  > build/platformio-watchdog-hil/nucleo_g474re_watchdog_hil/firmware.disassembly
python3 tools/check_isr_disassembly.py \
  build/platformio-watchdog-hil/nucleo_g474re_watchdog_hil/firmware.disassembly \
  hard_stop_isr
```

The ST-Link serial port runs at 115200 baud. The firmware accepts one-character
commands:

| Command | Effect |
|---|---|
| `H` | Submit one validated `STACK_HEALTHY` heartbeat and toggle D8. |
| `M` | Raise D7 only if the ISR has consumed a healthy heartbeat. |
| `D` | Submit `DISARMED`; the ISR must lower D7 and invalidate the motor command. |
| `B` | Permanently block the foreground loop; interrupts remain enabled. |
| `S` | Print watchdog and simulated-output state. |
| `?` | Print command help. |

`H` deliberately does not raise D7. This demonstrates that a heartbeat only
opens the health gate; a fresh motion command is still required.

## Reproducible scenarios

The dependency-free POSIX runner sends the command sequences at a 20 Hz
heartbeat rate:

```bash
python3 tools/watchdog_hil_host.py /dev/ttyACM0 loss
python3 tools/watchdog_hil_host.py /dev/ttyACM0 disarm
python3 tools/watchdog_hil_host.py /dev/ttyACM0 recovery
python3 tools/watchdog_hil_host.py /dev/ttyACM0 hang
```

Reset the board before each scenario, especially after `hang`.

For `loss`, measure from the final D8 transition to the D7 falling edge. The
heartbeat is consumed on the next 1 kHz timer tick, so the expected marker-to-
cutoff interval is 150-151 ms before accounting for interrupt latency. Repeat
under representative interrupt load and record minimum, maximum, sample count,
instrument, firmware commit, compiler version, and probe points.

For `hang`, D7 must fall even though no further serial command is processed.
For `recovery`, D7 must remain low after the recovering `H` and rise only after
the later `M` command.

## Result record

Copy this table into the PR comment when running the physical test:

| Field | Value |
|---|---|
| Firmware commit | |
| Nucleo board revision | |
| Compiler / PlatformIO version | GCC 12.3.1 / PlatformIO 6.1.19 |
| Instrument and sample rate | |
| Probe points | D8 heartbeat marker, D7 stop output |
| Concurrent interrupt/task load | |

| Scenario | Samples | Minimum | Maximum | Expected result | Pass |
|---|---:|---:|---:|---|---|
| Heartbeat loss | | | | D7 falls 150-151 ms after final D8 edge, plus measured ISR latency | |
| Foreground blocked | | | | D7 falls while the foreground remains blocked | |
| Explicit disarm | | | | D7 falls by the next 1 kHz watchdog tick | |
| Recovery | | | | `H` keeps D7 low; a later `M` may raise it | |

## Evidence still required

- Replace PA8 with the reviewed production GPIO/PWM shutdown sequence for the
  final OOMWOO PCB and verify every motion-capable output.
- Measure worst-case ISR jitter and electrical cutoff latency under maximum
  firmware and motor load.
- Verify command-latch invalidation at the production control-loop boundary.
- Add MCU independent-watchdog integration and reset-reason reporting.
- Record the current-limit and thermal behavior of the selected wheel driver.
