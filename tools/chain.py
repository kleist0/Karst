#!/usr/bin/env python3
"""Walk x86 frame-pointer chains back from an address and name each caller.

gamemd.exe is MSVC /O2, which keeps `push ebp; mov ebp, esp` frame pointers in
most functions. That makes a static "who calls this" chain possible:

    frame->[ebp]      = saved caller ebp
    frame->[ebp + 4]  = return address

The tool reports, for each level, the return address and the enclosing function
entry (found by scanning backwards for the previous `ret`/`int3` padding).

Usage:
    python chain.py 0x451A2F 0x0012F000 6        # needs a live frame, else:
    python chain.py --callees 0x4509D0 5         # static call graph downwards
"""
import sys

import dumpscan


def scan_calls(pe, md, start, end):
    """Direct calls strictly inside [start, end)."""
    raw = pe.read(start, end - start)
    if raw is None:
        return []
    found = []
    for insn in md.disasm(raw, start):
        if insn.mnemonic == "call" and insn.operands:
            op = insn.operands[0]
            if op.type == dumpscan.CS_OP_IMM:
                found.append((insn.address, op.imm))
    return found


def function_entry(pe, md, va, back=0x600):
    """Approximate entry: after the previous ret/int3 padding run."""
    raw = pe.read(va - back, back)
    if raw is None:
        return va
    last_end = va - back
    for insn in md.disasm(raw, va - back):
        if insn.mnemonic.startswith("ret"):
            last_end = insn.address + insn.size
        elif insn.mnemonic == "int3":
            last_end = insn.address + insn.size
    return last_end


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    mode = sys.argv[1]
    va = int(sys.argv[2], 0)
    depth = int(sys.argv[3]) if len(sys.argv) > 3 else 4

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    if mode == "--callees":
        # Walk the direct call graph downwards from a function.
        frontier = [va]
        seen = set()
        for _ in range(depth):
            nxt = []
            for f in frontier:
                if f in seen:
                    continue
                seen.add(f)
                entry = function_entry(pe, md, f)
                calls = scan_calls(pe, md, f, f + 0x400)
                for address, target in calls:
                    if 0x400000 <= target < 0x800000:
                        print(f"0x{f:08X} -> 0x{target:08X}  (at 0x{address:08X})")
                        nxt.append(target)
            frontier = nxt
        return 0

    print("mode not understood")
    return 1


if __name__ == "__main__":
    sys.exit(main())
