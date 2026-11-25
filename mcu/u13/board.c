#include "board.h"

#include "systime.h"

typedef struct {
  pin_t pin;
  bool level;
  uint8_t mode;
} pin_init_t;

/* Every U13 pin, in its safe reset-time state. Order matters only in that
 * the levels are written before the modes (pin_output). */
static const pin_init_t k_safe[] = {
    /* Rails: all off. Pull-ups/downs on the board hold them off in reset too. */
    {PIN_INIT(PIN_VMOT_14V4_EN_N), true, PM_OUT_PP},    /* high = motor rail off (R184 pull-up) */
    {PIN_INIT(PIN_VSENS_5V_USB_EN), false, PM_OUT_PP},  /* low = 5 V sensor rail off (R168 pull-down) */
    {PIN_INIT(PIN_VSENS_3V3_EN_N), true, PM_OUT_PP},    /* high = sensor 3.3 V off */
    {PIN_INIT(PIN_WDI), false, PM_OUT_PP},              /* RC watchdog idle: WD_OK low = rails gated */

    /* DRV8870 inputs low = coast (no external pull-downs; the driver's ~100k hold them) */
    {PIN_INIT(PIN_WHEEL_MOT_L_IN1), false, PM_OUT_PP},
    {PIN_INIT(PIN_WHEEL_MOT_L_IN2), false, PM_OUT_PP},
    {PIN_INIT(PIN_WHEEL_MOT_R_IN1), false, PM_OUT_PP},
    {PIN_INIT(PIN_WHEEL_MOT_R_IN2), false, PM_OUT_PP},
    {PIN_INIT(PIN_MAIN_BRUSH_IN1), false, PM_OUT_PP},
    {PIN_INIT(PIN_MAIN_BRUSH_IN2), false, PM_OUT_PP},
    {PIN_INIT(PIN_SIDE_BRUSH_IN1), false, PM_OUT_PP},
    {PIN_INIT(PIN_SIDE_BRUSH_IN2), false, PM_OUT_PP},
    {PIN_INIT(PIN_MAIN_FAN_PWM), false, PM_OUT_PP},      /* R74 pull-down as well */
    {PIN_INIT(PIN_LIDAR_MOTOR_CTRL), false, PM_OUT_PP},  /* R116 pull-down as well */
    {PIN_INIT(PIN_LIDAR_RXD), false, PM_OUT_PP},         /* UART5 TX: low while the LiDAR rail is off */

    /* 5 V emitters: low before the 5 V rail comes up */
    {PIN_INIT(PIN_CLIFF_LED_EN), false, PM_OUT_PP},
    {PIN_INIT(PIN_BUMP_L_LED_EN), false, PM_OUT_PP},
    {PIN_INIT(PIN_BUMP_R_LED_EN), false, PM_OUT_PP},
#ifdef BOARD_EVT
    /* EVT board: PC6-PC9 go to the Ethernet magnetics, centre taps on 3.3 V.
     * Driving PC6 low would short 3.3 V through the winding. */
    {PIN_INIT(PIN_CARPET_SENSOR_LO), false, PM_IN_FLOAT},
#else
    {PIN_INIT(PIN_CARPET_SENSOR_LO), false, PM_OUT_PP},
#endif

    /* Camera: PWDN and XCLK low while VSENS-3V3 is off; SCCB released */
    {PIN_INIT(PIN_CAM_PWDN), false, PM_OUT_PP},
    {PIN_INIT(PIN_CAM_OSC), false, PM_OUT_PP},
    {PIN_INIT(PIN_CAM_SCL), true, PM_IN_FLOAT}, /* PC14/PC15: R246/R247 pull-ups to VSENS-3V3 */
    {PIN_INIT(PIN_CAM_SDA), true, PM_IN_FLOAT},

    /* U28 control: NRST released (U28's internal pull-up), BOOT0 low */
    {PIN_INIT(PIN_MCU2_NRST), true, PM_OUT_OD_2M},
    {PIN_INIT(PIN_MCU2_BOOT), false, PM_OUT_PP},

    /* Analog inputs */
    {PIN_INIT(PIN_WHEEL_MOT_L_ADC), false, PM_ANALOG},
    {PIN_INIT(PIN_WHEEL_MOT_R_ADC), false, PM_ANALOG},
    {PIN_INIT(PIN_MAIN_BRUSH_ADC), false, PM_ANALOG},
    {PIN_INIT(PIN_SIDE_BRUSH_ADC), false, PM_ANALOG},
    {PIN_INIT(PIN_BUMP_L), false, PM_ANALOG},
    {PIN_INIT(PIN_BUMP_R), false, PM_ANALOG},
    {PIN_INIT(PIN_ECHO_ENV), false, PM_ANALOG},
    {PIN_INIT(PIN_CLIFF_LF), false, PM_ANALOG},
    {PIN_INIT(PIN_CLIFF_RF), false, PM_ANALOG},
    {PIN_INIT(PIN_CLIFF_LM), false, PM_ANALOG},
    {PIN_INIT(PIN_CLIFF_RM), false, PM_ANALOG},
    {PIN_INIT(PIN_CLIFF_LR), false, PM_ANALOG},
    {PIN_INIT(PIN_CLIFF_RR), false, PM_ANALOG},
    {PIN_INIT(PIN_ECHO_ADC), false, PM_ANALOG}, /* OPA3 CH0P */
    {PIN_INIT(PIN_ECHO_FB), false, PM_ANALOG},  /* OPA3 CH0N (also BOOT1, R241 1M pull-down) */
    {PIN_INIT(PIN_ECHO_AMP), false, PM_ANALOG}, /* OPA3 OUT1 */

    /* Digital inputs with board pull-ups to VSENS-3V3 (read low while that rail is off) */
    {PIN_INIT(PIN_WHEEL_MOT_L_ENCODE), false, PM_IN_FLOAT},
    {PIN_INIT(PIN_WHEEL_MOT_R_ENCODE), false, PM_IN_FLOAT},
    {PIN_INIT(PIN_WHEEL_MOT_L_DROP), false, PM_IN_FLOAT},
    {PIN_INIT(PIN_WHEEL_MOT_R_DROP), false, PM_IN_FLOAT},
    {PIN_INIT(PIN_MAIN_FAN_FB), false, PM_IN_FLOAT},
    {PIN_INIT(PIN_INT_SENSORS_N), false, PM_IN_FLOAT},
    {PIN_INIT(PIN_IMU_SCL), false, PM_IN_FLOAT},
    {PIN_INIT(PIN_IMU_SDA), false, PM_IN_FLOAT},

    /* No board pull resistor */
    {PIN_INIT(PIN_HALL_TOWER), true, PM_IN_PULL},  /* pull-up: open-drain Hall */
    {PIN_INIT(PIN_IMU_INT1), false, PM_IN_PULL},   /* pull-down while the IMU is unpowered */
    {PIN_INIT(PIN_IR_F_L), false, PM_IN_FLOAT},    /* IR receivers drive (internal pull-up) */
    {PIN_INIT(PIN_IR_F_R), false, PM_IN_FLOAT},
    {PIN_INIT(PIN_USART2_IR_R_RX), false, PM_IN_FLOAT},
    {PIN_INIT(PIN_REAR_IR_L), false, PM_IN_FLOAT},
    {PIN_INIT(PIN_REAR_IR_R), false, PM_IN_FLOAT},
    {PIN_INIT(PIN_LIDAR_TXD), false, PM_IN_FLOAT}, /* UART5 RX */

    /* Camera data and sync inputs: pull-downs while the camera is off */
    {PIN_INIT(PIN_CAM_HSYNC), false, PM_IN_PULL},
    {PIN_INIT(PIN_CAM_VSYNC), false, PM_IN_PULL},
    {PIN_INIT(PIN_CAM_PCLK), false, PM_IN_PULL},
    {PIN_INIT(PIN_CAM_Y2), false, PM_IN_PULL},
    {PIN_INIT(PIN_CAM_Y3), false, PM_IN_PULL},
    {PIN_INIT(PIN_CAM_Y4), false, PM_IN_PULL},
    {PIN_INIT(PIN_CAM_Y5), false, PM_IN_PULL},
    {PIN_INIT(PIN_CAM_Y6), false, PM_IN_PULL},
    {PIN_INIT(PIN_CAM_Y7), false, PM_IN_PULL},
    {PIN_INIT(PIN_CAM_Y8), false, PM_IN_PULL},
    {PIN_INIT(PIN_CAM_Y9), false, PM_IN_PULL},

    /* UART RX lines idle high */
    {PIN_INIT(PIN_MCU_RX), true, PM_IN_PULL},
    {PIN_INIT(PIN_MCU2_TX), true, PM_IN_PULL},

    /* Spares on J1/J13: pulled down until someone uses them */
    {PIN_INIT(PIN_SPARE_TX), false, PM_IN_PULL},
    {PIN_INIT(PIN_SPARE_RX), false, PM_IN_PULL},
    {PIN_INIT(PIN_SPARE_PE2), false, PM_IN_PULL},
    {PIN_INIT(PIN_SPARE_PA15), false, PM_IN_PULL},
};

