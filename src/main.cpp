#include <Arduino.h>

#include "oomwoo_protocol.h"

namespace {

constexpr uint32_t kFramingGapMs = 50u;

oomwoo_stream_decoder_t decoder;
uint8_t tx_buffer[OOMWOO_PROTOCOL_MAX_FRAME_SIZE];
uint32_t last_rx_ms = 0u;

void echo_frame(const oomwoo_decoded_frame_t *frame, void *) {
  size_t frame_length = 0u;

  if (oomwoo_encode_frame(frame->message_type, frame->payload,
                          frame->payload_length, frame->sequence, frame->flags,
                          tx_buffer, sizeof(tx_buffer), &frame_length) ==
      OOMWOO_PROTOCOL_OK) {
    Serial.write(tx_buffer, frame_length);
  }
}

}  // namespace

void setup() {
  oomwoo_stream_decoder_init(&decoder);
  Serial.begin(115200);
}

void loop() {
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value >= 0) {
      const uint8_t byte = static_cast<uint8_t>(value);
      last_rx_ms = millis();
      oomwoo_stream_decoder_feed(&decoder, &byte, 1u, echo_frame, nullptr);
    }
  }

  if (decoder.buffered_bytes != 0u &&
      static_cast<uint32_t>(millis() - last_rx_ms) > kFramingGapMs) {
    oomwoo_stream_decoder_reset_incomplete(&decoder);
  }
}
