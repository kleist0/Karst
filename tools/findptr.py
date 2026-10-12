#!/usr/bin/env python3
"""Find every occurrence of a 32-bit little-endian value anywhere in the image.

Catches function-pointer tables and relocation-like constants that a linear
disassembly sweep will not attribute to any instruction.

Usage:
    python findptr.py 0x451883 [0x4517d0 ...]
"""
import struct
import sys

import dumpscan


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    pe = dumpscan.Pe(dumpscan.GAMEMD)

    for arg in sys.argv[1:]:
        value = int(arg, 0)
        needle = struct.pack("<I", value)
        print(f"=== 0x{value:08X} ===")
        index = 0
        hits = 0
        while True:
            index = pe.data.find(needle, index)
            if index < 0:
                break
            va = pe.off_to_va(index)
            print(f"  file 0x{index:08X}  va {('0x%08X' % va) if va else 'n/a'}")
            hits += 1
            index += 1
        print(f"  {hits} occurrence(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