void board_safe_init(void) {
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                             RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE,
                         ENABLE);
  for (unsigned i = 0; i < sizeof(k_safe) / sizeof(k_safe[0]); ++i) {
    const pin_init_t *p = &k_safe[i];
    if (p->mode == PM_IN_PULL || p->mode == PM_IN_FLOAT || p->mode == PM_ANALOG) {
      pin_write(p->pin, p->level); /* selects pull-up/down for PM_IN_PULL */
      pin_mode(p->pin, p->mode);
    } else {
      pin_output(p->pin, p->level, p->mode);
    }
  }
}

void board_remap(void) {
  /* Remaps also route unused channels, ETR and BKIN onto pins with other jobs
   * (e.g. TIM10 ETR -> PD0 CLIFF_LED_EN, TIM10 BKIN -> PE2, TIM1 CH3/CH4 ->
   * PE13/PE14 UART7_RX/UART8_TX, TIM9 CH3 -> PD13). Never enable those
   * outputs, ETR or the break inputs. */
  GPIO_PinRemapConfig(GPIO_FullRemap_TIM10, ENABLE); /* CH1-4: PD1, PD3, PD5, PD7 wheels */
  GPIO_PinRemapConfig(GPIO_Remap_TIM4, ENABLE);      /* CH1-4: PD12-PD15 brushes */
  GPIO_PinRemapConfig(GPIO_FullRemap_TIM9, ENABLE);  /* CH1 PD9 fan, CH2 PD11 LiDAR motor */
  GPIO_PinRemapConfig(GPIO_FullRemap_TIM1, ENABLE);  /* CH1 PE9, CH2 PE11 bumper emitters */
  GPIO_PinRemapConfig(GPIO_PartialRemap_TIM3, ENABLE); /* CH1 PB4 fan tachometer */
  GPIO_PinRemapConfig(GPIO_FullRemap_USART7, ENABLE);  /* UART7 PE12/PE13 console */
  GPIO_PinRemapConfig(GPIO_FullRemap_USART8, ENABLE);  /* UART8 PE14/PE15 U28 link */
  GPIO_PinRemapConfig(GPIO_FullRemap_USART4, ENABLE);  /* UART4 PE0/PE1 spare (J1) */
}

