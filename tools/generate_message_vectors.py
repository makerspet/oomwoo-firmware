#!/usr/bin/env python3
"""Generate the firmware's C fixture from the canonical protocol snapshot."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests/conformance/protocol_v1.json"
VECTORS = ROOT / "tests/conformance/golden_vectors_v1.json"
OUTPUT = ROOT / "tests/generated/oomwoo_golden_vectors_v1.h"
UPSTREAM_COMMIT = "90324ec79491a1f71eaadd86fce0d92b0e13c15a"
HEADER_FORMAT = "<2sBBHHH"
CRC_FORMAT = "<H"


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def c_bytes(value: bytes) -> str:
    if not value:
        return "UINT8_C(0x00)"
    return ", ".join(f"UINT8_C(0x{byte:02x})" for byte in value)


def load_and_validate() -> list[dict[str, object]]:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    vectors = json.loads(VECTORS.read_text(encoding="utf-8"))

    if manifest["wire_version"] != 1 or vectors["wire_version"] != 1:
        raise ValueError("only the frozen wire-v1 contract can be generated")
    if (
        manifest["magic_ascii"] != "OW"
        or manifest["byte_order"] != "little"
        or manifest["crc"] != "CRC-16/CCITT-FALSE"
        or vectors["generated_from"] != MANIFEST.name
    ):
        raise ValueError("contract framing metadata does not match wire v1")

    messages = {message["name"]: message for message in manifest["messages"]}
    if len(messages) != len(manifest["messages"]):
        raise ValueError("manifest contains duplicate message names")
    if len({message["id"] for message in messages.values()}) != len(messages):
        raise ValueError("manifest contains duplicate message IDs")

    covered: set[str] = set()
    vector_names: set[str] = set()
    for vector in vectors["vectors"]:
        if vector["name"] in vector_names:
            raise ValueError(f"duplicate vector name {vector['name']}")
        vector_names.add(vector["name"])
        message = messages.get(vector["message_name"])
        if message is None or message["payload_status"] != "defined":
            raise ValueError(f"vector references undefined message {vector['message_name']}")
        if vector["message_type"] != message["id"]:
            raise ValueError(f"message ID mismatch for {vector['message_name']}")
        payload = bytes.fromhex(vector["payload_hex"])
        if len(payload) != struct.calcsize(message["struct_format"]):
            raise ValueError(f"payload size mismatch for {vector['name']}")

        frame = bytes.fromhex(vector["frame_hex"])
        header_size = struct.calcsize(HEADER_FORMAT)
        crc_size = struct.calcsize(CRC_FORMAT)
        if len(frame) != header_size + len(payload) + crc_size:
            raise ValueError(f"frame size mismatch for {vector['name']}")
        magic, version, flags, sequence, message_type, payload_size = struct.unpack(
            HEADER_FORMAT, frame[:header_size]
        )
        if (
            magic != b"OW"
            or version != 1
            or flags != vector["flags"]
            or sequence != vector["sequence"]
            or message_type != vector["message_type"]
            or payload_size != len(payload)
            or frame[header_size:-crc_size] != payload
        ):
            raise ValueError(f"frame fields mismatch for {vector['name']}")
        expected_crc = struct.unpack(CRC_FORMAT, frame[-crc_size:])[0]
        if expected_crc != crc16_ccitt_false(frame[:-crc_size]):
            raise ValueError(f"frame CRC mismatch for {vector['name']}")
        covered.add(vector["message_name"])

    defined = {
        name
        for name, message in messages.items()
        if message["payload_status"] == "defined"
    }
    if covered != defined:
        raise ValueError(f"vector coverage mismatch: missing={sorted(defined - covered)}")

    open_manifest = {
        (message["name"], message["id"])
        for message in messages.values()
        if message["payload_status"] == "open"
    }
    open_vectors = {
        (message["name"], message["message_type"])
        for message in vectors["open_messages"]
    }
    if open_manifest != open_vectors:
        raise ValueError("open-message lists differ between manifest and vectors")

    return vectors["vectors"]


def render() -> str:
    vectors = load_and_validate()
    max_payload = max(len(bytes.fromhex(vector["payload_hex"])) for vector in vectors)
    max_frame = max(len(bytes.fromhex(vector["frame_hex"])) for vector in vectors)
    rows: list[str] = []

    for vector in vectors:
        payload = bytes.fromhex(vector["payload_hex"])
        frame = bytes.fromhex(vector["frame_hex"])
        rows.append(
            "  {\n"
            f"    \"{vector['name']}\", UINT16_C({vector['message_type']}), "
            f"UINT16_C({vector['sequence']}), UINT8_C({vector['flags']}),\n"
            f"    UINT8_C({len(payload)}), {{ {c_bytes(payload)} }},\n"
            f"    UINT8_C({len(frame)}), {{ {c_bytes(frame)} }}\n"
            "  }"
        )

    return f"""/* Generated by tools/generate_message_vectors.py. Do not edit. */
#ifndef OOMWOO_GOLDEN_VECTORS_V1_H
#define OOMWOO_GOLDEN_VECTORS_V1_H

#include <stddef.h>
#include <stdint.h>

#define OOMWOO_GOLDEN_SOURCE_COMMIT \"{UPSTREAM_COMMIT}\"
#define OOMWOO_GOLDEN_MAX_PAYLOAD_SIZE ((size_t){max_payload})
#define OOMWOO_GOLDEN_MAX_FRAME_SIZE ((size_t){max_frame})

typedef struct {{
  const char *name;
  uint16_t message_type;
  uint16_t sequence;
  uint8_t flags;
  uint8_t payload_length;
  uint8_t payload[{max_payload}];
  uint8_t frame_length;
  uint8_t frame[{max_frame}];
}} oomwoo_golden_vector_v1_t;

static const oomwoo_golden_vector_v1_t OOMWOO_GOLDEN_VECTORS_V1[] = {{
{',\n'.join(rows)}
}};

#define OOMWOO_GOLDEN_VECTOR_V1_COUNT \\
  (sizeof(OOMWOO_GOLDEN_VECTORS_V1) / sizeof(OOMWOO_GOLDEN_VECTORS_V1[0]))

#endif
"""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check", action="store_true", help="fail if the generated header is stale"
    )
    args = parser.parse_args()
    expected = render()

    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != expected:
            raise SystemExit(
                "generated message vectors are stale; run "
                "python3 tools/generate_message_vectors.py"
            )
        print("message vector fixture is current")
        return

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(expected, encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
