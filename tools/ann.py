#!/usr/bin/env python3
"""Disassembly helpers with string annotations and call-argument context.

Subcommands:
    func <VA>            Walk backwards to the function prologue, then print
                         annotated disassembly to the next `ret`.
    around <VA> <before> <after>
                         Annotated disassembly window.
    callers <VA>         All direct call sites of VA, each shown with the
                         preceding 12 instructions (argument setup).
    writes <off>         Instructions writing [reg+off] with context.
"""

import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_IMM, CS_OP_MEM, CS_OP_REG
from capstone.x86 import X86_INS_CALL, X86_INS_LEA, X86_INS_JMP, X86_INS_RET

import dumpscan
import scan


def annotate(pe, op_imm):
    """If the immediate looks like a VA, try to describe the pointed-to data."""
    if op_imm < pe.image_base:
        return ""
    data = pe.read(op_imm, 64)
    if not data:
        return ""
    printable = []
    for b in data:
        if b == 0:
            break
        if 32 <= b < 127:
            printable.append(chr(b))
        else:
            printable = None
            break
    if printable and len(printable) >= 3:
        return f'   ; "{ "".join(printable) }"'
    return ""


def fmt(pe, insn):
    line = f"0x{insn.address:08X}  {insn.mnemonic:<8} {insn.op_str}"
    note = ""
    if insn.id == X86_INS_CALL and insn.operands and insn.operands[0].type == CS_OP_IMM:
        target = insn.operands[0].imm
        note = f"   ; -> sub_{target:08X}"
    for op in insn.operands:
        if op.type == CS_OP_MEM and op.mem.disp >= 0x7E0000 and op.mem.base == 0:
            a = annotate(pe, op.mem.disp)
            if a:
                note += a
                break
        if op.type == CS_OP_IMM:
            a = annotate(pe, op.imm)
            if a:
                note += a
                break
    return line + note


PROLOGUES = ("push ebp", "push ebx", "push esi", "push edi", "sub esp")


def collect(pe, md, section, start_va, stop_va):
    code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
    base = pe.image_base + section["vaddr"]
    out = []
    for insn in md.disasm(code, base):
        if insn.address < start_va:
            continue
        if insn.address >= stop_va:
            break
        out.append(insn)
    return out


def cmd_context(pe, md, va, before, after):
    section = None
    for s in pe.exec_sections():
        base = pe.image_base + s["vaddr"]
        if base <= va < base + max(s["vsize"], s["raw_size"]):
            section = s
            break
    if section is None:
        print("not in an executable section")
        return
    # linear-sweep from a bit before; capstone may desync but we filter by addr
    win_start = max(pe.image_base + section["vaddr"], va - before)
    insns = collect(pe, md, section, win_start, va + after)
    # linear sweep may have started mid-instruction; re-disassemble from a
    # conservative boundary by scanning raw bytes forward
    for insn in insns:
        print(fmt(pe, insn))


def find_prologue(pe, md, va):
    section = None
    for s in pe.exec_sections():
        base = pe.image_base + s["vaddr"]
        if base <= va < base + max(s["vsize"], s["raw_size"]):
            section = s
            break
    base = pe.image_base + section["vaddr"]
    candidates = []
    for start in range(base, va):
        if start & 0xFFF:
            continue
        insns = collect(pe, md, section, start, va + 1)
        if any(i.address == va for i in insns):
            candidates.append(start)
    return candidates[-1] if candidates else base


def cmd_func(pe, md, va):
    start = find_prologue(pe, md, va)
    print(f"; function containing 0x{va:08X} starts near 0x{start:08X}")
    section = None
    for s in pe.exec_sections():
        base = pe.image_base + s["vaddr"]
        if base <= va < base + max(s["vsize"], s["raw_size"]):
            section = s
            break
    insns = collect(pe, md, section, start, start + 0x2000)
    for insn in insns:
        print(fmt(pe, insn))
        if insn.id == X86_INS_RET and insn.address >= va:
            break


def cmd_callers(pe, md, target, ctx):
    for insn in scan.iter_code(pe, md):
        if insn.id != X86_INS_CALL:
            continue
        if not insn.operands or insn.operands[0].type != CS_OP_IMM:
            continue
        if insn.operands[0].imm != target:
            continue
        print(f"--- caller at 0x{insn.address:08X} ---")
        cmd_context(pe, md, insn.address, ctx, 4)
        print()


def main():
    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()
    cmd = sys.argv[1]
    if cmd == "func":
        cmd_func(pe, md, int(sys.argv[2], 0))
    elif cmd == "around":
        cmd_context(pe, md, int(sys.argv[2], 0), int(sys.argv[3], 0), int(sys.argv[4], 0))
    elif cmd == "callers":
        target = int(sys.argv[2], 0)
        ctx = int(sys.argv[3], 0) if len(sys.argv) > 3 else 10
        cmd_callers(pe, md, target, ctx)
    return 0


if __name__ == "__main__":
    sys.exit(main())
