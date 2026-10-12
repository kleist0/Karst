#!/usr/bin/env python3
"""Brute-force scan for x86 call/jmp instructions targeting an address.

Linear disassembly stops at the first byte it cannot decode, so callers living
after an embedded jump table are never seen. This scans raw bytes instead:

    E8 rel32        call rel32
    E9 rel32        jmp  rel32
    0F 8x rel32     jcc  rel32

Usage:
    python bytecalls.py 0x451883
"""
import struct
import sys

import dumpscan


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    target = int(sys.argv[1], 0)
    pe = dumpscan.Pe(dumpscan.GAMEMD)

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]

        for index in range(len(code) - 5):
            opcode = code[index]
            if opcode == 0xE8:
                kind = "call"
                size = 5
            elif opcode == 0xE9:
                kind = "jmp "
                size = 5
            elif opcode == 0x0F and 0x80 <= code[index + 1] <= 0x8F:
                kind = f"jcc{code[index + 1] - 0x80:x}"
                size = 6
            else:
                continue

            rel = struct.unpack_from("<i", code, index + (2 if size == 6 else 1))[0]
            here = start + index
            dest = here + size + rel
            if dest == target:
                print(f"0x{here:08X}  {kind} -> 0x{dest:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
