"""Delete whole function definitions (plus their leading comment block) by name.

Written for removing the per-country machinery from ShowByCountry.cpp: those
functions are large and hand-editing them is error-prone.

Brace counting is done LINE BY LINE, which is safe here because this codebase
uses the MSVC style where the opening and closing braces of a function body sit
alone on their own lines. A signature is only accepted when a line whose
stripped form ends with '{' follows it, so calls and declarations are skipped.

Usage:
    python strip_funcs.py <file> <name> [<name> ...]
"""
import re
import shutil
import sys


def find_function(lines, name):
    """Return (start_line, end_line) inclusive, or None.

    Entries already blanked out by an earlier pass are None and are skipped, so
    several functions can be stripped in one run.
    """
    # A name containing "::" is a member definition (RuleSet::Parse); the prefix
    # assertion must not apply to it, and it cannot collide with a call site
    # because qualified names only appear at the definition.
    if '::' in name:
        sig_re = re.compile(re.escape(name) + r'\s*\(')
    else:
        sig_re = re.compile(r'(?<![\w:])' + re.escape(name) + r'\s*\(')

    for idx, line in enumerate(lines):
        if line is None:
            continue

        stripped = line.strip()

        # skip comments and forward declarations
        if stripped.startswith('//') or stripped.endswith(';'):
            continue
        if not sig_re.search(line):
            continue

        # the opening brace must be the last thing on some line at or after the
        # signature, and the definition must not be a one-liner
        brace_line = None
        for k in range(idx, min(idx + 4, len(lines))):
            if lines[k] is not None and lines[k].strip().endswith('{'):
                brace_line = k
                break
        if brace_line is None:
            continue

        # count braces line by line from the opening brace
        depth = 0
        for k in range(brace_line, len(lines)):
            if lines[k] is None:
                continue
            depth += lines[k].count('{')
            depth -= lines[k].count('}')
            if depth <= 0:
                # include a trailing comment block right before the signature
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
    text = raw.replace('\r\n', '\n')
    lines = text.split('\n')

    removed = []
    missing = []

    for name in names:
        span = find_function(lines, name)
        if not span:
            missing.append(name)
            continue

        start, end = span
        removed.append((name, start + 1, end + 1, end - start + 1))
        # blank the range so later searches keep their line numbers valid
        for k in range(start, end + 1):
            lines[k] = None

    if not removed:
        print('nothing matched')
        for n in missing:
            print('  MISSING: %s' % n)
        return 1

    out = [l for l in lines if l is not None]
    result = '\n'.join(out)
    if uses_crlf:
        result = result.replace('\n', '\r\n')

    shutil.copyfile(path, path + '.bak-stripfuncs')
    open(path, 'wb').write(result.encode('latin-1'))

    for name, s, e, n in removed:
        print('  removed %-34s lines %d-%d (%d lines)' % (name, s, e, n))
    for n in missing:
        print('  MISSING: %s' % n)
    print('\nwritten: %s   (backup: %s.bak-stripfuncs)' % (path, path))
    return 0


if __name__ == '__main__':
    sys.exit(main())
