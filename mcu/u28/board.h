/* U28 (CH32L103C8T6) board layer: safe pin states, PWM outputs, ADC scan. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gpio.h"
#include "oomwoo_pins.h"

#define HCLK_HZ 96000000u
#define PCLK1_HZ 48000000u
#define PCLK2_HZ 96000000u
#define TIM1CLK_HZ 96000000u /* APB2 /1 */
#define TIM4CLK_HZ 96000000u /* APB1 /2 -> timers x2 */

typedef enum {
  U28_ADC_HALL_FLOOR_L = 0,
  U28_ADC_HALL_FLOOR_R,
  U28_ADC_BAT_SENSE,
  U28_ADC_DC_IN,
  U28_ADC_BAT_NTC,
  U28_ADC_VBUS,
  U28_ADC_BAT_ID,
  U28_ADC_LDR,
  U28_ADC_TEMP,
  U28_ADC_VREFINT,
  U28_ADC_COUNT
} u28_adc_t;

void board_safe_init(void); /* PWR-HOLD high first, then every other pin */
void board_pwm_init(void);  /* TIM1 CH1/CH2 button LEDs, TIM4 CH3 torch */
void board_adc_start(void);
uint16_t board_adc_raw(u28_adc_t ch);
uint32_t board_adc_mv(u28_adc_t ch);
int32_t board_temp_c10(void);

void led_home_set(uint16_t permille);
void led_power_set(uint16_t permille);
void torch_set(uint8_t pct);

void pwr_hold(bool on);
bool pwr_button_pressed(void);   /* PWR-BTN-SENSE low */
bool home_button_pressed(void);
bool clean_button_pressed(void);
bool hall_bin_present(void);     /* low = magnet near */
bool chrg_low(void);
bool stdby_low(void);

void iwdg_start(void);
void iwdg_feed(void);
