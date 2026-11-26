# OOMWOO firmware for Arduino (pilot: U28)

The same firmware as [`mcu/`](../mcu), built with the Arduino IDE (or arduino-cli) on top of [WCH's official Arduino core](https://github.com/openwch/arduino_core_ch32). The pilot covers U28, the CH32L103 power/UI companion on the main board. U13 and the dock still build from `mcu/` only.

- [`hardware/oomwoo/ch32v`](hardware/oomwoo/ch32v): the board definition "OOMWOO main board U28". It adds a pin variant with every U28 pin and `Serial` on USART2 (the link to U13) on top of WCH's core; WCH's compiler and upload tools are used as they are.
- [`libraries/OOMWOO`](libraries/OOMWOO): the portable code (board-link and phone protocols, power and charger state machines, motion gate, dock logic, NEC IR codec, the generated pin map) as an Arduino library, and the U28 sketch under `examples/U28`. The library's `src/` files are generated copies; edit the originals and run `tools/sync_arduino_lib.py` (`make -C mcu test` checks they are in sync).

## Set up

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) 2.x.
2. **File > Preferences > Additional boards manager URLs:** add
   `https://github.com/openwch/board_manager_files/raw/main/package_ch32v_index.json`,
   then **Tools > Board > Boards Manager**, search "CH32" and install **CH32 MCU EVT Boards** (WCH).
3. **File > Preferences > Sketchbook location:** this `arduino` folder of the repository. Restart the IDE.
4. **Tools > Board > OOMWOO boards > OOMWOO main board U28**, then **Tools > Hardware**: "OOMWOO main board" or "WCH CH32L103C8T6 evaluation board".
5. **File > Sketchbook > libraries > OOMWOO > U28** (or **File > Examples > OOMWOO > U28**), then Upload with a WCH-LinkE on SWDIO/SWCLK (on the robot board: J1 pins 10, 11 and GND on 13).

From a terminal: `mcu/scripts/setup.sh --arduino` installs arduino-cli and the WCH core, then `arduino/build.sh` (or `arduino/build.sh evt`) builds the sketch.

## How the sketch is split

| File | What | Change it? |
|---|---|---|
| `U28.ino` | `setup()`, `loop()`: LED patterns, analog readings, torch, USB-C role, the STATUS frame | Yes: this is the place to hack |
| `safety.cpp` | 1 kHz timer interrupt: power latch and button, charger status, the lines into the sensor rail and the 2.2 A charge current, the watchdog | With review |
| `link.cpp` | the board link to U13 over `Serial` | Keep the frames compatible with U13 |
| `typec.cpp` | USB-C baseline (Rd, Rp, robot 5 V on VBUS) | With review |

The safety parts run from a hardware timer, not from `loop()`, so a sketch that blocks, calls `delay()` or crashes cannot hold the power latch or leave the camera reset released. The watchdog is fed from that timer only while `loop()` also runs: if `loop()` stops for 100 ms, U28 resets (the power latch holds in hardware through the reset).

Spare U28 pins for your own hardware: PB12, PD0 and PD1 on J1 (pins 7, 8, 9), PC14 and PC15 on J13 (pins 2, 3; weak pins, inputs or LEDs to ground only).
