import struct
import unittest

from tools.watchdog_hil_host import (
    CPU_MODE_STACK_HEALTHY,
    MESSAGE_DRIVE_SETPOINT,
    MESSAGE_HEARTBEAT,
    crc16_ccitt_false,
    encode_frame,
)


class WatchdogHilProtocolTest(unittest.TestCase):
    def test_heartbeat_matches_canonical_wire_v1_vector(self) -> None:
        frame = encode_frame(
            MESSAGE_HEARTBEAT,
            struct.pack("<IB", 0x12345678, CPU_MODE_STACK_HEALTHY),
            1,
        )
        self.assertEqual(
            frame.hex(), "4f5701000100010005007856341201a488"
        )

    def test_frame_crc_covers_header_and_payload(self) -> None:
        frame = encode_frame(
            MESSAGE_DRIVE_SETPOINT, struct.pack("<hhH", 100, 0, 100), 7
        )
        self.assertEqual(
            struct.unpack("<H", frame[-2:])[0], crc16_ccitt_false(frame[:-2])
        )

    def test_corrupt_crc_changes_only_final_crc_byte(self) -> None:
        payload = struct.pack("<IB", 42, CPU_MODE_STACK_HEALTHY)
        valid = encode_frame(MESSAGE_HEARTBEAT, payload, 9)
        corrupt = encode_frame(
            MESSAGE_HEARTBEAT, payload, 9, corrupt_crc=True
        )
        self.assertEqual(corrupt[:-1], valid[:-1])
        self.assertEqual(corrupt[-1], valid[-1] ^ 0x01)
        self.assertNotEqual(
            struct.unpack("<H", corrupt[-2:])[0],
            crc16_ccitt_false(corrupt[:-2]),
        )


if __name__ == "__main__":
    unittest.main()
