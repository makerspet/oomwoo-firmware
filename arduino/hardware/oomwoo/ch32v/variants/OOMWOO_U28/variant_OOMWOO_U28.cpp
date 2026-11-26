/*
 * OOMWOO main-phone board, U28 (CH32L103C8T6): Arduino pin tables. Derived
 * from WCH's CH32L103C8T6 EVT variant (Copyright (c) 2021 Nanjing Qinheng
 * Microelectronics Co., Ltd., BSD 3-Clause).
 */
#include "pins_arduino.h"

/* Digital pin number -> PinName; must match the PAx defines in the variant header */
const PinName digitalPin[] = {
  PA_0,   /* D0  A0  HALL-FLOOR-L */
  PA_1,   /* D1  A1  HALL-FLOOR-R */
  PA_2,   /* D2  A2  USART2 TX -> U13 */
  PA_3,   /* D3  A3  USART2 RX <- U13 */
  PA_4,   /* D4  A4  BAT_SENSE */
  PA_5,   /* D5  A5  DC_IN_SENSE */
  PA_6,   /* D6  A6  BAT_NTC */
  PA_7,   /* D7  A7  USB_VBUS_SENSE */
  PB_0,   /* D8  A8  BAT_ID */
  PB_1,   /* D9  A9  LDR */
  PA_8,   /* D10     LED-HOME (TIM1 CH1) */
  PA_9,   /* D11     LED-PWR (TIM1 CH2) */
  PA_10,  /* D12     ~CLEAN-BTN */
  PA_11,  /* D13     ~HOME-BTN */
  PA_12,  /* D14     PWR-BTN-SENSE */
  PA_13,  /* D15     SWDIO */
  PA_14,  /* D16     SWCLK */
  PA_15,  /* D17     HALL-BIN */
  PB_2,   /* D18     BOOT1 */
  PB_3,   /* D19     ~STDBY */
  PB_4,   /* D20     ~CHRG */
  PB_5,   /* D21     CC-RD-EN */
  PB_6,   /* D22     USB_CC1 */
  PB_7,   /* D23     USB_CC2 */
  PB_8,   /* D24     TORCH (TIM4 CH3) */
  PB_9,   /* D25     CAM-RESET */
  PB_10,  /* D26     CAM-SCL (I2C2) */
  PB_11,  /* D27     CAM-SDA (I2C2) */
  PB_12,  /* D28     spare, J1 pin 7 */
  PB_13,  /* D29     PWR-HOLD */
  PB_14,  /* D30     SIDE-PROXI-CE-R */
  PB_15,  /* D31     USB_PWR_EN */
  PC_13,  /* D32     CHG-I-HI */
  PC_14,  /* D33     spare, J13 pin 2 */
  PC_15,  /* D34     spare, J13 pin 3 */
  PD_0,   /* D35     spare, J1 pin 8 */
  PD_1,   /* D36     spare, J1 pin 9 */
};

/* Analog pin Ax -> digital pin number */
const uint32_t analogInputPin[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
