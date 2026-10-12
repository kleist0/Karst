#!/usr/bin/env python3
"""Dump the Syringe hook declarations embedded in a built Karst.dll.

DEFINE_HOOK puts a `hookdecl { address, size, name }` record into section
.syhks00. Reading it back shows exactly what Syringe will be told, which is the
first thing to check when a hook crashes.

Usage:
    python hooktable.py path\\to\\Karst.dll
"""
import struct
import sys


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    path = sys.argv[1]
    with open(path, "rb") as handle:
        data = handle.read()

    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e_lfanew + 4
    num_sections = struct.unpack_from("<H", data, coff + 2)[0]
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    image_base = struct.unpack_from("<I", data, opt + 28)[0]

    sec = opt + opt_size
    for _ in range(num_sections):
        name = data[sec:sec + 8].rstrip(b"\0").decode("ascii", "replace")
        virt_size, virt_addr = struct.unpack_from("<II", data, sec + 8)
        raw_size, raw_ptr = struct.unpack_from("<II", data, sec + 16)
        if name.startswith(".syhks"):
            print(f"section {name}: va=0x{image_base + virt_addr:08X} "
                  f"size=0x{virt_size:X}")
            blob = data[raw_ptr:raw_ptr + raw_size]
            for off in range(0, len(blob) - 12, 16):
                addr, size, nameptr = struct.unpack_from("<III", blob, off)
                if addr == 0:
                    continue
                text = ""
                if image_base <= nameptr < image_base + 0x800000:
                    # map va -> file offset using the same section table
                    s2 = opt + opt_size
                    for _ in range(num_sections):
                        va = struct.unpack_from("<I", data, s2 + 12)[0]
                        vs = struct.unpack_from("<I", data, s2 + 8)[0]
                        rp = struct.unpack_from("<I", data, s2 + 20)[0]
                        rs = struct.unpack_from("<I", data, s2 + 16)[0]
                        start = image_base + va
                        if start <= nameptr < start + max(vs, rs):
                            idx = rp + (nameptr - start)
                            text = data[idx:idx + 64].split(b"\0")[0].decode(
                                "ascii", "replace")
                            break
                        s2 += 40
                print(f"  0x{addr:08X}  size={size:<3} {text}")
        sec += 40
    return 0


if __name__ == "__main__":
    sys.exit(main())
