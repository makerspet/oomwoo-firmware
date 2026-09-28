# MCU Identity Handshake

The identity service is the first protocol behavior implemented above the
validated CPU ingress gate. It lets the CPU discover the MCU firmware at boot
or after either endpoint reconnects, without touching an actuator or changing
the MCU safety state.

## Behavior

The Nucleo harness emits one `MCU_HELLO` after starting the serial port. Every
valid `IDENTIFY_REQUEST` received later produces a fresh `MCU_HELLO`, so the CPU
does not depend on observing the startup frame. The response contains the
configured firmware major version, minor version, and build ID.

Only a request that passes framing CRC, wire-version, CPU-to-MCU direction,
exact payload-length, and typed-message validation reaches the service. A bad
CRC, a wrong-version frame, or a malformed request produces no response.

The service ignores every other valid message. In particular, it does not:

- acknowledge a command
- arm or disarm the MCU
- refresh a CPU watchdog
- replay a previous response
- write a setpoint, GPIO, peripheral, or actuator

## Sequence and memory rules

The caller supplies the initial 16-bit transmit sequence. A successful
`MCU_HELLO` consumes one sequence value and wraps from `65535` to `0`. Failed
encoding and ignored messages do not consume a sequence value.

`oomwoo_identity` is heap-free and owns only firmware metadata plus the next
sequence value. The caller owns the output buffer. It must provide at least
`OOMWOO_IDENTITY_HELLO_FRAME_SIZE` bytes and may reuse the frame immediately
after it has been written to the transport.

The bench harness defaults to firmware `0.1`, build ID `0`, and sequence `0`.
Builds can override `OOMWOO_FIRMWARE_VERSION_MAJOR`,
`OOMWOO_FIRMWARE_VERSION_MINOR`, and `OOMWOO_FIRMWARE_BUILD_ID`.

## Verification boundary

Host conformance tests compare the generated response against the canonical
wire-v1 `MCU_HELLO` vector. They also cover reconnect requests through the full
stream-decoder and typed-ingress path, silent CRC rejection, ignored heartbeat,
undersized output, and sequence wraparound in strict C11 and C++17 builds.

This remains a non-actuating bring-up service. Watchdog timing, disarmed
telemetry policy, hardware UART loopback, and the production FreeRTOS transport
remain separate review and bench-validation steps.
