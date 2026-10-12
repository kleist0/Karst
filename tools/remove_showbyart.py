"""Remove the ShowByArt / ShowByArtType keys from rulesmd.ini.

They were the test configuration used to prove the feature. The feature itself
(the DLL code) stays; only the per-building declarations are taken out, so the
mod file goes back to having no opinion about which building looks like which.

A timestamped backup is written before the change.

Usage:
    python remove_showbyart.py
"""
import re
import shutil
import time

RULES = r'E:\_game\RA2\yr_karst_test\rulesmd.ini'


def main():
    raw = open(RULES, 'rb').read().decode('latin-1')
    uses_crlf = '\r\n' in raw
    lines = raw.replace('\r\n', '\n').split('\n')

    out = []
    removed = []
    cur = '?'

    for line in lines:
        m = re.match(r'^\s*\[([^\]]+)\]\s*$', line)
        if m:
            cur = m.group(1).strip()

        if re.match(r'^\s*(ShowByArt|ShowByArtType)\s*=', line, re.I):
            removed.append('[%s] %s' % (cur, line.strip()))
            continue

        out.append(line)

    if not removed:
        print('nothing to remove')
        return 0

    backup = RULES + '.bak-no-showbyart-' + time.strftime('%Y%m%d-%H%M%S')
    shutil.copyfile(RULES, backup)

    result = '\n'.join(out)
    if uses_crlf:
        result = result.replace('\n', '\r\n')
    open(RULES, 'wb').write(result.encode('latin-1'))

    print('removed %d line(s):' % len(removed))
    for r in removed:
        print('   ', r)
    print('\nbackup: %s' % backup)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
