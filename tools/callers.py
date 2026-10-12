#!/usr/bin/env python3
"""Report every caller of one function, with an instruction window.

Usage:
    python callers.py 0x451890 [before] [after]
"""
import sys

from capstone import CS_OP_IMM

import dumpscan


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    target = int(sys.argv[1], 0)
    before = int(sys.argv[2]) if len(sys.argv) > 2 else 12
    after = int(sys.argv[3]) if len(sys.argv) > 3 else 2

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
        window = []
        pending = None
        for insn in md.disasm(code, start):
            text = f"{insn.mnemonic} {insn.op_str}"
            is_call = insn.mnemonic == "call" and insn.operands
            if is_call:
                op = insn.operands[0]
                if op.type == CS_OP_IMM and op.imm == target:
                    print("----")
                    for address, line in window:
                        print(f"   0x{address:08X}  {line}")
                    print(f">> 0x{insn.address:08X}  {text}")
                    pending = after
                    window = []
                    continue
            window.append((insn.address, text))
            if pending is not None:
                print(f"   0x{insn.address:08X}  {text}")
                pending -= 1
                if pending <= 0:
                    pending = None
                continue
            if len(window) > before:
                window.pop(0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
