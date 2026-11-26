# MCU firmware plan: U13 (CH32V307) + U28 (CH32L103), and the dock (CH32V003)

The main-phone board has two WCH RISC-V MCUs. U13, a CH32V307VCT6, is the main MCU. U28, a CH32L103C8T6, is the power/UI companion. The pin maps are in [`include/oomwoo_pins.h`](../include/oomwoo_pins.h), which is generated from the schematic, and in the PCB repo's [mcu-ch32v307.md](https://github.com/makerspet/oomwoo-pcb/blob/main/docs/mcu-ch32v307.md).

This document covers:
- the starter firmware in [`mcu/`](../mcu);
- how to build and flash it;
- how it is structured;
- what writing it turned up about the schematic;
- the charging dock's firmware ([Charging dock](#charging-dock-ch32v003)).

## Status (2026-11-24)

- **Done:**
  - All three images build (U13, U28 and, since 2026-11-25, the dock).
  - The shared logic passes its host tests (`make -C mcu test`).
  - CI builds and tests on every push.
  - Nothing has run on silicon yet. The first targets are WCH's evaluation boards.
- **Written so far:**
  - **Board safety:** safe pin states on both MCUs, rail sequencing, the RC watchdog (WDI) and the IWDG.
  - **Inter-MCU:** the U13 ↔ U28 link.
  - **U28 power and status:**
    - the power latch and button state machine;
    - the button LEDs;
    - the charger status decoder;
    - the USB-C baseline roles (Type-C resistors only).
  - **U13 actuators:** motor PWM, the fan and its tachometer, the LiDAR motor and UART.
  - **U13 sensors:**
    - ADC scans on both MCUs;
    - pulsed cliff LEDs, bumper emitter PWM, wheel drops, encoders;
    - the IR receivers, with an NEC decoder;
    - the IMU data-ready line, I2C (hardware and bit-banged);
    - camera XCLK and sensor ID, carpet sensor ping.
  - **Bench tools:** a bench console on U13.
