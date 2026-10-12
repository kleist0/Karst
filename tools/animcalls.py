#!/usr/bin/env python3
"""Systematically tabulate call sites of the building animation creator.

For each `call 0x451890` this reports the four pushed arguments (in particular
arg #3 = the BuildingAnimSlot) and the nearest preceding conditional test.

The slot decides everything for this project: ActiveAnim is slot 3, and the
question is which condition makes the engine ask for slot 3.

Usage:
    python animcalls.py 0x451890
"""
import sys

from capstone import CS_OP_IMM

import dumpscan


def main():
    target = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x451890

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    rows = []

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
        recent = []
        for insn in md.disasm(code, start):
            text = f"{insn.mnemonic} {insn.op_str}"
            if (insn.mnemonic == "call" and insn.operands
                    and insn.operands[0].type == CS_OP_IMM
                    and insn.operands[0].imm == target):
                delta = 0
                args = []
                guard = None
                for addr, mnem, optext in reversed(recent):
                    if mnem == "push":
                        delta += 4
                        args.append(optext)
                    elif mnem == "add" and optext.startswith("esp,"):
                        delta -= int(optext.split(",")[1].strip(), 0)
                    elif mnem == "sub" and optext.startswith("esp,"):
                        delta += int(optext.split(",")[1].strip(), 0)
                    elif mnem in ("cmp", "test") and guard is None:
                        guard = f"{mnem} {optext}"
                    if delta >= 20:
                        break
                args = list(reversed(args))
                rows.append((insn.address, guard, args))
            recent.append((insn.address, insn.mnemonic, text))
            if len(recent) > 60:
                recent.pop(0)

    for address, guard, args in rows:
        print(f"0x{address:08X}  args={' | '.join(args)}")
        print(f"            guard: {guard}")
    print(f"\n{len(rows)} call site(s) of 0x{target:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
