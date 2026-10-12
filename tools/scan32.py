#!/usr/bin/env python3
"""Scan every executable section for instructions matching a pattern.

Examples:
    # every `mov [reg+0x520], imm/reg` store
    python scan32.py 'mov dword ptr [e?? + 0x520], e??'
    # every `mov e??, 0x7d4`
    python scan32.py 'mov e??, 0x7d4'

The pattern is a whitespace-normalised capstone Intel operand string with `?`
as a single-character wildcard. Tokens are compared after collapsing runs of
spaces, so 'mov dword ptr [eax + 0x520], ecx' matches regardless of the source
spacing.
"""
import re
import sys

import dumpscan

CACHE = {}


def compile_pattern(text):
    if text in CACHE:
        return CACHE[text]

    # Escape everything, then turn '?' into '.'.
    escaped = re.escape(text)
    escaped = escaped.replace(r"\?", "?")
    escaped = escaped.replace("?", ".")
    # Allow any whitespace where the pattern has a single space.
    escaped = escaped.replace(r"\ ", r"\s+")
    regex = re.compile("^" + escaped + "$")
    CACHE[text] = regex
    return regex


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    pattern = " ".join(sys.argv[1:])
    regex = compile_pattern(pattern)

    pe = dumpscan.Pe(dumpscan.GAMEMD)
    md = dumpscan.make_disassembler()

    hits = 0
    for section in pe.exec_sections():
        start = pe.image_base + section["vaddr"]
        code = pe.data[section["raw"]:section["raw"] + section["raw_size"]]
        for insn in md.disasm(code, start):
            text = f"{insn.mnemonic} {insn.op_str}".strip()
            if regex.match(text):
                print(f"0x{insn.address:08X}  {text}")
                hits += 1

    print(f"\n{hits} match(es)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