- **Not written yet** (see [Milestones](#milestones)):
  - USB high-speed CDC + UVC to the phone;
  - DVP capture;
  - VL53L7/VL53L0X/IMU drivers;
  - LiDAR packet parsing and timestamps;
  - the wheel control loop and odometry;
  - the phone protocol over USB;
  - U13 flashing U28;
  - the PD power-role swap.

## Toolchain

The toolchain is plain upstream GCC and GNU make. No MounRiver Studio is needed. Two scripts set everything up:

- **`mcu/scripts/setup.sh`** (Linux x64 or WSL2 Ubuntu): installs the build tools, the RISC-V GCC, the WCH SDK, `wlink` and `wchisp`, and the udev rules on native Linux. It adds a PATH block to `~/.bashrc`, then builds both images and runs the host tests. It is safe to run again.
- **`mcu/scripts/setup-windows.ps1`**: installs `wlink` (32-bit build, which works with WCH's own WCH-Link driver) and `wchisp` into `%LOCALAPPDATA%\oomwoo\wch` and adds that folder to the user PATH. On Windows, builds run in WSL2 and flashing runs on Windows, where the USB probe is.

| What | Version | Notes |
|---|---|---|
| xPack `riscv-none-elf-gcc` | 14.3.0-1 | Pinned and sha256-checked (`install-toolchain.sh`). |
| WCH SDK (peripheral library, startup, core) | pinned openwch commits | `fetch-wch-sdk.sh` → `mcu/third_party/wch`, ignored by git. The WCH files carry WCH's own terms, not Apache-2.0, so they are fetched rather than committed. |
| `wlink` | 0.1.3 | Flashing and debugging over the WCH-LinkE. Supports CH32V307 and CH32L103. |
| `wchisp` | 0.3.0 | Bootloader flashing: U13 over USB on PB6/PB7 (BOOT0 jumper), U28 over USART2 (`-s -p COMx`). |

```bash
mcu/scripts/setup.sh                    # once
make -C mcu                             # build/robot/u13.bin, build/robot/u28.bin
make -C mcu BOARD=evt u13               # U13 for WCH's CH32V307V-R1 board
make -C mcu test                        # host tests of the shared logic
```

- **Clone inside WSL** (for example `~/git`) rather than under `/mnt/c`: builds are several times faster. `.gitattributes` keeps the scripts and the Makefile in LF even in a Windows clone.
- **Flashing from Windows:** `wlink flash --address 0x08000000 build\robot\u13.bin`. The WCH-LinkE needs WCH's driver (WCH-LinkUtility). From WSL, attaching the probe with `usbipd-win` also works.
- **ISR attribute:** interrupt handlers use the standard `__attribute__((interrupt))`. WCH's `"WCH-Interrupt-fast"` attribute is not upstream GCC.
  - The WCH startup turns on the hardware prologue/epilogue, so registers are saved twice per interrupt. That is correct, just a few cycles slower. Switch to the fast path later if an ISR's latency matters.

## Layout

```
mcu/
  Makefile, ld/            our linker scripts (Apache-2.0)
  common/                  shared by both MCUs
    mcu_link.*             U13 <-> U28 messages (framed with src/oomwoo_protocol.c)
    power_sm.*             power latch / button state machine        (host-tested)
    charger_sm.*           SLM6900 ~CHRG/~STDBY decoding              (host-tested)
    drive_gate.*           which motors may run, and in which direction (host-tested)
    nec.*                  NEC IR decoder for the dock beacon         (host-tested)
    gpio.h systime.* uart.* cli.* fmt.*
  u13/                     CH32V307: board, motors, sensors, i2c, camera, carpet, lidar, link, main
  u28/                     CH32L103: board, typec, main
  dock/                    CH32V003 (charging dock): main (ADC, CH224A, beacon, console)
tests/mcu_logic_test.c     host tests (ASan + UBSan)
```

## Architecture

**Bare metal and interrupts, no RTOS yet.**
- A 1 kHz SysTick paces a main-loop control tick.
- Anything with a hard deadline runs in an interrupt:
  - the cliff LED off-edge (TIM7);
  - encoder and IR edges (EXTI);
  - the fan tachometer (TIM3);
  - the UARTs.
- This keeps the safety path short and easy to reason about.
- FreeRTOS (WCH ships a V307 port) can come later for the USB, camera and ToF work, with the safety tick kept in an interrupt. The STM32-era Arduino layering in the README no longer applies.

**Time:** the QingKe SysTick's 64-bit counter at HCLK/8 is the microsecond timebase (18 MHz on U13, 12 MHz on U28). Every timestamp uses it: encoders, IR, IMU INT1 and, later, LiDAR packets. Phone time sync (two-way, as in the SPEC) maps this clock to ROS time.

**Safety layers on U13:**
1. **Hardware (independent of firmware):**
   - The motor rail needs WD_OK.
   - WD_OK needs WDI toggling at 50–1000 Hz.
   - WDI toggles once per control tick (500 Hz), and only when the tick came on time.
2. **IWDG:** ~260 ms, frozen while the debugger halts.
3. **`drive_gate`:** evaluated every tick.
   - Nothing runs unless all of these hold:
     - armed;
     - the motor rail is on;
     - U28 is alive;
     - no power-off request;
     - the motion command has not expired.
   - A wheel drop stops everything.
   - A front cliff or a bumper blocks forward wheel motion only, so the robot can back away. A rear cliff blocks reverse.
   - Hazard stops brake the wheels (both DRV8870 inputs high).
   - Overcurrent is a placeholder until the current readings are characterized.
4. **Pin safety:**
   - `board_safe_init()` puts every pin in its safe level before anything else.
   - Rails come up in order: sensor 3.3 V, 10 ms, 5 V, then the motor rail only on `arm`.
   - Emitters, the torch, XSHUT and camera lines stay low unless their rail is up.

**U28:**
- **At boot:** U28 holds PWR-HOLD first thing.
- **Power state machine:**
  - The press that turned the robot on is ignored until it is released.
  - The power LED fast-blinks from 1 s.
  - At 3 s, power + Home is a factory reset and the robot stays on; power alone is an off request.
  - U13 turns its rails off and acks, then U28 drops the latch. With no ack, it drops the latch after 500 ms.
  - While docked, a new press turns the robot back on.
- **Charger state:** decoded from ~CHRG/~STDBY only while docked.
- **LEDs:** they show the power and charge state unless U13 overrides them.
- **Lines into sensor-rail parts:** the torch, CAM-RESET and VL53L0X XSHUT stay low unless U13 reports the sensor rails up and has been heard within 500 ms.
- **Charge current:** 2.2 A charging (CHG-I-HI) needs both docked and U13's request. U13 knows the dock's power budget from the IR beacon.
- **USB-C:**
  - **Default:** SINK_DATA (Rd on). The phone hosts and powers VBUS.
  - **SOURCE_CHARGE, on U13's request:**
    - Rd goes off and Rp 1.5 A goes on.
    - Once the phone's Rd has been seen for 150 ms, and only if VBUS reads ~0 V, U28 turns USB_PWR_EN on.
    - It turns it off 10 ms after the phone's Rd disappears.

**U13 ↔ U28 link:**
- UART8 (PE14/PE15) to USART2 (PA3/PA2), 1 Mbit/s.
- Frames reuse the phone protocol's "OW" framing with message types 0xC001 (STATUS, U28 → U13) and 0xC002 (CONTROL, U13 → U28). Each side sends every 10 ms.
- CONTROL is also U13's heartbeat.
- STATUS carries:
  - battery, DC-in and VBUS voltages;
  - raw NTC, battery ID, LDR and floor Hall readings;
  - the U28 die temperature;
  - buttons;
  - the power, charge and USB states.
- U28 runs on its internal oscillator (±1 % at room temperature). 1 Mbit/s divides exactly on both sides, and U13 can trim U28's oscillator from frame timing if bench tests show errors.

## Charging dock (CH32V003)

The dock ([`kicad/charging-dock`](https://github.com/makerspet/oomwoo-pcb/tree/main/kicad/charging-dock), U1001) runs `mcu/dock/` with the same toolchain: `make -C mcu dock` builds `build/robot/dock.bin` (11 KB of 16 KB flash, ~1.9 KB of 2 KB RAM with the stack). Flash it with a WCH-LinkE on J1001 (1 SWIO, 2 GND) while the dock runs from its charger: `make -C mcu flash-dock`. The debug console is USART1 on J1005 (pins 3 TX, 4 RX, 115200), not fitted by default; `status` and `pd` print the state.

**State machine** (`common/dock_sm.c`, host-tested), every millisecond on filtered readings:

| State | Contacts | Leaves when |
|---|---|---|
| NO_POWER | off, LED double-blinks | VIN is 18.5-22 V **and** the CH224A reports a PD contract, for 500 ms |
| BLEED | off | VDOCK-20V below 3 V, or 3 s |
| PROBING | probe on, LED blips | the measured resistance (100k probe, diode drop cancelled, the dock's own 1.1M divider removed) sits in 90-180 kΩ for 1 s, **and** it drifted there over at least 300 ms |
| CONNECTING | CHARGE-EN on | VDOCK-20V within 1.5 V of VIN: CHARGING; not within 50 ms: FAULT |
| CHARGING | on, LED on | current < 5 mA for 100 ms (the robot left): BLEED; > 3.2 A or > PD limit + 10 %: FAULT; VDOCK-20V sags 2 V below VIN for 5 ms: FAULT |
| FAULT | off, LED fast | after 10 s (60 s after three in a row) |

- **Robot signature:** a robot's ~30 µF input capacitance makes the reading drift into the window over seconds, from below when its capacitors start empty, from above when they are still charged (a charger blip mid-charge). A plain resistor or a finger jumps straight in and is refused; a short reads ~0 Ω. The window and times are first guesses: measure real robots (full and flat battery) and tune `dock_sm_init()`.
- **Fast trip:** the ADC analog watchdog watches I-SENSE (~3.3 A) and its interrupt drops CHARGE-EN directly, without waiting for the 1 ms loop. It is re-armed on every connection.
- **INA180 offset:** averaged while the contacts are off and subtracted.
- **CH224A:** read every 250 ms over I2C1 (PC5/PC6), single-byte reads: 0x09 bit 3 (PD contract) and 0x50 (current limit, 50 mA units).
- **IR beacon:** TIM1 makes the 38 kHz carrier (1/3 duty) on CH3 (IR-L) and CH4 (IR-R); TIM2 walks the NEC timeline and switches the active channel between PWM and forced-low, so the AC-coupled gates only ever see the carrier. The two strings take turns, a frame each with 40 ms gaps, so a receiver that sees both never gets overlapping frames. Codes (`common/dock_beacon.h`, shared with U13): address 0xD1 = left string, 0xD2 = right string; command = watts available at the contacts / 2 (20 V x the PD limit - 1 W; 0 = refusing: no 20 V, no PD contract, or a fault). The beacon keeps running while charging, so a docked robot reads the budget too. U13's `ir` command decodes it.
- **IWDG:** 500 ms.

## Bring-up on the evaluation boards

**U13 on the CH32V307V-R1 board:** build with `make BOARD=evt u13`, which changes two things.
- The console moves to USART1 (PA9/PA10). That port is wired to the board's on-board WCH-Link serial port, so it needs no wires.
- PC6 is left as an input. On that board, PC6–PC9 go to the Ethernet magnetics, whose centre taps sit on 3.3 V, so driving PC6 low would short the rail through a winding.

**Board differences to know about:**
- That board has a 32.768 kHz crystal on PC14/PC15, so the bit-banged camera bus does not work there.
- USB HS is its P6 connector (PB6/PB7).

**Both boards together:** wire the CH32L103 board to the V307 board exactly like the PCB:

| CH32L103 board | CH32V307 board |
|---|---|
| PA2 | PE15 |
| PA3 | PE14 |
| GND | GND |
| NRST | PC13 (optional) |
| BOOT0 | PD4 (optional) |

`status` on the U13 console then shows U28's readings, and the LEDs, buttons, torch PWM (PB8) and PWR-HOLD (PB13) can be checked with a scope.

**Console commands** (115200 8N1, `help` lists them):
- `status`, `rails on|off`, `arm` / `disarm`;
- `drive l r [ms]`, `brush`, `fan`, `lidar`;
- `adc`, `cliff [thr N]`, `bump [duty]`, `enc`, `ir`;
- `i2c main|cam`, `imu`, `cam`, `carpet`, `torch`, `side`;
- `u28 [reset|boot]`, `reboot`.

Motion commands expire after their time window (500 ms by default, 5 s maximum).

## What writing the firmware found in the schematic

### Schematic changes made (2026-11-24)

1. **BAT_ID (U28 PB0) had no bias resistor.** Fixed: R254 (1M) to VMCU-3V3 + C203 (100 nF), below J2.
   - The battery's ID pin is 620 kΩ to ground (SPEC battery row), and the net goes straight from J2 pin 3 to the ADC pin.
   - Undriven, the ADC reads noise.
   - Fix (two parts, no new BOM lines): a 1 MΩ pull-up to VMCU-3V3 (C22935, as R252) plus 100 nF to GND at the pin. That gives:
     - ~1.25 V with the 620 k pack;
     - ~3.3 V with no ID resistor;
     - ~0 V with the pin shorted.
   - Or drop the feature: BAT_SENSE already shows whether a battery is there.
2. **Motor current sense was unfiltered, and the wheel PWM timer cannot trigger the ADC.** Fixed: R258-R261 (10k) + C204-C207 (100 nF) on the four motor sheets.
   - Each DRV8870's ISEN only carries current while the bridge drives. In slow decay (brake between pulses) the sense resistor sees 0 A.
   - An ADC sample therefore reads either the full current or zero, depending on when it lands in the 20 kHz cycle.
   - The wheels are on TIM10, which is not an ADC trigger source (TIM1/2/3/4/5/8 are), so samples cannot be synced to the PWM.
   - Firmware can average many scans, but it aliases (an 82 µs scan against a 50 µs PWM period).
   - Recommendation: an RC filter on the four current-sense ADC inputs. 10 kΩ + 100 nF (1 ms) on PA0, PA7, PB0 and PB1 gives the average current, I × duty, directly. Both values are existing JLC basic parts.
   - Overcurrent and jam detection depend on this.

### Firmware constraints, no schematic change

3. **DVP_D5 on PB3 (datasheet note 8).**
   - The remap only happens when all of these hold:
     - DVPEN = 1;
     - USBHSEN = 1;
     - the USB HS controller is out of reset (RB_UC_RST_SIE = 0).
   - It also only exists on lots whose 5th-from-last lot digit is > 1, or whose 6th-from-last is not 0.
   - Firmware must keep USB HS enabled whenever the camera runs, and never reset the USB block while capturing.
   - **Check the lot code on the CH32V307s before the run**, and test with the GC2145's colour bar.
4. **PC14/PC15 (camera bus, bit-banged) are low-drive pins:** 2 MHz only, 30 pF, sink only.
   - The bit-bang code uses them open drain with the 2.2 kΩ pull-ups, which is within their rating.
   - Whether 400 kHz works with the flex and two devices (~50–100 pF) is a bench test.
   - U28's I2C2 in parallel is the fallback, already wired.
5. **Timer remaps route unused functions onto pins with other jobs:**
   - TIM10: ETR → PD0 (CLIFF_LED_EN), BKIN → PE2, CH1N–CH3N → PE3–PE5;
   - TIM1 full remap: CH3/CH4 → PE13/PE14 (UART7 RX, UART8 TX), BKIN → PE15, ETR → PE7;
   - TIM9: CH3/CH4 → PD13/PD15, CH1N → PD8, CH2N → PD10.

   The firmware never enables those outputs, ETR or the break inputs. TIM1 CC3/CC4 can still trigger the ADC with their outputs off.
6. **The carpet boost (U5, MT3608, VUSONIC-14V4) is enabled by VSENS-5V-USB.** The carpet sensor only works with the sensor 5 V rail up. That rail also carries the ~1.9 A cliff LED pulses, so check the boost's input sag during a ping.
7. **The carpet burst uses PWM2.** The one-pulse timer stops with its counter at 0, so PWM1 would leave LO high (FET on) after every burst.
8. **The V307 op-amps have no internal gain or DAC bias.** OPA3 is a plain op-amp: the gain comes from R239/R240 and the bias from the existing divider. The STM32-era SPEC steps (internal DAC bias, PGA) are obsolete.

### Checked and fine

- Every U13 remap the board uses exists in the CH32V307 pin table (LQFP100):
  - TIM10 full remap: PD1/PD3/PD5/PD7;
  - TIM9 full remap: PD9/PD11;
  - TIM4 remap: PD12–PD15;
  - TIM1 full remap: PE9/PE11;
  - TIM3 partial remap: PB4;
  - UART4/7/8 full remaps: PE0/PE1, PE12/PE13, PE14/PE15;
  - OPA3: CH0P PB13, CH0N PB2, OUT1 PE7;
  - I2C2: PB10/PB11;
  - DVP;
  - MCO on PA8. PLL3 = 8 MHz / 2 × 6 = 24 MHz is available as an MCO source.
- EXTI lines 3–8, 11 and 12 are each used by one port only.
- USB ISP for U13 works on PB6/PB7 (USB HS) with BOOT0. WCH's tool lists PB6/PB7 as well as PA11/PA12.
- The CH32L103 bootloader loads over USART2 or USB (datasheet 1.4.2), so U13 can flash U28 on the link.
- U28's PB6/PB7 (CC1/CC2) are 5 V tolerant.
- The PWR-BTN-SENSE diode (D26) and the carpet detector diode (D48) are the right way round.
- U28's PC13 (CHG-I-HI) is a weak pin, which is fine for the 1 MΩ-loaded gate.

### Needs answers from the first boards

- Wheel-drop switch polarity: `DROP_ACTIVE_LOW` in `u13/sensors.c`.
- Bumper flag polarity and threshold.
- Cliff reflectance threshold.
- Fan FG pulses per revolution.
- LD14P motor-PWM behaviour.
- Bumper emitter duty per unit.

## Arduino version (pilot, 2026-11-25)

Goal: a hackable firmware people can open, change and upload from the Arduino IDE. WCH's official Arduino core supports all three chips (CH32V307VCT6, CH32L103C8T6, CH32V003F4) and is built on the same WCH peripheral library, so the code moves over rather than being rewritten. The pilot is U28 ([arduino/README.md](../arduino/README.md)):

- **Board definition** `arduino/hardware/oomwoo/ch32v`: references WCH's core and tools and adds a U28 variant (WCH's EVT variant defines only 22 of the 37 pins and puts `Serial` on USART1, which is the LEDs and the clean button here).
- **Library** `arduino/libraries/OOMWOO`: generated copies of the portable code (`tools/sync_arduino_lib.py`; `make -C mcu test` fails if they drift), so the Arduino and plain-C builds share the same tested logic.
- **Sketch** `examples/U28`: `loop()` holds the hackable parts (LEDs, readings, torch, USB-C role, STATUS frames). The safety parts (power latch and button, charger status, the lines into the sensor rail, the 2.2 A charge current, the watchdog) run from a 1 kHz TIM2 interrupt; the watchdog is fed there only while `loop()` also runs.
- **Next:** run it on the CH32L103 evaluation board against U13's plain-C build, measure the core's overhead, then decide on U13 (USB high speed, camera) and the dock (16 KB flash).

## Milestones

1. **EVT bring-up:**
   - console;
   - clocks (HSE ok, MCO 24 MHz on a scope);
   - IWDG reset;
   - WDI toggling;
   - the U13 ↔ U28 link between the two dev boards;
   - power-button state machine with a push button on U28's board.
2. **USB HS on U13:** start with CDC from WCH's `USBHS/DEVICE/SimulateCDC` example, then the phone protocol (`src/oomwoo_cpu_ingress.c`, identity, heartbeat). Then UVC with DVP: GC2145 YUV at VGA.
3. **Sensors:**
   - VL53L7 (ST ULD; firmware upload from the phone);
   - LSM6DS3TR-C FIFO with INT1 timestamps;
   - VL53L0X with the XSHUT address dance;
   - LiDAR packet parsing with packet-start timestamps.
4. **Motion:**
   - encoder speed estimation;
   - a wheel PID loop in a timer interrupt;
   - odometry;
   - per-motor current limits (read average / duty; the sense is filtered since 2026-11-24);
   - staggered motor starts and the total-current budget (fan throttling).
5. **Power:**
   - USB-C source/sink switching driven by docking;
   - dock beacon budget decoding (NEC) → CHG-I-HI;
   - NTC conversion and limits;
   - then the PD power-role swap from WCH's `USBPD_SRC`/`USBPD_SNK` examples.
6. **Updates:** U13 flashes U28 over USART2 (WCH serial ISP protocol); U13 jumps to its own USB bootloader on command.
7. **Safety review:** measured worst-case reaction times for cliff, drop, bumper and watchdog, plus a hazard note.
