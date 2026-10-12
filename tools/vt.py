#!/usr/bin/env python3
"""Dump one or more vtables and follow interesting entries.

Usage:
    python vt.py vt 0x7E3C14 60
    python vt.py vt 0x7E3C14 60 --from 0x7E3A00
"""
import sys

import dumpscan


def dump(pe, va, count):
    off = pe.va_to_off(va)
    if off is None:
        print(f"0x{va:08X} not mapped")
        return
    for i in range(count):
        ptr = pe.u32(off + i * 4)
        tag = "CODE" if pe.is_executable(ptr) else ("NULL" if ptr == 0 else "????")
        print(f"[{i:3d}] 0x{va + i * 4:08X} -> 0x{ptr:08X}  {tag}")


def main():
    pe = dumpscan.Pe(dumpscan.GAMEMD)
    argv = sys.argv[1:]
    if argv and argv[0] == "vt":
        argv = argv[1:]
    start = int(argv[0], 0)
    count = int(argv[1], 0) if len(argv) > 1 else 60
    dump(pe, start, count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
