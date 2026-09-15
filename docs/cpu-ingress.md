# CPU Ingress Gate

`oomwoo_cpu_ingress` is the MCU-side trust boundary between untrusted serial
bytes and a future command dispatcher.

## Pipeline

Input passes through these gates in order:

1. stream synchronization, frame length, wire version, and CRC
2. known message ID and CPU-to-MCU or bidirectional endpoint direction
3. exact typed payload length
4. command value validation
5. synchronous consumer callback

A failure at any gate prevents the callback. The module emits no ACK or NACK,
refreshes no watchdog, stores no command, and accesses no hardware.

## Memory ownership

The ingress object owns its fixed 524-byte framing buffer and all counters. It
uses no heap allocation.

The frame payload points into that buffer. The typed message is local to the
internal frame callback. Both pointers passed to the consumer are valid only for
the duration of the synchronous call; the consumer must copy any state it needs
to retain. The consumer must not recursively call `oomwoo_cpu_ingress_feed` on
the same ingress object.

## Accepted input

The MCU receive path accepts the eight frozen CPU-to-MCU message types plus the
bidirectional `ACK` and `NACK` types. Frozen MCU-to-CPU telemetry and safety
messages are valid protocol messages, but they are rejected on this endpoint as
wrong-direction input. Unknown IDs are rejected before payload decoding.

The two open payloads, `POWER_TELEMETRY` and `MCU_DIAGNOSTIC`, remain output-side
contract work and cannot reach an MCU-side consumer through this gate.

## Counters

Framing diagnostics remain available in `ingress.decoder.stats`: valid frames,
discarded bytes, CRC errors, version errors, length errors, and receive-gap
resets.

`ingress.stats` counts accepted typed messages and rejections for wrong
direction, unknown type, undefined payload, wrong payload length, invalid field
values, and unexpected internal codec errors. A CRC-valid but semantically
invalid command increments the framing valid-frame count and the corresponding
ingress rejection count.

## Safety boundary

Passing this gate means only that bytes match the protocol contract. It does not
mean an actuator command is safe to execute.

A later dispatcher must still check the current MCU state, fault latches,
command freshness, and actuator-specific limits. Only a validated
`STACK_HEALTHY` heartbeat may be submitted to the watchdog, and doing so must
remain separate from arbitrary message receipt. Hard-stop behavior stays in the
reviewed timer-ISR/HAL layer.

## Verification

The host conformance test sends all 23 accepted wire-v1 vectors through the
composed path. Ten CPU/bidirectional messages reach the callback and thirteen
MCU-to-CPU messages are rejected by direction. Additional cases cover CRC
corruption, noise, byte-at-a-time input, unknown IDs, wrong payload lengths,
out-of-range commands, receive-gap reset, and null inputs under C11 and C++17
sanitizers.
