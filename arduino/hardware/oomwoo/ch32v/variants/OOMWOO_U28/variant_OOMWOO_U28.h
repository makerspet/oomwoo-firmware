/*
 * OOMWOO main-phone board, U28 (CH32L103C8T6, LQFP-48): Arduino variant for
 * WCH's arduino_core_ch32. Derived from WCH's CH32L103C8T6 EVT variant
 * (Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd., BSD 3-Clause,
 * opensource.org/licenses/BSD-3-Clause), with every one of the board's pins
 * defined and Serial on USART2, the link to U13.
 *
 * Pin numbers: D0-D9 are the ten ADC pins (A0-A9), then the rest in port order.
 * Use the PAx names or the board names from oomwoo_pins.h (PIN_MCU2_LED_PWR...).
 */
#pragma once

#define ADC_MODULE_ENABLED
#define UART_MODULE_ENABLED
#define I2C_MODULE_ENABLED
#define TIM_MODULE_ENABLED

/* ADC pins first: PIN_Ax = analog numbering */
#define PA0                     PIN_A0
#define PA1                     PIN_A1
#define PA2                     PIN_A2
#define PA3                     PIN_A3
#define PA4                     PIN_A4
#define PA5                     PIN_A5
#define PA6                     PIN_A6
#define PA7                     PIN_A7
#define PB0                     PIN_A8
#define PB1                     PIN_A9
#define PA8                     10
#define PA9                     11
#define PA10                    12
#define PA11                    13
#define PA12                    14
#define PA13                    15
#define PA14                    16
#define PA15                    17
#define PB2                     18
#define PB3                     19
#define PB4                     20
#define PB5                     21
#define PB6                     22
#define PB7                     23
#define PB8                     24
#define PB9                     25
#define PB10                    26
#define PB11                    27
#define PB12                    28
#define PB13                    29
#define PB14                    30
#define PB15                    31
#define PC13                    32
#define PC14                    33
#define PC15                    34
#define PD0                     35
#define PD1                     36

#define NUM_DIGITAL_PINS        37
#define NUM_ANALOG_INPUTS       10
#define ADC_RESOLUTION          12

#ifndef LED_BUILTIN
  #define LED_BUILTIN           PA9   /* the power button LED */
#endif
#ifndef USER_BTN
  #define USER_BTN              PA10  /* the clean button, low = pressed */
#endif

/* I2C2: the camera bus (shared with U13's bit-banged master) */
#define PIN_WIRE_SDA            PB11
#define PIN_WIRE_SCL            PB10

#ifndef TIMER_TONE
  #define TIMER_TONE            TIM3
#endif
#ifndef TIMER_SERVO
  #define TIMER_SERVO           TIM2
#endif

/* Serial = USART2 (PA2 TX, PA3 RX): the board link to U13 */
#ifndef SERIAL_UART_INSTANCE
  #define SERIAL_UART_INSTANCE  2
#endif
#ifndef PIN_SERIAL_RX
  #define PIN_SERIAL_RX         PA3
#endif
#ifndef PIN_SERIAL_TX
  #define PIN_SERIAL_TX         PA2
#endif

#ifdef __cplusplus
  #ifndef SERIAL_PORT_MONITOR
    #define SERIAL_PORT_MONITOR   Serial
  #endif
  #ifndef SERIAL_PORT_HARDWARE
    #define SERIAL_PORT_HARDWARE  Serial
  #endif
#endif
