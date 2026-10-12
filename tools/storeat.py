#!/usr/bin/env python3
"""Report writes to a fixed offset on any register base, with context.

Answers "who creates the per-slot objects" for arrays such as
BuildingClass::Anims[] at +0x55C.

Usage:
    python storeat.py 0x55c [before] [after]
"""
import sys

from capstone import CS_OP_MEM, CS_OP_REG

import dumpscan


def mem_offset(op):
    """Return the displacement if the operand is [base + index*scale + disp]."""
    if op.type != CS_OP_MEM:
        return None
    if op.mem.base == 0 and op.mem.index == 0:
        return None
    return op.mem.disp


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    wanted = int(sys.argv[1], 0)
    before = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    after = int(sys.argv[3]) if len(sys.argv) > 3 else 2

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
        window = []
        pending = 0
        for insn in md.disasm(code, start):
            text = f"{insn.mnemonic} {insn.op_str}"
            hit = False
            if insn.operands and insn.operands[0].type == CS_OP_MEM:
                disp = mem_offset(insn.operands[0])
                if disp is not None and insn.operands[0].mem.index != 0:
                    # index*scale + disp where disp is the array base
                    if disp == wanted:
                        hit = True
            if hit:
                print("----")
                for address, line in window:
                    print(f"   0x{address:08X}  {line}")
                print(f">> 0x{insn.address:08X}  {text}")
                pending = after
                window = []
                continue
            window.append((insn.address, text))
            if pending > 0:
                print(f"   0x{insn.address:08X}  {text}")
                pending -= 1
                continue
            if len(window) > before:
                window.pop(0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
