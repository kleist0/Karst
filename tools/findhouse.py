#!/usr/bin/env python3
"""Locate the HouseClass constructor by its COM interface initialisation.

HouseClass embeds three COM interfaces whose vtable pointers are stored at
+0x24, +0x28 and +0x2C (seen at 0x4F7164 in gamemd.exe). A function that writes
all three consecutive slots is initialising a HouseClass (or a class embedding
the same trio), and the constructor is the most useful one: it receives the
country as an argument, so whichever member offset it stores that argument into
is the real country pointer offset.

Usage:
    python findhouse.py
    python findhouse.py --context 0x4F7000   # disassemble around a hit
"""

import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_IMM, CS_OP_MEM, CS_OP_REG

import dumpscan

TRIPLE = (0x24, 0x28, 0x2C)
WINDOW = 24          # instructions to look ahead for the remaining writes


def main():
    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    hits = []

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]

        pending = []   # (address, base_reg, offsets_written)

        for insn in md.disasm(code, start):
            written = None
            base = None

            # A store to [reg+disp] has the memory operand first.
            if (len(insn.operands) >= 1 and insn.operands[0].type == CS_OP_MEM
                    and insn.mnemonic.startswith("mov")):
                written = insn.operands[0].mem.disp
                base = insn.operands[0].mem.base

            if written in TRIPLE and base:
                # Start or extend a run for this register.
                for entry in pending:
                    if entry[1] == base:
                        entry[2].add(written)
                        if entry[2] >= set(TRIPLE):
                            hits.append((entry[0], base, sorted(entry[2])))
                            pending.remove(entry)
                        break
                else:
                    pending.append([insn.address, base, {written}])

            # Keep the pending list short so stale registers do not accumulate.
            if len(pending) > 8:
                pending.pop(0)

    for address, base, offsets in hits:
        print(f"0x{address:08X}  base={base}  wrote {[hex(o) for o in offsets]}")

    print(f"\n{len(hits)} triple-write site(s)")

    if "--context" in sys.argv:
        target = int(sys.argv[sys.argv.index("--context") + 1], 0)
        print()
        dumpscan.cmd_disasm(pe, target, 40)

    return 0


if __name__ == "__main__":
    sys.exit(main())
