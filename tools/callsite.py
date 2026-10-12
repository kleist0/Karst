#!/usr/bin/env python3
"""Report instruction sites that call/read a vtable slot, with context.

Useful for finding "who draws an ObjectClass": the pattern is

    mov eax, [ecx]            ; vtable
    call dword ptr [eax+0x8c] ; -> GetType()
    ...
    call dword ptr [edx+0x70] ; -> GetImage()

Usage:
    python callsite.py 'call dword ptr \[e?? \+ 0x70\]' [before] [after]
"""
import re
import sys

import dumpscan


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    pattern = sys.argv[1]
    before = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    after = int(sys.argv[3]) if len(sys.argv) > 3 else 2
    regex = re.compile(pattern)

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
        window = []
        for insn in md.disasm(code, start):
            text = f"{insn.mnemonic} {insn.op_str}".strip()
            window.append((insn.address, text))
            if len(window) > before + after + 1:
                window.pop(0)
            if regex.search(text):
                print("----")
                for address, line in window:
                    marker = ">>" if address == insn.address else "  "
                    print(f"{marker} 0x{address:08X}  {line}")
                window = window[-(after + 1):] if after else []
    return 0


if __name__ == "__main__":
    sys.exit(main())
