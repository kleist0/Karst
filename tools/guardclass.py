#!/usr/bin/env python3
"""Classify call sites of the building anim creator by their slot guard.

For each `call 0x451890` the code around it loads the animation NAME from
`Type + 0xF4C + slot*0x44 (+0x10/+0x20)`. Two families exist:

  * guarded   - the creator is only reached when the building INSTANCE already
                has an object in that slot (`[building + 0x55C + slot*4] != 0`),
                i.e. it rebuilds an existing animation;
  * unguarded - the only requirement is a non-empty name on the type, so the
                creator can establish the object.

Distinguishing them is the whole question for this project.

Usage:
    python guardclass.py 0x451890
"""
import re
import sys

from capstone import CS_OP_IMM

import dumpscan

# Type anim-name base (measured): entry stride 0x44, name at entry+0.
TYPE_ANIM_BASE = 0xF4C
STRIDE = 0x44


def slot_of(disp):
    delta = disp - TYPE_ANIM_BASE
    if delta < 0:
        return None
    slot, rem = divmod(delta, STRIDE)
    if slot > 0x14:
        return None
    return slot, rem


def main():
    target = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x451890

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
        recent = []
        for insn in md.disasm(code, start):
            text = f"{insn.mnemonic} {insn.op_str}"
            if (insn.mnemonic == "call" and insn.operands
                    and insn.operands[0].type == CS_OP_IMM
                    and insn.operands[0].imm == target):
                slots = []
                inst_guard = []
                for addr, mnem, optext in recent:
                    m = re.search(r"\[e[a-z]{2} \+ e[a-z]{2}\*4 \+ (0x[0-9a-f]+)\]",
                                  optext)
                    if m:
                        disp = int(m.group(1), 16)
                        if 0x55C <= disp < 0x55C + 0x15 * 4:
                            inst_guard.append((addr, (disp - 0x55C) // 4, text))
                    if mnem in ("add", "lea"):
                        mm = re.search(r"0x([0-9a-f]+)$", optext)
                        if mm:
                            found = slot_of(int(mm.group(1), 16))
                            if found:
                                slots.append(found)
                slot_txt = ",".join(f"s{s}" for s, r in slots) or "?"
                guard_txt = ",".join(f"s{s}@{a:08X}" for a, s, t in inst_guard) or "none"
                print(f"0x{insn.address:08X}  name-slot={slot_txt:20s} "
                      f"instance-guard={guard_txt}")
            recent.append((insn.address, insn.mnemonic, text))
            if len(recent) > 30:
                recent.pop(0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
