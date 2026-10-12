#!/usr/bin/env python3
"""Disassemble a range of ANY PE (extension DLL or gamemd.exe) by RVA or VA.

`dumpscan.py disasm` is hard-wired to gamemd.exe. A crash EIP inside an
extension DLL has to be read in that DLL instead, and the minidump gives the
address as "module base + offset", where the offset is the RVA (the DLLs are
relocated, so their 0x10000000 image base is not the address they run at).

Usage:
    python disrva.py <pe> <rva>            # 20 instructions
    python disrva.py <pe> <rva> 40         # 40 instructions
    python disrva.py <pe> 0x100C5DB0 12    # a preferred-base VA also works

The middle of a function is often data or padding, so reading a crash site is
much easier with a few instructions of lead-in: pass the RVA of the enclosing
function when it is known.
"""

import sys

import dumpscan


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    path = sys.argv[1]
    value = int(sys.argv[2], 0)
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 20

    pe = dumpscan.Pe(path)
    print(f"{path}: image base 0x{pe.image_base:08X}")

    if value >= pe.image_base:
        value -= pe.image_base
        print(f"  (treated as a VA, so RVA = 0x{value:X})")

    code = pe.read(pe.image_base + value, count * 16)
    if code is None:
        print(f"  RVA 0x{value:X} is not mapped")
        return 2

    md = dumpscan.make_disassembler()
    for insn in dumpscan.iter_insns(md, code, pe.image_base + value):
        print(f"  RVA 0x{insn.address - pe.image_base:06X}  "
              f"0x{insn.address:08X}  {insn.mnemonic:<8} {insn.op_str}")
        count -= 1
        if count <= 0:
            break

    return 0


if __name__ == "__main__":
    sys.exit(main())
