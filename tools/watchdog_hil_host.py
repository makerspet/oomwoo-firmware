#!/usr/bin/env python3
"""Drive watchdog HIL scenarios with real OOMWOO protocol frames."""

import argparse
import os
import select
import struct
import termios
import time


PROTOCOL_VERSION = 1
MESSAGE_HEARTBEAT = 0x0001
MESSAGE_DRIVE_SETPOINT = 0x0101
CPU_MODE_DISARMED = 0
CPU_MODE_STACK_HEALTHY = 1
HEARTBEAT_PERIOD_S = 0.05


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (
                crc << 1
            ) & 0xFFFF
    return crc


def encode_frame(
    message_type: int,
    payload: bytes,
    sequence: int,
    *,
    corrupt_crc: bool = False,
) -> bytes:
    header = b"OW" + struct.pack(
        "<BBHHH",
        PROTOCOL_VERSION,
        0,
        sequence & 0xFFFF,
        message_type,
        len(payload),
    )
    crc = crc16_ccitt_false(header + payload)
    frame = header + payload + struct.pack("<H", crc)
    if corrupt_crc:
        frame = frame[:-1] + bytes((frame[-1] ^ 0x01,))
    return frame


class SerialPort:
    def __init__(self, path: str) -> None:
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attributes = termios.tcgetattr(self.fd)
        attributes[0] = 0
        attributes[1] = 0
        attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attributes[3] = 0
        attributes[4] = termios.B115200
        attributes[5] = termios.B115200
        attributes[6][termios.VMIN] = 0
        attributes[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attributes)
        termios.tcflush(self.fd, termios.TCIOFLUSH)

    def close(self) -> None:
        os.close(self.fd)

    def write(self, value: bytes) -> None:
        remaining = memoryview(value)
        while remaining:
            try:
                written = os.write(self.fd, remaining)
            except BlockingIOError:
                _, writable, _ = select.select([], [self.fd], [], 1.0)
                if not writable:
                    raise TimeoutError("serial port did not become writable")
                continue
            if written == 0:
                raise OSError("serial write returned zero bytes")
            remaining = remaining[written:]

    def read_for(self, duration_s: float) -> str:
        deadline = time.monotonic() + duration_s
        chunks = []
        while time.monotonic() < deadline:
            timeout = max(0.0, deadline - time.monotonic())
            readable, _, _ = select.select([self.fd], [], [], timeout)
            if not readable:
                break
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                continue
            if chunk:
                chunks.append(chunk)
        return b"".join(chunks).decode("ascii", errors="replace")


class ProtocolClient:
    def __init__(self, port: SerialPort) -> None:
        self.port = port
        self.sequence = 1

    def send(
        self, message_type: int, payload: bytes = b"", *, corrupt_crc: bool = False
    ) -> None:
        self.port.write(
            encode_frame(
                message_type,
                payload,
                self.sequence,
                corrupt_crc=corrupt_crc,
            )
        )
        self.sequence = (self.sequence + 1) & 0xFFFF

    def heartbeat(self, mode: int, *, corrupt_crc: bool = False) -> None:
        cpu_time_ms = int(time.monotonic() * 1000) & 0xFFFFFFFF
        self.send(
            MESSAGE_HEARTBEAT,
            struct.pack("<IB", cpu_time_ms, mode),
            corrupt_crc=corrupt_crc,
        )

    def drive(self) -> None:
        self.send(MESSAGE_DRIVE_SETPOINT, struct.pack("<hhH", 100, 0, 100))

    def status(self) -> None:
        self.port.write(b"S")

    def block_foreground(self) -> None:
        self.port.write(b"B")


def send_heartbeats(client: ProtocolClient, count: int) -> None:
    for _ in range(count):
        client.heartbeat(CPU_MODE_STACK_HEALTHY)
        time.sleep(HEARTBEAT_PERIOD_S)


def prime_motion(client: ProtocolClient) -> None:
    send_heartbeats(client, 5)
    client.drive()
    send_heartbeats(client, 5)
    print(client.port.read_for(0.05), end="")


def print_status(client: ProtocolClient) -> None:
    client.status()
    print(client.port.read_for(0.1), end="")


def run_loss(client: ProtocolClient) -> None:
    prime_motion(client)
    print("Heartbeats stopped; measure the final D8 edge to the D7 falling edge.")
    time.sleep(0.3)
    print_status(client)


def run_hang(client: ProtocolClient) -> None:
    prime_motion(client)
    client.heartbeat(CPU_MODE_STACK_HEALTHY)
    time.sleep(0.01)
    client.block_foreground()
    print(client.port.read_for(0.1), end="")
    print("Foreground is blocked; D7 must still fall after the watchdog deadline.")
    time.sleep(0.3)


def run_disarm(client: ProtocolClient) -> None:
    prime_motion(client)
    client.heartbeat(CPU_MODE_DISARMED)
    time.sleep(0.01)
    print_status(client)


def run_recovery(client: ProtocolClient) -> None:
    prime_motion(client)
    time.sleep(0.3)
    client.heartbeat(CPU_MODE_STACK_HEALTHY)
    time.sleep(0.01)
    print("After heartbeat recovery, D7 must remain low:")
    print_status(client)
    client.drive()
    time.sleep(0.01)
    print("Only the fresh DRIVE_SETPOINT may raise D7 again:")
    print_status(client)


def run_corrupt(client: ProtocolClient) -> None:
    prime_motion(client)
    for _ in range(5):
        client.heartbeat(CPU_MODE_STACK_HEALTHY, corrupt_crc=True)
        time.sleep(HEARTBEAT_PERIOD_S)
    print("CRC-invalid heartbeats must not refresh the deadline:")
    print_status(client)


def run_invalid_mode(client: ProtocolClient) -> None:
    prime_motion(client)
    for _ in range(5):
        client.heartbeat(2)
        time.sleep(HEARTBEAT_PERIOD_S)
    print("Semantically invalid heartbeats must not refresh the deadline:")
    print_status(client)


SCENARIOS = {
    "loss": run_loss,
    "hang": run_hang,
    "disarm": run_disarm,
    "recovery": run_recovery,
    "corrupt": run_corrupt,
    "invalid-mode": run_invalid_mode,
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="serial device, for example /dev/ttyACM0")
    parser.add_argument("scenario", choices=SCENARIOS)
    arguments = parser.parse_args()

    port = SerialPort(arguments.port)
    try:
        SCENARIOS[arguments.scenario](ProtocolClient(port))
    finally:
        port.close()


if __name__ == "__main__":
    main()
