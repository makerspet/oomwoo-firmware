/* Charging dock firmware (CH32V003F4P6, kicad/charging-dock, U1001).
 *
 *   PA2 A0 VIN-SENSE    x11            PC1 CHARGE-EN  high = VDOCK-20V on (R1013 pull-down)
 *   PA1 A1 DOCK-SENSE   x11            PC2 PROBE-DRV  high = probe on (R1014 100k, D1002)
 *   PD2 A3 PROBE-SENSE                 PC3 IR-L       TIM1_CH3, 38 kHz
 *   PD3 A4 I-SENSE      1 V/A (INA180) PC4 IR-R       TIM1_CH4, 38 kHz
 *   PC5/PC6 I2C1 (full remap) to the CH224A (0x22)    PC0 status LED (high = on)
 *   PD5/PD6 USART1 debug console on J1005 (115200)    PD1 SWIO (J1001)
 *
 * The decisions live in common/dock_sm.c (host-tested). This file samples,
 * filters, talks to the CH224A, sends the IR beacon and drives the pins. */
#include <string.h>

#include "chip.h"
#include "cli.h"
#include "dock_beacon.h"
#include "dock_sm.h"
#include "gpio.h"
#include "nec_tx.h"
#include "systime.h"
#include "uart.h"

#define HCLK_HZ 48000000u

static const pin_t P_CHARGE_EN = {GPIOC, 1};
static const pin_t P_PROBE_DRV = {GPIOC, 2};
static const pin_t P_LED = {GPIOC, 0};
static const pin_t P_IR_L = {GPIOC, 3};
static const pin_t P_IR_R = {GPIOC, 4};
static const pin_t P_SCL = {GPIOC, 5};
static const pin_t P_SDA = {GPIOC, 6};
static const pin_t P_TX = {GPIOD, 5};
static const pin_t P_RX = {GPIOD, 6};

/* ---------------------------------------------------------------- ADC */

enum { A_VIN = 0, A_DOCK, A_PROBE, A_ISENSE, A_VREF, A_COUNT };
static const uint8_t k_ch[A_COUNT] = {ADC_Channel_0, ADC_Channel_1, ADC_Channel_3, ADC_Channel_4,
                                      ADC_Channel_Vrefint};
static volatile uint16_t s_adc[A_COUNT];
static uint32_t s_filt[A_COUNT]; /* raw x 16, IIR 1/8 per ms */
static volatile bool s_trip;

#define VREFINT_MV 1200u
#define TRIP_MA 3300u /* hardware trip, a little above the firmware limit */

void ADC1_IRQHandler(void) ISR;
void ADC1_IRQHandler(void) {
  if (ADC_GetITStatus(ADC1, ADC_IT_AWD)) {
    pin_low(P_CHARGE_EN); /* first, before anything else */
    s_trip = true;
    ADC_ITConfig(ADC1, ADC_IT_AWD, DISABLE);
    ADC_ClearITPendingBit(ADC1, ADC_IT_AWD);
  }
}

static void adc_start(void) {
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
  DMA_DeInit(DMA1_Channel1);
  DMA_InitTypeDef d = {0};
  d.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
  d.DMA_MemoryBaseAddr = (uint32_t)s_adc;
  d.DMA_DIR = DMA_DIR_PeripheralSRC;
  d.DMA_BufferSize = A_COUNT;
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
  a.ADC_NbrOfChannel = A_COUNT;
  ADC_Init(ADC1, &a);
  for (uint8_t i = 0; i < A_COUNT; ++i) {
    ADC_RegularChannelConfig(ADC1, k_ch[i], (uint8_t)(i + 1u), ADC_SampleTime_241Cycles);
  }
  /* Fast overcurrent trip on I-SENSE (1 V/A, 10-bit, VDD reference). */
  ADC_AnalogWatchdogSingleChannelConfig(ADC1, ADC_Channel_4);
  ADC_AnalogWatchdogThresholdsConfig(ADC1, (uint16_t)(TRIP_MA * 1023u / 3300u), 0);
  ADC_AnalogWatchdogCmd(ADC1, ADC_AnalogWatchdog_SingleRegEnable);
  NVIC_SetPriority(ADC_IRQn, 0x00);
  NVIC_EnableIRQ(ADC_IRQn);

  ADC_DMACmd(ADC1, ENABLE);
  ADC_Cmd(ADC1, ENABLE);
  ADC_ResetCalibration(ADC1);
  while (ADC_GetResetCalibrationStatus(ADC1)) {
  }
  ADC_StartCalibration(ADC1);
  while (ADC_GetCalibrationStatus(ADC1)) {
  }
  ADC_SoftwareStartConvCmd(ADC1, ENABLE);
  for (int i = 0; i < A_COUNT; ++i) s_filt[i] = 0u;
}

