#include "carpet.h"

#include "board.h"
#include "systime.h"

#define BURST_HZ 290000u
#define BURST_PERIOD (TIMCLK_HZ / BURST_HZ) /* 496 -> 290.3 kHz */
#define LISTEN_US 1500u
#define BLANK_US 150u /* ignore the drive ringing; tune on the bench */

void carpet_init(void) {
  /* OPA3: CHP0 = PB13 (biased, clamped echo), CHN0 = PB2 (feedback), OUT1 = PE7 */
  OPA_InitTypeDef o;
  OPA_StructInit(&o);
  o.OPA_NUM = OPA3;
  o.PSEL = CHP0;
  o.NSEL = CHN0;
  o.Mode = OUT_IO_OUT1;
  OPA_Init(&o);
  OPA_Cmd(OPA3, ENABLE);

  /* TIM8 CH1: one burst of N cycles, LO high = FET on */
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM8 | RCC_APB2Periph_ADC2, ENABLE);
  TIM_TimeBaseInitTypeDef tb = {0};
  tb.TIM_Period = BURST_PERIOD - 1u;
  tb.TIM_Prescaler = 0;
  tb.TIM_CounterMode = TIM_CounterMode_Up;
  tb.TIM_RepetitionCounter = 0;
  TIM_TimeBaseInit(TIM8, &tb);
  TIM_OCInitTypeDef oc = {0};
  /* PWM2: low while CNT < CCR. The one-pulse timer stops with CNT = 0, so
   * the output rests low (FET off), not high as it would with PWM1. */
  oc.TIM_OCMode = TIM_OCMode_PWM2;
  oc.TIM_OutputState = TIM_OutputState_Enable;
  oc.TIM_OutputNState = TIM_OutputNState_Disable;
  oc.TIM_Pulse = BURST_PERIOD / 2u;
  oc.TIM_OCPolarity = TIM_OCPolarity_High;
  oc.TIM_OCIdleState = TIM_OCIdleState_Reset;
  TIM_OC1Init(TIM8, &oc);
  TIM_SelectOnePulseMode(TIM8, TIM_OPMode_Single);
  TIM_CtrlPWMOutputs(TIM8, ENABLE);

  /* ADC2: PA1 (channel 1) only, fastest sample time, software triggered */
  ADC_InitTypeDef a = {0};
  a.ADC_Mode = ADC_Mode_Independent;
  a.ADC_ScanConvMode = DISABLE;
  a.ADC_ContinuousConvMode = DISABLE;
  a.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
  a.ADC_DataAlign = ADC_DataAlign_Right;
  a.ADC_NbrOfChannel = 1;
  ADC_Init(ADC2, &a);
  ADC_RegularChannelConfig(ADC2, ADC_Channel_1, 1, ADC_SampleTime_7Cycles5);
  ADC_Cmd(ADC2, ENABLE);
  ADC_ResetCalibration(ADC2);
  while (ADC_GetResetCalibrationStatus(ADC2)) {
  }
  ADC_StartCalibration(ADC2);
  while (ADC_GetCalibrationStatus(ADC2)) {
  }
}

static uint16_t adc2_read_mv(void) {
  ADC_SoftwareStartConvCmd(ADC2, ENABLE);
  while (!ADC_GetFlagStatus(ADC2, ADC_FLAG_EOC)) {
  }
  return (uint16_t)((ADC_GetConversionValue(ADC2) * 3300u) / 4095u);
}

bool carpet_ping(uint8_t cycles, carpet_echo_t *out) {
  if (cycles == 0u) return false;
  out->idle_mv = adc2_read_mv();
  out->peak_mv = 0u;
  out->peak_us = 0u;
  out->samples = 0u;

  TIM8->RPTCR = (uint16_t)(cycles - 1u); /* repetition counter: N periods, then stop */
  TIM_GenerateEvent(TIM8, TIM_EventSource_Update);
  TIM_ClearFlag(TIM8, TIM_FLAG_Update);
  pin_mode(PIN(PIN_CARPET_SENSOR_LO), PM_AF_PP);
  const uint32_t t0 = systime_us();
  TIM_Cmd(TIM8, ENABLE);

  uint32_t dt;
  while ((dt = systime_us() - t0) < LISTEN_US) {
    const uint16_t mv = adc2_read_mv();
    ++out->samples;
    if (dt >= BLANK_US && mv > out->peak_mv) {
      out->peak_mv = mv;
      out->peak_us = (uint16_t)dt;
    }
  }
  /* The one-pulse timer has long stopped: park LO low as a GPIO. */
  pin_output(PIN(PIN_CARPET_SENSOR_LO), false, PM_OUT_PP);
  return true;
}
