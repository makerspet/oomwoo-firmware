/* U13 (CH32V307VCT6) starter firmware: board bring-up console, rails, safety
 * gate, RC watchdog, link to U28, motors and sensors. See docs/firmware-plan.md.
 *
 * Main loop, paced by the 1 kHz SysTick:
 *   every 1 ms   link RX, sensors, motion gate -> motor outputs, WDI toggle
 *   every 10 ms  CONTROL frame to U28, power-off handshake
 *   always       console RX, LiDAR RX, IWDG feed
 * Real-time pieces that must not wait for the loop run in interrupts: cliff
 * LED off (TIM7), encoders and IR edges (EXTI), fan tachometer (TIM3). */
#include <string.h>

#include "board.h"
#include "camera.h"
#include "carpet.h"
#include "cli.h"
#include "drive_gate.h"
#include "i2c.h"
#include "lidar.h"
#include "link.h"
#include "motors.h"
#include "sensors.h"
#include "systime.h"
#include "uart.h"

#define CONSOLE_BAUD 115200u
#define CMD_MAX_MS 5000u

/* ---------------------------------------------------------------- state */

static uart_t s_console;
static bool s_sensors_up;  /* VSENS-3V3 + VSENS-5V-USB on and peripherals live */
static bool s_armed;
static bool s_rails_off_ack;
static bool s_reset_iwdg;
static gate_out_t s_gate;
static uint8_t s_ctl_seq;
static uint8_t s_torch_pct;
static uint8_t s_ctl_flags;

static struct {
  int16_t wheel[2];
  int16_t brush[2];
  uint16_t fan;
  uint32_t until_ms;
} s_cmd;

/* ---------------------------------------------------------------- console */

#ifdef BOARD_EVT
/* WCH CH32V307V-R1 evaluation board: the on-board WCH-Link's serial port is
 * wired to USART1 (PA9 TX, PA10 RX), so the console needs no extra wires. */
void USART1_IRQHandler(void) ISR;
void USART1_IRQHandler(void) { uart_irq(&s_console); }
#else
void UART7_IRQHandler(void) ISR;
void UART7_IRQHandler(void) { uart_irq(&s_console); }
#endif

static void console_write(const char *s, uint32_t len) {
  /* Bench console: wait for room rather than drop text (bounded by the ring). */
  while (len != 0u) {
    const uint32_t n = uart_write(&s_console, s, len);
    s += n;
    len -= n;
    if (len != 0u) iwdg_feed();
  }
}

static void console_init(void) {
#ifdef BOARD_EVT
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
  pin_output(PIN(PIN_CAM_Y2), true, PM_AF_PP); /* PA9 USART1 TX */
  pin_input_pullup(PIN(PIN_CAM_Y3));            /* PA10 USART1 RX */
  uart_init(&s_console, USART1, PCLK2_HZ, CONSOLE_BAUD, USART1_IRQn, 0xA0);
#else
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART7, ENABLE);
  pin_output(PIN(PIN_MCU_TX), true, PM_AF_PP); /* PE12 UART7 TX (J4 pin 5) */
  pin_input_pullup(PIN(PIN_MCU_RX));            /* PE13 UART7 RX (J4 pin 4) */
  uart_init(&s_console, UART7, PCLK1_HZ, CONSOLE_BAUD, UART7_IRQn, 0xA0);
#endif
}

/* ---------------------------------------------------------------- rails */

static void sensors_up(bool on) {
  if (on == s_sensors_up) return;
  if (on) {
    rail_set(RAIL_SENS_3V3, true);
    systime_delay_us(10000u); /* ~6 ms to come up; then the 5 V buck */
    rail_set(RAIL_SENS_5V, true);
    systime_delay_us(10000u);
    i2c_init();
    sensors_enable(true);
    s_ctl_flags |= MCU_LINK_CTL_SENSORS_ON;
  } else {
    s_ctl_flags &= (uint8_t)~(MCU_LINK_CTL_SENSORS_ON | MCU_LINK_CTL_CAM_RUN | MCU_LINK_CTL_SIDE_TOF_ON);
    s_torch_pct = 0u;
    sensors_enable(false);
    i2c_release();
    camera_clock(false);
    camera_power_down(false);
    rail_set(RAIL_SENS_5V, false);
    systime_delay_us(10000u);
    rail_set(RAIL_SENS_3V3, false);
  }
  s_sensors_up = on;
}

