#include "oomwoo_protocol.h"

#include <string.h>

static void write_u16_le(uint8_t *output, uint16_t value) {
  output[0] = (uint8_t)(value & 0xffu);
  output[1] = (uint8_t)((value >> 8u) & 0xffu);
}

static uint16_t read_u16_le(const uint8_t *input) {
  return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8u));
}

static void remove_prefix(oomwoo_stream_decoder_t *decoder, size_t count,
                          uint8_t discarded) {
  if (count == 0u || count > decoder->buffered_bytes) {
    return;
  }
  if (discarded != 0u) {
    decoder->stats.discarded_bytes += (uint32_t)count;
  }
  decoder->buffered_bytes -= count;
  if (decoder->buffered_bytes != 0u) {
    memmove(decoder->buffer, decoder->buffer + count,
            decoder->buffered_bytes);
  }
}

static void resync_after_error(oomwoo_stream_decoder_t *decoder) {
  size_t index;

  for (index = 1u; index + 1u < decoder->buffered_bytes; ++index) {
    if (decoder->buffer[index] == OOMWOO_PROTOCOL_MAGIC_0 &&
        decoder->buffer[index + 1u] == OOMWOO_PROTOCOL_MAGIC_1) {
      remove_prefix(decoder, index, 1u);
      return;
    }
  }

  if (decoder->buffered_bytes != 0u &&
      decoder->buffer[decoder->buffered_bytes - 1u] ==
          OOMWOO_PROTOCOL_MAGIC_0) {
    remove_prefix(decoder, decoder->buffered_bytes - 1u, 1u);
  } else {
    remove_prefix(decoder, decoder->buffered_bytes, 1u);
  }
}

uint16_t oomwoo_crc16_ccitt_false(const uint8_t *data, size_t length) {
  uint16_t crc = 0xffffu;
  size_t index;

  if (data == NULL && length != 0u) {
    return 0u;
  }

  for (index = 0u; index < length; ++index) {
    uint8_t bit;
    crc ^= (uint16_t)((uint16_t)data[index] << 8u);
    for (bit = 0u; bit < 8u; ++bit) {
      if ((crc & 0x8000u) != 0u) {
        crc = (uint16_t)((crc << 1u) ^ 0x1021u);
      } else {
        crc = (uint16_t)(crc << 1u);
      }
    }
  }
  return crc;
}

oomwoo_protocol_result_t oomwoo_encode_frame(
    uint16_t message_type, const uint8_t *payload, uint16_t payload_length,
    uint16_t sequence, uint8_t flags, uint8_t *output, size_t output_capacity,
    size_t *output_length) {
  size_t frame_length;
  uint16_t crc;

  if (output == NULL || output_length == NULL ||
      (payload == NULL && payload_length != 0u)) {
    return OOMWOO_PROTOCOL_NULL_ARGUMENT;
  }
  if ((size_t)payload_length > OOMWOO_PROTOCOL_MAX_PAYLOAD_SIZE) {
    return OOMWOO_PROTOCOL_PAYLOAD_TOO_LARGE;
  }

  frame_length =
      OOMWOO_PROTOCOL_HEADER_SIZE + (size_t)payload_length +
      OOMWOO_PROTOCOL_CRC_SIZE;
  if (output_capacity < frame_length) {
    return OOMWOO_PROTOCOL_OUTPUT_TOO_SMALL;
  }

  output[0] = OOMWOO_PROTOCOL_MAGIC_0;
  output[1] = OOMWOO_PROTOCOL_MAGIC_1;
  output[2] = OOMWOO_PROTOCOL_VERSION;
  output[3] = flags;
  write_u16_le(output + 4u, sequence);
  write_u16_le(output + 6u, message_type);
  write_u16_le(output + 8u, payload_length);
  if (payload_length != 0u) {
    memcpy(output + OOMWOO_PROTOCOL_HEADER_SIZE, payload, payload_length);
  }

  crc = oomwoo_crc16_ccitt_false(
      output, OOMWOO_PROTOCOL_HEADER_SIZE + (size_t)payload_length);
  write_u16_le(output + frame_length - OOMWOO_PROTOCOL_CRC_SIZE, crc);
  *output_length = frame_length;
  return OOMWOO_PROTOCOL_OK;
}

