#!/usr/bin/env python3
"""Find functions matching short instruction patterns, with vtable-membership info."""

import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

import dumpscan
import rsweep


def insn_seq(pe, md, section, start, count):
    base = pe.image_base + section["vaddr"]
    code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
    i = start - base
    out = []
    while i < len(code) and len(out) < count:
        got = None
        for insn in md.disasm(code[i:i + 16], base + i, 1):
            got = insn
            break
        if got is None:
            return None
        out.append(got)
        i += got.size
    return out


def main():
    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True

    pattern = sys.argv[1].split("|")
    print(f"pattern: {pattern}")

    for section in pe.exec_sections():
        base = pe.image_base + section["vaddr"]
        size = section["raw_size"]
        for insn in rsweep.iter_insns(pe, md, section):
            if insn.mnemonic != pattern[0]:
                continue
            seq = insn_seq(pe, md, section, insn.address, len(pattern))
            if not seq:
                continue
            ok = True
            for got, want in zip(seq, pattern):
                key = f"{got.mnemonic} {got.op_str}".replace(" ", "")
                if key != want.replace(" ", ""):
                    ok = False
                    break
            if ok:
                print(f"0x{insn.address:08X}  " + " ; ".join(
                    f"{i.mnemonic} {i.op_str}" for i in seq))
    return 0


if __name__ == "__main__":
    sys.exit(main())
