import sys

from capstone import CS_ARCH_X86, CS_MODE_32, CS_OP_IMM, CS_OP_MEM, CS_OP_REG

import dumpscan

# BuildingTypeClass's vtable is written into its constructor. Find the
# constructor, then the vtable, then read LoadTheaterSpecificArt out of it.

CTOR = 0x45E50C          # BuildingTypeClass::CTOR, from Phobos
FROM = 0x45E500
TO = 0x45E700


def main():
    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    code = pe.read(FROM, TO - FROM)
    if code is None:
        print("range not mapped")
        return 1

    vtables = []
    for insn in md.disasm(code, FROM):
        if insn.mnemonic != "mov" or len(insn.operands) != 2:
            continue
        dst, src = insn.operands
        if dst.type != CS_OP_MEM or src.type != CS_OP_IMM:
            continue
        # mov dword ptr [reg], imm   -> a vtable store
        if dst.mem.disp == 0 and src.imm > 0x700000:
            vtables.append((insn.address, src.imm))

    print("candidate vtable stores in BuildingTypeClass ctor:")
    for address, value in vtables:
        print(f"  0x{address:08X}  ->  0x{value:08X}")

    for _, vtable in vtables:
        print(f"\n--- vtable 0x{vtable:08X} ---")
        entries = []
        for index in range(40):
            raw = pe.read(vtable + index * 4, 4)
            if raw is None:
                break
            value = int.from_bytes(raw, "little")
            if not (0x401000 <= value < 0x7E0000):
                break
            entries.append(value)
            print(f"  [{index:2}] 0x{value:08X}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
