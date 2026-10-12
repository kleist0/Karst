#!/usr/bin/env python3
"""Detect direct calls to a function, showing the vtable for each call site.

Use `vtslots.py`/`xref2.py` for retired tools; this one exists because the
"who decides the BuildingAnimSlot" question needs a full, systematic table of
call sites rather than a hexdig.

Usage:
    python dis_calls.py 0x451890 [max_insns]
"""
import re
import sys

import dumpscan


def main():
    target = int(sys.argv[1], 0)
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 30

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
        window = []
        for insn in md.disasm(code, start):
            text = f"{insn.mnemonic} {insn.op_str}"
            window.append((insn.address, text))
            if len(window) > limit:
                window.pop(0)
            m = re.match(r"call (0x[0-9a-f]+)$", text)
            if m and int(m.group(1), 16) == target:
                for address, line in window:
                    mark = ">>" if address == insn.address else "  "
                    print(f"{mark} 0x{address:08X}  {line}")
                print("----")
                window = []
    return 0


if __name__ == "__main__":
    sys.exit(main())