static void adc_filter(void) {
  for (int i = 0; i < A_COUNT; ++i) {
    const uint32_t x = (uint32_t)s_adc[i] << 4;
    if (s_filt[i] == 0u) {
      s_filt[i] = x;
    } else {
      s_filt[i] = s_filt[i] - (s_filt[i] >> 3) + (x >> 3);
    }
  }
}

static uint32_t vdd_mv(void) {
  const uint32_t v = s_filt[A_VREF];
  return v ? (VREFINT_MV * 1023u * 16u) / v : 3300u;
}

/* pin voltage in mV, ratiometric to VDD */
static uint32_t pin_mv(int ch) { return (s_filt[ch] * vdd_mv()) / (1023u * 16u); }

/* ---------------------------------------------------------------- CH224A */

#define CH224_ADDR 0x22u
#define I2C_TIMEOUT_US 2000u

static bool i2c_wait(uint16_t flag) {
  const uint32_t t0 = systime_us();
  while ((I2C1->STAR1 & flag) == 0u) {
    if (I2C1->STAR1 & (I2C_STAR1_AF | I2C_STAR1_BERR | I2C_STAR1_ARLO)) return false;
    if (systime_us() - t0 > I2C_TIMEOUT_US) return false;
  }
  return true;
}

/* Single-byte register read (the CH224A does not do burst reads). */
static bool ch224_read(uint8_t reg, uint8_t *val) {
  bool ok = false;
  I2C1->CTLR1 |= I2C_CTLR1_START;
  if (!i2c_wait(I2C_STAR1_SB)) goto out;
  I2C1->DATAR = CH224_ADDR << 1;
  if (!i2c_wait(I2C_STAR1_ADDR)) goto out;
  (void)I2C1->STAR2;
  I2C1->DATAR = reg;
  if (!i2c_wait(I2C_STAR1_BTF)) goto out;
  I2C1->CTLR1 |= I2C_CTLR1_START;
  if (!i2c_wait(I2C_STAR1_SB)) goto out;
  I2C1->DATAR = (CH224_ADDR << 1) | 1u;
  if (!i2c_wait(I2C_STAR1_ADDR)) goto out;
  I2C1->CTLR1 &= ~I2C_CTLR1_ACK;
  __disable_irq();
  (void)I2C1->STAR2;
  I2C1->CTLR1 |= I2C_CTLR1_STOP;
  __enable_irq();
  if (!i2c_wait(I2C_STAR1_RXNE)) goto out;
  *val = (uint8_t)I2C1->DATAR;
  return true;
out:
  I2C1->CTLR1 |= I2C_CTLR1_STOP;
  I2C1->STAR1 = 0u;
  return ok;
}

static void i2c_start(void) {
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
  GPIO_PinRemapConfig(GPIO_FullRemap_I2C1, ENABLE); /* SCL PC5, SDA PC6 */
  I2C_InitTypeDef c = {0};
  c.I2C_ClockSpeed = 100000;
  c.I2C_Mode = I2C_Mode_I2C;
  c.I2C_DutyCycle = I2C_DutyCycle_2;
  c.I2C_OwnAddress1 = 0x02;
  c.I2C_Ack = I2C_Ack_Enable;
  c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
  I2C_Init(I2C1, &c);
  I2C_Cmd(I2C1, ENABLE);
  pin_mode(P_SCL, PM_AF_OD); /* pull-ups: the CH224A's own (7-15k) */
  pin_mode(P_SDA, PM_AF_OD);
}

