"""Delete a DEFINE_HOOK block (and its leading comment) by handler name.

DEFINE_HOOK(<addr>, <name>, <size>) { ... } - the handler name is the stable
handle, so that is what this matches on.

Usage:
    python strip_hook.py <file> <handler-name> [<handler-name> ...]
"""
import re
import shutil
import sys


def find_hook(lines, name):
    pat = re.compile(r'DEFINE_HOOK\s*\([^,]+,\s*' + re.escape(name) + r'\s*,')

    for idx, line in enumerate(lines):
        if line is None or not pat.search(line):
            continue

        # opening brace
        brace = None
        for k in range(idx, min(idx + 3, len(lines))):
            if lines[k] is not None and lines[k].strip() == '{':
                brace = k
                break
        if brace is None:
            continue

        depth = 0
        for k in range(brace, len(lines)):
            if lines[k] is None:
                continue
            depth += lines[k].count('{')
            depth -= lines[k].count('}')
            if depth <= 0:
                start = idx
                while (start > 0 and lines[start - 1] is not None
                       and lines[start - 1].strip().startswith('//')):
                    start -= 1
                return start, k

    return None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    path = sys.argv[1]
    names = sys.argv[2:]

    raw = open(path, 'rb').read().decode('latin-1')
    uses_crlf = '\r\n' in raw
    lines = raw.replace('\r\n', '\n').split('\n')

    removed, missing = [], []
    for name in names:
        span = find_hook(lines, name)
        if not span:
            missing.append(name)
            continue
        s, e = span
        removed.append((name, s + 1, e + 1))
        for k in range(s, e + 1):
            lines[k] = None

    if not removed:
        print('nothing matched')
        for n in missing:
            print('  MISSING: %s' % n)
        return 1

    out = '\n'.join(l for l in lines if l is not None)
    if uses_crlf:
        out = out.replace('\n', '\r\n')

    shutil.copyfile(path, path + '.bak-striphook')
    open(path, 'wb').write(out.encode('latin-1'))

    for name, s, e in removed:
        print('  removed hook %-42s lines %d-%d' % (name, s, e))
    for n in missing:
        print('  MISSING: %s' % n)
    print('\nwritten: %s' % path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
