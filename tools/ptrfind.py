#!/usr/bin/env python3
"""Find dword occurrences of a VA anywhere in the file (vtable / pointer search)."""

import struct
import sys

import dumpscan


def main():
    pe = dumpscan.Pe(dumpscan.GAMEMD)
    target = int(sys.argv[1], 0)
    needle = struct.pack("<I", target)
    start = 0
    hits = 0
    while True:
        i = pe.data.find(needle, start)
        if i < 0:
            break
        va = pe.off_to_va(i)
        print(f"file 0x{i:08X}  va {('0x%08X' % va) if va else 'n/a'}")
        hits += 1
        start = i + 1
    print(f"\n{hits} pointer(s) to 0x{target:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
