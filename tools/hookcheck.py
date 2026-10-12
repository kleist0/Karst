#!/usr/bin/env python3
"""Verify that every hook declared in Karst.dll is safe to install.

Two rules, both learned from a crash:

1. `size` must land on an instruction boundary.
   A DEFINE_HOOK whose size stops mid-instruction makes Syringe resume at a
   bogus address. That crashed the game with C0000005 at 0x00451898 (hook
   0x4509DE was declared as 2 bytes, but `lea edi, [esi+0x100]` is 3).

2. `size` must be at least 5 bytes.
   Syringe overwrites the hook site with a 5-byte `jmp rel32`. The original
   bytes it displaced are replayed from a trampoline, so if `size` < 5 the jmp
   also destroys bytes belonging to the NEXT instruction. The replay then runs a
   corrupted instruction. That crashed the game with C0000005 at 0x007CE9F6:
   a 3-byte hook at 0x5350D3 (`lea esi,[ebx+1]`) swallowed the first 2 bytes of
   the following `lea eax,[esp+0x14]`, and sprintf got garbage stack arguments.

   Rule 1 alone does NOT catch this - "3 bytes, and 3 is a boundary" passes it.

Usage:
    python hookcheck.py Karst\\Release\\Karst.dll
"""
import os
import struct
import sys

import dumpscan

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Length of `jmp rel32`, which is what Syringe writes at the hook site.
MIN_HOOK_SIZE = 5


def read_hooks(path):
    with open(path, "rb") as handle:
        data = handle.read()

    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e_lfanew + 4
    num_sections = struct.unpack_from("<H", data, coff + 2)[0]
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    image_base = struct.unpack_from("<I", data, opt + 28)[0]

    sections = []
    sec = opt + opt_size
    for _ in range(num_sections):
        name = data[sec:sec + 8].rstrip(b"\0").decode("ascii", "replace")
        virt_size, virt_addr = struct.unpack_from("<II", data, sec + 8)
        raw_size, raw_ptr = struct.unpack_from("<II", data, sec + 16)
        sections.append((name, image_base + virt_addr, virt_size,
                         raw_ptr, raw_size))
        sec += 40

    def read_string(va):
        for _name, start, vsize, raw_ptr, raw_size in sections:
            if start <= va < start + max(vsize, raw_size):
                idx = raw_ptr + (va - start)
                return data[idx:idx + 64].split(b"\0")[0].decode("ascii", "replace")
        return "?"

    hooks = []
    for name, start, vsize, raw_ptr, raw_size in sections:
        if not name.startswith(".syhks"):
            continue
        blob = data[raw_ptr:raw_ptr + raw_size]
        for off in range(0, len(blob) - 12, 16):
            addr, size, nameptr = struct.unpack_from("<III", blob, off)
            if addr:
                hooks.append((addr, size, read_string(nameptr)))
    return hooks


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else r"Karst\Release\Karst.dll"

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    hooks = read_hooks(path)
    bad = 0

    for addr, size, name in sorted(hooks):
        raw = pe.read(addr, 32)
        covered = 0
        boundaries = []

        # iter_insns, not md.disasm: the latter stops at the first undecodable
        # byte, which would make a valid hook look like it covers nothing.
        for insn in dumpscan.iter_insns(md, raw, addr):
            covered += insn.size
            boundaries.append(covered)
            if covered >= max(size, MIN_HOOK_SIZE):
                break

        on_boundary = size in boundaries
        long_enough = size >= MIN_HOOK_SIZE
        ok = on_boundary and long_enough

        if ok:
            flag = "OK  "
        elif not long_enough:
            flag = "BAD "   # too short for the jmp Syringe writes
        else:
            flag = "BAD "

        if not ok:
            bad += 1

        print(f"{flag}0x{addr:08X} size={size:<3} {name}")

        if not long_enough:
            print(f"      size {size} < {MIN_HOOK_SIZE}: the 5-byte jmp would "
                  f"destroy the next instruction")

        if not on_boundary:
            print(f"      covers {covered} byte(s); instruction ends at "
                  f"{[hex(b) for b in boundaries]}")

    print(f"\n{len(hooks)} hook(s), {bad} unsafe")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
