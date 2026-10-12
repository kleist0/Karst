#!/usr/bin/env python3
"""Write, show or remove the CountryArt rules in a rules file.

CountryArt reads two parallel lists from the building's OWN section:

    [GACNST]
    ShowByCountries=Americans,Alliance
    ShowBySHP=YACNST,NACNST

Index k of the first list names the country whose buildings use index k of the
second. Both keys must be present, the lists must be the same length, and the
country names must match the engine's own list - CountryArt reports and ignores
anything else rather than half-applying it.

The keys are read at load time, so adding, changing or removing them needs NO
rebuild: change the file, restart the game.

Usage:
    python countryart_config.py                          # show what is configured
    python countryart_config.py --write GACNST           # write one building
    python countryart_config.py --write all              # write every building below
    python countryart_config.py --remove                 # take them all out again
    python countryart_config.py --file <path/to/rulesmd.ini> ...

Every write backs the file up first, as
`<path>.bak-before-countryart-<timestamp>`.
"""

import datetime
import re
import shutil
import sys

DEFAULT_INI = r"E:\_game\RA2\yr_karst_test\rulesmd.ini"

# The country order every entry below is written in.
COUNTRY_LIST = "Americans,Alliance"

# building -> the ART sections, in the SAME order as COUNTRY_LIST.
# YACNST/NACNST and friends are the retail Yuri (Y...) and Soviet (N...) art,
# so the mapping is deliberately literal: it is what makes the difference easy
# to see in a live match.
ART = {
    "GACNST": "YACNST,NACNST",   # construction yard
    "GAREFN": "YAREFN,NAREFN",   # refinery (there is no YACLON)
    "GAPOWR": "YAPOWR,NAPOWR",   # power plant
    "GAPILE": "YABRCK,NAHAND",   # barracks
    "GAWEAP": "YAWEAP,NAWEAP",   # war factory
    "GATECH": "YATECH,NATECH",   # battle lab
    "GAYARD": "YAYARD,NAYARD",   # shipyard
}

KEYS = ("ShowByCountries", "ShowBySHP")
KEY_LINE = re.compile(r"^\s*(ShowByCountries|ShowBySHP)\s*=", re.IGNORECASE)
SECTION = re.compile(r"^\[([^\]]+)\]")


def read_text(path):
    with open(path, "rb") as handle:
        return handle.read().decode("latin-1")


def write_text(path, text):
    with open(path, "wb") as handle:
        handle.write(text.encode("latin-1"))


def show(path):
    current = None
    found = 0

    for line in read_text(path).splitlines():
        match = SECTION.match(line.strip())
        if match:
            current = match.group(1).upper()
        elif KEY_LINE.match(line):
            found += 1
            state = "comment" if line.strip().startswith(";") else "ACTIVE "
            print(f"  [{state}] [{current}] {line.strip()}")

    if not found:
        print("  (no ShowByCountries / ShowBySHP lines at all)")


def rewrite(path, targets, remove):
    """Insert, replace or delete the keys in each target section."""
    text = read_text(path)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    shutil.copyfile(path, f"{path}.bak-before-countryart-{stamp}")

    out = []
    current = None
    handled = []

    for line in text.splitlines(keepends=True):
        match = SECTION.match(line.strip())
        if match:
            current = match.group(1).upper()
            out.append(line)

            # Put the keys immediately after the section header, which is where a
            # mod author would write them.
            if current in targets and not remove:
                out.append(f"ShowByCountries={COUNTRY_LIST}\n")
                out.append(f"ShowBySHP={ART[current]}\n")
                handled.append(current)
            continue

        # Drop an earlier copy of the keys inside a section being rewritten; leave
        # every other line - including comments elsewhere - byte for byte alone.
        if current in targets and KEY_LINE.match(line):
            continue

        out.append(line)

    write_text(path, "".join(out))
    return handled


def main():
    args = sys.argv[1:]
    path = DEFAULT_INI

    if "--file" in args:
        index = args.index("--file")
        path = args[index + 1]
        del args[index:index + 2]

    if not args or "--show" in args:
        print(f"{path}:")
        show(path)
        return 0

    if "--remove" in args:
        handled = rewrite(path, set(ART), remove=True)
        print(f"{path}: removed the keys from {len(handled)} section(s)")
        return 0

    if "--write" in args:
        names = [a.upper() for a in args[args.index("--write") + 1:] if not a.startswith("-")]
        if not names:
            print(__doc__)
            return 1

        if names == ["ALL"]:
            targets = set(ART)
        else:
            unknown = [n for n in names if n not in ART]
            if unknown:
                print(f"not a building this script knows: {', '.join(unknown)}")
                print(f"known: {', '.join(sorted(ART))}")
                return 2
            targets = set(names)

        handled = rewrite(path, targets, remove=False)
        print(f"{path}: wrote {len(handled)} section(s)")
        for name in sorted(handled):
            print(f"  [{name}] ShowByCountries={COUNTRY_LIST}  ShowBySHP={ART[name]}")

        missing = sorted(targets - set(handled))
        if missing:
            print("  NOT FOUND in this file: " + ", ".join(missing))
        return 0

    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())
