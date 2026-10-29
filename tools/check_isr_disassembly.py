#!/usr/bin/env python3
"""Reject ARM call instructions in a selected disassembled ISR symbol."""

import argparse
import re
from pathlib import Path


SYMBOL = re.compile(r"^[0-9a-fA-F]+ <(.+)>:$")
INSTRUCTION = re.compile(
    r"^\s*[0-9a-fA-F]+:\s+(?:[0-9a-fA-F]{2,8}\s+)+([a-zA-Z][a-zA-Z0-9.]*)"
)
CALL_MNEMONICS = {"bl", "blx"}


def symbol_instructions(disassembly: str, symbol_fragment: str) -> list[str]:
    instructions: list[str] = []
    found = False

    for line in disassembly.splitlines():
        symbol = SYMBOL.match(line)
        if symbol:
            if found:
                break
            found = symbol_fragment in symbol.group(1)
            continue

        if found:
            instruction = INSTRUCTION.match(line)
            if instruction:
                instructions.append(instruction.group(1).lower())

    if not found:
        raise ValueError(f"symbol containing {symbol_fragment!r} was not found")
    if not instructions:
        raise ValueError(f"symbol containing {symbol_fragment!r} has no instructions")
    return instructions


def verify_leaf(disassembly: str, symbol_fragment: str) -> list[str]:
    instructions = symbol_instructions(disassembly, symbol_fragment)
    calls = [instruction for instruction in instructions if instruction in CALL_MNEMONICS]
    if calls:
        raise ValueError(
            f"symbol containing {symbol_fragment!r} contains call instructions: {calls}"
        )
    return instructions


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("disassembly", type=Path)
    parser.add_argument("symbol_fragment")
    arguments = parser.parse_args()

    instructions = verify_leaf(
        arguments.disassembly.read_text(encoding="utf-8"),
        arguments.symbol_fragment,
    )
    print(
        f"PASS: {arguments.symbol_fragment} has {len(instructions)} instructions "
        "and no bl/blx calls"
    )


if __name__ == "__main__":
    main()
