<div align="center">

# OOMWOO I/O Firmware

*Open-source robot vacuum you build yourself*

CH32V307 + CH32L103 · Bare-metal C · Safety · Motors · Sensors · Charging

![License](https://img.shields.io/badge/License-Apache--2.0-blue)
![Status](https://img.shields.io/badge/Status-Starter%20firmware-orange)
[![Part of OOMWOO](https://img.shields.io/badge/Part%20of-OOMWOO-5eead4)](https://github.com/makerspet/oomwoo)

</div>

MCU firmware for the OOMWOO [main-phone board](https://github.com/makerspet/oomwoo-pcb/tree/main/kicad/main-phone):
a **WCH CH32V307VCT6** (U13: motors, sensors, safety, camera, USB to the phone) and a
**CH32L103C8T6** companion (U28: power latch, buttons, charger status, USB-C roles),
plus the charging dock's **CH32V003F4P6**.
Bare-metal C with interrupt-driven real-time parts, built with upstream GCC and make.
Start with the [firmware plan](docs/firmware-plan.md); the starter firmware is in [`mcu/`](mcu).

> **Status: starter firmware.** All three images (U13, U28, dock) build in CI, and the
> shared logic passes its host tests. Nothing has run on silicon yet: WCH's evaluation
> boards come first. Not written yet: USB to the phone, camera capture, the ToF and IMU
> drivers, the wheel control loop and odometry. Say hi in
> [Discussions](https://github.com/makerspet/oomwoo/discussions) or on
> [Discord](https://discord.gg/3y2JKz5T25) if you want to help.

OOMWOO splits compute across two processors ([ARCHITECTURE.md §5.4](https://github.com/makerspet/oomwoo/blob/main/docs/ARCHITECTURE.md)):
the **CPU** (an Android phone on the first board, a Raspberry Pi Compute Module later)
runs mapping, navigation and the app; the **MCUs** on the I/O board own *motors,
encoders, sensors, battery charging, and hard safety*. This repo is that MCU firmware.
Its defining constraint: **safety must never depend on the CPU**, and it must never
depend on feature code either.

## Why CH32V307 + CH32L103

The STM32G473 went to $7.39 with 20 in stock; the CH32V307 + CH32L103 pair costs about
$2.60, adds a parallel camera port and high-speed USB with a built-in PHY, and leaves the
slow power and UI work to a separate MCU. Details: [mcu-ch32v307.md](https://github.com/makerspet/oomwoo-pcb/blob/main/docs/mcu-ch32v307.md)
in the PCB repo.

## Architecture

**Bare metal and interrupts, no RTOS yet** (details in the [firmware plan](docs/firmware-plan.md#architecture)):

| Part | What runs there | Determinism |
|---|---|---|
| **Interrupts** | Anything with a hard deadline: cliff LED timing, encoder and IR edges, the fan tachometer, the UARTs, and the hazard stops. | hard real-time |
| **Control tick** (1 kHz SysTick, main loop) | The drive gate, motor commands, sensor scans, the WDI toggle, telemetry, the U13 ↔ U28 link, the bench console. | bounded, watchdog-checked |
| **Later: FreeRTOS** (WCH ships a V307 port) | USB to the phone, camera and ToF work, if they need it. The safety tick stays in an interrupt. | soft real-time |

**The rule that makes this safe:** the safety core is **structurally isolated** from
feature code. A bug or an infinite loop in a new feature **cannot** defeat a cliff
stop, an overcurrent cutoff, or the CPU watchdog: those live in interrupts and in
hardware watchdogs that the main loop can't starve. The Arduino version keeps the
same rule: its safety tick runs from a hardware timer, so `loop()` is free to hack.

## Safety (non-negotiable, CE-oriented)

Hard safety runs on the MCUs, independent of the CPU and of feature code:

- **Hardware watchdog:** the motor rail needs WD_OK, and WD_OK needs WDI toggling.
  U13 toggles WDI once per control tick, and only when the tick came on time. Every
  actuator rail is off while the MCU is in reset or being flashed.
- **MCU watchdog (IWDG):** ~260 ms on U13.
- **Drive gate**, every tick: nothing runs unless the robot is armed, the motor rail is
  on, U28 is alive, there is no power-off request, and the motion command hasn't
  expired. A wheel drop stops everything. A front cliff or a bumper blocks forward
  motion only, so the robot can back away; a rear cliff blocks reverse.
- **Per-motor overcurrent limiting:** a stuck brush, jammed wheel, or stall is
  current-limited or cut before thermal or mechanical damage (being characterized).
- **CPU watchdog:** if the CPU's periodic health messages stop, the MCU stops the
  motors.
- Static memory only, and *measured, documented* worst-case reaction times.

Per the project's safety-review gate, **safety-critical changes require maintainer
review before merge** and a short hazard note (over-current, thermal, short,
mechanical pinch).

## What the firmware owns

Source of truth is the board [SPEC.md](https://github.com/makerspet/oomwoo-pcb/blob/main/docs/SPEC.md)
and the pin map in [`include/oomwoo_pins.h`](include/oomwoo_pins.h), generated from
the schematic.

- **U13 actuators:** 2× drive wheels and the main and side brushes (DRV8870 H-bridges,
  filtered current sense), the suction fan (BLDC controller, PWM + FG tachometer), the
  LiDAR motor.
- **U13 sensors:** cliff IR (pulsed LEDs), bumper photo-interrupters, wheel drops,
  wheel encoders, dock IR receivers (NEC beacon), 2× VL53L7 front ToF, the side
  VL53L0X, the LSM6DS3TR-C IMU, the DVP camera, the ultrasonic carpet sensor, the
  LiDAR UART.
- **U13 to the phone:** high-speed USB, CDC for the robot link and UVC for the camera.
- **U28 power and UI:** the power latch, buttons and button LEDs, charger status and
  charge current, battery, DC-in and VBUS voltages, the battery NTC and ID, the
  USB-C roles, the torch and the camera reset.
- **Dock (CH32V003):** USB-C PD contract check, robot detection by its resistance
  signature, the contact switch with a fast overcurrent trip, and the IR beacon.

## CPU ↔ MCU link

A **custom serial protocol**, deliberately **not** micro-ROS, so the safety core
carries no heavyweight third-party dependency. On the first board it runs over USB
CDC to the phone; on the Pi board, over a UART. The framing, command set, telemetry,
and health/watchdog handshake are defined in the
[io-board-interface RFC](https://github.com/makerspet/oomwoo/tree/main/contributions/io-board-interface).
The MCU accepts **bounded, expiring** commands (drive setpoints, motor and actuator
commands), publishes telemetry (encoders, battery and charge state, bumper, cliff,
wheel drop, motor current), and enforces the health handshake that backs the CPU
watchdog. During development, the simulated MCU serial tool in
[oomwoo-install](https://github.com/makerspet/oomwoo-install) can stand in for the board.

## Toolchain

- **CH32 firmware (`mcu/`):** xPack `riscv-none-elf-gcc` and GNU make (Linux, WSL2, CI);
  the WCH SDK is fetched at pinned commits, not committed. Flash with `wlink` over a
  WCH-LinkE, or `wchisp` through the chips' bootloaders; on Windows, `mcu/scripts/setup-windows.ps1`
  installs both. See the [firmware plan](docs/firmware-plan.md).

  ```bash
  mcu/scripts/setup.sh   # Linux/WSL2: toolchain, WCH SDK, wlink, wchisp, PATH
  make -C mcu            # build/robot/u13.bin, u28.bin and dock.bin
  make -C mcu test       # host tests of the shared logic
  ```
- **Arduino (`arduino/`, pilot):** the U28 firmware as an Arduino sketch on WCH's official core,
  with an OOMWOO board variant and an OOMWOO library (the same portable code). Hack `loop()`;
  the safety parts run from a timer interrupt. See [arduino/README.md](arduino/README.md).
- **Protocol core (`src/`, `tests/`):** portable C11, tested on the host.

## Protocol core

The CPU-link protocol is portable C, independent of motors and the board HAL, and is
reused by the U13 ↔ U28 link:

- heap-free C frame encoder/decoder and CRC-16/CCITT-FALSE
- bounded incremental UART decoder with corruption and receive-gap recovery
- heap-free typed payload codec with exact lengths and contract-defined bounds
- all 23 canonical wire-v1 vectors imported from the accepted interface contract
- CPU ingress gate that dispatches only CRC-, direction-, and payload-valid
  messages
- reconnect-safe `MCU_HELLO` identity service
- native Unity tests plus strict C11/C++17 sanitizer conformance tests

```bash
python -m pip install platformio==6.1.19
pio test -e native -e native_v2
```

The `nucleo_g474re` PlatformIO environments are left over from the STM32 bring-up;
they still build the identity harness but aren't the target any more.

See [CPU/MCU protocol bring-up](docs/protocol-bringup.md) and the
[CPU ingress gate](docs/cpu-ingress.md) for memory ownership, failure behavior,
typed payload validation, compatibility, and the explicit safety boundary. The
[MCU identity handshake](docs/identity-handshake.md) documents startup and
reconnect behavior.
Wire v1 is the build default; candidate v2 remains an
explicitly tested framing override while the payload-version decision is
tracked in
[`oomwoo-firmware#1`](https://github.com/makerspet/oomwoo-firmware/issues/1).

## Request for contribution: bring-up milestones

Phased, each testable on WCH's evaluation boards before the robot board exists
(details in the [firmware plan](docs/firmware-plan.md#milestones)):

1. **EVT bring-up:** console, clocks, IWDG reset, WDI toggling, the U13 ↔ U28 link
   between the two dev boards, the power-button state machine.
2. **USB HS on U13:** CDC, then the phone protocol (ingress, identity, heartbeat),
   then UVC with the DVP camera.
3. **Sensors:** VL53L7, the IMU FIFO with timestamps, the VL53L0X, LiDAR packet
   parsing with timestamps.
4. **Motion:** encoder speed, a wheel PID loop in a timer interrupt, odometry,
   per-motor current limits, staggered motor starts and the total-current budget.
5. **Power:** USB-C source/sink switching, the dock beacon budget, NTC limits, then the
   PD power-role swap.
6. **Updates:** U13 flashes U28 over USART2; U13 jumps to its own USB bootloader on
   command.
7. **Safety review:** measured worst-case reaction times for cliff, drop, bumper and
   watchdog, plus a hazard note.

## Acceptance criteria

- **Deterministic real-time core** — measured, documented worst-case reaction time
  for each safety cutoff.
- **Safety is independent of feature code** — a deliberately hung main-loop feature
  must *not* defeat a cliff stop, overcurrent cutoff, or the CPU watchdog. Demonstrate it.
- **Implements the io-board-interface serial contract** — loopback + integration
  tested.
- **Every actuator and sensor exercised on the bench**, documented, reproducible by
  someone else.
- **Charging behaves per spec** — 0.5C cap held; graceful degradation on a weak
  charger.
- **Safety-critical code passed maintainer safety review** with a hazard note.

## References

- Board: [oomwoo-pcb](https://github.com/makerspet/oomwoo-pcb) · [SPEC.md](https://github.com/makerspet/oomwoo-pcb/blob/main/docs/SPEC.md) · [mcu-ch32v307.md](https://github.com/makerspet/oomwoo-pcb/blob/main/docs/mcu-ch32v307.md)
- CPU↔MCU contract: [io-board-interface RFC](https://github.com/makerspet/oomwoo/tree/main/contributions/io-board-interface)
- System architecture: [ARCHITECTURE.md §5.4](https://github.com/makerspet/oomwoo/blob/main/docs/ARCHITECTURE.md)
- Set-up guide: [OOMWOO firmware: set up, build, flash and bring up the CH32 MCUs](https://makerspet.com/blog/oomwoo-firmware-ch32-setup-build-flash/)
- [WCH CH32V307 SDK and examples](https://github.com/openwch/ch32v307) · [CH32L103](https://github.com/openwch/ch32l103)
- [Project Discussions](https://github.com/makerspet/oomwoo/discussions) · [Discord](https://discord.gg/3y2JKz5T25)

## License

[Apache License 2.0](LICENSE). The WCH SDK files are fetched at build time and carry
WCH's own terms. Contributions are made on that basis; safety-critical firmware
additionally passes maintainer safety review before merge.
