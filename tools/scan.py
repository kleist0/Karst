#!/usr/bin/env python3
"""Extra helper scans for gamemd.exe static analysis.

Subcommands:
    mem <offset> [--write|--read] [--base-only] [--show-reg]
        Scan for [reg+offset] accesses with correct read/write classification.
    xrefall <VA> [--push] [--max N]
        All instructions referencing an absolute VA (calls, pushes, movs).
    strfind <text> [--max N]
        File offsets + VAs of ASCII occurrences.
    dump <VA> <size>
        Hex dump.
    u32 <VA>
        Read a dword.

Uses dumpscan.Pe for PE parsing.
"""

import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_IMM, CS_OP_MEM, CS_OP_REG
from capstone.x86 import X86_INS_LEA, X86_INS_CALL, X86_INS_JMP, X86_INS_PUSH

import dumpscan


def iter_code(pe, md):
    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
        for insn in md.disasm(code, start):
            yield insn


def _is_struct_mem(insn, op):
    """Memory operand [reg+disp] (optionally +index*scale) with a non-stack base."""
    if op.type != CS_OP_MEM:
        return False
    if op.mem.base == 0:
        return False
    # skip stack frames: [esp+off] / [ebp+off] are locals, not struct members
    if is_stack_reg(insn, op.mem.base):
        return False
    return True


def is_stack_reg(insn, reg_id):
    name = insn.reg_name(reg_id)
    return name in ("esp", "ebp", "sp", "bp")


def is_write_to(insn, off):
    """True if operand 0 is a memory write to [reg+off]."""
    if not insn.operands:
        return False
    if insn.id in (X86_INS_LEA, X86_INS_CALL, X86_INS_JMP):
        return False
    op0 = insn.operands[0]
    if op0.type != CS_OP_MEM or op0.mem.disp != off:
        return False
    return _is_struct_mem(insn, op0)


def is_read_of(insn, off):
    if not insn.operands:
        return False
    if insn.id == X86_INS_LEA:
        return any(o.type == CS_OP_MEM and o.mem.disp == off and _is_struct_mem(insn, o)
                   for o in insn.operands)
    if insn.id in (X86_INS_CALL, X86_INS_JMP):
        return False  # indirect [reg+off] call is NOT a memory read
    op0 = insn.operands[0]
    if op0.type == CS_OP_MEM and op0.mem.disp == off:
        return False
    return any(o.type == CS_OP_MEM and o.mem.disp == off and _is_struct_mem(insn, o)
               for o in insn.operands[1:])


def cmd_mem(pe, md, offset, want_write, want_read):
    rows = []
    for insn in iter_code(pe, md):
        w = is_write_to(insn, offset)
        r = is_read_of(insn, offset)
        if w and not want_write:
            continue
        if r and not want_read:
            continue
        if not (w or r):
            continue
        kind = "W" if w else "R"
        rows.append(f"0x{insn.address:08X} {kind} {insn.mnemonic:<8} {insn.op_str}")
    for row in rows:
        print(row)
    print(f"\n{len(rows)} access(es) to [reg+0x{offset:X}]")


def cmd_xrefall(pe, md, target, only_push, maxn):
    hits = []
    for insn in iter_code(pe, md):
        for op in insn.operands:
            if op.type == CS_OP_IMM and op.imm == target:
                if only_push and insn.id != X86_INS_PUSH:
                    break
                hits.append(f"0x{insn.address:08X}  {insn.mnemonic:<8} {insn.op_str}")
                break
    for h in hits[:maxn]:
        print(h)
    print(f"\n{len(hits)} reference(s) to 0x{target:08X}")


def cmd_strfind(pe, text, maxn):
    needle = text.encode("ascii")
    count = 0
    start = 0
    while True:
        index = pe.data.find(needle, start)
        if index < 0:
            break
        va = pe.off_to_va(index)
        nxt = pe.data[index + len(needle):index + len(needle) + 1]
        print(f"file 0x{index:08X}  va {('0x%08X' % va) if va else 'n/a'}  term={nxt!r}")
        count += 1
        if count >= maxn:
            break
        start = index + 1
    print(f"\n{count} occurrence(s)")


def cmd_dump(pe, va, size):
    off = pe.va_to_off(va)
    data = pe.read(va, size)
    if data is None:
        print(f"0x{va:08X} unmapped")
        return
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hexs = " ".join(f"{b:02X}" for b in chunk)
        print(f"0x{va + i:08X}  {hexs:<48}")


def main():
    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()
    cmd = sys.argv[1]
    if cmd == "mem":
        off = int(sys.argv[2], 0)
        w = "--write" in sys.argv or "--read" not in sys.argv
        r = "--read" in sys.argv or "--write" not in sys.argv
        cmd_mem(pe, md, off, w, r)
    elif cmd == "xrefall":
        target = int(sys.argv[2], 0)
        if target < pe.image_base:
            target += pe.image_base
        maxn = 500
        if "--max" in sys.argv:
            maxn = int(sys.argv[sys.argv.index("--max") + 1], 0)
        cmd_xrefall(pe, md, target, "--push" in sys.argv, maxn)
    elif cmd == "strfind":
        maxn = 40
        if "--max" in sys.argv:
            maxn = int(sys.argv[sys.argv.index("--max") + 1], 0)
        cmd_strfind(pe, sys.argv[2], maxn)
    elif cmd == "dump":
        cmd_dump(pe, int(sys.argv[2], 0), int(sys.argv[3], 0))
    elif cmd == "u32":
        va = int(sys.argv[2], 0)
        print(f"0x{pe.u32(pe.va_to_off(va)):08X}  (at 0x{va:08X})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
