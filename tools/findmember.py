#!/usr/bin/env python3
"""Find instructions that read or write a given struct offset.

YRpp tells us the member offsets inside gamemd.exe's classes. This script scans
the executable sections for instructions touching `[reg + offset]` so a
specific member (for example ObjectTypeClass::Image at 0x9C) can be located
without a full interactive disassembler.

Usage:
    python findmember.py 0x9C            # any access
    python findmember.py 0x9C --write    # stores only
    python findmember.py 0x9C --read     # loads only
"""

import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_MEM

import dumpscan


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    offset = int(sys.argv[2 - 1], 0)
    want_write = "--write" in sys.argv
    want_read = "--read" in sys.argv
    if not want_write and not want_read:
        want_write = want_read = True

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    hits = []
    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]

        for insn in md.disasm(code, start):
            for op in insn.operands:
                if op.type != CS_OP_MEM or op.mem.disp != offset:
                    continue

                # capstone: operand 0 is the destination for most x86 insns
                is_write = insn.operands and insn.operands[0].type == CS_OP_MEM \
                    and insn.operands[0].mem.disp == offset

                if is_write and not want_write:
                    continue
                if not is_write and not want_read:
                    continue

                kind = "W" if is_write else "R"
                hits.append((insn.address, kind, insn.mnemonic, insn.op_str))
                break

    for address, kind, mnemonic, op_str in hits:
        print(f"0x{address:08X} {kind} {mnemonic:<8} {op_str}")

    print(f"\n{len(hits)} access(es) to [reg+0x{offset:X}]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
