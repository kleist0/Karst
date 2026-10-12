#!/usr/bin/env python3
"""Locate vtables whose leading entries match a known base vtable.

A derived class's vtable starts with a copy of its base class's entries, so
searching for a run of identical dwords finds every virtual table built on top
of the known one.

Usage:
    python findvt.py 0x7E3C14 41            # find vtables starting with these 41 entries
    python findvt.py 0x7E3C14 41 0x007E0000 # also print where the new entries begin
"""
import sys

import dumpscan


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    base_va = int(sys.argv[1], 0)
    count = int(sys.argv[2], 0)

    base_off = pe.va_to_off(base_va)
    pattern = bytes(pe.data[base_off:base_off + count * 4])

    # Search the whole file: vtables live in .rdata, but be liberal.
    hits = []
    start = 0
    while True:
        index = pe.data.find(pattern, start)
        if index < 0:
            break
        hits.append(index)
        start = index + 4

    for index in hits:
        va = pe.off_to_va(index)
        if va is None:
            continue

        # How many more consecutive CODE pointers follow, and where do they stop?
        run = count
        while True:
            nxt = pe.u32(index + run * 4)
            if not pe.is_executable(nxt):
                break
            run += 1
            if run > 400:
                break

        print(f"vtable base 0x{va:08X}  shared={count}  total_code={run}")
        for i in range(count, run):
            ptr = pe.u32(index + i * 4)
            print(f"    [{i:3d}] 0x{va + i * 4:08X} -> 0x{ptr:08X}")

    print(f"\n{len(hits)} hit(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
