#include "sensors.h"

#include "board.h"
#include "systime.h"

sensors_t g_sens;
nec_t g_ir[IR_COUNT];

/* ---------------------------------------------------------------- ADC */

static const struct {
  uint8_t channel;
  uint8_t sample;
  const char *name;
} k_adc[ADC_COUNT] = {
    [ADC_WHEEL_L_I] = {ADC_Channel_0, ADC_SampleTime_28Cycles5, "wheel_l_i"},
    [ADC_WHEEL_R_I] = {ADC_Channel_7, ADC_SampleTime_28Cycles5, "wheel_r_i"},
    [ADC_MAIN_BRUSH_I] = {ADC_Channel_8, ADC_SampleTime_28Cycles5, "main_i"},
    [ADC_SIDE_BRUSH_I] = {ADC_Channel_9, ADC_SampleTime_28Cycles5, "side_i"},
    [ADC_BUMP_L] = {ADC_Channel_2, ADC_SampleTime_28Cycles5, "bump_l"},
    [ADC_BUMP_R] = {ADC_Channel_3, ADC_SampleTime_28Cycles5, "bump_r"},
    [ADC_CLIFF_LF] = {ADC_Channel_10, ADC_SampleTime_28Cycles5, "cliff_lf"},
    [ADC_CLIFF_RF] = {ADC_Channel_11, ADC_SampleTime_28Cycles5, "cliff_rf"},
    [ADC_CLIFF_LM] = {ADC_Channel_12, ADC_SampleTime_28Cycles5, "cliff_lm"},
    [ADC_CLIFF_RM] = {ADC_Channel_13, ADC_SampleTime_28Cycles5, "cliff_rm"},
    [ADC_CLIFF_LR] = {ADC_Channel_14, ADC_SampleTime_28Cycles5, "cliff_lr"},
    [ADC_CLIFF_RR] = {ADC_Channel_15, ADC_SampleTime_28Cycles5, "cliff_rr"},
    [ADC_TEMP] = {ADC_Channel_TempSensor, ADC_SampleTime_239Cycles5, "temp"},
    [ADC_VREFINT] = {ADC_Channel_Vrefint, ADC_SampleTime_239Cycles5, "vrefint"},
};

/* One scan of all channels every ~82 us (12 MHz ADC clock), DMA circular. */
static volatile uint16_t s_adc[ADC_COUNT];

