#!/usr/bin/env python3
"""Report every `call dword ptr [reg + <slot>]` in the executable, grouped by slot.

Used to identify which vtable slot is the image getter: the interesting callers
load a BuildingTypeClass* (from BuildingClass +0x520) and then invoke slot X.
"""
import collections
import re
import sys

import dumpscan


def main():
    slots = [int(a, 0) for a in sys.argv[1:]] or [0x6C, 0x9C, 0xC0]
    wanted = set(slots)

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    by_slot = collections.defaultdict(list)

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
        for insn in md.disasm(code, start):
            text = f"{insn.mnemonic} {insn.op_str}"
            m = re.match(r"call dword ptr \[e[a-z]{2} \+ (0x[0-9a-f]+)\]$", text)
            if not m:
                continue
            slot = int(m.group(1), 16)
            if slot in wanted:
                by_slot[slot].append(insn.address)

    for slot in sorted(by_slot):
        addrs = by_slot[slot]
        print(f"slot +0x{slot:02X} (index {slot // 4}): {len(addrs)} call site(s)")
        for a in addrs:
            print(f"    0x{a:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
