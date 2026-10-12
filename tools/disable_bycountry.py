"""Disable the per-country mechanism, keep the global ShowByArt.

Two edits, both reversible:

  1. karst.ini : CloneTypes=yes -> no
     Turns off the whole per-country path (type clones, the construction-time
     Type substitution and the per-frame draw-time substitution). The clones
     are what gave a building one country's ImageFile while keeping the source
     type's animation names, which is the suspected cause of the activity
     animation being wrong.

  2. rulesmd.ini : comment out the two ShowByCountries/ShowBySHP lines
     Kept as comments so the intent is still visible, and so re-enabling is a
     one-line change once the per-country path is fixed properly.

The global ShowByArt entries are deliberately NOT touched: that path is
verified working and is what the user wants.
"""
import io
import re
import shutil

TEST = r'E:\_game\RA2\yr_karst_test'
KARST_INI = TEST + r'\karst.ini'
RULES_INI = TEST + r'\rulesmd.ini'


def patch_karst_ini():
    raw = open(KARST_INI, 'rb').read().decode('latin-1')
    before = raw
    raw = re.sub(r'(?mi)^CloneTypes=yes\s*$', 'CloneTypes=no', raw)
    if raw != before:
        shutil.copyfile(KARST_INI, KARST_INI + '.bak-before-disable-bycountry')
        open(KARST_INI, 'wb').write(raw.encode('latin-1'))
        print('karst.ini      : CloneTypes=yes -> no   (backup .bak-before-disable-bycountry)')
    else:
        print('karst.ini      : no CloneTypes=yes line found (already off?)')


def patch_rules_ini():
    raw = open(RULES_INI, 'rb').read().decode('latin-1')
    lines = raw.split('\r\n')
    out = []
    cur = None
    changed = 0

    for line in lines:
        m = re.match(r'^\s*\[([^\]]+)\]\s*$', line)
        if m:
            cur = m.group(1).strip()

        if (cur == 'GACNST'
                and re.match(r'^\s*(ShowByCountries|ShowBySHP)\s*=', line, re.I)):
            out.append('; disabled (per-country path conflicts with the activity '
                       'animation fix): ' + line.strip())
            changed += 1
        else:
            out.append(line)

    if changed:
        shutil.copyfile(RULES_INI, RULES_INI + '.bak-before-disable-bycountry')
        open(RULES_INI, 'wb').write('\r\n'.join(out).encode('latin-1'))
        print('rulesmd.ini    : commented %d line(s) in [GACNST]   '
              '(backup .bak-before-disable-bycountry)' % changed)
    else:
        print('rulesmd.ini    : nothing to comment (already done?)')


if __name__ == '__main__':
    patch_karst_ini()
    patch_rules_ini()
