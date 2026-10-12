#!/usr/bin/env python3
"""Find trivial getters: functions that load one member and return it.

This identifies a slot by its shape rather than by guessing vtable indices.
ObjectTypeClass::GetImage in every type class is `mov eax, [ecx+0x9c]; ret`
(or a near variant), so scanning for that shape finds every override at once.

Usage:
    python getters.py 0x9c        # 'mov eax, [ecx+0x9c]' style getters
    python getters.py 0x9c 0xa4
"""
import sys

from capstone import CS_OP_IMM, CS_OP_MEM, CS_OP_REG

import dumpscan


def main():
    offsets = [int(a, 0) for a in sys.argv[1:]] or [0x9C]
    wanted = set(offsets)

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    print(f"scanning for single-member getters at {[hex(o) for o in offsets]}")
    print()

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]

        # Decode straight through; look at each instruction as a candidate
        # function head, requiring the *next* instruction to be a ret.
        insns = list(md.disasm(code, start))
        for index, insn in enumerate(insns):
            if len(insn.operands) != 2:
                continue
            dst, src = insn.operands
            if dst.type != CS_OP_REG or src.type != CS_OP_MEM:
                continue
            if src.mem.disp not in wanted:
                continue
            if src.mem.base != 0x9:   # ecx
                continue

            # Require a following `ret` (allowing one intervening instruction,
            # e.g. MSVC's `mov esp,ebp` prologue/epilogue noise).
            tail = insns[index + 1:index + 3]
            if not any(i.mnemonic == "ret" for i in tail):
                continue

            print(f"0x{insn.address:08X}  {insn.mnemonic} {insn.op_str}"
                  f"   ; member +0x{src.mem.disp:02X}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