void adc_start(void) {
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

  DMA_DeInit(DMA1_Channel1);
  DMA_InitTypeDef d = {0};
  d.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
  d.DMA_MemoryBaseAddr = (uint32_t)s_adc;
  d.DMA_DIR = DMA_DIR_PeripheralSRC;
  d.DMA_BufferSize = ADC_COUNT;
  d.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  d.DMA_MemoryInc = DMA_MemoryInc_Enable;
  d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
  d.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
  d.DMA_Mode = DMA_Mode_Circular;
  d.DMA_Priority = DMA_Priority_High;
  d.DMA_M2M = DMA_M2M_Disable;
  DMA_Init(DMA1_Channel1, &d);
  DMA_Cmd(DMA1_Channel1, ENABLE);

  ADC_DeInit(ADC1);
  ADC_InitTypeDef a = {0};
  a.ADC_Mode = ADC_Mode_Independent;
  a.ADC_ScanConvMode = ENABLE;
  a.ADC_ContinuousConvMode = ENABLE;
  a.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
  a.ADC_DataAlign = ADC_DataAlign_Right;
  a.ADC_NbrOfChannel = ADC_COUNT;
  ADC_Init(ADC1, &a);
  for (uint8_t i = 0; i < ADC_COUNT; ++i) {
    ADC_RegularChannelConfig(ADC1, k_adc[i].channel, (uint8_t)(i + 1u), k_adc[i].sample);
  }
  ADC_TempSensorVrefintCmd(ENABLE);
  ADC_DMACmd(ADC1, ENABLE);
  ADC_Cmd(ADC1, ENABLE);
  ADC_ResetCalibration(ADC1);
  while (ADC_GetResetCalibrationStatus(ADC1)) {
  }
  ADC_StartCalibration(ADC1);
  while (ADC_GetCalibrationStatus(ADC1)) {
  }
  ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

uint16_t adc_raw(adc_ch_t ch) { return ch < ADC_COUNT ? s_adc[ch] : 0u; }

uint32_t adc_mv(adc_ch_t ch) {
  /* VDDA = VMCU-3V3; scale through VREFINT (1.2 V typ.) so supply drift cancels */
  const uint32_t vref = s_adc[ADC_VREFINT];
  if (vref == 0u) return (uint32_t)adc_raw(ch) * 3300u / 4095u;
  return (uint32_t)adc_raw(ch) * 1200u / vref;
}

int32_t mcu_temp_c10(void) { return TempSensor_Volt_To_Temper((int32_t)adc_mv(ADC_TEMP)) * 10; }

const char *adc_name(adc_ch_t ch) { return ch < ADC_COUNT ? k_adc[ch].name : "?"; }

/* ---------------------------------------------------------------- cliff */
/* All six LEDs share CLIFF_LED_EN (PD0), ~1.9 A from VSENS-5V-USB while on.
 * The gate is AC-coupled (C201/R253, ~4.7 ms), so pulses must stay under
 * ~1 ms. Every 10 ms: read "dark", LED on, TIM7 fires after CLIFF_ON_US,
 * read "lit", LED off. ~8 % duty, ~150 mA average. */
#define CLIFF_PERIOD_MS 10u
#define CLIFF_ON_US 800u
static int16_t s_cliff_threshold = 100; /* counts; placeholder until measured */
static uint16_t s_dark[CLIFF_COUNT];
static bool s_enabled;

void TIM7_IRQHandler(void) ISR;
void TIM7_IRQHandler(void) {
  TIM_ClearITPendingBit(TIM7, TIM_IT_Update);
  uint16_t mask = 0u;
  for (int i = 0; i < CLIFF_COUNT; ++i) {
    const int16_t r = (int16_t)(s_dark[i] - s_adc[ADC_CLIFF_LF + i]);
    g_sens.cliff_reflect[i] = r;
    if (r < s_cliff_threshold) mask |= (uint16_t)(1u << i);
  }
  pin_low(PIN(PIN_CLIFF_LED_EN));
  g_sens.cliff_mask = mask;
  ++g_sens.cliff_samples;
}

static void cliff_timer_init(void) {
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);
  TIM_TimeBaseInitTypeDef tb = {0};
  tb.TIM_Prescaler = (uint16_t)(TIMCLK_HZ / 1000000u - 1u); /* 1 us */
  tb.TIM_Period = CLIFF_ON_US - 1u;
  tb.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM7, &tb);
  TIM_SelectOnePulseMode(TIM7, TIM_OPMode_Single);
  TIM_ClearITPendingBit(TIM7, TIM_IT_Update);
  TIM_ITConfig(TIM7, TIM_IT_Update, ENABLE);
  NVIC_SetPriority(TIM7_IRQn, 0x20); /* high: bounds the LED-on time */
  NVIC_EnableIRQ(TIM7_IRQn);
}

static void cliff_fire(void) {
  for (int i = 0; i < CLIFF_COUNT; ++i) s_dark[i] = s_adc[ADC_CLIFF_LF + i];
  pin_high(PIN(PIN_CLIFF_LED_EN));
  TIM_SetCounter(TIM7, 0);
  TIM_Cmd(TIM7, ENABLE);
}

void cliff_set_threshold(int16_t counts) { s_cliff_threshold = counts; }

/* ---------------------------------------------------------------- bumpers */
/* ITR9606 emitters, BUMP_L_LED_EN PE9 (TIM1 CH1), BUMP_R_LED_EN PE11 (TIM1 CH2),
 * PWM at 200 kHz; the 47 nF filters average it. Adjust the duty per unit so
 * the clear-slot level sits around 0.5-1 V. */
#define BUMP_PWM_HZ 200000u
#define BUMP_PERIOD (TIMCLK_HZ / BUMP_PWM_HZ) /* 720 */
#define BUMP_THRESHOLD_MV 1650u  /* placeholder: slot blocked reads high */
static uint16_t s_bump_duty = 300u;

