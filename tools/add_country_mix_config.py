"""Add the crossed test configuration for per-COUNTRY sidebar MIX.

Crossed on purpose: Americans gets the SOVIET sidebar and Russians gets the
ALLIED one, so the result is unmistakable on screen (the two sidebars are
completely different) rather than a subtle difference in cameo positions.

    [Americans] Sidebar.MixFileIndex=2   -> SIDEC02MD.MIX
    [Russians]  Sidebar.MixFileIndex=1   -> SIDEC01MD.MIX

Appends nothing if the keys already exist. A timestamped backup is written.

Usage:
    python add_country_mix_config.py
"""
import re
import shutil
import time

RULES = r'E:\_game\RA2\yr_karst_test\rulesmd.ini'

# section -> value
SETTINGS = [
    ('Americans', '2'),
    ('Russians', '1'),
]


def main():
    raw = open(RULES, 'rb').read().decode('latin-1')
    uses_crlf = '\r\n' in raw
    lines = raw.replace('\r\n', '\n').split('\n')

    # Skip anything already present, so re-running is harmless.
    existing = any(re.match(r'^\s*Sidebar\.MixFileIndex\s*=', l, re.I) for l in lines)
    if existing:
        print('Sidebar.MixFileIndex already present - nothing to do')
        return 0

    # Find each section and append the key right after its header.
    out = []
    applied = []
    pending = dict(SETTINGS)
    current = None

    for line in lines:
        out.append(line)

        m = re.match(r'^\s*\[([^\]]+)\]\s*$', line)
        if m:
            current = m.group(1).strip()
            if current in pending:
                value = pending.pop(current)
                out.append('Sidebar.MixFileIndex=%s' % value)
                applied.append((current, value))

    if not applied:
        print('none of the target sections were found in %s' % RULES)
        return 1

    backup = RULES + '.bak-country-mix-' + time.strftime('%Y%m%d-%H%M%S')
    shutil.copyfile(RULES, backup)

    result = '\n'.join(out)
    if uses_crlf:
        result = result.replace('\n', '\r\n')
    open(RULES, 'wb').write(result.encode('latin-1'))

    for section, value in applied:
        print('  [%s] Sidebar.MixFileIndex=%s  -> SIDEC%02dMD.MIX'
              % (section, value, int(value)))

    for section in pending:
        print('  WARNING: section [%s] not found - skipped' % section)

    print('\nbackup: %s' % backup)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
