#!/usr/bin/env python3
"""Find instructions whose memory displacement equals a value, in any operand.

`storeat.py` only looked for the base case [base + index*4 + disp]; this scans
all operands so nothing is missed.

Usage:
    python finddisp.py 0x55c
"""
import sys

from capstone import CS_OP_MEM

import dumpscan


def main():
    wanted = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x55C

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    total = 0
    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
        # iter_insns, not md.disasm: a linear sweep stops at the first
        # undecodable byte (jump tables live in .text), which silently hid every
        # later instruction from this tool.
        for insn in dumpscan.iter_insns(md, code, start):
            for op in insn.operands:
                if op.type == CS_OP_MEM and op.mem.disp == wanted:
                    print(f"0x{insn.address:08X}  {insn.mnemonic} {insn.op_str}")
                    total += 1
                    break
    print(f"\n{total} instruction(s) touching +0x{wanted:X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