static void arm(bool on) {
  if (on) {
    if (!s_sensors_up) sensors_up(true);
    rail_set(RAIL_MOTOR, true); /* hardware ANDs it with WD_OK from the RC watchdog */
    lidar_uart_enable(true);
    s_armed = true;
  } else {
    s_armed = false;
    memset(&s_cmd, 0, sizeof(s_cmd));
    motors_all_off();
    lidar_uart_enable(false);
    rail_set(RAIL_MOTOR, false);
  }
}

/* ---------------------------------------------------------------- control */

static void control_tick(void) {
  const uint32_t now = systime_ms();
  const mcu_link_status_t *st = link_status();
  const bool alive = link_alive();
  const bool off_req = alive && st->power == MCU_LINK_PWR_OFF_REQUEST;

  sensors_tick_1ms();

  gate_inputs_t in = {0};
  in.armed = s_armed;
  in.motor_rail_on = rail_on(RAIL_MOTOR);
  in.u28_alive = alive;
  in.power_off_requested = off_req;
  in.cmd_valid = (int32_t)(s_cmd.until_ms - now) > 0;
  in.wheel_drop = g_sens.drop[0] || g_sens.drop[1];
  in.cliff_front = (g_sens.cliff_mask & 0x0Fu) != 0u;
  in.cliff_rear = (g_sens.cliff_mask & 0x30u) != 0u;
  in.bumper = g_sens.bumper[0] || g_sens.bumper[1];
  in.overcurrent = false; /* TODO: per-motor thresholds from measured currents */
  s_gate = drive_gate_eval(&in);

  if (s_gate.reasons & (GATE_WHEEL_DROP | GATE_CLIFF_FRONT | GATE_CLIFF_REAR | GATE_BUMPER)) {
    /* Hazard: stop the wheels hard; brake shorts the motor through the low sides. */
    for (int i = 0; i < 2; ++i) {
      const int16_t w = drive_gate_wheel(&s_gate, s_cmd.wheel[i]);
      if (w == 0) {
        motor_brake((motor_t)(MOTOR_WHEEL_L + i));
      } else {
        motor_set((motor_t)(MOTOR_WHEEL_L + i), w);
      }
    }
  } else {
    motor_set(MOTOR_WHEEL_L, drive_gate_wheel(&s_gate, s_cmd.wheel[0]));
    motor_set(MOTOR_WHEEL_R, drive_gate_wheel(&s_gate, s_cmd.wheel[1]));
  }
  motor_set(MOTOR_MAIN_BRUSH, s_gate.cleaning_motors ? s_cmd.brush[0] : 0);
  motor_set(MOTOR_SIDE_BRUSH, s_gate.cleaning_motors ? s_cmd.brush[1] : 0);
  fan_set(s_gate.cleaning_motors ? s_cmd.fan : 0u);

  if (off_req && !s_rails_off_ack) {
    /* Power button held 3 s on U28: everything off, then ack so U28 drops the latch. */
    arm(false);
    sensors_up(false);
    s_rails_off_ack = true;
    cli_printf("\npower off requested by U28: rails off\n");
  } else if (alive && st->power == MCU_LINK_PWR_RUN) {
    s_rails_off_ack = false;
  }

  if (now % 10u == 0u) {
    mcu_link_control_t c = {0};
    c.flags = s_ctl_flags | (s_rails_off_ack ? MCU_LINK_CTL_RAILS_OFF : 0u);
    c.torch_pct = s_sensors_up ? s_torch_pct : 0u;
    c.led_home = MCU_LINK_LED_AUTO;
    c.led_power = MCU_LINK_LED_AUTO;
    c.usb_mode = MCU_LINK_USB_SINK_DATA;
    c.seq = s_ctl_seq++;
    link_send(&c);
  }
}

