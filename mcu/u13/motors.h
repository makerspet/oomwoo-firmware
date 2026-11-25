/* DRV8870 H-bridges (wheels on TIM10, brushes on TIM4), suction fan and
 * LiDAR motor PWM (TIM9), fan tachometer (TIM3 CH1 capture). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { MOTOR_WHEEL_L = 0, MOTOR_WHEEL_R, MOTOR_MAIN_BRUSH, MOTOR_SIDE_BRUSH, MOTOR_COUNT } motor_t;

void motors_init(void);
/* -1000..1000, slow decay (brake between PWM pulses). 0 = coast. */
void motor_set(motor_t m, int16_t cmd);
void motor_brake(motor_t m);
int16_t motor_get(motor_t m);
void motors_all_off(void); /* everything coasts, fan and LiDAR motor stop */
const char *motor_name(motor_t m);

void fan_set(uint16_t permille);   /* 0..1000; the blower's PWM input, low = off */
uint16_t fan_get(void);
uint32_t fan_tach_hz_x10(void);    /* FG frequency in 0.1 Hz; 0 if no pulse for 500 ms */

void lidar_motor_set(uint16_t permille);
uint16_t lidar_motor_get(void);
