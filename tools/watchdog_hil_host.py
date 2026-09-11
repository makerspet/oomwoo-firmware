#!/usr/bin/env python3
"""Drive deterministic watchdog HIL scenarios over a POSIX serial device."""

import argparse
import os
import select
import termios
import time


HEARTBEAT_PERIOD_S = 0.05


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

    def command(self, value: str) -> None:
        os.write(self.fd, value.encode("ascii"))

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


def send_heartbeats(port: SerialPort, count: int) -> None:
    for _ in range(count):
        port.command("H")
        time.sleep(HEARTBEAT_PERIOD_S)


def prime_motion(port: SerialPort) -> None:
    send_heartbeats(port, 5)
    port.command("M")
    send_heartbeats(port, 5)
    print(port.read_for(0.05), end="")


def run_loss(port: SerialPort) -> None:
    prime_motion(port)
    print("Heartbeats stopped; measure the final D8 edge to the D7 falling edge.")
    time.sleep(0.3)
    port.command("S")
    print(port.read_for(0.1), end="")


def run_hang(port: SerialPort) -> None:
    prime_motion(port)
    port.command("H")
    time.sleep(0.01)
    port.command("B")
    print(port.read_for(0.1), end="")
    print("Foreground is blocked; D7 must still fall after the watchdog deadline.")
    time.sleep(0.3)


def run_disarm(port: SerialPort) -> None:
    prime_motion(port)
    port.command("D")
    time.sleep(0.01)
    port.command("S")
    print(port.read_for(0.1), end="")


def run_recovery(port: SerialPort) -> None:
    prime_motion(port)
    time.sleep(0.3)
    port.command("H")
    time.sleep(0.01)
    port.command("S")
    print("After heartbeat recovery, D7 must remain low:")
    print(port.read_for(0.1), end="")
    port.command("M")
    time.sleep(0.01)
    port.command("S")
    print("Only the fresh M command may raise D7 again:")
    print(port.read_for(0.1), end="")


SCENARIOS = {
    "loss": run_loss,
    "hang": run_hang,
    "disarm": run_disarm,
    "recovery": run_recovery,
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="serial device, for example /dev/ttyACM0")
    parser.add_argument("scenario", choices=SCENARIOS)
    arguments = parser.parse_args()

    port = SerialPort(arguments.port)
    try:
        SCENARIOS[arguments.scenario](port)
    finally:
        port.close()


if __name__ == "__main__":
    main()
