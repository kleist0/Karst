#!/usr/bin/env python3
"""Walk the crashing thread's stack out of a minidump's Memory64List.

`except.txt` gives only ESP/EIP plus a flat dump of ~0x260 bytes, which is not
enough to see WHO called the faulting function: the interesting return
addresses sit further up the stack, past unreadable 4-byte values.

The minidump that ships next to the report contains the real stack memory, so
this tool reads it and reports only the DWORDs that are plausible return
addresses - i.e. values landing inside an executable module (module list from
the dump, so relocated extension DLLs are attributed correctly).

Usage:
    python dumpstack.py <dump> --esp 0x1AD1B0            # 256 bytes up from ESP
    python dumpstack.py <dump> --esp 0x1AD1B0 --size 0x400
    python dumpstack.py <dump> --thread                  # use the dump's own ESP
    python dumpstack.py <dump> --range 0x1AD1B0 0x1AD300 # raw words, all of them
"""

import struct
import sys

from dumpmods import Minidump


def memory_ranges(dump):
    """Yield (start, size, file_rva) for every range in the Memory64List."""
    for stream_type, size, rva in dump.streams():
        if stream_type != 9:
            continue

        head = dump.read(rva, 16)
        count, base_rva = struct.unpack_from("<QQ", head, 0)
        descriptors = dump.read(rva + 16, count * 16)

        data_rva = base_rva
        for i in range(count):
            start, length = struct.unpack_from("<QQ", descriptors, i * 16)
            yield start, length, data_rva
            data_rva += length
        return


def read_memory(dump, address, size):
    for start, length, file_rva in memory_ranges(dump):
        if start <= address < start + length:
            offset = address - start
            available = min(size, length - offset)
            return dump.read(file_rva + offset, available)
    return None


def exception_esp(dump):
    for stream_type, size, rva in dump.streams():
        if stream_type != 6:
            continue
        body = dump.read(rva, size)
        # MINIDUMP_EXCEPTION_STREAM: ThreadId(4) Align(4) ExceptionRecord(152)
        # MINIDUMP_EXCEPTION: code(4) flags(4) record(8) address(8) numparams(4)
        # params(15*4=60)  -> Context offset differs per bitness; ESP is only
        # reliably available from the dump's own context, so this is a fallback.
        exception_address = struct.unpack_from("<Q", body, 24)[0]
        return exception_address & 0xFFFFFFFF, body
    return None, None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    dump = Minidump(sys.argv[1])
    modules = dump.modules()

    def attribute(value):
        for module in modules:
            if module["base"] <= value < module["base"] + module["size"]:
                return f"{module['name']} + 0x{value - module['base']:X}"
        return None

    if "--range" in sys.argv:
        index = sys.argv.index("--range")
        low = int(sys.argv[index + 1], 0)
        high = int(sys.argv[index + 2], 0)
        esp = low
        size = high - low
        show_all = True
    else:
        show_all = False
        if "--esp" in sys.argv:
            esp = int(sys.argv[sys.argv.index("--esp") + 1], 0)
        elif "--thread" in sys.argv:
            esp, _body = exception_esp(dump)
            print(f"dump's exception address gives ESP=0x{esp:X}")
        else:
            print(__doc__)
            return 1

        size = 0x400
        if "--size" in sys.argv:
            size = int(sys.argv[sys.argv.index("--size") + 1], 0)

    raw = read_memory(dump, esp, size)
    if raw is None:
        print(f"0x{esp:08X} is not present in the dump")
        return 2

    print(f"stack 0x{esp:08X} .. 0x{esp + len(raw):08X}")
    for offset in range(0, len(raw) - 3, 4):
        value = struct.unpack_from("<I", raw, offset)[0]
        where = attribute(value)
        if where is None and not show_all:
            continue
        marker = "  <<< return address" if where else ""
        print(f"  0x{esp + offset:08X}  [{offset:04X}]  0x{value:08X}  "
              f"{where or '(data)'}{marker}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
