#include <Arduino.h>

#include "oomwoo_cpu_ingress.h"
#include "oomwoo_identity.h"

#ifndef OOMWOO_FIRMWARE_VERSION_MAJOR
#define OOMWOO_FIRMWARE_VERSION_MAJOR 0u
#endif

#ifndef OOMWOO_FIRMWARE_VERSION_MINOR
#define OOMWOO_FIRMWARE_VERSION_MINOR 1u
#endif

#ifndef OOMWOO_FIRMWARE_BUILD_ID
#define OOMWOO_FIRMWARE_BUILD_ID 0u
#endif

namespace {

constexpr uint32_t kFramingGapMs = 50u;

oomwoo_cpu_ingress_t ingress;
oomwoo_identity_t identity;
uint8_t tx_buffer[OOMWOO_IDENTITY_HELLO_FRAME_SIZE];
uint32_t last_rx_ms = 0u;

void send_hello() {
  size_t frame_length = 0u;

  if (oomwoo_identity_emit_hello(&identity, tx_buffer, sizeof(tx_buffer),
                                 &frame_length) == OOMWOO_IDENTITY_OK) {
    Serial.write(tx_buffer, frame_length);
  }
}

void handle_message(const oomwoo_decoded_frame_t *,
                    const oomwoo_message_t *message, void *) {
  size_t frame_length = 0u;

  if (oomwoo_identity_handle_message(&identity, message, tx_buffer,
                                     sizeof(tx_buffer), &frame_length) ==
      OOMWOO_IDENTITY_OK) {
    Serial.write(tx_buffer, frame_length);
  }
}

}  // namespace

void setup() {
  oomwoo_cpu_ingress_init(&ingress, handle_message, nullptr);
  oomwoo_identity_init(
      &identity, static_cast<uint16_t>(OOMWOO_FIRMWARE_VERSION_MAJOR),
      static_cast<uint16_t>(OOMWOO_FIRMWARE_VERSION_MINOR),
      static_cast<uint32_t>(OOMWOO_FIRMWARE_BUILD_ID), UINT16_C(0));
  Serial.begin(115200);
  send_hello();
}

void loop() {
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value >= 0) {
      const uint8_t byte = static_cast<uint8_t>(value);
      last_rx_ms = millis();
      oomwoo_cpu_ingress_feed(&ingress, &byte, 1u);
    }
  }

  if (ingress.decoder.buffered_bytes != 0u &&
      static_cast<uint32_t>(millis() - last_rx_ms) > kFramingGapMs) {
    oomwoo_cpu_ingress_reset_incomplete(&ingress);
  }
}
