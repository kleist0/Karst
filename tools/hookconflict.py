#!/usr/bin/env python3
"""Report Syringe hooks that overlap another DLL's hooks.

WHY THIS EXISTS

Two hooks must not share a single byte. Syringe writes a 5-byte `jmp` at every
hook address and restores the instructions it replaced, so a hook whose declared
size reaches into another DLL's hook makes both of them resume in the middle of
an instruction - which is a crash, not a warning.

Measured, and the reason this tool now exists: Karst hooked
`TechnoTypeClass::GetCameo` at 0x712040 with size 6, while Ares.dll hooks the
same function at 0x712045 with size 5. The overlap made the game die with
`C0000005 at 0x71204D` - the middle of a 6-byte instruction - the moment a
construction yard was deployed.

HANDOFF already said "check hooktable.py before choosing an address"; this turns
that manual check into one command.

Usage:
    python hookconflict.py <candidate.dll> <other.dll> [other.dll ...]

Exit code 1 when any overlap is found, so it can gate a build.
"""

import struct
import sys

NEAR_MISS_BYTES = 8


def parse_sections(data):
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e_lfanew + 4
    count = struct.unpack_from("<H", data, coff + 2)[0]
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    image_base = struct.unpack_from("<I", data, opt + 28)[0]

    sections = []
    entry = opt + opt_size
    for _ in range(count):
        name = data[entry:entry + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, raw_size, raw_ptr = struct.unpack_from("<IIII",
            data, entry + 8)
        sections.append((name, vaddr, vsize, raw_ptr, raw_size))
        entry += 40

    return image_base, sections


def hooks(path):
    """Every `hookdecl` record in a built DLL: (address, size, name)."""
    with open(path, "rb") as handle:
        data = handle.read()

    image_base, sections = parse_sections(data)
    found = []

    for name, _vaddr, _vsize, raw_ptr, raw_size in sections:
        if not name.startswith(".syhks"):
            continue

        blob = data[raw_ptr:raw_ptr + raw_size]
        for offset in range(0, len(blob) - 12, 16):
            address, size, name_ptr = struct.unpack_from("<III", blob, offset)
            if address == 0:
                continue

            text = ""
            if image_base <= name_ptr < image_base + 0x800000:
                for sname, svaddr, svsize, srp, srs in sections:
                    start = image_base + svaddr
                    if start <= name_ptr < start + max(svsize, srs):
                        index = srp + (name_ptr - start)
                        text = data[index:index + 64].split(b"\0")[0].decode(
                            "ascii", "replace")
                        break

            found.append((address, size, text))

    return found


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    candidate_path = sys.argv[1]
    candidate = hooks(candidate_path)

    others = []
    for path in sys.argv[2:]:
        for address, size, name in hooks(path):
            # A size of 0 still occupies the byte Syringe patches.
            others.append((address, max(size, 1), name, path))

    print(f"{candidate_path}: {len(candidate)} hook(s)")
    print(f"against {len(sys.argv) - 2} other DLL(s), {len(others)} hook(s)")

    overlaps = 0
    near = 0
    shared = 0

    for address, size, name in sorted(candidate):
        span = max(size, 1)
        low, high = address, address + span

        for other_address, other_size, other_name, other_path in others:
            other_low, other_high = other_address, other_address + other_size

            if address == other_address:
                # SAME address: Syringe installs one patch and runs both handlers
                # in turn, so this is supported rather than a conflict. Karst has
                # shared 0x0055D360 (main loop) and 0x007CD810 (entry point) with
                # Phobos and Kratos for the whole project without trouble.
                shared += 1
                continue

            if low < other_high and other_low < high:
                # DIFFERENT start with overlapping bytes: both trampolines would
                # resume inside one another, which is a crash (see the module
                # docstring for the measured case).
                overlaps += 1
                print(f"  OVERLAP  0x{address:08X} size={size} ({name})")
                print(f"           vs {other_path} 0x{other_address:08X} "
                      f"size={other_size} ({other_name})")
            elif abs(high - other_low) <= NEAR_MISS_BYTES \
                    or abs(other_high - low) <= NEAR_MISS_BYTES:
                near += 1
                print(f"  adjacent 0x{address:08X} size={size} ({name}) "
                      f"| {other_path} 0x{other_address:08X} size={other_size} "
                      f"({other_name})")

    print()
    if overlaps:
        print(f"{overlaps} OVERLAP(S) - these must be fixed before deploying")
        return 1

    print(f"no overlaps ({shared} same-address handler(s) shared, "
          f"{near} adjacent pair(s) checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
