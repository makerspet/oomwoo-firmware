## How to drive carpet sensor

- 290KHz ultrasonic piezoelectric analog
- ≥12V DC stabilized per spec (not 4S battery directly)
  - make DC voltage configurable using a resistive divider
  - current consumption - calculate 300KHz driving 1300±20% pF per sensor spec
  - make it withstand shorts
- connect sensor analog I/O to MCU ADC input
- use STM32G473VCT6 internal op-amp as echo input (AC via a cap)
- clamp amplitude to 3.3V (back-to-back clamp diodes)
- add a series resistor to STM32 op-amp input (extra protection against 12V)
- (firmware) bias the MCU internal op-amp to Vref/2 using MCU internal DAC
- (firmware) configure ADC pre-amp gain, PGA mode (op-amp bandwidth is 10MHz)
- (firmware) configure op-amp to output signal to internal ADC channel
- drive sensor analog I/O using a FET half-bridge
- drive the half-bridge by MCU, one GPIO for high side, one GPIO for low side
- add pull-up/down to FET inputs, so the bridge is off when MCU GPIO is tristated
- (firmware) drive the sensor for a brief while
- (firmware) tristate both FETs
- (firmware) measure using ADC, calculate return amplitude

# Misc notes

- Firmware budgeting the current - Suction, main mop RPM are high on carpets, where mops are off. And vice versa for non-carpeted areas.
- Firmware: budget total current. ~13 A from a 5200 mAh 2P pack is ~2.5 C — high for standard 18650s, with real voltage sag and BMS-trip risk. Normal operation is ~5 A (≈1 C), fine. But rather than letting the sum go wherever it goes, have firmware throttle the fan when brush and drive current climb — you already have per-motor sense to do it with, and the fan is the natural give.
- Firmware: A driver that fails shorted, shut battery off
  - Stagger motor startup. Even with ramping, bringing all five up together stacks the transients into exactly the aggregate spike that trips things.
  - 0.8× stall is a backstop, not jam detection. At that limit a jammed brush sits there drawing near-stall current — hard on the motor and the rail. Firmware should trip on a much lower threshold, much faster; the hardware limit exists only to stop the driver destroying itself if firmware misses it.
- Firmware: Detect that the backup domain lost power and mark time as invalid. The G4 gives you the RTC init/backup-reset flag for exactly this.
  - Then: refuse to run schedules until time is re-established. A robot that cleans at 3 a.m. because its clock silently reset is much worse than one that says "please set the time." Every option above eventually runs out — this is what makes running out safe rather than alarming.
- most blower fans soft-start by themselves once PWM is high
