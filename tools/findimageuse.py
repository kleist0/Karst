#!/usr/bin/env python3
"""Find code that reads ObjectTypeClass::Image through BuildingClass::Type.

The interesting access pattern for "which SHP is this building drawn with" is a
two-step load:

    mov reg, [obj + BuildingClass::Type]      ; Type at +0x520
    ...
    mov reg, [reg + ObjectTypeClass::Image]   ; Image at +0x9C

This script reports every `[reg+0x9C]` read and, when it can, resolves whether a
`[reg+0x520]` load happened shortly before, which marks the site as
building-specific.

Usage:
    python findimageuse.py            # all Image reads, annotated
    python findimageuse.py --building # only likely building sites
"""

import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_MEM, CS_OP_REG

import dumpscan

TYPE_OFFSET = 0x520
IMAGE_OFFSET = 0x9C


def main():
    want_building = "--building" in sys.argv

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    results = []

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]

        # Track, per instruction index, which registers most recently received a
        # load from [reg+0x520] (i.e. hold a BuildingTypeClass*).
        window = []
        type_regs = {}

        for insn in md.disasm(code, start):
            window.append(insn)

            # Remember `mov dst, [src + 0x520]`
            if len(insn.operands) == 2:
                dst, src = insn.operands
                if (dst.type == CS_OP_REG and src.type == CS_OP_MEM
                        and src.mem.disp == TYPE_OFFSET):
                    type_regs[dst.reg] = insn.address

                # Any other write to that register invalidates it.
                elif dst.type == CS_OP_REG and dst.reg in type_regs:
                    del type_regs[dst.reg]

            # Report `mov dst, [src + 0x9C]`
            for index, op in enumerate(insn.operands):
                if op.type != CS_OP_MEM or op.mem.disp != IMAGE_OFFSET:
                    continue

                is_dest = (index == 0 and insn.operands[0].type == CS_OP_MEM
                           and insn.operands[0].mem.disp == IMAGE_OFFSET)
                if is_dest:
                    continue  # a store, not a read of the image

                base_reg = op.mem.base
                via_type = type_regs.get(base_reg)
                results.append((insn.address, insn.mnemonic, insn.op_str,
                                base_reg, via_type))

    shown = 0
    for address, mnemonic, op_str, base_reg, via_type in results:
        if want_building and via_type is None:
            continue

        if via_type is not None:
            note = f"  <-- via Type loaded at 0x{via_type:08X}"
        else:
            note = ""

        print(f"0x{address:08X}  {mnemonic:<8} {op_str}{note}")
        shown += 1

    print(f"\n{shown} Image read(s) shown out of {len(results)} total")
    return 0


if __name__ == "__main__":
    sys.exit(main())
