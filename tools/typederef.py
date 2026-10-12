#!/usr/bin/env python3
"""Track which register holds a BuildingClass::Type pointer, then report reads
of a type member through it.

This is the analysis that answers "what does the draw path read off the type":
the engine stores `obj->Type` (BuildingClass +0x520) in a register, then loads a
member such as ObjectTypeClass::Image (+0x9C) or AlphaImage (+0xA4).

The tracker is deliberately conservative: any write to a register clears its
recorded provenance, so a hit means the same register provably received the
value from a [reg+0x520] load with no intervening write.

Usage:
    python typederef.py                 # Image (+0x9C) and AlphaImage (+0xA4)
    python typederef.py 0x9c 0xa4 0x1f8
"""
import sys

from capstone import CS_OP_MEM, CS_OP_REG

import dumpscan

TYPE_OFFSET = 0x520


def main():
    wanted = [int(a, 0) for a in sys.argv[1:]] or [0x9C, 0xA4]
    wanted_set = set(wanted)

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    print(f"BuildingClass::Type at +0x{TYPE_OFFSET:X}; "
          f"members {[hex(w) for w in wanted]}")
    print()

    total = 0
    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]

        type_regs = {}

        for insn in md.disasm(code, start):
            ops = insn.operands

            # A hit: a tracked register used as the base of a wanted member.
            for index, op in enumerate(ops):
                if op.type != CS_OP_MEM or op.mem.disp not in wanted_set:
                    continue
                if op.mem.base == 0:
                    continue
                origin = type_regs.get(op.mem.base)
                if origin is None:
                    continue
                is_store = (index == 0 and ops[0].type == CS_OP_MEM)
                kind = "WRITE" if is_store else "read "
                print(f"0x{insn.address:08X}  {kind} {insn.mnemonic} {insn.op_str}"
                      f"   <-- Type loaded at 0x{origin:08X}")
                total += 1

            # Update provenance after reporting, so `mov eax,[eax+0x520]` counts.
            if len(ops) == 2 and ops[0].type == CS_OP_REG and ops[1].type == CS_OP_MEM:
                if ops[1].mem.disp == TYPE_OFFSET:
                    type_regs[ops[0].reg] = insn.address
                    continue

            # Any other write to a register destroys its provenance.
            if ops and ops[0].type == CS_OP_REG:
                type_regs.pop(ops[0].reg, None)

    print(f"\n{total} hit(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
