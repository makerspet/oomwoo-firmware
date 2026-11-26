#!/usr/bin/env python3
"""Copy the portable firmware sources into the Arduino library.

An Arduino library has to be self-contained (Library Manager installs it on its
own), so arduino/libraries/OOMWOO/src holds copies of the hardware-free code:
the board-link and phone protocols, the state machines, the IR codec and the
generated pin map. The copies are generated; edit the originals in mcu/common,
src/ and include/, then run this script.

    tools/sync_arduino_lib.py          # write the copies
    tools/sync_arduino_lib.py --check  # exit 1 if a copy is out of date (CI)
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEST = ROOT / "arduino" / "libraries" / "OOMWOO" / "src"

SOURCES = [
    "mcu/common/mcu_link.h", "mcu/common/mcu_link.c",
    "mcu/common/power_sm.h", "mcu/common/power_sm.c",
    "mcu/common/charger_sm.h", "mcu/common/charger_sm.c",
    "mcu/common/drive_gate.h", "mcu/common/drive_gate.c",
    "mcu/common/nec.h", "mcu/common/nec.c",
    "mcu/common/nec_tx.h", "mcu/common/nec_tx.c",
    "mcu/common/dock_sm.h", "mcu/common/dock_sm.c",
    "mcu/common/dock_beacon.h",
    "include/oomwoo_protocol.h", "src/oomwoo_protocol.c",
    "include/oomwoo_pins.h",
]

BANNER = {
    ".h": "/* Generated copy of {src} (tools/sync_arduino_lib.py). Do not edit here. */\n",
    ".c": "/* Generated copy of {src} (tools/sync_arduino_lib.py). Do not edit here. */\n",
}


def expected(src: str) -> bytes:
    path = ROOT / src
    text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
    return (BANNER[path.suffix].format(src=src) + text).encode("utf-8")


def main() -> int:
    check = "--check" in sys.argv[1:]
    stale = []
    for src in SOURCES:
        dst = DEST / pathlib.Path(src).name
        want = expected(src)
        have = dst.read_bytes().replace(b"\r\n", b"\n") if dst.exists() else None
        if have == want:
            continue
        if check:
            stale.append(str(dst.relative_to(ROOT)))
        else:
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(want)
            print(f"updated {dst.relative_to(ROOT)}")
    if stale:
        print("Arduino library copies out of date (run tools/sync_arduino_lib.py):")
        for s in stale:
            print("  " + s)
        return 1
    if check:
        print("Arduino library copies are in sync")
    return 0


if __name__ == "__main__":
    sys.exit(main())
