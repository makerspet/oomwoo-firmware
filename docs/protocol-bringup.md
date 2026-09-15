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
- no heap allocation and no HAL or Arduino dependency

The stream decoder owns one 524-byte maximum-frame buffer. A decoded payload
pointer is valid only during the synchronous callback. The callback must copy
anything that needs to outlive it.

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
but it no longer blocks review of this payload-agnostic framing core.

Changing the macro changes frame acceptance and CRC golden vectors. Payload
layouts must be reviewed separately; this module does not reinterpret them.

## Failure behavior

| Input | Behavior |
|---|---|
| Noise before `OW` | Discard and count bytes |
| Wrong wire version | Reject and increment `version_errors` |
| Payload length above 512 | Reject and increment `length_errors` |
| Bad CRC | Reject and increment `crc_errors` |
| Incomplete frame followed by a receive gap | Reset and increment `gap_resets` |
| Valid frame after corruption | Resynchronize and invoke the callback once |

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
```

PlatformIO:

```bash
pio test -e native -e native_v2
pio pkg install -e nucleo_g474re
pio run -e nucleo_g474re -e nucleo_g474re_v2
```

Hardware loopback, UART electrical validation, measured timing, and fault
reaction tests remain required before connecting any motor load.
