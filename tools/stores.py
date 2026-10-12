#!/usr/bin/env python3
"""Resync-robust scans for `mov [reg+off], src` showing the source register.

Also can filter to a VA range to cut noise.
"""

import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

import dumpscan
import rsweep


def main():
    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True

    off = int(sys.argv[1], 0)
    lo = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0
    hi = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x10000000

    rows = []
    for insn in rsweep.all_insns(pe, md):
        if not (lo <= insn.address < hi):
            continue
        if insn.mnemonic != "mov" or len(insn.operands) != 2:
            continue
        dst, src = insn.operands
        from capstone import CS_OP_MEM, CS_OP_REG, CS_OP_IMM
        if dst.type != CS_OP_MEM or dst.mem.disp != off:
            continue
        if insn.reg_name(dst.mem.base) in ("esp", "ebp"):
            continue
        if src.type == CS_OP_REG:
            note = f"src={insn.reg_name(src.reg)}"
        elif src.type == CS_OP_IMM:
            note = f"src=imm 0x{src.imm:X}"
        elif src.type == CS_OP_MEM:
            note = f"src=[{insn.reg_name(src.mem.base)}+0x{src.mem.disp:X}]"
        else:
            note = "src=?"
        rows.append(f"0x{insn.address:08X}  {insn.mnemonic} {insn.op_str:<40} {note}")
    for r in rows:
        print(r)
    print(f"\n{len(rows)} store(s) to [reg+0x{off:X}]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
