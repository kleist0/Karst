#!/usr/bin/env python3
"""Read the module list from a Windows minidump (Syringe's extcrashdump.dmp).

WHY THIS EXISTS

A crash report (`debug\\snapshot-*\\except.txt`) carries a bare EIP such as
`5C955DB0`, and every extension DLL in this install is relocated far away from
its 0x10000000 image base, so that address cannot be attributed by looking at
the PE files alone. The minidump written next to the report contains the real
load addresses, which turns the EIP into "Phobos.dll + 0x1A2B3" - the same
attribution the crash log's own `Loaded at` lines would have given, but for a
run whose karst.log has no such line.

The dump is large (hundreds of MB), so this reader only seeks: it never loads
the file into memory.

Usage:
    python dumpmods.py <dump.dmp>                    # list every module
    python dumpmods.py <dump.dmp> 0x5C955DB0 ...     # attribute addresses
    python dumpmods.py <dump.dmp> --streams           # show the stream table
"""

import struct
import sys

SIGNATURE = 0x504D444D  # 'MDMP'

STREAM_NAMES = {
    3: "ThreadList",
    4: "ModuleList",
    5: "MemoryList",
    6: "Exception",
    7: "SystemInfo",
    9: "Memory64List",
    15: "MiscInfo",
}

MODULE_ENTRY_SIZE = 108


class Minidump:
    def __init__(self, path):
        self.handle = open(path, "rb")

        header = self.handle.read(32)
        if len(header) < 32:
            raise ValueError("file is too small to be a minidump")

        (signature, self.version, self.stream_count, directory_rva,
         self.checksum, self.timestamp, self.flags) = struct.unpack_from(
            "<IIIIIIQ", header, 0)

        if signature != SIGNATURE:
            raise ValueError(f"bad signature 0x{signature:08X} (not a minidump)")

        self.directory_rva = directory_rva

    def read(self, rva, size):
        self.handle.seek(rva)
        data = self.handle.read(size)
        if len(data) != size:
            raise ValueError(f"short read at rva 0x{rva:X} ({len(data)}/{size})")
        return data

    def streams(self):
        table = self.read(self.directory_rva, self.stream_count * 12)
        for i in range(self.stream_count):
            stream_type, size, rva = struct.unpack_from("<III", table, i * 12)
            yield stream_type, size, rva

    def read_string(self, rva):
        length = struct.unpack_from("<I", self.read(rva, 4), 0)[0]
        raw = self.read(rva + 4, length)
        return raw.decode("utf-16-le", "replace")

    def modules(self):
        for stream_type, size, rva in self.streams():
            if stream_type != 4:
                continue
            count = struct.unpack_from("<I", self.read(rva, 4), 0)[0]
            body = self.read(rva + 4, count * MODULE_ENTRY_SIZE)
            out = []
            for i in range(count):
                base = struct.unpack_from("<Q", body, i * MODULE_ENTRY_SIZE)[0]
                image_size, _checksum, timestamp, name_rva = struct.unpack_from(
                    "<IIII", body, i * MODULE_ENTRY_SIZE + 8)
                out.append({
                    "base": base,
                    "size": image_size,
                    "timestamp": timestamp,
                    "name": self.read_string(name_rva),
                })
            return out
        return []


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    path = sys.argv[1]
    dump = Minidump(path)

    print(f"{path}: {dump.stream_count} stream(s)")

    if "--streams" in sys.argv:
        for stream_type, size, rva in dump.streams():
            name = STREAM_NAMES.get(stream_type, "?")
            print(f"  type {stream_type:<3} {name:<12} size=0x{size:<9X} rva=0x{rva:X}")

    modules = dump.modules()
    modules.sort(key=lambda m: m["base"])

    wanted = [int(a, 0) for a in sys.argv[2:] if a.lower().startswith("0x")]

    if not wanted:
        print(f"\n{len(modules)} module(s):")
        for module in modules:
            print(f"  0x{module['base']:08X}  size=0x{module['size']:08X}  "
                  f"{module['name']}")

    for address in wanted:
        print(f"\n0x{address:08X}:")
        matched = False
        for module in modules:
            if module["base"] <= address < module["base"] + module["size"]:
                offset = address - module["base"]
                print(f"  inside {module['name']}  "
                      f"(base 0x{module['base']:08X})  + 0x{offset:X}")
                matched = True
        if not matched:
            print("  no module covers this address")

    return 0


if __name__ == "__main__":
    sys.exit(main())
