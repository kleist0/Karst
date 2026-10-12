#!/usr/bin/env python3
"""Read raw memory out of a minidump's Memory64List.

The dump is the only way to see what a pointer held at the moment of the crash:
`except.txt` has registers and a short stack window, but a fault inside another
extension's container needs the container itself - e.g. Phobos' inline extension
slot `*(type + 0x18)` and the std::vector it points at.

Usage:
    python dumpmem.py <dump> --dwords <address> <count>
    python dumpmem.py <dump> --hex <address> <size>
    python dumpmem.py <dump> --cstr <address> [max]

Addresses are the addresses the process actually had (as printed in except.txt
and karst.log), not RVAs.
"""

import struct
import sys

from dumpmods import Minidump
from dumpstack import read_memory


def describe(dump, address):
    """One-line summary of a type-like object: ID string, image file, slots."""
    raw = read_memory(dump, address, 0x1800)
    if raw is None:
        return "  (not present in the dump)"

    def cstr(offset, limit=32):
        chunk = raw[offset:offset + limit]
        end = chunk.find(b"\0")
        if end >= 0:
            chunk = chunk[:end]
        try:
            return chunk.decode("ascii")
        except UnicodeDecodeError:
            return None

    slots = {}
    for name, offset in (("+0x18 BuildingTypeExt", 0x18),
                         ("+0xDF4 TechnoTypeExt", 0xDF4),
                         ("+0xE24 Ares", 0xE24),
                         ("+0xEA0 ?", 0xEA0)):
        if offset + 4 <= len(raw):
            slots[name] = struct.unpack_from("<I", raw, offset)[0]

    text = f"  ID='{cstr(0x24)}' ImageFile='{cstr(0x1F8)}'"
    for name, value in slots.items():
        text += f"\n    {name} = 0x{value:08X}"
    return text


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1

    dump = Minidump(sys.argv[1])
    mode = sys.argv[2]
    address = int(sys.argv[3], 0)

    if mode == "--dwords":
        count = int(sys.argv[4]) if len(sys.argv) > 4 else 16
        raw = read_memory(dump, address, count * 4)
        if raw is None:
            print(f"0x{address:08X} is not present in the dump")
            return 2
        for i in range(0, len(raw) - 3, 4):
            value = struct.unpack_from("<I", raw, i)[0]
            print(f"  0x{address + i:08X}  0x{value:08X}   {value}")

    elif mode == "--hex":
        size = int(sys.argv[4], 0) if len(sys.argv) > 4 else 64
        raw = read_memory(dump, address, size)
        if raw is None:
            print(f"0x{address:08X} is not present in the dump")
            return 2
        for offset in range(0, len(raw), 16):
            chunk = raw[offset:offset + 16]
            text = " ".join(f"{b:02X}" for b in chunk)
            ascii_text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            print(f"  0x{address + offset:08X}  {text:<47}  {ascii_text}")

    elif mode == "--describe":
        raw = read_memory(dump, address, 4)
        if raw is None:
            print(f"0x{address:08X} is not present in the dump")
            return 2
        pointer = struct.unpack_from("<I", raw, 0)[0]
        print(f"0x{address:08X} holds 0x{pointer:08X}")
        print(f"--- the object at 0x{pointer:08X}:")
        print(describe(dump, pointer))

        raw = read_memory(dump, pointer + 0x18, 4)
        if raw:
            ext = struct.unpack_from("<I", raw, 0)[0]
            print(f"--- *(0x{pointer:08X} + 0x18) = 0x{ext:08X}")
            if ext:
                for offset in (0x144, 0x148):
                    chunk = read_memory(dump, ext + offset, 4)
                    if chunk:
                        value = struct.unpack_from("<I", chunk, 0)[0]
                        print(f"    ext+0x{offset:X} = 0x{value:08X}")

    elif mode == "--cstr":
        limit = int(sys.argv[4]) if len(sys.argv) > 4 else 32
        raw = read_memory(dump, address, limit)
        if raw is None:
            print(f"0x{address:08X} is not present in the dump")
            return 2
        end = raw.find(b"\0")
        print(repr(raw[:end if end >= 0 else len(raw)]))

    else:
        print(__doc__)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
