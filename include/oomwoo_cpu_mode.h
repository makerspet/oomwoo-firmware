#ifndef OOMWOO_CPU_MODE_H
#define OOMWOO_CPU_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Values are part of the CPU/MCU HEARTBEAT payload contract. */
typedef enum {
  OOMWOO_CPU_MODE_DISARMED = 0,
  OOMWOO_CPU_MODE_STACK_HEALTHY = 1
} oomwoo_cpu_mode_t;

#ifdef __cplusplus
}
#endif

#endif
