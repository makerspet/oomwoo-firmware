# STM32G473 production safety HAL

Status: compile- and host-tested integration for the current OOMWOO I/O board
schematic. Physical validation on a manufactured board is still required before
connecting a motor load.

## Schematic contract

The mapping is taken from
[`makerspet/oomwoo-io-board@5c275ad`](https://github.com/makerspet/oomwoo-io-board/commit/5c275ad),
which renamed the watchdog input and finalized the motor-rail enable assignment:

| Function | STM32G473VCT6 pin | Electrical behavior |
|---|---|---|
| External watchdog input `WDI` | PD8, package pin 55 | Must toggle from the foreground control loop. High, low, or floating eventually removes `WD_OK`. |
| Motor/fan rail disable `~VM-VBAT-EN` | PE10, package pin 41 | Active low enable. Driving high disables `VM-VBAT`; the board pull-up keeps it off during reset and flash. |
| CPU UART TX | PC4, package pin 30 | USART1 TX toward the CM4/CM5 carrier path. |
| CPU UART RX | PC5, package pin 31 | USART1 RX from the CM4/CM5 carrier path. |

The schematic's RC watchdog also gates the LiDAR and water-pump power switches.
It is independent of the 150 ms CPU heartbeat deadline: `WDI` proves that the
MCU foreground control loop is progressing, while the CPU heartbeat proves that
the Linux/ROS stack is healthy.

## Startup and stop sequence

`oomwoo_stm32g473_safety_init` loads safe GPIO output values before selecting
output mode:

1. PE10 is driven high, keeping `VM-VBAT` disabled.
2. PD8 is initialized low.
3. Both pins become low-speed push-pull outputs with no internal pulls.
4. The CPU watchdog is initialized, which repeats the hard stop for the boot
   state before TIM7 starts.

The timer-ISR callback writes the PE10 set bit directly through `GPIOE->BSRR`.
It does not use Arduino, allocate memory, wait, lock, or call another function.
CI checks the linked G473 image and rejects any `bl`/`blx` instruction in that
callback.

The bring-up firmware deliberately has no motor-rail enable operation. A future
motor-control change must add that transition inside the same interrupt-masked
critical section that verifies `oomwoo_cpu_watchdog_motion_permitted`; otherwise
a timeout could race an enable write. It must also invalidate every command
latch and force each PWM/control output safe before the rail can be enabled.

## External watchdog ownership

PD8 toggles every 5 ms only after one complete pass through the Arduino
foreground loop. It is never driven by TIM7 or hardware PWM. If the foreground
loop blocks while interrupts continue, TIM7 can still enforce the CPU heartbeat
deadline and the external RC watchdog independently loses its `WDI` edges.

The schematic estimates `WD_OK` below 1 V in about 100 ms and below 0.4 V in
about 190 ms with nominal components. These are design estimates, not measured
cutoff guarantees. Final acceptance still requires oscilloscope measurements of
PD8, `WD_OK`, PE10, `VM-VBAT`, and representative actuator outputs under load.

## Build

The repository includes a 256 KiB flash / 128 KiB RAM PlatformIO definition for
the actual STM32G473VCT6:

```bash
pio run -e oomwoo_stm32g473vc
```

The image uses PC4/PC5 for the CPU USART1 link. Flash over the board's SWD header;
do not energize the motor rail for initial UART and watchdog bring-up.
