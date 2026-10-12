#!/usr/bin/env python3
"""Minimal PE (32-bit) disassembly helper for locating gamemd.exe code.

Supports the workflow needed to find hook points:
  * map a virtual address (as used in YRpp / DEFINE_HOOK) to a file offset
  * disassemble a range or a function prologue
  * find all references to an absolute address (e.g. a known function)
  * find all references to a string (e.g. "Image")

Usage:
    python dumpscan.py info
    python dumpscan.py disasm 0x43B740 [count]
    python dumpscan.py xrefs 0x43B740
    python dumpscan.py strrefs Image
"""

import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_IMM, CS_OP_MEM

GAMEMD = r"E:\_game\RA2\Yuri'sRevenge\gamemd.exe"


class Pe:
    def __init__(self, path):
        with open(path, "rb") as handle:
            self.data = handle.read()

        e_lfanew = self.u32(0x3C)
        if self.data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
            raise ValueError("not a PE file")

        coff = e_lfanew + 4
        self.num_sections = self.u16(coff + 2)
        self.opt_size = self.u16(coff + 16)
        opt = coff + 20

        self.image_base = self.u32(opt + 28)
        size_of_headers = self.u32(opt + 60)

        self.sections = []
        sec = opt + self.opt_size
        for _ in range(self.num_sections):
            name = self.data[sec:sec + 8].rstrip(b"\0").decode("ascii", "replace")
            virt_size = self.u32(sec + 8)
            virt_addr = self.u32(sec + 12)
            raw_size = self.u32(sec + 16)
            raw_ptr = self.u32(sec + 20)
            characteristics = self.u32(sec + 36)
            self.sections.append({
                "name": name,
                "vaddr": virt_addr,
                "vsize": virt_size,
                "raw": raw_ptr,
                "raw_size": raw_size,
                "exec": bool(characteristics & 0x20000000),
            })
            sec += 40

        self.headers_size = size_of_headers
        self._va_to_off_cache = {}

    def u16(self, off):
        return struct.unpack_from("<H", self.data, off)[0]

    def u32(self, off):
        return struct.unpack_from("<I", self.data, off)[0]

    def va_to_off(self, va):
        """Virtual address -> file offset, or None."""
        cached = self._va_to_off_cache.get(va)
        if cached is not None:
            return cached

        for section in self.sections:
            start = self.image_base + section["vaddr"]
            end = start + max(section["vsize"], section["raw_size"])
            if start <= va < end:
                offset = section["raw"] + (va - start)
                if offset < len(self.data):
                    self._va_to_off_cache[va] = offset
                    return offset

        if va < self.image_base + self.headers_size:
            self._va_to_off_cache[va] = va - self.image_base
            return self._va_to_off_cache[va]

        return None

    def off_to_va(self, off):
        for section in self.sections:
            if section["raw"] <= off < section["raw"] + section["raw_size"]:
                return self.image_base + section["vaddr"] + (off - section["raw"])
        return None

    def is_executable(self, va):
        for section in self.sections:
            start = self.image_base + section["vaddr"]
            if start <= va < start + max(section["vsize"], section["raw_size"]):
                return section["exec"]
        return False

    def read(self, va, size):
        off = self.va_to_off(va)
        if off is None:
            return None
        return self.data[off:off + size]

    def exec_sections(self):
        return [s for s in self.sections if s["exec"]]


def make_disassembler():
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    md.syntax = 0  # Intel

    # Continue past bytes that do not decode instead of stopping.
    #
    # WHY THIS MATTERS
    #
    # `md.disasm(...)` stops at the first undecodable byte. gamemd.exe has jump
    # tables and other embedded data inside .text, so a linear sweep stopped
    # early and every later instruction became invisible to the tools built on
    # this function. It was caught when an offset scan reported
    # "0 instruction(s) touching +0x34B8" for an offset that provably appears at
    # 0x67E75B (`mov ecx,[eax+0x34b8]`).
    #
    # With skipdata capstone emits a `db <byte>` pseudo-instruction for the
    # undecodable byte and carries on, which resynchronises on the next real
    # instruction boundary. Those pseudo-instructions have no operands, so the
    # offset/xref scans simply ignore them.
    md.skipdata = True
    return md


