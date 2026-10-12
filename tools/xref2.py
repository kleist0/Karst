#!/usr/bin/env python3
"""Find every place in the executable that references a given absolute address.

Unlike dumpscan.py xrefs (which only matches immediates), this also reports
`mov dword ptr [mem], 0xADDR` stores, which is how a constructor writes its
vtable pointer, and any other operand carrying the constant.

Usage:
    python xref2.py 0x7E3C14
"""
import sys

import dumpscan


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    target = int(sys.argv[1], 0)
    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    hits = []
    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
        for insn in md.disasm(code, start):
            for op in insn.operands:
                if op.type == dumpscan.CS_OP_IMM and op.imm == target:
                    hits.append((insn.address, f"{insn.mnemonic} {insn.op_str}"))
                    break
                if op.type == dumpscan.CS_OP_MEM and op.mem.disp == target:
                    hits.append((insn.address, f"{insn.mnemonic} {insn.op_str}"))
                    break

    for address, text in hits:
        print(f"0x{address:08X}  {text}")
    print(f"\n{len(hits)} reference(s) to 0x{target:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
