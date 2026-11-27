#include "board.h"

#include "wch_globals.h"

typedef struct {
  pin_t pin;
  bool level;
  uint8_t mode;
} pin_init_t;

static const pin_init_t k_safe[] = {
    /* Outputs: everything off. PWR-HOLD is set before this table runs. */
    {PIN_INIT(PIN_MCU2_LED_HOME), false, PM_OUT_PP},
    {PIN_INIT(PIN_MCU2_LED_PWR), false, PM_OUT_PP},
    {PIN_INIT(PIN_MCU2_LED_CLEAN), false, PM_OUT_PP},
    {PIN_INIT(PIN_MCU2_TORCH), false, PM_OUT_PP},          /* R257 pull-down too */
    {PIN_INIT(PIN_MCU2_CAM_RESET), false, PM_OUT_PP},      /* camera held in reset (R244) */
    {PIN_INIT(PIN_MCU2_SIDE_PROXI_CE_R), false, PM_OUT_PP},/* VL53L0X XSHUT low: off */
    {PIN_INIT(PIN_MCU2_USB_PWR_EN), false, PM_OUT_PP},     /* no 5 V to the phone */
    {PIN_INIT(PIN_MCU2_CC_RD_EN), true, PM_OUT_PP},        /* Rd on: dead-battery sink (R238) */
    {PIN_INIT(PIN_MCU2_CHG_I_HI), false, PM_OUT_PP_2M},    /* 1.2 A charging (PC13: 2 MHz) */

    /* Inputs */
    {PIN_INIT(PIN_MCU2_CLEAN_BTN_N), true, PM_IN_PULL},  /* PC14, to GND when pressed */
    {PIN_INIT(PIN_MCU2_HOME_BTN_N), true, PM_IN_PULL},
#ifdef BOARD_EVT
    /* Evaluation board: no R173 (and PA12 is its USB D+): pull up internally. */
    {PIN_INIT(PIN_MCU2_PWR_BTN_SENSE), true, PM_IN_PULL},
#else
    {PIN_INIT(PIN_MCU2_PWR_BTN_SENSE), true, PM_IN_FLOAT}, /* R173 pull-up, D26 to the button */
#endif
    {PIN_INIT(PIN_MCU2_HALL_BIN), true, PM_IN_PULL},     /* open-drain Hall, no board pull-up */
    {PIN_INIT(PIN_MCU2_STDBY_N), true, PM_IN_PULL},      /* SLM6900 open drain via 1k */
    {PIN_INIT(PIN_MCU2_CHRG_N), true, PM_IN_PULL},
    {PIN_INIT(PIN_MCU2_USB_CC1), false, PM_IN_FLOAT},    /* USBPD PHY */
    {PIN_INIT(PIN_MCU2_USB_CC2), false, PM_IN_FLOAT},
    {PIN_INIT(PIN_MCU2_CAM_SCL), true, PM_IN_FLOAT},     /* U13 bit-bangs this bus by default */
    {PIN_INIT(PIN_MCU2_CAM_SDA), true, PM_IN_FLOAT},
    {PIN_INIT(PIN_MCU2_BOOT1), false, PM_IN_FLOAT},      /* R249 pull-down */
    {PIN_INIT(PIN_MCU2_MCU2_RX), true, PM_IN_PULL},      /* PA3 USART2 RX */

    /* Analog */
    {PIN_INIT(PIN_MCU2_HALL_FLOOR_L), false, PM_ANALOG},
    {PIN_INIT(PIN_MCU2_HALL_FLOOR_R), false, PM_ANALOG},
    {PIN_INIT(PIN_MCU2_BAT_SENSE), false, PM_ANALOG},
    {PIN_INIT(PIN_MCU2_DC_IN_SENSE), false, PM_ANALOG},
    {PIN_INIT(PIN_MCU2_BAT_NTC), false, PM_ANALOG},
    {PIN_INIT(PIN_MCU2_USB_VBUS_SENSE), false, PM_ANALOG},
    {PIN_INIT(PIN_MCU2_BATTERY_CHARGER_ID), false, PM_ANALOG},
    {PIN_INIT(PIN_MCU2_LDR), false, PM_ANALOG},

    /* Spares (J1, J13) */
    {PIN_INIT(PIN_MCU2_MCU2_PB12), false, PM_IN_PULL},
    {PIN_INIT(PIN_MCU2_MCU2_PC15), false, PM_IN_PULL},
};

