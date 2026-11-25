/* U13 sensors: ADC1 scan (motor currents, bumpers, cliffs, die temperature;
 * the carpet echo envelope has ADC2 to itself, carpet.c), pulsed cliff LEDs, bumper emitter PWM, wheel drops,
 * wheel encoders, IR receivers (NEC) and the IMU data-ready line. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nec.h"

typedef enum {
  ADC_WHEEL_L_I = 0,
  ADC_WHEEL_R_I,
  ADC_MAIN_BRUSH_I,
  ADC_SIDE_BRUSH_I,
  ADC_BUMP_L,
  ADC_BUMP_R,
  ADC_CLIFF_LF,
  ADC_CLIFF_RF,
  ADC_CLIFF_LM,
  ADC_CLIFF_RM,
  ADC_CLIFF_LR,
  ADC_CLIFF_RR,
  ADC_TEMP,
  ADC_VREFINT,
  ADC_COUNT
} adc_ch_t;

enum { CLIFF_LF = 0, CLIFF_RF, CLIFF_LM, CLIFF_RM, CLIFF_LR, CLIFF_RR, CLIFF_COUNT };
enum { IR_FRONT_L = 0, IR_FRONT_R, IR_SIDE_R, IR_REAR_L, IR_REAR_R, IR_COUNT };

typedef struct {
  /* Cliff: reflectance = LED-off reading minus LED-on reading (more light
   * reads lower). Thresholds are placeholders until measured on a floor. */
  int16_t cliff_reflect[CLIFF_COUNT];
  uint16_t cliff_mask;  /* bit i set: no floor seen under sensor i */
  uint32_t cliff_samples;
  uint16_t bump_raw[2];
  bool bumper[2];       /* polarity and threshold TBD on the first boards */
  bool drop[2];         /* raw pin level interpreted by DROP_ACTIVE_LOW */
  uint32_t enc_count[2];
  uint32_t enc_period_us[2];
  uint32_t imu_int_count;
  uint32_t imu_int_us;
} sensors_t;

extern sensors_t g_sens;
extern nec_t g_ir[IR_COUNT];

void adc_start(void);
uint16_t adc_raw(adc_ch_t ch);
uint32_t adc_mv(adc_ch_t ch); /* scaled by the measured VREFINT */
int32_t mcu_temp_c10(void);
const char *adc_name(adc_ch_t ch);

void sensors_init(void);          /* EXTI lines, bumper PWM timer, cliff timer */
void sensors_enable(bool on);     /* follow the sensor rails: off = all emitters low, EXTI off */
void sensors_tick_1ms(void);      /* from the control loop */
void bumper_led_duty(uint16_t permille);
void cliff_set_threshold(int16_t counts);
