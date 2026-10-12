"""Remove the Sidebar.MixFileIndex test lines from rulesmd.ini.

They were the crossed test configuration (Americans -> Soviet sidebar, Russians
-> Allied sidebar). The feature itself stays in the DLL; only the test
declarations come out, so the mod file goes back to having no opinion about
which country uses which sidebar.

A timestamped backup is written before the change.

Usage:
    python remove_country_mix_config.py
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
    current = '?'

    for line in lines:
        m = re.match(r'^\s*\[([^\]]+)\]\s*$', line)
        if m:
            current = m.group(1).strip()

        if re.match(r'^\s*Sidebar\.MixFileIndex\s*=', line, re.I):
            removed.append('[%s] %s' % (current, line.strip()))
            continue

        out.append(line)

    if not removed:
        print('nothing to remove')
        return 0

    backup = RULES + '.bak-no-country-mix-' + time.strftime('%Y%m%d-%H%M%S')
    shutil.copyfile(RULES, backup)

    result = '\n'.join(out)
    if uses_crlf:
        result = result.replace('\n', '\r\n')
    open(RULES, 'wb').write(result.encode('latin-1'))

    print('removed %d line(s):' % len(removed))
    for item in removed:
        print('   ', item)
    print('\nbackup: %s' % backup)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
