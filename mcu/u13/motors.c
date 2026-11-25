#include "motors.h"

#include "board.h"
#include "systime.h"

#define PWM_HZ 20000u
#define PWM_PERIOD (TIMCLK_HZ / PWM_HZ) /* 7200 ticks */

typedef struct {
  TIM_TypeDef *tim;
  uint8_t ch_in1; /* 1..4 */
  uint8_t ch_in2;
  const char *name;
} bridge_t;

static const bridge_t k_bridges[MOTOR_COUNT] = {
    [MOTOR_WHEEL_L] = {TIM10, 1, 2, "wheel_l"},    /* PD1, PD3 */
    [MOTOR_WHEEL_R] = {TIM10, 3, 4, "wheel_r"},    /* PD5, PD7 */
    [MOTOR_MAIN_BRUSH] = {TIM4, 1, 2, "main"},     /* PD12, PD13 */
    [MOTOR_SIDE_BRUSH] = {TIM4, 3, 4, "side"},     /* PD14, PD15 */
};

static int16_t s_cmd[MOTOR_COUNT];
static uint16_t s_fan, s_lidar;

static void set_ccr(TIM_TypeDef *t, uint8_t ch, uint16_t v) {
  switch (ch) {
    case 1: TIM_SetCompare1(t, v); break;
    case 2: TIM_SetCompare2(t, v); break;
    case 3: TIM_SetCompare3(t, v); break;
    default: TIM_SetCompare4(t, v); break;
  }
}

static void pwm_timer(TIM_TypeDef *t, uint32_t period, bool advanced) {
  TIM_TimeBaseInitTypeDef tb = {0};
  tb.TIM_Period = (uint16_t)(period - 1u);
  tb.TIM_Prescaler = 0;
  tb.TIM_ClockDivision = TIM_CKD_DIV1;
  tb.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(t, &tb);

  TIM_OCInitTypeDef oc = {0};
  oc.TIM_OCMode = TIM_OCMode_PWM1;
  oc.TIM_OutputState = TIM_OutputState_Enable;
  oc.TIM_OutputNState = TIM_OutputNState_Disable; /* complementary outputs stay off */
  oc.TIM_Pulse = 0;
  oc.TIM_OCPolarity = TIM_OCPolarity_High;
  oc.TIM_OCIdleState = TIM_OCIdleState_Reset;
  TIM_OC1Init(t, &oc);
  TIM_OC2Init(t, &oc);
  TIM_OC1PreloadConfig(t, TIM_OCPreload_Enable);
  TIM_OC2PreloadConfig(t, TIM_OCPreload_Enable);
  TIM_ARRPreloadConfig(t, ENABLE);
  if (advanced) TIM_CtrlPWMOutputs(t, ENABLE); /* MOE; BKIN stays disabled */
  TIM_Cmd(t, ENABLE);
}

static void pwm_ch34(TIM_TypeDef *t) {
  TIM_OCInitTypeDef oc = {0};
  oc.TIM_OCMode = TIM_OCMode_PWM1;
  oc.TIM_OutputState = TIM_OutputState_Enable;
  oc.TIM_OutputNState = TIM_OutputNState_Disable;
  oc.TIM_Pulse = 0;
  oc.TIM_OCPolarity = TIM_OCPolarity_High;
  TIM_OC3Init(t, &oc);
  TIM_OC4Init(t, &oc);
  TIM_OC3PreloadConfig(t, TIM_OCPreload_Enable);
  TIM_OC4PreloadConfig(t, TIM_OCPreload_Enable);
}

/* ---- fan tachometer on TIM3 CH1 (PB4), 1 MHz timebase */
static volatile uint32_t s_tach_period_us;
static volatile uint32_t s_tach_last_ms;
static volatile uint16_t s_tach_last_cap;
static volatile uint32_t s_tach_overflows;

void TIM3_IRQHandler(void) ISR;
void TIM3_IRQHandler(void) {
  if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET) {
    TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    if (s_tach_overflows < 100u) ++s_tach_overflows;
  }
  if (TIM_GetITStatus(TIM3, TIM_IT_CC1) != RESET) {
    TIM_ClearITPendingBit(TIM3, TIM_IT_CC1);
    const uint16_t cap = TIM_GetCapture1(TIM3);
    const uint32_t period = s_tach_overflows * 65536u + cap - s_tach_last_cap;
    s_tach_last_cap = cap;
    s_tach_overflows = 0u;
    s_tach_period_us = period;
    s_tach_last_ms = systime_ms();
  }
}

