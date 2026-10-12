#!/usr/bin/env python3
"""Dump a vtable: read consecutive dwords and resolve them to code addresses."""

import sys

import dumpscan


def main():
    pe = dumpscan.Pe(dumpscan.GAMEMD)
    va = int(sys.argv[1], 0)
    count = int(sys.argv[2], 0) if len(sys.argv) > 2 else 60
    names = {}
    for arg in sys.argv[3:]:
        idx, _, name = arg.partition("=")
        names[int(idx)] = name

    off = pe.va_to_off(va)
    for i in range(count):
        ptr = pe.u32(off + i * 4)
        tag = ""
        if pe.is_executable(ptr):
            tag = "CODE"
        elif ptr == 0:
            tag = "NULL"
        label = f"  {names[i]}" if i in names else ""
        print(f"[{i:3d}] 0x{va + i * 4:08X} -> 0x{ptr:08X}  {tag}{label}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
