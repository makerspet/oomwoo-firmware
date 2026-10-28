# OOMWOO I/O firmware spec (work in progress)

Target: the v1 board [`oomwoo-pcb/kicad/main-phone`](https://github.com/makerspet/oomwoo-pcb/tree/main/kicad/main-phone), an STM32G473VCT6 with an Android phone as the compute, connected over USB-C. Hardware details are in the [PCB spec](https://github.com/makerspet/oomwoo-pcb/blob/main/docs/SPEC.md). Notes that differ on the Raspberry Pi board (`kicad/main`) are marked **Pi board**.

## Safety

- **RC watchdog (WDI, PD8).** Toggle WDI from the control loop, in software, only while the loop is healthy: 50-1000 Hz, ~50% duty. Never drive it from a timer or PWM, which keeps running after the CPU hangs. If WDI stops, WD_OK falls below 1 V in ~100 ms and the motor rail, the LiDAR and (Pi board) the water pump lose power.
- **Gated rails are WD_OK AND an active-low enable:**
  - `~VM-VBAT-EN` (PE10): wheels, brushes, fan
  - `LiDAR-EN` (PD9): the whole LiDAR, laser included. **Low = on.**
  - (Pi board) `WATER-PUMPU-CTRL`: the pump
  - A 100k pull-up holds each one off while pins float (reset, flashing). Configure these pins as outputs, driven high, before anything else, so start-up never glitches them on.
- **MCU reset path.** There is no external reset supervisor. Enable the IWDG through the hardware-watchdog option byte, and set the DBGMCU freeze bits so the IWDG and WDI timing stop at breakpoints. On v1 the phone cannot pulse NRST; the IWDG is the only automatic reset.
- **Soft start.** The LiDAR rail has a gate-RC soft start (~1.5 ms on, ~30 ms off). PWM on an enable can only slow that ramp, never speed it up. The motor rail has no soft start by design (fast turn-off under load). Bring rails up one at a time, ~10 ms apart.
- **Carpet sensor (LO, PE11):** drive it in short bursts only. LO is AC-coupled to the FET gate (C7303 100 nF, R7012 100k, τ ≈ 10 ms), so a stuck-high LO turns the FET off within ~10 ms and can't overheat the 330 Ω pull-up (R7011). Keep bursts well under 1 ms.

## Power control

- **Latch (PA15, `STM-PWR-CTRL`).** The power button (or charger power) turns on the MCU rail only. At boot, drive PA15 high (push-pull is fine; it goes through a 1k resistor) before anything else, or the MCU loses power when the button is released. Then enable the other rails one at a time.
- **Power button (PE7, `PWR-BTN-SENSE`, low = pressed):**
  - 3 s press: turn off every rail (motors, LiDAR, 5 V, sensor 3.3 V), then drive PA15 low.
  - 3 s press while holding Home (PD5): factory reset, and stay on.
  - A hardware timer turns the MCU rail off after ~4-9 s of holding the button, whatever the firmware does. Give feedback (LEDs) before 3 s so users release in time.
- **While charging** (dock or barrel jack), hardware holds the MCU rail on, and PA15 low can't turn it off. "Off" while charging means: all other rails off, LEDs show charging, MCU idles.
- If the MCU rail is off, all pins float and every other rail is held off by its pull resistor.

## Phone link (USB-C)

- **USB device.** The STM32 USB FS port (PA11/PA12) is a CDC device; the phone is the USB host. The STM32G4 USB cannot be a host.
- **USB-C / PD (UCPD1).** CC1 = PB6, CC2 = PB4.
  - Run as a sink with Rd by default, so the phone attaches as host and power source.
  - To charge the phone while it stays host, negotiate a PD power-role swap (PR_Swap) to become the source. Phone support varies; phones that refuse it alternate between charging and data.
  - Dead-battery pull-downs: on the G4, PA9/PA10 control the PB6/PB4 dead-battery Rd. PA9 is the IMU SCL (pulled up, so Rd on CC1 is active during reset); PA10 is unused. Call `HAL_PWREx_DisableUCPDDeadBattery()` only after UCPD is configured, so CC never floats.
- **Sourcing 5 V to the phone (`PWR_EN`, PC10).** High turns on the TMI6240 and the reverse-block FET Q7101 together. Hardware won't let the robot drive VBUS while the phone is sourcing it: a lockout holds the switch off if VBUS is present when PWR_EN goes high, and it turns on by itself once VBUS drops to ~0 V (as in a PD power-role swap). Firmware should still check VBUS sense first, and use it to confirm the switch actually turned on.
- **VBUS sense (PA7, ADC).** 100k/47k divider: VBUS = V_adc x 147/47. 5 V reads ~1.6 V.
- **Firmware updates from the phone.** Jump to the built-in system bootloader (USB DFU) on command; no BOOT0 wiring is needed. (Pi board: the CM reflashes the STM32 over the UART bootloader with BOOT0/NRST from CM GPIOs, or over SWD from CM GPIOs.)

## Time sync and timestamps

- Every timing-critical sensor goes through the STM32: wheel encoders, IMU, LiDAR, ToF, bumpers. Stamp everything on one free-running 32-bit µs timer (TIM2 or TIM5).
- Stamp as close to the hardware as possible:
  - encoders in the timer interrupt
  - IMU on its data-ready interrupt (INT1/INT2) or FSYNC
  - LiDAR at the UART DMA packet-start interrupt
- **Map STM32 time to phone (ROS) time** with a two-way exchange, like NTP or MAVLink `TIMESYNC`:
  1. The phone sends t1; the STM32 records receive time t2 and reply time t3; the phone stamps arrival t4.
  2. Offset = ((t2 - t1) + (t3 - t4)) / 2; round trip = (t4 - t1) - (t3 - t2).
  3. Run it a few times a second. Keep the shortest round trips, and fit offset and drift over a sliding window (the crystals differ by tens of ppm).
  4. The phone's bridge node converts each STM32 timestamp before publishing. Never step time backwards.
- Expect a sub-millisecond offset; measure it on the target phone.

## Sensors

- **LiDAR on UART5:** RX = PD2 (from the LiDAR's TX, through R113 1k), TX = PC12 (to the LiDAR's RX on LIDAR2, through R117 and R66). Standard pin mapping, no TX/RX swap. LIDAR1 is an LDROBOT LD14P (TX only); LIDAR2 is a 0.1" header for other 2D LiDARs. Motor control (PWM) is PC2.
- **Front ToF (2x VL53L7) on I2C4** (PC6 SCL, PC7 SDA, 2.2k pull-ups), shared with the right side sensor.
  - `VL-I2C-RESET` is PC0 (shared, 47k pull-down; high = reset).
  - LPn is PB13 (left) and PE8 (right): bring the sensors up one at a time to set their I2C addresses.
  - INT is PD12 (left) and PC8 (right), with 47k pull-ups.
- **Right side sensor:** GPIO PE2, CE PE3, docking IR receive PD6. There is no left side sensor on v1.
- **Docking IR receivers** (38 kHz, NEC): front left PE15 (U7501) and front right PB11 (U7502), right side PD6. The two front receivers have a baffle between them: both receiving = dock straight ahead, one = steer to that side. Decode with timer input capture or EXTI plus the µs timer; any pin works.
- **IMU ICM-42607-P on I2C2** (PA8 SDA, PA9 SCL, 10k pull-ups); INT1 PD3, INT2 PD7, FSYNC PD4.
- **Wheel encoders:** one channel each, PE13 (left) and PE14 (right). Wheel-drop switches: PB1 (left), PB0 (right).
- **Cliff sensors** (Roomba-compatible harness): analog PA0-PA3 (RR, RF, LR, LF). Cliff LED enable PC1.
- **Bumpers:** two ITR9606 slotted photo-interrupters on the main board, plus the harness bumpers in parallel as a backup: left PA4, right PA5 (47k pull-ups; low = light passes the slot; with both fitted, either one seeing light reads low). All bumper emitters are on while `BUMPER_LED_EN` (PC3) is **high**; it's pulled low in reset.
- **Charger power (DC_IN) sense:** PB15 (ADC4_IN5), DC_IN = V_adc x 11; ~1.82 V with the 20 V brick. Use it to detect docking or a plugged-in barrel jack (a 100 nF filter cap is on the pin, so any sample time works).
- **Wake-up sensor (LDR):** PB12 (ADC).
- **Board temperature:** NTC on PB2. **Battery ID:** PB14.

## Carpet sensor

- 290 kHz ultrasonic piezo, driven from VUS (~14.5 V boost rail) by one low-side FET (LO, PE11) with a 330 Ω pull-up; the echo comes back AC-coupled and clamped on PE12 (ECHO_ADC)
- (firmware) bias the MCU internal op-amp to Vref/2 using MCU internal DAC
- (firmware) configure ADC pre-amp gain, PGA mode (op-amp bandwidth is 10MHz)
- (firmware) configure op-amp to output signal to internal ADC channel
- (firmware) drive the sensor in a short burst (LO high = FET on), then release LO low. PE11 is TIM1_CH2: use one-pulse mode with the repetition counter for an exact cycle count
- (firmware) measure using ADC, calculate return amplitude
- (firmware) calibrate return amplitude when docked

## Motors, power and UI pins

| Function | Pins |
| --- | --- |
| Left wheel DRV8870 IN1/IN2, current ADC | PE0/PE1, PD11 |
| Right wheel DRV8870 IN1/IN2, current ADC | PB7/PB9, PD10 |
| Main brush IN1/IN2, current ADC | PD14/PD15, PA6 |
| Side brush IN1/IN2, current ADC | PF9/PF10, PD13 |
| Suction fan PWM, FG tachometer | PD1, PE9 |
| 5 V buck enable (`5V_EN`) | PB5 |
| Sensor 3.3 V enable (`3.3V_EN`, low = on, through 1k) | PB10 |
| Power latch (`STM-PWR-CTRL`), power button sense | PA15, PE7 |
| Buttons: clean, home | PD0, PD5 |
| LEDs: home, power (also used for debug, there is no buzzer) | PC13, PF2 |
| Debug UART (STDC14 VCP + 0.1" header) | PC4 TX, PC5 RX |
| SWD | PA13, PA14, SWO PB3 |
| Charger power sense (`DC_IN_SENSE`, ADC) | PB15 |
| Spare (on the unfitted J7301 header, with I2C4, 3.3 V, 5 V) | PA10, PC9, PC11, PE4, PE5, PE6 |

## Misc notes

- Firmware budgeting the current - Suction, main mop RPM are high on carpets, where mops are off. And vice versa for non-carpeted areas.
- Firmware: budget total current. ~13 A from a 5200 mAh 2P pack is ~2.5 C — high for standard 18650s, with real voltage sag and BMS-trip risk. Normal operation is ~5 A (≈1 C), fine. But rather than letting the sum go wherever it goes, have firmware throttle the fan when brush and drive current climb — you already have per-motor sense to do it with, and the fan is the natural give.
- Firmware: A driver that fails shorted, shut battery off
  - Stagger motor startup. Even with ramping, bringing all five up together stacks the transients into exactly the aggregate spike that trips things.
  - 0.8× stall is a backstop, not jam detection. At that limit a jammed brush sits there drawing near-stall current — hard on the motor and the rail. Firmware should trip on a much lower threshold, much faster; the hardware limit exists only to stop the driver destroying itself if firmware misses it.
- Firmware: Detect that the backup domain lost power and mark time as invalid. The G4 gives you the RTC init/backup-reset flag for exactly this.
  - Then: refuse to run schedules until time is re-established. A robot that cleans at 3 a.m. because its clock silently reset is much worse than one that says "please set the time." Every option above eventually runs out — this is what makes running out safe rather than alarming.
- most blower fans soft-start by themselves once PWM is high