static struct {
  bool ok;        /* last read answered */
  uint8_t status; /* 0x09 */
  uint8_t limit;  /* 0x50, 50 mA units */
  uint32_t errors;
} s_pd;

static void pd_poll(void) {
  uint8_t st = 0, lim = 0;
  const bool ok = ch224_read(0x09, &st) && ch224_read(0x50, &lim);
  if (!ok) {
    ++s_pd.errors;
    s_pd.ok = false;
    return;
  }
  s_pd.ok = true;
  s_pd.status = st;
  s_pd.limit = lim;
}

/* ---------------------------------------------------------------- beacon */
/* TIM1 makes the 38 kHz carrier at 1/3 duty on CH3 (IR-L) and CH4 (IR-R).
 * TIM2 (1 us ticks) walks the NEC mark/space timeline: each update switches
 * the active side's channel between PWM1 (mark) and forced-inactive (space).
 * Left and right take turns with a 40 ms gap, so a receiver that sees both
 * strings never gets overlapping frames. */
#define CARRIER_HZ 38000u
#define CARRIER_PERIOD (HCLK_HZ / CARRIER_HZ) /* 1263 */
#define BEACON_GAP_US 40000u

static uint16_t s_seg[NEC_TX_SEGMENTS];
static volatile uint8_t s_seg_i;
static volatile uint8_t s_side; /* 0 left, 1 right */
static volatile uint8_t s_budget_code;
static volatile bool s_in_gap;
static volatile uint32_t s_frames_sent;

static void carrier(uint8_t side, bool on) {
  /* CHCTLR2: OC3M bits 6:4, OC4M bits 14:12; 110 = PWM1, 100 = forced inactive */
  const uint16_t mode = on ? 0x6u : 0x4u;
  uint16_t r = TIM1->CHCTLR2;
  if (side == 0u) {
    r = (uint16_t)((r & ~0x0070u) | (mode << 4));
  } else {
    r = (uint16_t)((r & ~0x7000u) | (mode << 12));
  }
  TIM1->CHCTLR2 = r;
}

void TIM2_IRQHandler(void) ISR;
void TIM2_IRQHandler(void) {
  TIM2->INTFR = (uint16_t)~TIM_IT_Update;
  if (s_in_gap) {
    s_in_gap = false;
    s_side ^= 1u;
    nec_tx_frame(dock_beacon_frame(s_side ? DOCK_BEACON_ADDR_RIGHT : DOCK_BEACON_ADDR_LEFT,
                                   s_budget_code),
                 s_seg);
    s_seg_i = 0u;
  }
  if (s_seg_i < NEC_TX_SEGMENTS) {
    carrier(s_side, (s_seg_i & 1u) == 0u);
    TIM2->ATRLR = (uint16_t)(s_seg[s_seg_i] - 1u);
    ++s_seg_i;
  } else {
    carrier(s_side, false);
    TIM2->ATRLR = (uint16_t)(BEACON_GAP_US - 1u);
    s_in_gap = true;
    ++s_frames_sent;
  }
}

