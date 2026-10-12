#!/usr/bin/env python3
"""Read NUL-terminated strings at one or more virtual addresses.

Usage:
    python peekstr.py 0x81aa5c 0x81aa68 0x81aa4c
"""
import sys

import dumpscan


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    for arg in sys.argv[1:]:
        va = int(arg, 0)
        if va < pe.image_base:
            va += pe.image_base
        raw = pe.read(va, 64)
        if raw is None:
            print(f"0x{va:08X}  <unmapped>")
            continue
        text = raw.split(b"\0")[0].decode("latin-1")
        print(f"0x{va:08X}  '{text}'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
