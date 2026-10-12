#!/usr/bin/env python3
"""Resync-robust instruction scan.

The plain linear sweep used by dumpscan.describe_operands desyncs when the
section has embedded data or a different code origin. This variant decodes
instruction by instruction and, whenever decoding fails, skips one byte and
tries again, so embedded data cannot poison the following instructions.
"""

import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_IMM, CS_OP_MEM
from capstone.x86 import X86_INS_CALL, X86_INS_JMP, X86_INS_LEA

import dumpscan


def make_md():
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    return md


def iter_insns(pe, md, section):
    base = pe.image_base + section["vaddr"]
    code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
    i = 0
    n = len(code)
    while i < n:
        got = None
        for insn in md.disasm(code[i:i + 16], base + i, 1):
            got = insn
            break
        if got is None:
            i += 1
            continue
        yield got
        i += got.size


def all_insns(pe, md):
    for section in pe.exec_sections():
        for insn in iter_insns(pe, md, section):
            yield insn


def main():
    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = make_md()
    mode = sys.argv[1]
    if mode == "writes":
        off = int(sys.argv[2], 0)
        for insn in all_insns(pe, md):
            if not insn.operands:
                continue
            if insn.id in (X86_INS_LEA, X86_INS_CALL, X86_INS_JMP):
                continue
            op0 = insn.operands[0]
            if op0.type != CS_OP_MEM or op0.mem.disp != off:
                continue
            if op0.mem.base == 0:
                continue
            if insn.reg_name(op0.mem.base) in ("esp", "ebp"):
                continue
            print(f"0x{insn.address:08X}  {insn.mnemonic:<8} {insn.op_str}")
    elif mode == "disasm":
        va = int(sys.argv[2], 0)
        count = int(sys.argv[3], 0) if len(sys.argv) > 3 else 40
        for section in pe.exec_sections():
            base = pe.image_base + section["vaddr"]
            if base <= va < base + max(section["vsize"], section["raw_size"]):
                for insn in iter_insns(pe, md, section):
                    if insn.address < va:
                        continue
                    print(f"0x{insn.address:08X}  {insn.mnemonic:<8} {insn.op_str}")
                    count -= 1
                    if count <= 0:
                        return 0
                break
    elif mode == "xref":
        target = int(sys.argv[2], 0)
        for insn in all_insns(pe, md):
            for op in insn.operands:
                if op.type == CS_OP_IMM and op.imm == target:
                    print(f"0x{insn.address:08X}  {insn.mnemonic:<8} {insn.op_str}")
                    break
    elif mode == "reads":
        off = int(sys.argv[2], 0)
        for insn in all_insns(pe, md):
            if not insn.operands:
                continue
            touch = False
            for idx, op in enumerate(insn.operands):
                if op.type != CS_OP_MEM or op.mem.disp != off or op.mem.base == 0:
                    continue
                if insn.reg_name(op.mem.base) in ("esp", "ebp"):
                    continue
                if idx == 0 and insn.id not in (X86_INS_LEA, X86_INS_CALL, X86_INS_JMP):
                    continue
                touch = True
            if touch:
                print(f"0x{insn.address:08X}  {insn.mnemonic:<8} {insn.op_str}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
