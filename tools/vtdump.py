#!/usr/bin/env python3
"""Dump a vtable and name each entry by the code range it points into.

Usage:
    python vtdump.py 0x7E3EBC [count]
"""
import sys

import dumpscan


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    va = int(sys.argv[1], 0)
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 64

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    raw = pe.read(va, count * 4)
    if raw is None:
        print("not mapped")
        return 1

    for index in range(count):
        value = int.from_bytes(raw[index * 4:index * 4 + 4], "little")
        if value == 0:
            continue
        where = "gamemd" if 0x400000 <= value < 0x800000 else "other"
        print(f"[{index:3d}] +0x{index * 4:03X}  0x{value:08X}  ({where})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