static void beacon_start(void) {
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

  TIM_TimeBaseInitTypeDef tb = {0};
  tb.TIM_Period = CARRIER_PERIOD - 1u;
  tb.TIM_Prescaler = 0;
  tb.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM1, &tb);
  TIM_OCInitTypeDef oc = {0};
  oc.TIM_OCMode = TIM_OCMode_PWM1;
  oc.TIM_OutputState = TIM_OutputState_Enable;
  oc.TIM_OutputNState = TIM_OutputNState_Disable;
  oc.TIM_Pulse = CARRIER_PERIOD / 3u;
  oc.TIM_OCPolarity = TIM_OCPolarity_High;
  oc.TIM_OCIdleState = TIM_OCIdleState_Reset;
  TIM_OC3Init(TIM1, &oc);
  TIM_OC4Init(TIM1, &oc);
  /* CH1 (PD2) and CH2 (PA1) are analog inputs here: never enabled. */
  carrier(0, false);
  carrier(1, false);
  TIM_CtrlPWMOutputs(TIM1, ENABLE);
  TIM_Cmd(TIM1, ENABLE);
  pin_mode(P_IR_L, PM_AF_PP);
  pin_mode(P_IR_R, PM_AF_PP);

  tb.TIM_Period = BEACON_GAP_US - 1u;
  tb.TIM_Prescaler = (uint16_t)(HCLK_HZ / 1000000u - 1u);
  TIM_TimeBaseInit(TIM2, &tb);
  TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
  s_in_gap = true;
  s_side = 1u; /* the first frame goes out on the left */
  TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
  NVIC_SetPriority(TIM2_IRQn, 0x40);
  NVIC_EnableIRQ(TIM2_IRQn);
  TIM_Cmd(TIM2, ENABLE);
}

/* ---------------------------------------------------------------- LED */

static void led_update(uint8_t pattern, uint32_t ms) {
  bool on;
  switch (pattern) {
    case DOCK_LED_ON: on = true; break;
    case DOCK_LED_BLIP: on = (ms % 2000u) < 60u; break;
    case DOCK_LED_DOUBLE_BLINK: {
      const uint32_t t = ms % 2000u;
      on = t < 150u || (t >= 300u && t < 450u);
      break;
    }
    case DOCK_LED_FAST: on = (ms % 200u) < 100u; break;
    default: on = false; break;
  }
  pin_write(P_LED, on);
}

/* ---------------------------------------------------------------- console */

#define CON_BAUD 115200u
static uart_t s_con;
static dock_sm_t s_sm;
static dock_in_t s_in;
static dock_out_t s_out;
static int32_t s_i_zero_ma;

void USART1_IRQHandler(void) ISR;
void USART1_IRQHandler(void) { uart_irq(&s_con); }

static void con_write(const char *s, uint32_t len) {
  while (len != 0u) {
    const uint32_t n = uart_write(&s_con, s, len);
    s += n;
    len -= n;
  }
}

static void cmd_status(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cli_printf("state %s, fault %u (x%u), vin %lu mV, vdock %lu mV, probe %lu mV, vdd %lu mV\n",
             dock_state_name(s_sm.state), s_sm.fault, s_sm.consecutive_faults,
             (unsigned long)s_in.vin_mv, (unsigned long)s_in.vdock_mv,
             (unsigned long)s_in.vprobe_mv, (unsigned long)s_in.vdd_mv);
  cli_printf("current %ld mA (zero %ld), contacts %s, R %lu ohm, budget %lu W, beacon frames %lu\n",
             (long)s_in.i_ma, (long)s_i_zero_ma, s_out.charge_en ? "ON" : "off",
             (unsigned long)(s_sm.r_ohm == UINT32_MAX ? 0u : s_sm.r_ohm),
             (unsigned long)dock_beacon_budget_w(s_out.budget_code), (unsigned long)s_frames_sent);
}

static void cmd_pd(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cli_printf("CH224A: %s, status 0x%02x (PD %s), limit %u mA, errors %lu\n",
             s_pd.ok ? "ok" : "no answer", s_pd.status, (s_pd.status & 0x08u) ? "yes" : "no",
             s_pd.limit * 50u, (unsigned long)s_pd.errors);
}

static const cli_cmd_t k_cmds[] = {
    {"status", "state, voltages, current, budget", cmd_status},
    {"pd", "CH224A status and current limit", cmd_pd},
};

/* ---------------------------------------------------------------- main */