void board_safe_init(void) {
  /* Factory calibration words the WCH library expects in globals. */
  ADC_Trim = *(volatile uint16_t *)ADC_TRIM_BASE;
  TS_Val = *(volatile uint32_t *)TS_BASE;
  CHIPID = *(volatile uint32_t *)CHIPID_BASE;
  USBPD_CFG = *(volatile uint16_t *)USBPD_CFG_BASE;

  RCC_PB2PeriphClockCmd(RCC_PB2Periph_AFIO | RCC_PB2Periph_GPIOA | RCC_PB2Periph_GPIOB |
                            RCC_PB2Periph_GPIOC | RCC_PB2Periph_GPIOD,
                        ENABLE);
  /* First: hold the power latch (R172 holds it only until here). */
  pin_output(PIN(PIN_MCU2_PWR_HOLD), true, PM_OUT_PP);
  for (unsigned i = 0; i < sizeof(k_safe) / sizeof(k_safe[0]); ++i) {
    const pin_init_t *p = &k_safe[i];
    if (p->mode == PM_IN_PULL || p->mode == PM_IN_FLOAT || p->mode == PM_ANALOG) {
      pin_write(p->pin, p->level);
      pin_mode(p->pin, p->mode);
    } else {
      pin_output(p->pin, p->level, p->mode);
    }
  }
}

/* ---------------------------------------------------------------- PWM */
#define LED_PERIOD 1000u /* TIM1 at 1 MHz / 1000 = 1 kHz */
#define TORCH_HZ 25000u
#define TORCH_PERIOD (TIM4CLK_HZ / TORCH_HZ) /* 3840 */

static void oc_pwm(TIM_TypeDef *t, uint8_t ch) {
  TIM_OCInitTypeDef oc = {0};
  oc.TIM_OCMode = TIM_OCMode_PWM1;
  oc.TIM_OutputState = TIM_OutputState_Enable;
  oc.TIM_OutputNState = TIM_OutputNState_Disable;
  oc.TIM_Pulse = 0;
  oc.TIM_OCPolarity = TIM_OCPolarity_High;
  if (ch == 1) TIM_OC1Init(t, &oc);
  if (ch == 2) TIM_OC2Init(t, &oc);
  if (ch == 3) TIM_OC3Init(t, &oc);
}

void board_pwm_init(void) {
  RCC_PB2PeriphClockCmd(RCC_PB2Periph_TIM1, ENABLE);
  RCC_PB1PeriphClockCmd(RCC_PB1Periph_TIM4, ENABLE);

  TIM_TimeBaseInitTypeDef tb = {0};
  tb.TIM_Prescaler = (uint16_t)(TIM1CLK_HZ / 1000000u - 1u); /* 1 MHz */
  tb.TIM_Period = LED_PERIOD - 1u;                           /* 1 kHz */
  tb.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM1, &tb);
  oc_pwm(TIM1, 1);
  oc_pwm(TIM1, 2);
  oc_pwm(TIM1, 3);
  TIM_CtrlPWMOutputs(TIM1, ENABLE);
  TIM_Cmd(TIM1, ENABLE);

  tb.TIM_Prescaler = 0;
  tb.TIM_Period = TORCH_PERIOD - 1u;
  TIM_TimeBaseInit(TIM4, &tb);
  oc_pwm(TIM4, 3);
  TIM_Cmd(TIM4, ENABLE);

  pin_mode(PIN(PIN_MCU2_LED_HOME), PM_AF_PP);
  pin_mode(PIN(PIN_MCU2_LED_PWR), PM_AF_PP);
  pin_mode(PIN(PIN_MCU2_LED_CLEAN), PM_AF_PP);
  pin_mode(PIN(PIN_MCU2_TORCH), PM_AF_PP);
}