oomwoo_protocol_result_t
oomwoo_decode_frame(const uint8_t *data, size_t length,
                    oomwoo_decoded_frame_t *output) {
  uint16_t payload_length;
  size_t expected_length;
  uint16_t expected_crc;
  uint16_t actual_crc;

  if (data == NULL || output == NULL) {
    return OOMWOO_PROTOCOL_NULL_ARGUMENT;
  }
  if (length < OOMWOO_PROTOCOL_HEADER_SIZE + OOMWOO_PROTOCOL_CRC_SIZE) {
    return OOMWOO_PROTOCOL_FRAME_TOO_SHORT;
  }
  if (data[0] != OOMWOO_PROTOCOL_MAGIC_0 ||
      data[1] != OOMWOO_PROTOCOL_MAGIC_1) {
    return OOMWOO_PROTOCOL_BAD_MAGIC;
  }
  if (data[2] != OOMWOO_PROTOCOL_VERSION) {
    return OOMWOO_PROTOCOL_BAD_VERSION;
  }

  payload_length = read_u16_le(data + 8u);
  if ((size_t)payload_length > OOMWOO_PROTOCOL_MAX_PAYLOAD_SIZE) {
    return OOMWOO_PROTOCOL_PAYLOAD_TOO_LARGE;
  }
  expected_length =
      OOMWOO_PROTOCOL_HEADER_SIZE + (size_t)payload_length +
      OOMWOO_PROTOCOL_CRC_SIZE;
  if (length != expected_length) {
    return OOMWOO_PROTOCOL_LENGTH_MISMATCH;
  }

  expected_crc = read_u16_le(data + length - OOMWOO_PROTOCOL_CRC_SIZE);
  actual_crc =
      oomwoo_crc16_ccitt_false(data, length - OOMWOO_PROTOCOL_CRC_SIZE);
  if (expected_crc != actual_crc) {
    return OOMWOO_PROTOCOL_BAD_CRC;
  }

  output->version = data[2];
  output->flags = data[3];
  output->sequence = read_u16_le(data + 4u);
  output->message_type = read_u16_le(data + 6u);
  output->payload_length = payload_length;
  output->payload = data + OOMWOO_PROTOCOL_HEADER_SIZE;
  return OOMWOO_PROTOCOL_OK;
}

void oomwoo_stream_decoder_init(oomwoo_stream_decoder_t *decoder) {
  if (decoder != NULL) {
    memset(decoder, 0, sizeof(*decoder));
  }
}

size_t oomwoo_stream_decoder_feed(oomwoo_stream_decoder_t *decoder,
                                  const uint8_t *data, size_t length,
                                  oomwoo_frame_callback_t callback,
                                  void *context) {
  size_t input_index;
  size_t emitted = 0u;

  if (decoder == NULL || (data == NULL && length != 0u)) {
    return 0u;
  }

  for (input_index = 0u; input_index < length; ++input_index) {
    uint8_t processing = 1u;

    if (decoder->buffered_bytes == OOMWOO_PROTOCOL_MAX_FRAME_SIZE) {
      resync_after_error(decoder);
    }
    decoder->buffer[decoder->buffered_bytes] = data[input_index];
    ++decoder->buffered_bytes;

    while (processing != 0u) {
      uint16_t payload_length;
      size_t frame_length;
      oomwoo_decoded_frame_t frame;
      oomwoo_protocol_result_t result;

      processing = 0u;
      if (decoder->buffered_bytes == 1u) {
        if (decoder->buffer[0] != OOMWOO_PROTOCOL_MAGIC_0) {
          remove_prefix(decoder, 1u, 1u);
        }
        continue;
      }
      if (decoder->buffered_bytes < 2u) {
        continue;
      }
      if (decoder->buffer[0] != OOMWOO_PROTOCOL_MAGIC_0 ||
          decoder->buffer[1] != OOMWOO_PROTOCOL_MAGIC_1) {
        resync_after_error(decoder);
        processing = decoder->buffered_bytes >= 2u ? 1u : 0u;
        continue;
      }
      if (decoder->buffered_bytes < OOMWOO_PROTOCOL_HEADER_SIZE) {
        continue;
      }
      if (decoder->buffer[2] != OOMWOO_PROTOCOL_VERSION) {
        ++decoder->stats.version_errors;
        resync_after_error(decoder);
        processing = decoder->buffered_bytes >= 2u ? 1u : 0u;
        continue;
      }

      payload_length = read_u16_le(decoder->buffer + 8u);
      if ((size_t)payload_length > OOMWOO_PROTOCOL_MAX_PAYLOAD_SIZE) {
        ++decoder->stats.length_errors;
        resync_after_error(decoder);
        processing = decoder->buffered_bytes >= 2u ? 1u : 0u;
        continue;
      }
      frame_length = OOMWOO_PROTOCOL_HEADER_SIZE + (size_t)payload_length +
                     OOMWOO_PROTOCOL_CRC_SIZE;
      if (decoder->buffered_bytes < frame_length) {
        continue;
      }

      result = oomwoo_decode_frame(decoder->buffer, frame_length, &frame);
      if (result != OOMWOO_PROTOCOL_OK) {
        if (result == OOMWOO_PROTOCOL_BAD_CRC) {
          ++decoder->stats.crc_errors;
        } else {
          ++decoder->stats.length_errors;
        }
        resync_after_error(decoder);
        processing = decoder->buffered_bytes >= 2u ? 1u : 0u;
        continue;
      }

      if (callback != NULL) {
        callback(&frame, context);
      }
      ++decoder->stats.frames;
      ++emitted;
      remove_prefix(decoder, frame_length, 0u);
      processing = decoder->buffered_bytes >= 2u ? 1u : 0u;
    }
  }

  return emitted;
}

void oomwoo_stream_decoder_reset_incomplete(oomwoo_stream_decoder_t *decoder) {
  if (decoder == NULL || decoder->buffered_bytes == 0u) {
    return;
  }
  decoder->stats.discarded_bytes += (uint32_t)decoder->buffered_bytes;
  ++decoder->stats.gap_resets;
  decoder->buffered_bytes = 0u;
}