static void pins_init(void) {
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC |
                             RCC_APB2Periph_GPIOD,
                         ENABLE);
  pin_output(P_CHARGE_EN, false, PM_OUT_PP); /* contacts off, first */
  pin_output(P_PROBE_DRV, false, PM_OUT_PP);
  pin_output(P_LED, false, PM_OUT_PP);
  pin_output(P_IR_L, false, PM_OUT_PP);
  pin_output(P_IR_R, false, PM_OUT_PP);
  pin_mode((pin_t){GPIOA, 2}, PM_ANALOG);
  pin_mode((pin_t){GPIOA, 1}, PM_ANALOG);
  pin_mode((pin_t){GPIOD, 2}, PM_ANALOG);
  pin_mode((pin_t){GPIOD, 3}, PM_ANALOG);
  /* spares on J1005: PC7, PD0, PD4 pulled down */
  pin_input_pulldown((pin_t){GPIOC, 7});
  pin_input_pulldown((pin_t){GPIOD, 0});
  pin_input_pulldown((pin_t){GPIOD, 4});
}

static void iwdg_start(void) {
  DBGMCU_Config(DBGMCU_IWDG_STOP, ENABLE);
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_128); /* LSI 128 kHz -> 1 kHz */
  IWDG_SetReload(500u);                  /* 500 ms */
  IWDG_ReloadCounter();
  IWDG_Enable();
}

static void tick(void) {
  const uint32_t now = systime_ms();
  adc_filter();
  s_in.vdd_mv = vdd_mv();
  s_in.vin_mv = pin_mv(A_VIN) * 11u;
  s_in.vdock_mv = pin_mv(A_DOCK) * 11u;
  s_in.vprobe_mv = pin_mv(A_PROBE);
  const int32_t i_raw = (int32_t)pin_mv(A_ISENSE); /* 1 mV = 1 mA */
  if (!s_out.charge_en) s_i_zero_ma += (i_raw - s_i_zero_ma) / 16; /* INA180 offset, contacts off */
  s_in.i_ma = i_raw - s_i_zero_ma;
  s_in.pd_contract = s_pd.ok && (s_pd.status & 0x08u) != 0u;
  s_in.pd_limit_ma = (uint16_t)(s_pd.ok ? s_pd.limit * 50u : 0u);
  s_in.trip = s_trip;

  const uint8_t before = s_sm.state;
  dock_sm_step(&s_sm, 1u, &s_in, &s_out);
  if (s_out.charge_en && !s_trip) {
    pin_high(P_CHARGE_EN);
  } else {
    pin_low(P_CHARGE_EN);
  }
  pin_write(P_PROBE_DRV, s_out.probe_drv);
  s_budget_code = s_out.budget_code;
  led_update(s_out.led, now);

  if (s_sm.state != before) {
    if (s_sm.state == DOCK_CONNECTING) {
      /* re-arm the hardware trip for this connection */
      s_trip = false;
      ADC_ClearITPendingBit(ADC1, ADC_IT_AWD);
      ADC_ITConfig(ADC1, ADC_IT_AWD, ENABLE);
    }
    cli_printf("\n[%lu ms] %s -> %s", (unsigned long)now, dock_state_name(before),
               dock_state_name(s_sm.state));
    if (s_sm.state == DOCK_CONNECTING) cli_printf(" (R %lu ohm)", (unsigned long)s_sm.r_ohm);
    if (s_sm.state == DOCK_FAULT) cli_printf(" (fault %u, %ld mA)", s_sm.fault, (long)s_in.i_ma);
    cli_printf("\n");
  }
  if (now % 250u == 0u) pd_poll();
}

int main(void) {
  pins_init();
  systime_init(HCLK_HZ);
  iwdg_start();
  dock_sm_init(&s_sm);

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
  pin_output(P_TX, true, PM_AF_PP);
  pin_input_pullup(P_RX);
  uart_init(&s_con, USART1, HCLK_HZ, CON_BAUD, USART1_IRQn, 0x80);

  adc_start();
  i2c_start();
  beacon_start();
  cli_init(con_write, k_cmds, sizeof(k_cmds) / sizeof(k_cmds[0]), "dock> ");
  cli_printf("OOMWOO dock firmware %d\n", FW_VERSION);

  for (;;) {
    IWDG_ReloadCounter();
    int c;
    while ((c = uart_getc(&s_con)) >= 0) cli_feed((char)c);
    while (systime_tick_pending()) tick();
  }
}
