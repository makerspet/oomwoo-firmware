#include "oomwoo_cpu_ingress.h"

#include <string.h>

typedef struct {
  oomwoo_cpu_ingress_t *ingress;
  size_t accepted;
} feed_context_t;

static oomwoo_cpu_ingress_result_t map_message_result(
    oomwoo_message_result_t result) {
  switch (result) {
    case OOMWOO_MESSAGE_OK:
      return OOMWOO_CPU_INGRESS_OK;
    case OOMWOO_MESSAGE_NULL_ARGUMENT:
      return OOMWOO_CPU_INGRESS_NULL_ARGUMENT;
    case OOMWOO_MESSAGE_UNKNOWN_TYPE:
      return OOMWOO_CPU_INGRESS_UNKNOWN_TYPE;
    case OOMWOO_MESSAGE_PAYLOAD_UNDEFINED:
      return OOMWOO_CPU_INGRESS_PAYLOAD_UNDEFINED;
    case OOMWOO_MESSAGE_WRONG_PAYLOAD_LENGTH:
      return OOMWOO_CPU_INGRESS_WRONG_PAYLOAD_LENGTH;
    case OOMWOO_MESSAGE_VALUE_OUT_OF_RANGE:
      return OOMWOO_CPU_INGRESS_VALUE_OUT_OF_RANGE;
    case OOMWOO_MESSAGE_OUTPUT_TOO_SMALL:
    default:
      return OOMWOO_CPU_INGRESS_CODEC_ERROR;
  }
}

oomwoo_cpu_ingress_result_t oomwoo_cpu_ingress_validate_frame(
    const oomwoo_decoded_frame_t *frame, oomwoo_message_t *output) {
  oomwoo_message_direction_t direction;

  if (output == NULL) {
    return OOMWOO_CPU_INGRESS_NULL_ARGUMENT;
  }
  memset(output, 0, sizeof(*output));
  if (frame == NULL ||
      (frame->payload == NULL && frame->payload_length != UINT16_C(0))) {
    return OOMWOO_CPU_INGRESS_NULL_ARGUMENT;
  }
  if (frame->version != OOMWOO_PROTOCOL_VERSION) {
    return OOMWOO_CPU_INGRESS_BAD_VERSION;
  }

  direction = oomwoo_message_direction(frame->message_type);
  if (direction == OOMWOO_MESSAGE_DIRECTION_UNKNOWN) {
    return OOMWOO_CPU_INGRESS_UNKNOWN_TYPE;
  }
  if (direction != OOMWOO_MESSAGE_DIRECTION_CPU_TO_MCU &&
      direction != OOMWOO_MESSAGE_DIRECTION_BOTH) {
    return OOMWOO_CPU_INGRESS_WRONG_DIRECTION;
  }

  return map_message_result(oomwoo_message_decode_payload(
      frame->message_type, frame->payload, frame->payload_length, output));
}

static void count_rejection(oomwoo_cpu_ingress_t *ingress,
                            oomwoo_cpu_ingress_result_t result) {
  switch (result) {
    case OOMWOO_CPU_INGRESS_WRONG_DIRECTION:
      ++ingress->stats.wrong_direction;
      break;
    case OOMWOO_CPU_INGRESS_UNKNOWN_TYPE:
      ++ingress->stats.unknown_type;
      break;
    case OOMWOO_CPU_INGRESS_PAYLOAD_UNDEFINED:
      ++ingress->stats.undefined_payload;
      break;
    case OOMWOO_CPU_INGRESS_WRONG_PAYLOAD_LENGTH:
      ++ingress->stats.wrong_payload_length;
      break;
    case OOMWOO_CPU_INGRESS_VALUE_OUT_OF_RANGE:
      ++ingress->stats.value_out_of_range;
      break;
    case OOMWOO_CPU_INGRESS_CODEC_ERROR:
      ++ingress->stats.codec_errors;
      break;
    default:
      break;
  }
}

static void handle_frame(const oomwoo_decoded_frame_t *frame, void *context) {
  feed_context_t *feed = (feed_context_t *)context;
  oomwoo_message_t message;
  const oomwoo_cpu_ingress_result_t result =
      oomwoo_cpu_ingress_validate_frame(frame, &message);

  if (result != OOMWOO_CPU_INGRESS_OK) {
    count_rejection(feed->ingress, result);
    return;
  }

  ++feed->ingress->stats.accepted_messages;
  ++feed->accepted;
  if (feed->ingress->callback != NULL) {
    feed->ingress->callback(frame, &message,
                            feed->ingress->callback_context);
  }
}

void oomwoo_cpu_ingress_init(oomwoo_cpu_ingress_t *ingress,
                             oomwoo_cpu_message_callback_t callback,
                             void *callback_context) {
  if (ingress == NULL) {
    return;
  }

  memset(ingress, 0, sizeof(*ingress));
  oomwoo_stream_decoder_init(&ingress->decoder);
  ingress->callback = callback;
  ingress->callback_context = callback_context;
}

size_t oomwoo_cpu_ingress_feed(oomwoo_cpu_ingress_t *ingress,
                               const uint8_t *data, size_t length) {
  feed_context_t context;

  if (ingress == NULL || (data == NULL && length != 0u)) {
    return 0u;
  }

  context.ingress = ingress;
  context.accepted = 0u;
  (void)oomwoo_stream_decoder_feed(&ingress->decoder, data, length,
                                   handle_frame, &context);
  return context.accepted;
}

void oomwoo_cpu_ingress_reset_incomplete(oomwoo_cpu_ingress_t *ingress) {
  if (ingress != NULL) {
    oomwoo_stream_decoder_reset_incomplete(&ingress->decoder);
  }
}
