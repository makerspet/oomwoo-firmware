# CPU/MCU Protocol Bring-up

This is the first executable firmware slice for milestone 2. It deliberately
contains no motor, power, charging, watchdog-reset, or hard-safety GPIO code.

## Scope

The portable C module provides:

- fixed `OW` framing with a compile-time wire version
- little-endian header fields
- CRC-16/CCITT-FALSE
- strict complete-frame validation
- a bounded incremental stream decoder
- counters for CRC, version, length, discarded-byte, and receive-gap failures
- typed encode/decode for every payload defined by the accepted wire-v1 contract
- exact payload-length, direction, and contract-range validation
- no heap allocation and no HAL or Arduino dependency

The stream decoder owns one 524-byte maximum-frame buffer. A decoded payload
pointer is valid only during the synchronous callback. The callback must copy
anything that needs to outlive it.

## Typed messages

`oomwoo_messages` is a side-effect-free translation layer between payload bytes
and typed C structures. It does not dispatch commands, refresh a watchdog, call
application callbacks, or write hardware. Callers must perform those operations
only after successful decode and after checking the current MCU safety state.

The codec validates the accepted contract and reference-codec bounds. Its
current fail-closed mode policy is aligned with the watchdog draft:

- heartbeat mode must be the watchdog's `DISARMED` or `STACK_HEALTHY` value
- E-stop and safety-event active fields are boolean
- drive velocity is within +/-500 mm/s and +/-4000 mrad/s
- drive command validity is 1-250 ms
- cleaning, LiDAR, and LED percentages are 0-100
- safety events fit the current 16-bit safety mask

Wrong lengths, invalid field values, unknown IDs, and the still-open
`POWER_TELEMETRY` and `MCU_DIAGNOSTIC` payloads fail closed. Decode clears the
destination structure before returning an error, while encode reports a zero
output length. Direction metadata also lets a future dispatcher reject a valid
message sent by the wrong endpoint before it reaches application code.

The 23 canonical frames in `tests/conformance/golden_vectors_v1.json` and their
manifest are an unchanged snapshot from the accepted interface contract in
`makerspet/oomwoo` merge commit
`90324ec79491a1f71eaadd86fce0d92b0e13c15a`. The generator validates coverage,
status, IDs, and packed sizes before producing the C fixture used by tests.

## CPU ingress gate

`oomwoo_cpu_ingress` composes the stream decoder and typed codec for the MCU's
receive path. A consumer callback runs only after framing, wire version,
endpoint direction, exact payload length, and semantic value validation all
succeed. CRC-valid MCU-to-CPU messages received on this path are counted and
dropped rather than dispatched.

Framing counters remain in the embedded stream decoder, while the ingress layer
separately counts accepted messages and each semantic rejection class. See
[CPU ingress gate](cpu-ingress.md) for the API, memory ownership, and integration
boundary.

## Bench sketch

`src/main.cpp` is a serial framing echo for a Nucleo G474RE. It starts no
actuator and re-encodes each valid frame byte-for-byte. Corrupt, truncated, or
wrong-version input is dropped.

The harness deliberately emits no `ACK`: frame integrity does not mean that a
message type, payload, or requested action has been accepted. A future command
dispatcher may acknowledge a command only after payload and safety-state
validation.

The sketch resets an incomplete candidate after a 50 ms receive gap. This
prevents a corrupted but in-range length field from holding later traffic
indefinitely.

This Arduino loop is a bring-up harness, not the final communication task. The
production UART path should feed the same decoder from a statically allocated
DMA/ring buffer owned by a FreeRTOS task.

## Compatibility

The framing implementation is version-agnostic at source level:

```text
OOMWOO_PROTOCOL_VERSION=<wire version>
```

The source and default PlatformIO environments compile wire version `1`, which
is the version in the accepted interface draft. Parallel `native_v2` and
`nucleo_g474re_v2` environments prove that candidate version `2` remains a
compile-time override matching
[`oomwoo-mcu-bridge@v0.1.0`](https://github.com/xbattlax/oomwoo-mcu-bridge/releases/tag/v0.1.0).
The final v1-extension versus v2 payload decision remains tracked in
[`oomwoo-io-firmware#1`](https://github.com/makerspet/oomwoo-io-firmware/issues/1),
but it no longer blocks review of the framing core or the accepted wire-v1
payload snapshot.

Changing the macro changes frame acceptance and CRC golden vectors. The typed
payload byte layouts are version-independent in code, but exact full-frame
canonical-vector conformance is intentionally pinned to wire v1. A future
payload revision must update the contract snapshot and codec in one reviewed
change.

## Failure behavior

| Input | Behavior |
|---|---|
| Noise before `OW` | Discard and count bytes |
| Wrong wire version | Reject and increment `version_errors` |
| Payload length above 512 | Reject and increment `length_errors` |
| Bad CRC | Reject and increment `crc_errors` |
| Incomplete frame followed by a receive gap | Reset and increment `gap_resets` |
| Valid frame after corruption | Resynchronize and invoke the callback once |
| Known message with wrong payload length | Reject and clear typed output |
| Payload field outside its contract bound | Reject and clear typed output |
| Unknown or still-open message payload | Reject without dispatching |
| Valid MCU-to-CPU message on CPU ingress | Count and reject wrong direction |

No malformed input can authorize an actuator because this slice has no actuator
output. Later command handling must validate payload bounds and MCU safety state
before writing any setpoint.

## Verification

Host conformance with sanitizers, first using the v1 default and then the v2
override:

```bash
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined \
  -Iinclude src/oomwoo_protocol.c tests/protocol_conformance.c \
  -o /tmp/oomwoo_protocol_conformance
/tmp/oomwoo_protocol_conformance

cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -DOOMWOO_PROTOCOL_VERSION=2 \
  -DOOMWOO_PROTOCOL_EXPECTED_VERSION=2 \
  -fsanitize=address,undefined \
  -Iinclude src/oomwoo_protocol.c tests/protocol_conformance.c \
  -o /tmp/oomwoo_protocol_conformance_v2
/tmp/oomwoo_protocol_conformance_v2

python3 tools/generate_message_vectors.py --check
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined \
  -Iinclude \
  src/oomwoo_protocol.c src/oomwoo_messages.c \
  tests/message_conformance.c \
  -o /tmp/oomwoo_message_conformance
/tmp/oomwoo_message_conformance
```

PlatformIO:

```bash
pio test -e native -e native_v2
pio pkg install -e nucleo_g474re
pio run -e nucleo_g474re -e nucleo_g474re_v2
```

Hardware loopback, UART electrical validation, measured timing, and fault
reaction tests remain required before connecting any motor load.