static void bumper_pwm_init(void) {
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
  TIM_TimeBaseInitTypeDef tb = {0};
  tb.TIM_Period = BUMP_PERIOD - 1u;
  tb.TIM_Prescaler = 0;
  tb.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM1, &tb);
  TIM_OCInitTypeDef oc = {0};
  oc.TIM_OCMode = TIM_OCMode_PWM1;
  oc.TIM_OutputState = TIM_OutputState_Enable;
  oc.TIM_OutputNState = TIM_OutputNState_Disable;
  oc.TIM_Pulse = 0;
  oc.TIM_OCPolarity = TIM_OCPolarity_High;
  TIM_OC1Init(TIM1, &oc);
  TIM_OC2Init(TIM1, &oc);
  /* CH3/CH4 would land on PE13/PE14 (UART7 RX, UART8 TX): never enabled. */
  TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
  TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);
  TIM_CtrlPWMOutputs(TIM1, ENABLE);
  TIM_Cmd(TIM1, ENABLE);
}

void bumper_led_duty(uint16_t permille) {
  if (permille > 1000u) permille = 1000u;
  s_bump_duty = permille;
  if (s_enabled) {
    const uint16_t v = (uint16_t)(BUMP_PERIOD * permille / 1000u);
    TIM_SetCompare1(TIM1, v);
    TIM_SetCompare2(TIM1, v);
  }
}

/* ---------------------------------------------------------------- EXTI */
/* Lines: 3 PE3 IMU INT1, 4 PE4 IR front L, 5 PE5 IR front R, 6 PE6 IR side,
 * 7 PC7 IR rear L, 8 PD8 IR rear R, 11 PA11 encoder L, 12 PA12 encoder R. */
static uint32_t s_enc_last_us[2];

static void exti_line(uint8_t port, uint8_t pin, uint32_t line, EXTITrigger_TypeDef trig) {
  GPIO_EXTILineConfig(port, pin);
  EXTI_InitTypeDef e = {0};
  e.EXTI_Line = line;
  e.EXTI_Mode = EXTI_Mode_Interrupt;
  e.EXTI_Trigger = trig;
  e.EXTI_LineCmd = ENABLE;
  EXTI_Init(&e);
}

static void ir_edge(int i) { nec_falling_edge(&g_ir[i], systime_us()); }

void EXTI3_IRQHandler(void) ISR;
void EXTI3_IRQHandler(void) {
  EXTI->INTFR = EXTI_Line3;
  g_sens.imu_int_us = systime_us();
  ++g_sens.imu_int_count;
}

void EXTI4_IRQHandler(void) ISR;
void EXTI4_IRQHandler(void) {
  EXTI->INTFR = EXTI_Line4;
  ir_edge(IR_FRONT_L);
}

void EXTI9_5_IRQHandler(void) ISR;
void EXTI9_5_IRQHandler(void) {
  const uint32_t f = EXTI->INTFR & (EXTI_Line5 | EXTI_Line6 | EXTI_Line7 | EXTI_Line8);
  EXTI->INTFR = f;
  if (f & EXTI_Line5) ir_edge(IR_FRONT_R);
  if (f & EXTI_Line6) ir_edge(IR_SIDE_R);
  if (f & EXTI_Line7) ir_edge(IR_REAR_L);
  if (f & EXTI_Line8) ir_edge(IR_REAR_R);
}

void EXTI15_10_IRQHandler(void) ISR;
void EXTI15_10_IRQHandler(void) {
  const uint32_t f = EXTI->INTFR & (EXTI_Line11 | EXTI_Line12);
  EXTI->INTFR = f;
  const uint32_t now = systime_us();
  for (int i = 0; i < 2; ++i) {
    if (f & (i == 0 ? EXTI_Line11 : EXTI_Line12)) {
      g_sens.enc_period_us[i] = now - s_enc_last_us[i];
      s_enc_last_us[i] = now;
      ++g_sens.enc_count[i];
    }
  }
}

static const IRQn_Type k_exti_irqs[] = {EXTI3_IRQn, EXTI4_IRQn, EXTI9_5_IRQn, EXTI15_10_IRQn};