/* ---------------------------------------------------------------- commands */

static bool arg_int(int argc, char **argv, int i, long lo, long hi, long *v) {
  if (i >= argc || !cli_int(argv[i], v) || *v < lo || *v > hi) {
    cli_printf("bad argument %d (range %ld..%ld)\n", i, lo, hi);
    return false;
  }
  return true;
}

static void cmd_window(int argc, char **argv, int i) {
  long ms = 500;
  if (i < argc && !arg_int(argc, argv, i, 1, CMD_MAX_MS, &ms)) ms = 500;
  s_cmd.until_ms = systime_ms() + (uint32_t)ms;
}

static void print_gate(void) {
  static const char *const names[] = {"not_armed", "rail_off", "u28_lost", "cmd_expired",
                                      "wheel_drop", "cliff_front", "cliff_rear", "bumper",
                                      "overcurrent", "power_off"};
  cli_printf("gate:");
  if (s_gate.reasons == 0u) cli_printf(" clear");
  for (unsigned b = 0; b < sizeof(names) / sizeof(names[0]); ++b) {
    if (s_gate.reasons & (1u << b)) cli_printf(" %s", names[b]);
  }
  cli_printf("\n");
}

static const char *power_name(uint8_t p) {
  static const char *const n[] = {"boot", "run", "off_request", "off"};
  return p < 4u ? n[p] : "?";
}

static const char *charge_name(uint8_t c) {
  static const char *const n[] = {"off_dock", "charging", "done", "fault", "no_battery"};
  return c < 5u ? n[c] : "?";
}

static void cmd_status(int argc, char **argv) {
  cli_printf("u13 fw %d, up %lu ms, clock %s, last reset %s\n", FW_VERSION,
             (unsigned long)systime_ms(), SystemClockFromHse ? "HSE 8 MHz" : "HSI (crystal failed!)",
             s_reset_iwdg ? "IWDG" : "power/pin");
  cli_printf("rails: sens3v3 %d sens5v %d motor %d, armed %d, die %ld.%ld C\n", rail_on(RAIL_SENS_3V3),
             rail_on(RAIL_SENS_5V), rail_on(RAIL_MOTOR), s_armed, (long)(mcu_temp_c10() / 10),
             (long)(mcu_temp_c10() % 10));
  print_gate();
  const mcu_link_status_t *s = link_status();
  if (!link_alive()) {
    cli_printf("u28: no link\n");
    return;
  }
  cli_printf("u28: fw %d up %lu ms power %s, bat %u mV, dc_in %u mV, vbus %u mV, charge %s, usb %u\n",
             s->fw_version, (unsigned long)s->uptime_ms, power_name(s->power), s->vbat_mv,
             s->dc_in_mv, s->vbus_mv, charge_name(s->charge), s->usb);
  cli_printf("u28: buttons 0x%02x flags 0x%02x ntc %u id %u ldr %u floor %u/%u temp %d.%d C\n",
             s->buttons, s->flags, s->bat_ntc_raw, s->bat_id_raw, s->ldr_raw, s->hall_floor_l_raw,
             s->hall_floor_r_raw, s->mcu_temp_c10 / 10, s->mcu_temp_c10 % 10);
}

static void cmd_rails(int argc, char **argv) {
  bool on;
  if (argc < 2 || !cli_on_off(argv[1], &on)) {
    cli_printf("usage: rails on|off   (sensor 3.3 V, then 5 V)\n");
    return;
  }
  if (!on) arm(false);
  sensors_up(on);
}

static void cmd_arm(int argc, char **argv) { arm(true); }
static void cmd_disarm(int argc, char **argv) { arm(false); }

static void cmd_drive(int argc, char **argv) {
  long l, r;
  if (!arg_int(argc, argv, 1, -1000, 1000, &l) || !arg_int(argc, argv, 2, -1000, 1000, &r)) {
    cli_printf("usage: drive <left> <right> [ms]   (-1000..1000)\n");
    return;
  }
  s_cmd.wheel[0] = (int16_t)l;
  s_cmd.wheel[1] = (int16_t)r;
  cmd_window(argc, argv, 3);
}

