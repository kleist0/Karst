#!/usr/bin/env python3
"""Map ART-parser boolean keys to the type offsets they are stored at.

The parser at 0x4609xx has the shape

    mov  dl, [ebp + OFFSET]      ; previous value (the Boolean's default)
    push edx
    push 0x81AAnnn               ; key name
    push ebx                     ; section
    call 0x5295f0                ; CCINIClass::ReadBool
    mov  [ebp + OFFSET], al

so walking it yields "key name -> offset" directly.

Usage:
    python artkeys.py [start] [end]
"""
import re
import sys

import dumpscan


def main():
    start = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x460900
    end = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x460C00

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    raw = pe.read(start, end - start)
    insns = list(md.disasm(raw, start))

    pending_key = None
    for insn in insns:
        text = f"{insn.mnemonic} {insn.op_str}"
        m = re.match(r"push (0x8[0-9a-f]{5})$", text)
        if m:
            pending_key = int(m.group(1), 16)
            continue
        m = re.match(r"mov byte ptr \[e[a-z]{2} \+ (0x[0-9a-f]+)\], al$", text)
        if m:
            offset = int(m.group(1), 16)
            name = "?"
            if pending_key:
                blob = pe.read(pending_key, 32)
                if blob:
                    name = blob.split(b"\0")[0].decode("latin-1")
            print(f"0x{insn.address:08X}  +0x{offset:04X}  <- '{name}'")
            pending_key = None
    return 0


if __name__ == "__main__":
    sys.exit(main())