static void exti_init(void) {
  exti_line(GPIO_PortSourceGPIOE, GPIO_PinSource3, EXTI_Line3, EXTI_Trigger_Rising);
  exti_line(GPIO_PortSourceGPIOE, GPIO_PinSource4, EXTI_Line4, EXTI_Trigger_Falling);
  exti_line(GPIO_PortSourceGPIOE, GPIO_PinSource5, EXTI_Line5, EXTI_Trigger_Falling);
  exti_line(GPIO_PortSourceGPIOE, GPIO_PinSource6, EXTI_Line6, EXTI_Trigger_Falling);
  exti_line(GPIO_PortSourceGPIOC, GPIO_PinSource7, EXTI_Line7, EXTI_Trigger_Falling);
  exti_line(GPIO_PortSourceGPIOD, GPIO_PinSource8, EXTI_Line8, EXTI_Trigger_Falling);
  exti_line(GPIO_PortSourceGPIOA, GPIO_PinSource11, EXTI_Line11, EXTI_Trigger_Rising);
  exti_line(GPIO_PortSourceGPIOA, GPIO_PinSource12, EXTI_Line12, EXTI_Trigger_Rising);
  for (unsigned i = 0; i < sizeof(k_exti_irqs) / sizeof(k_exti_irqs[0]); ++i) {
    NVIC_SetPriority(k_exti_irqs[i], 0x40);
  }
}

/* ---------------------------------------------------------------- top */

#define DROP_ACTIVE_LOW 1 /* TBD on the first boards: switch closed = wheel dropped? */

void sensors_init(void) {
  for (int i = 0; i < IR_COUNT; ++i) nec_init(&g_ir[i]);
  cliff_timer_init();
  bumper_pwm_init();
  exti_init();
}

void sensors_enable(bool on) {
  /* Call with on=true only after VSENS-3V3 and VSENS-5V-USB are up, and with
   * on=false before they go down: the emitters run from VSENS-5V-USB and the
   * receivers, encoders and drop switches from VSENS-3V3. */
  s_enabled = on;
  for (unsigned i = 0; i < sizeof(k_exti_irqs) / sizeof(k_exti_irqs[0]); ++i) {
    if (on) {
      NVIC_EnableIRQ(k_exti_irqs[i]);
    } else {
      NVIC_DisableIRQ(k_exti_irqs[i]);
    }
  }
  if (on) {
    pin_mode(PIN(PIN_BUMP_L_LED_EN), PM_AF_PP);
    pin_mode(PIN(PIN_BUMP_R_LED_EN), PM_AF_PP);
    bumper_led_duty(s_bump_duty);
  } else {
    TIM_Cmd(TIM7, DISABLE);
    pin_low(PIN(PIN_CLIFF_LED_EN));
    pin_output(PIN(PIN_BUMP_L_LED_EN), false, PM_OUT_PP);
    pin_output(PIN(PIN_BUMP_R_LED_EN), false, PM_OUT_PP);
    TIM_SetCompare1(TIM1, 0);
    TIM_SetCompare2(TIM1, 0);
    g_sens.cliff_mask = 0x3Fu; /* unknown = unsafe */
  }
}

void sensors_tick_1ms(void) {
  static uint32_t phase;
  if (!s_enabled) return;
  if (++phase >= CLIFF_PERIOD_MS) {
    phase = 0u;
    cliff_fire();
  }
  for (int i = 0; i < 2; ++i) {
    g_sens.bump_raw[i] = (uint16_t)adc_mv((adc_ch_t)(ADC_BUMP_L + i));
    g_sens.bumper[i] = g_sens.bump_raw[i] > BUMP_THRESHOLD_MV;
  }
  const bool l = pin_read(PIN(PIN_WHEEL_MOT_L_DROP));
  const bool r = pin_read(PIN(PIN_WHEEL_MOT_R_DROP));
  g_sens.drop[0] = DROP_ACTIVE_LOW ? !l : l;
  g_sens.drop[1] = DROP_ACTIVE_LOW ? !r : r;
}
