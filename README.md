<div align="center">

# OOMWOO I/O Firmware

*Open-source robot vacuum you build yourself.*

STM32G473 · Arduino · FreeRTOS · Safety · Motors · Sensors · Charging

![License](https://img.shields.io/badge/license-Apache--2.0-blue)
![Status](https://img.shields.io/badge/status-RFC%20%C2%B7%20not%20started-orange)
[![Part of OOMWOO](https://img.shields.io/badge/part%20of-OOMWOO-5eead4)](https://github.com/makerspet/oomwoo)

</div>

MCU firmware for the OOMWOO [I/O board](https://github.com/makerspet/oomwoo-io-board),
targeting an **STM32G473VCT6**. Arduino (STM32duino) API on top, FreeRTOS for task
structure, and a HAL/timer-ISR real-time core underneath.

> **Status — RFC / not started.** This is a request for contribution: the plan
> below is the design intent, and the code doesn't exist yet. Say hi in
> [Discussions](https://github.com/makerspet/oomwoo/discussions) or on
> [Discord](https://discord.gg/3y2JKz5T25) if you want to build it.

OOMWOO splits compute across two processors ([ARCHITECTURE.md §5.4](https://github.com/makerspet/oomwoo/blob/main/docs/ARCHITECTURE.md)):
a **CPU** (CM4/CM5-class module) runs ROS 2, SLAM, Nav2, and behaviour; the **MCU**
on this I/O board owns *motors, encoders, sensors, battery charging, and hard
safety*. This repo is that MCU firmware. Its defining constraint: **safety must
never depend on Linux/ROS 2** — and, as you'll see below, it must never depend on
the friendly Arduino layer either.

## Why STM32G473VCT6

The board drives ~10 actuators and reads a dozen-plus analog channels, so the MCU
needs headroom the earlier STM32G0 (Cortex-M0+, no FPU) didn't comfortably have:

- **Cortex-M4F @ up to 170 MHz with an FPU** — real control math (PID, filters,
  odometry) in hardware float, with cycles to spare for FreeRTOS + the Arduino layer.
- **Many timers incl. HRTIM** — enough PWM channels for every motor, plus the
  high-resolution timer for clean BLDC/fan drive.
- **5× 12-bit ADCs** — the board has many simultaneous analog channels (per-motor
  current sense, `VBat`, source current, 4× cliff IR, 2× dock IR, 2× side IR); five
  ADCs let safety-critical currents be sampled fast and independently.
- **CORDIC + FMAC** math accelerators, 512 KB flash / 128 KB RAM, **LQFP100** (hand-
  solderable, JLCPCB-friendly).

STM32duino supports the G4 family (Nucleo-G474 is a good bring-up board), and
FreeRTOS is available via the `STM32FreeRTOS` library — so "Arduino API + FreeRTOS"
is a supported, real combination on this part.

## Architecture — three layers (the core design decision)

The usual "friendly Arduino **or** deterministic real-time safety" trade-off is a
false choice here. We get both by **layering**, so the layer a contributor touches
is not the layer that keeps the robot safe:

| Layer | What | Owned by | Determinism |
|---|---|---|---|
| **3 — Arduino (STM32duino) API** | New behaviours, features, and peripheral bring-up live here. This is the contributor-friendly surface. | community | best-effort |
| **2 — FreeRTOS tasks** (static allocation) | Task structure: CPU-serial comms, control loop, telemetry, charging supervisor, safety supervisor. Watchdog-fed, bounded reaction times. | maintainer + community | soft real-time |
| **1 — Real-time core** (HAL + timer ISRs) | Motor commutation/PWM, encoder capture, the hard-safety cutoffs, the CPU watchdog. Runs in hardware timers / ISRs. | maintainer, safety-reviewed | **hard real-time** |

**The rule that makes this safe:** contributor code lives at the Arduino level;
the motor-control and safety core is HAL/ISR and **structurally isolated** from it.
A bug or an infinite loop in someone's Arduino feature **cannot** defeat a cliff
stop, an overcurrent cutoff, or the CPU watchdog — those live in interrupts and a
hardware watchdog that the upper layers cannot starve. That's how you get the
community-attraction of Arduino *and* the real-time integrity a robot needs.

## Safety (non-negotiable, CE-oriented)

Hard safety runs on the MCU, independent of *both* Linux/ROS 2 and the Arduino
layer:

- **Bumper / cliff / wheel-drop → immediate motor stop**, at ISR level.
- **Per-motor overcurrent limiting** — a stuck brush, jammed wheel, or stall is
  current-limited or cut before thermal/mechanical damage.
- **CPU watchdog** — if the CPU's periodic health packets stop, the MCU stops the
  motors and can assert the CPU-reset line.
- **MCU independent watchdog (IWDG)**, static memory allocation, and *measured,
  documented* worst-case reaction times.

Per the project's safety-review gate, **safety-critical changes require maintainer
review before merge** and a short hazard note (over-current, thermal, short,
mechanical pinch).

## What the firmware owns

Source of truth is the board [SPEC.md](https://github.com/makerspet/oomwoo-io-board/blob/main/docs/SPEC.md)
(work in progress — treat it as authoritative over this summary, and note its open
TODOs, e.g. the GPIO 36/46 bumper-label question):

- **Actuators:** 2× drive wheels (H-bridge + hall encoders), suction fan (BLDC, PWM
  + FG feedback), main brush, side brush(es), 2D-LiDAR spin motor, water pump, mop
  motors, and the mop-lift / mop-arm / side-brush-arm servos — each drive path with
  current sense where the board provides it.
- **Sensors:** 4× cliff IR, 2× dock IR, 2× side-proximity IR (+ their IR-LED PWM),
  2× bumper switches, 2× wheel-drop switches, wheel encoders, IMU (SPI + interrupts
  + FSYNC), and the current-sense/`VBat`/source-current analog channels.
- **Power & charging:** supervise the power-path charger (0.5C charge cap, input
  DPM, USB-C PD *and* dock input, graceful "insufficient charger" handling), plus
  `motors power enable`, `vacuum power`, `CPU power on/off`, and `CPU reset`.
- **HMI:** power/home buttons and power/home LEDs.

## CPU ↔ MCU link

A **custom serial protocol over UART** — deliberately **not** micro-ROS, so the
safety core carries no heavyweight third-party dependency (and no risk of an
upstream library update slipping a bug into safety-critical firmware). The framing,
command set, telemetry, and health/watchdog handshake are being defined in the
[io-board-interface RFC](https://github.com/makerspet/oomwoo/tree/main/contributions/io-board-interface).
The MCU: accepts **bounded, expiring** commands (drive setpoints, motor/actuator
commands); publishes telemetry (encoders, battery/charge state, bumper/cliff/
wheel-drop, per-motor current); and enforces the health handshake that backs the
CPU watchdog. During development, the CPU side can be stood in for by the simulated
MCU serial tool in [oomwoo-install](https://github.com/makerspet/oomwoo-install).

## Toolchain

- **STM32duino** (the STM32 Arduino core) via the Arduino IDE **or** PlatformIO;
  G473 through the generic-G4 board definition (bring up on a Nucleo-G474 first).
- **FreeRTOS** via `STM32FreeRTOS`.
- **SWD** debug (ST-Link) on the board's `SWDIO`/`SWCLK` header; test/program pads.

## Request for contribution — bring-up milestones

Phased, each testable on the bench before the board even exists (start on a
Nucleo-G474, move to the real board when it's fabbed):

1. **Blink + SWD + serial echo** on a G473 dev board.
2. **CPU serial link** — implement the io-board-interface framing + health/watchdog
   handshake; loopback and echo tests green.
3. **One drive motor, closed loop** — H-bridge PWM + encoder capture + velocity PID
   in the real-time core. This is the pattern every other motor follows.
4. **All actuators** — fan (BLDC + FG), brushes, LiDAR spin, pump, mop motors/servos,
   each with current sense.
5. **All sensors** — cliff/dock/side IR (ADC), bumpers, wheel-drop, IMU (SPI),
   current channels.
6. **Safety layer** — ISR-level cliff/bumper/wheel-drop stop, overcurrent limiting,
   IWDG, CPU watchdog/reset; **measure and document** each cutoff's worst-case
   reaction time; hazard note.
7. **Charging supervisor** — power-path charger control, 0.5C cap, input DPM,
   insufficient-charger handling.
8. **Integration** — run end-to-end against the CPU (or the simulated MCU serial
   tool) driving the ROS 2 hardware bridge.

## Acceptance criteria

- **Deterministic real-time core** — measured, documented worst-case reaction time
  for each safety cutoff.
- **Safety is layer-independent** — a deliberately hung Arduino-level task must
  *not* defeat a cliff-stop, overcurrent cutoff, or the CPU watchdog. Demonstrate it.
- **Implements the io-board-interface serial contract** — loopback + integration
  tested.
- **Every actuator and sensor exercised on the bench**, documented, reproducible by
  someone else.
- **Charging behaves per spec** — 0.5C cap held; graceful degradation on a weak
  charger.
- **Safety-critical code passed maintainer safety review** with a hazard note.

## References

- Board: [oomwoo-io-board](https://github.com/makerspet/oomwoo-io-board) · [SPEC.md](https://github.com/makerspet/oomwoo-io-board/blob/main/docs/SPEC.md)
- CPU↔MCU contract: [io-board-interface RFC](https://github.com/makerspet/oomwoo/tree/main/contributions/io-board-interface)
- System architecture: [ARCHITECTURE.md §5.4](https://github.com/makerspet/oomwoo/blob/main/docs/ARCHITECTURE.md)
- [STM32duino](https://github.com/stm32duino/Arduino_Core_STM32) · [STM32FreeRTOS](https://github.com/stm32duino/STM32FreeRTOS)
- [Project Discussions](https://github.com/makerspet/oomwoo/discussions) · [Discord](https://discord.gg/3y2JKz5T25)

## License

[Apache License 2.0](LICENSE). Contributions are made on that basis; safety-critical
firmware additionally passes maintainer safety review before merge.