static void cmd_brush(int argc, char **argv) {
  long m, s;
  if (!arg_int(argc, argv, 1, -1000, 1000, &m) || !arg_int(argc, argv, 2, -1000, 1000, &s)) {
    cli_printf("usage: brush <main> <side> [ms]\n");
    return;
  }
  s_cmd.brush[0] = (int16_t)m;
  s_cmd.brush[1] = (int16_t)s;
  cmd_window(argc, argv, 3);
}

static void cmd_fan(int argc, char **argv) {
  long f;
  if (!arg_int(argc, argv, 1, 0, 1000, &f)) {
    cli_printf("usage: fan <0..1000> [ms]   tach %lu.%lu Hz\n",
               (unsigned long)(fan_tach_hz_x10() / 10), (unsigned long)(fan_tach_hz_x10() % 10));
    return;
  }
  s_cmd.fan = (uint16_t)f;
  cmd_window(argc, argv, 2);
}

static void cmd_lidar(int argc, char **argv) {
  long p;
  uint32_t bytes, packets, ovr;
  lidar_stats(&bytes, &packets, &ovr);
  if (argc < 2) {
    cli_printf("lidar: motor %u, rx %lu bytes, %lu packets, %lu overruns\n", lidar_motor_get(),
               (unsigned long)bytes, (unsigned long)packets, (unsigned long)ovr);
    return;
  }
  if (!arg_int(argc, argv, 1, 0, 1000, &p)) return;
  if (!s_armed) {
    cli_printf("arm first (the LiDAR is on the motor rail)\n");
    return;
  }
  lidar_motor_set((uint16_t)p);
}

static void cmd_adc(int argc, char **argv) {
  for (int i = 0; i < ADC_COUNT; ++i) {
    cli_printf("  %-10s %4u raw %5lu mV\n", adc_name((adc_ch_t)i), adc_raw((adc_ch_t)i),
               (unsigned long)adc_mv((adc_ch_t)i));
  }
}

static void cmd_cliff(int argc, char **argv) {
  long t;
  if (argc >= 3 && strcmp(argv[1], "thr") == 0 && arg_int(argc, argv, 2, -4095, 4095, &t)) {
    cliff_set_threshold((int16_t)t);
  }
  cli_printf("cliff reflect (dark - lit, counts): LF %d RF %d LM %d RM %d LR %d RR %d  mask 0x%02x  n %lu\n",
             g_sens.cliff_reflect[0], g_sens.cliff_reflect[1], g_sens.cliff_reflect[2],
             g_sens.cliff_reflect[3], g_sens.cliff_reflect[4], g_sens.cliff_reflect[5],
             g_sens.cliff_mask, (unsigned long)g_sens.cliff_samples);
}

static void cmd_bump(int argc, char **argv) {
  long d;
  if (argc >= 2 && arg_int(argc, argv, 1, 0, 1000, &d)) bumper_led_duty((uint16_t)d);
  cli_printf("bumper L %u mV (%d) R %u mV (%d); drop L %d R %d\n", g_sens.bump_raw[0],
             g_sens.bumper[0], g_sens.bump_raw[1], g_sens.bumper[1], g_sens.drop[0], g_sens.drop[1]);
}

static void cmd_enc(int argc, char **argv) {
  cli_printf("encoder L %lu (period %lu us)  R %lu (period %lu us)\n",
             (unsigned long)g_sens.enc_count[0], (unsigned long)g_sens.enc_period_us[0],
             (unsigned long)g_sens.enc_count[1], (unsigned long)g_sens.enc_period_us[1]);
}

