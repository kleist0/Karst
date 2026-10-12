#!/usr/bin/env python3
"""For each call site, decide whether it is reachable only when the building
instance already holds an animation in that slot.

Heuristic: look back for `cmp dword ptr [reg(+index*4) + 0x55C], 0` followed by
`je <past the call>`. That is the pattern the engine uses in its per-slot
rebuild loops.

Usage:
    python guard2.py 0x451890
"""
import re
import sys

from capstone import CS_OP_IMM, CS_OP_MEM

import dumpscan

ANIMS = 0x55C


def main():
    target = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x451890

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    guarded = 0
    unguarded = 0

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
        recent = []
        for insn in md.disasm(code, start):
            text = f"{insn.mnemonic} {insn.op_str}"
            if (insn.mnemonic == "call" and insn.operands
                    and insn.operands[0].type == CS_OP_IMM
                    and insn.operands[0].imm == target):
                flagged = False
                why = ""
                for addr, mnem, optext, ops in recent:
                    if mnem in ("cmp", "test") and ops:
                        op = ops[0]
                        if op.type == CS_OP_MEM and ANIMS <= op.mem.disp < ANIMS + 0x54:
                            # slot guard read
                            flagged = True
                            why = f"{mnem} {optext} @0x{addr:08X}"
                            break
                if flagged:
                    guarded += 1
                    print(f"GUARDED   0x{insn.address:08X}  ({why})")
                else:
                    unguarded += 1
                    print(f"unguarded 0x{insn.address:08X}")
            recent.append((insn.address, insn.mnemonic, text, insn.operands))
            if len(recent) > 24:
                recent.pop(0)

    print(f"\nguarded={guarded} unguarded={unguarded}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