static const struct {
  pin_t pin;
  bool on_level;
  const char *name;
} k_rails[RAIL_COUNT] = {
    [RAIL_SENS_3V3] = {PIN_INIT(PIN_VSENS_3V3_EN_N), false, "sens3v3"},
    [RAIL_SENS_5V] = {PIN_INIT(PIN_VSENS_5V_USB_EN), true, "sens5v"},
    [RAIL_MOTOR] = {PIN_INIT(PIN_VMOT_14V4_EN_N), false, "motor"},
};

void rail_set(rail_t r, bool on) {
  if (r >= RAIL_COUNT) return;
  pin_write(k_rails[r].pin, on ? k_rails[r].on_level : !k_rails[r].on_level);
}

bool rail_on(rail_t r) {
  if (r >= RAIL_COUNT) return false;
  return pin_out_state(k_rails[r].pin) == k_rails[r].on_level;
}

const char *rail_name(rail_t r) { return r < RAIL_COUNT ? k_rails[r].name : "?"; }

void wdi_toggle(void) {
  const pin_t p = PIN(PIN_WDI);
  pin_write(p, !pin_out_state(p));
}

void iwdg_start(void) {
  DBGMCU_Config(DBGMCU_IWDG_STOP, ENABLE);
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_32); /* LSI ~40 kHz / 32 = 1.25 kHz */
  IWDG_SetReload(325u);                 /* ~260 ms */
  IWDG_ReloadCounter();
  IWDG_Enable();
}

void iwdg_feed(void) { IWDG_ReloadCounter(); }

bool board_reset_was_iwdg(void) {
  const bool iwdg = (RCC->RSTSCKR & RCC_IWDGRSTF) != 0u;
  RCC->RSTSCKR |= RCC_RMVF;
  return iwdg;
}

void u28_reset_pulse(bool to_bootloader) {
  pin_write(PIN(PIN_MCU2_BOOT), to_bootloader);
  pin_low(PIN(PIN_MCU2_NRST));
  systime_delay_us(1000u);
  pin_high(PIN(PIN_MCU2_NRST)); /* open drain: released */
  /* BOOT0 is sampled at reset release; leave it where it is so the
   * bootloader stays selected until the flashing code drops it. */
}
