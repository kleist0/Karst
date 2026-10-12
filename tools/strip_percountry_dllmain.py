"""Remove the per-country ("by owner") machinery from dllmain.cpp.

What goes:
  * the 0x4513D0 DrawType hook   (draw-time type swap)
  * the 0x423AC0 AnimDraw hook   (per-animation country visibility)
  * the two per-country calls in the 0x43B756 hook
        SyncAnimNamesAtCreation()      (hand-written animation-name sync)
        ApplyOwnerTypeAtConstruction() (construction-time type swap)

What stays: every hook the global ShowByArt path needs (0x45FE50 section
redirect, 0x43B756 type store, 0x451A2F anim store, the alloc logs, the main
loop, the entry point).

Run once; it is idempotent enough to re-run safely (it reports no-change).
"""
import re
import shutil

PATH = r'Karst\src\dllmain.cpp'
BAK = PATH + '.bak-before-percountry-removal'

HEAD_4513D0 = '''// ---------------------------------------------------------------------------
// REMOVED: Draw-time art selection  (BuildingClass::GetImage, 0x4513D0)
// ---------------------------------------------------------------------------
//
// This hook returned a per-country type clone at draw time, i.e. it was one of
// the two mechanisms behind "show a building as its owner's country". Both are
// gone now:
//
//   * it needed per-country type clones, and a clone was built as
//     `memcpy(source) + ImageFile=<target section>` - so it carried the TARGET
//     section's art together with the SOURCE type's animation names. That is
//     what made the activity animations wrong, and it is why the per-country
//     path was disabled before.
//   * it also duplicated the construction-time swap, so a single building was
//     being redirected twice, once per frame.
//
// The global ShowByArt path (rename the type's ART section before the engine
// parses it) needs no clone and no draw hook, and is what this DLL uses now.
'''

HEAD_423AC0 = '''// ---------------------------------------------------------------------------
// REMOVED: AnimClass::Draw hook  (0x423AC0) -- per-animation country visibility
// ---------------------------------------------------------------------------
//
// This hooked the animation draw path and marked an animation Invisible when the
// owner's country was not listed on the animation's own art section. It was part
// of the per-country feature and is gone with it (it also depended on the same
// clone/owner lookups).
'''


def strip_block(text, start_marker, end_marker, replacement, label):
    """Replace text from the line containing start_marker through end_marker."""
    i = text.find(start_marker)
    if i < 0:
        print('  %-28s : start marker not found (already removed?)' % label)
        return text, False

    j = text.find(end_marker, i)
    if j < 0:
        print('  %-28s : end marker not found - ABORT this block' % label)
        return text, False

    j += len(end_marker)
    out = text[:i] + replacement + text[j:]
    print('  %-28s : removed %d chars' % (label, j - i))
    return out, True


def main():
    raw = open(PATH, 'rb').read().decode('latin-1')

    # Normalise line endings while matching: the file is CRLF, and every marker
    # below is written with plain \n. Converted back on write.
    uses_crlf = '\r\n' in raw
    raw = raw.replace('\r\n', '\n')

    changed = False

    # ---- 1. the DrawType hook (comment header + DEFINE_HOOK block) ----------
    raw2, c = strip_block(
        raw,
        '// Draw-time art selection  (BuildingClass::GetImage, vtable slot 27)',
        '\tR->EAX<BuildingTypeClass*>(pDrawType);\n\treturn Continue;\n}',
        HEAD_4513D0,
        'DrawType hook 0x4513D0')
    changed = changed or c
    if c:
        # the separator line just before that header is now orphaned
        raw2 = raw2.replace(
            '// ---------------------------------------------------------------------------\n'
            + HEAD_4513D0,
            HEAD_4513D0, 1)
    raw = raw2

    # ---- 2. the AnimDraw hook ---------------------------------------------
    raw2, c = strip_block(
        raw,
        '// AnimClass::Draw  (0x423AC0)  --  per-animation country visibility',
        'BYTE* raw = reinterpret_cast<BYTE*>(pAnim);\n\t\traw[0x19A] = 1;\n\t}\n\n\treturn 0;\n}',
        HEAD_423AC0,
        'AnimDraw hook 0x423AC0')
    changed = changed or c
    if c:
        raw2 = raw2.replace(
            '// ---------------------------------------------------------------------------\n'
            + HEAD_423AC0,
            HEAD_423AC0, 1)
    raw = raw2

    # ---- 3. the two per-country calls in the 0x43B756 hook ------------------
    old_calls = re.search(
        r'\n\t// Make sure the type carries the country\'s animation names.*?'
        r'\t\tR->Stack<void\*\(0x4\),\n\t\treinterpret_cast<void\*>\(typeValue\)\);\n',
        raw, re.S)

    if old_calls:
        print('  %-28s : removed %d chars'
              % ('per-country calls in 43B756', old_calls.end() - old_calls.start()))
        raw = raw[:old_calls.start()] + '\n' + raw[old_calls.end():]
        changed = True
    else:
        print('  %-28s : pattern not found (already removed?)'
              % 'per-country calls in 43B756')

    if changed:
        if uses_crlf:
            raw = raw.replace('\n', '\r\n')
        shutil.copyfile(PATH, BAK)
        open(PATH, 'wb').write(raw.encode('latin-1'))
        print('\nwritten: %s   (backup: %s)' % (PATH, BAK))
    else:
        print('\nnothing changed')


if __name__ == '__main__':
    main()
