#ifndef OOMWOO_CPU_INGRESS_H
#define OOMWOO_CPU_INGRESS_H

#include "oomwoo_messages.h"
#include "oomwoo_protocol.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  OOMWOO_CPU_INGRESS_OK = 0,
  OOMWOO_CPU_INGRESS_NULL_ARGUMENT,
  OOMWOO_CPU_INGRESS_BAD_VERSION,
  OOMWOO_CPU_INGRESS_WRONG_DIRECTION,
  OOMWOO_CPU_INGRESS_UNKNOWN_TYPE,
  OOMWOO_CPU_INGRESS_PAYLOAD_UNDEFINED,
  OOMWOO_CPU_INGRESS_WRONG_PAYLOAD_LENGTH,
  OOMWOO_CPU_INGRESS_VALUE_OUT_OF_RANGE,
  OOMWOO_CPU_INGRESS_CODEC_ERROR
} oomwoo_cpu_ingress_result_t;

typedef struct {
  uint32_t accepted_messages;
  uint32_t wrong_direction;
  uint32_t unknown_type;
  uint32_t undefined_payload;
  uint32_t wrong_payload_length;
  uint32_t value_out_of_range;
  uint32_t codec_errors;
} oomwoo_cpu_ingress_stats_t;

typedef void (*oomwoo_cpu_message_callback_t)(
    const oomwoo_decoded_frame_t *frame, const oomwoo_message_t *message,
    void *context);

typedef struct {
  oomwoo_stream_decoder_t decoder;
  oomwoo_cpu_ingress_stats_t stats;
  oomwoo_cpu_message_callback_t callback;
  void *callback_context;
} oomwoo_cpu_ingress_t;

void oomwoo_cpu_ingress_init(oomwoo_cpu_ingress_t *ingress,
                             oomwoo_cpu_message_callback_t callback,
                             void *callback_context);

oomwoo_cpu_ingress_result_t oomwoo_cpu_ingress_validate_frame(
    const oomwoo_decoded_frame_t *frame, oomwoo_message_t *output);

size_t oomwoo_cpu_ingress_feed(oomwoo_cpu_ingress_t *ingress,
                               const uint8_t *data, size_t length);

void oomwoo_cpu_ingress_reset_incomplete(oomwoo_cpu_ingress_t *ingress);

#ifdef __cplusplus
}
#endif

#endif