void led_home_set(uint16_t permille) {
  TIM_SetCompare1(TIM1, (uint16_t)((permille > 1000u ? 1000u : permille) * LED_PERIOD / 1000u));
}

void led_power_set(uint16_t permille) {
  TIM_SetCompare2(TIM1, (uint16_t)((permille > 1000u ? 1000u : permille) * LED_PERIOD / 1000u));
}

void led_clean_set(uint16_t permille) {
  TIM_SetCompare3(TIM1, (uint16_t)((permille > 1000u ? 1000u : permille) * LED_PERIOD / 1000u));
}

void torch_set(uint8_t pct) {
  if (pct > 100u) pct = 100u;
  TIM_SetCompare3(TIM4, (uint16_t)(TORCH_PERIOD * pct / 100u));
}

/* ---------------------------------------------------------------- ADC */

static const uint8_t k_adc_ch[U28_ADC_COUNT] = {
    ADC_Channel_0, ADC_Channel_1, ADC_Channel_4, ADC_Channel_5, ADC_Channel_6,
    ADC_Channel_7, ADC_Channel_8, ADC_Channel_9, ADC_Channel_TempSensor, ADC_Channel_Vrefint,
};
static volatile uint16_t s_adc[U28_ADC_COUNT];

void board_adc_start(void) {
  RCC_PB2PeriphClockCmd(RCC_PB2Periph_ADC1, ENABLE);
  RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);

  DMA_DeInit(DMA1_Channel1);
  DMA_InitTypeDef d = {0};
  d.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
  d.DMA_MemoryBaseAddr = (uint32_t)s_adc;
  d.DMA_DIR = DMA_DIR_PeripheralSRC;
  d.DMA_BufferSize = U28_ADC_COUNT;
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
  a.ADC_NbrOfChannel = U28_ADC_COUNT;
  ADC_Init(ADC1, &a);
  for (uint8_t i = 0; i < U28_ADC_COUNT; ++i) {
    /* Longest sample time: BAT_SENSE and DC_IN_SENSE are 1M/100k dividers
     * (with 100 nF, so fine), BAT_ID has R254 (1M) as pull-up, ~380k source with the pack. */
    ADC_RegularChannelConfig(ADC1, k_adc_ch[i], (uint8_t)(i + 1u), ADC_SampleTime_CyclesMode7);
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

uint16_t board_adc_raw(u28_adc_t ch) { return ch < U28_ADC_COUNT ? s_adc[ch] : 0u; }

uint32_t board_adc_mv(u28_adc_t ch) {
  const uint32_t vref = s_adc[U28_ADC_VREFINT];
  if (vref == 0u) return (uint32_t)board_adc_raw(ch) * 3300u / 4095u;
  return (uint32_t)board_adc_raw(ch) * 1200u / vref;
}

int32_t board_temp_c10(void) {
  return TempSensor_Volt_To_Temper((int32_t)board_adc_mv(U28_ADC_TEMP)) * 10;
}

/* ---------------------------------------------------------------- misc */

void pwr_hold(bool on) { pin_write(PIN(PIN_MCU2_PWR_HOLD), on); }
bool pwr_button_pressed(void) { return !pin_read(PIN(PIN_MCU2_PWR_BTN_SENSE)); }
bool home_button_pressed(void) { return !pin_read(PIN(PIN_MCU2_HOME_BTN_N)); }
bool clean_button_pressed(void) { return !pin_read(PIN(PIN_MCU2_CLEAN_BTN_N)); }
bool hall_bin_present(void) { return !pin_read(PIN(PIN_MCU2_HALL_BIN)); }
bool chrg_low(void) { return !pin_read(PIN(PIN_MCU2_CHRG_N)); }
bool stdby_low(void) { return !pin_read(PIN(PIN_MCU2_STDBY_N)); }

void iwdg_start(void) {
  DBGMCU_Config(DBGMCU_IWDG_STOP, ENABLE);
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_32);
  IWDG_SetReload(325u); /* ~260 ms at LSI ~40 kHz */
  IWDG_ReloadCounter();
  IWDG_Enable();
}

void iwdg_feed(void) { IWDG_ReloadCounter(); }