static void tach_init(void) {
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
  TIM_TimeBaseInitTypeDef tb = {0};
  tb.TIM_Period = 0xFFFF;
  tb.TIM_Prescaler = (uint16_t)(TIMCLK_HZ / 1000000u - 1u);
  tb.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM3, &tb);
  TIM_ICInitTypeDef ic = {0};
  ic.TIM_Channel = TIM_Channel_1;
  ic.TIM_ICPolarity = TIM_ICPolarity_Falling; /* open-collector FG pulls low */
  ic.TIM_ICSelection = TIM_ICSelection_DirectTI;
  ic.TIM_ICPrescaler = TIM_ICPSC_DIV1;
  ic.TIM_ICFilter = 0x8;
  TIM_ICInit(TIM3, &ic);
  TIM_ITConfig(TIM3, TIM_IT_CC1 | TIM_IT_Update, ENABLE);
  NVIC_SetPriority(TIM3_IRQn, 0x80);
  NVIC_EnableIRQ(TIM3_IRQn);
  TIM_Cmd(TIM3, ENABLE);
}

void motors_init(void) {
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM10 | RCC_APB2Periph_TIM9, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

  pwm_timer(TIM10, PWM_PERIOD, true);
  pwm_ch34(TIM10);
  pwm_timer(TIM4, PWM_PERIOD, false);
  pwm_ch34(TIM4);
  pwm_timer(TIM9, PWM_PERIOD, true); /* CH1 fan PD9, CH2 LiDAR motor PD11 */

  /* Only now hand the pins to the timers (their outputs are at 0 % duty). */
  const pin_t af[] = {PIN(PIN_WHEEL_MOT_L_IN1), PIN(PIN_WHEEL_MOT_L_IN2),
                      PIN(PIN_WHEEL_MOT_R_IN1), PIN(PIN_WHEEL_MOT_R_IN2),
                      PIN(PIN_MAIN_BRUSH_IN1),  PIN(PIN_MAIN_BRUSH_IN2),
                      PIN(PIN_SIDE_BRUSH_IN1),  PIN(PIN_SIDE_BRUSH_IN2),
                      PIN(PIN_MAIN_FAN_PWM),    PIN(PIN_LIDAR_MOTOR_CTRL)};
  for (unsigned i = 0; i < sizeof(af) / sizeof(af[0]); ++i) pin_mode(af[i], PM_AF_PP);

  tach_init();
}

void motor_set(motor_t m, int16_t cmd) {
  if (m >= MOTOR_COUNT) return;
  if (cmd > 1000) cmd = 1000;
  if (cmd < -1000) cmd = -1000;
  const bridge_t *b = &k_bridges[m];
  s_cmd[m] = cmd;
  if (cmd == 0) {
    set_ccr(b->tim, b->ch_in1, 0u); /* coast */
    set_ccr(b->tim, b->ch_in2, 0u);
    return;
  }
  /* Slow decay: one input stays high, the other is high for (1 - duty):
   * IN1=1, IN2=PWM -> forward while IN2 is low, brake while IN2 is high. */
  const uint32_t mag = (uint32_t)(cmd > 0 ? cmd : -cmd);
  const uint16_t off = (uint16_t)(PWM_PERIOD - (PWM_PERIOD * mag) / 1000u);
  if (cmd > 0) {
    set_ccr(b->tim, b->ch_in1, (uint16_t)PWM_PERIOD);
    set_ccr(b->tim, b->ch_in2, off);
  } else {
    set_ccr(b->tim, b->ch_in1, off);
    set_ccr(b->tim, b->ch_in2, (uint16_t)PWM_PERIOD);
  }
}

void motor_brake(motor_t m) {
  if (m >= MOTOR_COUNT) return;
  const bridge_t *b = &k_bridges[m];
  s_cmd[m] = 0;
  set_ccr(b->tim, b->ch_in1, (uint16_t)PWM_PERIOD);
  set_ccr(b->tim, b->ch_in2, (uint16_t)PWM_PERIOD);
}

int16_t motor_get(motor_t m) { return m < MOTOR_COUNT ? s_cmd[m] : 0; }

const char *motor_name(motor_t m) { return m < MOTOR_COUNT ? k_bridges[m].name : "?"; }

void motors_all_off(void) {
  for (int m = 0; m < MOTOR_COUNT; ++m) motor_set((motor_t)m, 0);
  fan_set(0);
  lidar_motor_set(0);
}

void fan_set(uint16_t permille) {
  if (permille > 1000u) permille = 1000u;
  s_fan = permille;
  TIM_SetCompare1(TIM9, (uint16_t)((PWM_PERIOD * permille) / 1000u));
}

uint16_t fan_get(void) { return s_fan; }

uint32_t fan_tach_hz_x10(void) {
  const uint32_t p = s_tach_period_us;
  if (p == 0u || systime_ms() - s_tach_last_ms > 500u) return 0u;
  return 10000000u / p;
}

void lidar_motor_set(uint16_t permille) {
  if (permille > 1000u) permille = 1000u;
  s_lidar = permille;
  TIM_SetCompare2(TIM9, (uint16_t)((PWM_PERIOD * permille) / 1000u));
}

uint16_t lidar_motor_get(void) { return s_lidar; }
