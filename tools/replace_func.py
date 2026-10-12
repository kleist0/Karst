"""Replace a whole function definition with new text, by name.

Used to rewrite Init() after the RuleSet (ShowByCountries/ShowBySHP) machinery
was removed: its entire body parsed those rules.

Usage:
    python replace_func.py <file> <name> <newtext-file>
"""
import re
import shutil
import sys


def find_span(text, name):
    pat = re.compile(r'(?<![\w:])' + re.escape(name) + r'\s*\(')

    for m in pat.finditer(text):
        start = text.rfind('\n', 0, m.start()) + 1
        line_end = text.find('\n', m.start())
        line = text[start:line_end if line_end > 0 else len(text)]

        # A mention inside a comment is not a definition. Skipping these is what
        # the first version of this script got wrong: it matched "Init()" in a
        # comment near the top of the file and replaced a 188-char region there.
        if line.strip().startswith('//'):
            continue

        # find the opening brace of the body
        j = text.find('{', m.end())
        if j < 0:
            continue

        # the brace must belong to this definition, i.e. there must be no ';'
        # between the signature and it
        if ';' in text[m.end():j]:
            continue

        depth = 0
        k = j
        while k < len(text):
            if text[k] == '{':
                depth += 1
            elif text[k] == '}':
                depth -= 1
                if depth == 0:
                    break
            k += 1

        return start, k + 1

    return None


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1

    path, name, newtext_path = sys.argv[1], sys.argv[2], sys.argv[3]

    raw = open(path, 'rb').read().decode('latin-1')
    uses_crlf = '\r\n' in raw
    text = raw.replace('\r\n', '\n')

    span = find_span(text, name)
    if not span:
        print('function %s not found' % name)
        return 1

    new = open(newtext_path, 'rb').read().decode('latin-1').replace('\r\n', '\n')
    start, end = span
    out = text[:start] + new + text[end:]

    if uses_crlf:
        out = out.replace('\n', '\r\n')

    shutil.copyfile(path, path + '.bak-replacefunc')
    open(path, 'wb').write(out.encode('latin-1'))
    print('replaced %s (%d chars -> %d chars)' % (name, end - start, len(new)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