def iter_insns(md, code, start):
    """Yield every real instruction in `code`, skipping data pseudo-instructions.

    In skipdata mode capstone represents each undecodable byte as a pseudo
    instruction whose `id` is 0 (mnemonic ".byte"/"db"). Reading `operands` on
    one of those raises CS_ERR_SKIPDATA, so they must be filtered out before any
    operand inspection - which is what this wrapper guarantees for every tool.
    """
    for insn in md.disasm(code, start):
        if insn.id == 0:
            continue
        yield insn


def describe_operands(md, insn):
    """Return operand immediates and memory displacements worth inspection."""
    imms = []
    mems = []
    for op in insn.operands:
        if op.type == CS_OP_IMM:
            imms.append(op.imm)
        elif op.type == CS_OP_MEM:
            mems.append(op.mem.disp)
    return imms, mems


def cmd_info(pe):
    print(f"image base : 0x{pe.image_base:08X}")
    print(f"sections   : {pe.num_sections}")
    for section in pe.sections:
        print(f"  {section['name']:<8} va=0x{pe.image_base + section['vaddr']:08X} "
              f"vsize=0x{section['vsize']:08X} raw=0x{section['raw']:08X} "
              f"exec={section['exec']}")


def cmd_disasm(pe, va, count):
    md = make_disassembler()
    code = pe.read(va, count * 16)
    if code is None:
        print(f"0x{va:08X} is not mapped")
        return

    for insn in md.disasm(code, va):
        print(f"0x{insn.address:08X}  {insn.mnemonic:<8} {insn.op_str}")
        count -= 1
        if count <= 0:
            break


def cmd_xrefs(pe, target):
    """Find instructions whose immediate equals `target`.

    In 32-bit code a call to a fixed address appears as `call 0xADDR`, so
    scanning for the immediate value finds direct callers.
    """
    md = make_disassembler()
    hits = []

    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]

        # iter_insns filters the skipdata pseudo-instructions, whose `operands`
        # access raises CS_ERR_SKIPDATA. Calling md.disasm directly here is what
        # made this command crash once skipdata was enabled.
        for insn in iter_insns(md, code, start):
            imms, _ = describe_operands(md, insn)
            if target in imms:
                hits.append((insn.address, insn.mnemonic, insn.op_str))

    for address, mnemonic, op_str in hits:
        print(f"0x{address:08X}  {mnemonic:<8} {op_str}")
    print(f"\n{len(hits)} reference(s) to 0x{target:08X}")


def cmd_strrefs(pe, text):
    """Find every occurrence of an ASCII string, then report addresses that
    hold a pointer to it (the usual 'push offset aImage' pattern)."""
    needle = text.encode("ascii")

    occurrences = []
    start = 0
    while True:
        index = pe.data.find(needle, start)
        if index < 0:
            break
        occurrences.append(index)
        start = index + 1

    if not occurrences:
        print(f"string {text!r} not found")
        return

    print(f"{len(occurrences)} occurrence(s) of {text!r} in the file")
    for index in occurrences[:20]:
        va = pe.off_to_va(index)
        print(f"  file 0x{index:08X}  va {('0x%08X' % va) if va else 'n/a'}")

    # Report the first occurrence's VA so it can be xref'd.
    for index in occurrences[:1]:
        va = pe.off_to_va(index)
        if va:
            print(f"\nDisassemble around references with:\n"
                  f"  python dumpscan.py xrefs 0x{va:08X}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    pe = Pe(GAMEMD)
    command = sys.argv[1]

    if command == "info":
        cmd_info(pe)

    elif command == "disasm":
        va = int(sys.argv[2], 0)
        count = int(sys.argv[3]) if len(sys.argv) > 3 else 40
        cmd_disasm(pe, pe.image_base + va if va < pe.image_base else va, count)

    elif command == "xrefs":
        va = int(sys.argv[2], 0)
        cmd_xrefs(pe, va if va >= pe.image_base else pe.image_base + va)

    elif command == "strrefs":
        cmd_strrefs(pe, sys.argv[2])

    else:
        print(__doc__)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
