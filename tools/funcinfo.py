#!/usr/bin/env python3
"""Derive function boundaries around an address, then report the entry point.

Walks backwards from `va` to the nearest byte the disassembler can start a
fresh instruction stream at (a `ret`/`ret imm`/`int3` run followed by a
non-padding byte), and forwards to the terminating `ret`.

Usage:
    python funcinfo.py 0x450CB2
"""
import sys

from capstone import CS_OP_IMM

import dumpscan


def main():
    va = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x450CB2

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    print("--- 200 bytes before ---")
    raw = pe.read(va - 200, 200)
    for insn in md.disasm(raw, va - 200):
        print(f"0x{insn.address:08X}  {insn.mnemonic} {insn.op_str}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