static void cmd_ir(int argc, char **argv) {
  static const char *const names[IR_COUNT] = {"front_l", "front_r", "side_r", "rear_l", "rear_r"};
  for (int i = 0; i < IR_COUNT; ++i) {
    uint16_t a = 0;
    uint8_t c = 0;
    const bool ok = nec_frame_valid(g_ir[i].frame, &a, &c);
    cli_printf("  %-8s frames %lu repeats %lu errors %lu last 0x%08lx %s addr 0x%04x cmd 0x%02x age %lu ms\n",
               names[i], (unsigned long)g_ir[i].frames, (unsigned long)g_ir[i].repeats,
               (unsigned long)g_ir[i].errors, (unsigned long)g_ir[i].frame, ok ? "ok" : "--", a, c,
               g_ir[i].frames ? (unsigned long)((systime_us() - g_ir[i].frame_us) / 1000u) : 0ul);
  }
}

static void cmd_i2c(int argc, char **argv) {
  if (!s_sensors_up) {
    cli_printf("rails on first\n");
    return;
  }
  const i2c_bus_t bus = (argc >= 2 && strcmp(argv[1], "cam") == 0) ? I2C_CAM : I2C_MAIN;
  cli_printf("%s bus:", bus == I2C_CAM ? "cam" : "main");
  for (uint8_t a = 0x08; a < 0x78; ++a) {
    if (i2c_probe(bus, a)) cli_printf(" 0x%02x", a);
  }
  cli_printf("\n");
}

static void cmd_imu(int argc, char **argv) {
  uint8_t who = 0;
  if (!s_sensors_up || !i2c_read_reg8(I2C_MAIN, 0x6A, 0x0F, &who)) {
    cli_printf("imu: no answer at 0x6a\n");
    return;
  }
  cli_printf("imu: WHO_AM_I 0x%02x (%s), INT1 edges %lu\n", who,
             who == 0x6Au ? "LSM6DS3TR-C" : "unexpected", (unsigned long)g_sens.imu_int_count);
}

static void cmd_cam(int argc, char **argv) {
  if (!s_sensors_up) {
    cli_printf("rails on first\n");
    return;
  }
  if (argc >= 2 && strcmp(argv[1], "off") == 0) {
    s_ctl_flags &= (uint8_t)~MCU_LINK_CTL_CAM_RUN;
    camera_clock(false);
    return;
  }
  /* XCLK first, then RESETB released by U28 (next CONTROL frame), then SCCB. */
  camera_power_down(false);
  camera_clock(true);
  s_ctl_flags |= MCU_LINK_CTL_CAM_RUN;
  const uint32_t t0 = systime_ms();
  while (systime_ms() - t0 < 30u) {
    if (systime_tick_pending()) control_tick();
    iwdg_feed();
  }
  uint16_t id = 0;
  const char *name = camera_identify(&id);
  cli_printf("camera: %s (id 0x%04x), XCLK 24 MHz on PA8\n", name ? name : "not found", id);
}

static void cmd_carpet(int argc, char **argv) {
#ifdef BOARD_EVT
  cli_printf("carpet: disabled on the EVT board (PC6 drives its Ethernet magnetics)\n");
  return;
#else
  long n = 8;
  if (!s_sensors_up) {
    /* VUSONIC-14V4 is an MT3608 boost (U5) enabled by VSENS-5V-USB itself. */
    cli_printf("rails on first (the carpet boost runs from VSENS-5V-USB)\n");
    return;
  }
  if (argc >= 2 && !arg_int(argc, argv, 1, 1, 200, &n)) return;
  carpet_echo_t e;
  carpet_ping((uint8_t)n, &e);
  cli_printf("carpet: %ld cycles, idle %u mV, peak %u mV at %u us, %u samples\n", n, e.idle_mv,
             e.peak_mv, e.peak_us, e.samples);
#endif
}

static void cmd_torch(int argc, char **argv) {
  long p;
  if (!arg_int(argc, argv, 1, 0, 100, &p)) {
    cli_printf("usage: torch <0..100>  (U28 PB8 PWM, needs rails on)\n");
    return;
  }
  s_torch_pct = (uint8_t)p;
}

static void cmd_u28(int argc, char **argv) {
  uint32_t frames, crc, ovr;
  if (argc >= 2 && strcmp(argv[1], "reset") == 0) {
    u28_reset_pulse(false);
  } else if (argc >= 2 && strcmp(argv[1], "boot") == 0) {
    u28_reset_pulse(true);
    cli_printf("U28 in its USART2 bootloader (BOOT0 high); 'u28 reset' to leave\n");
  }
  link_stats(&frames, &crc, &ovr);
  cli_printf("link: alive %d, frames %lu, crc errors %lu, overruns %lu\n", link_alive(),
             (unsigned long)frames, (unsigned long)crc, (unsigned long)ovr);
}

static void cmd_side(int argc, char **argv) {
  bool on;
  if (argc < 2 || !cli_on_off(argv[1], &on)) {
    cli_printf("usage: side on|off   (VL53L0X XSHUT via U28)\n");
    return;
  }
  if (on) {
    s_ctl_flags |= MCU_LINK_CTL_SIDE_TOF_ON;
  } else {
    s_ctl_flags &= (uint8_t)~MCU_LINK_CTL_SIDE_TOF_ON;
  }
}

static void cmd_reboot(int argc, char **argv) {
  arm(false);
  sensors_up(false);
  NVIC_SystemReset();
}

static const cli_cmd_t k_cmds[] = {
    {"status", "clocks, rails, gate, U28 status", cmd_status},
    {"rails", "on|off: sensor 3.3 V and 5 V rails", cmd_rails},
    {"arm", "motor rail + LiDAR on (needs WD_OK)", cmd_arm},
    {"disarm", "motors off, motor rail off", cmd_disarm},
    {"drive", "<l> <r> [ms]: wheels -1000..1000", cmd_drive},
    {"brush", "<main> <side> [ms]", cmd_brush},
    {"fan", "<0..1000> [ms]: suction fan", cmd_fan},
    {"lidar", "[0..1000]: LiDAR motor, RX stats", cmd_lidar},
    {"adc", "all ADC channels", cmd_adc},
    {"cliff", "[thr N]: reflectance and mask", cmd_cliff},
    {"bump", "[duty]: bumpers, emitter duty, drops", cmd_bump},
    {"enc", "wheel encoder counts", cmd_enc},
    {"ir", "NEC frames per receiver", cmd_ir},
    {"i2c", "main|cam: scan a bus", cmd_i2c},
    {"imu", "LSM6DS3TR-C WHO_AM_I", cmd_imu},
    {"cam", "[off]: XCLK, reset, sensor ID", cmd_cam},
    {"carpet", "[cycles]: ultrasonic ping", cmd_carpet},
    {"torch", "<0..100>: front torch LEDs", cmd_torch},
    {"side", "on|off: side VL53L0X", cmd_side},
    {"u28", "[reset|boot]: companion MCU link", cmd_u28},
    {"reboot", "rails off, reset", cmd_reboot},
};

/* ---------------------------------------------------------------- main */

int main(void) {
  board_safe_init();  /* outputs to safe levels before anything else */
  board_remap();
  s_reset_iwdg = board_reset_was_iwdg();
  systime_init(HCLK_HZ);
  iwdg_start();

  console_init();
  link_init();
  adc_start();
  motors_init();
  sensors_init();
#ifndef BOARD_EVT
  carpet_init();
#endif

  cli_init(console_write, k_cmds, sizeof(k_cmds) / sizeof(k_cmds[0]), "u13> ");
  cli_printf("OOMWOO U13 (CH32V307) starter firmware %d, %s, reset: %s\n", FW_VERSION,
             SystemClockFromHse ? "HSE ok" : "HSE FAILED (HSI)", s_reset_iwdg ? "IWDG" : "power/pin");

  uint32_t last_tick_ms = systime_ms();
  for (;;) {
    iwdg_feed();
    int c;
    while ((c = uart_getc(&s_console)) >= 0) cli_feed((char)c);
    lidar_poll();
    while (systime_tick_pending()) {
      link_poll();
      control_tick();
      /* The RC watchdog proves the control loop runs: toggle only if this
       * tick came on time (a stalled loop lets WD_OK fall and cuts the rails). */
      const uint32_t now = systime_ms();
      if (now - last_tick_ms <= 2u) wdi_toggle();
      last_tick_ms = now;
    }
  }
}
