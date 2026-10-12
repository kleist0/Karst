"""Delete an inclusive line range from a file, preserving CRLF.

Small helper for removing blocks that are not single functions (a namespace
plus its trailing free functions, for example).

Usage:
    python strip_lines.py <file> <first> <last>
"""
import shutil
import sys


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1

    path = sys.argv[1]
    first, last = int(sys.argv[2]), int(sys.argv[3])

    raw = open(path, 'rb').read().decode('latin-1')
    uses_crlf = '\r\n' in raw
    lines = raw.replace('\r\n', '\n').split('\n')

    n = len(lines)
    if first < 1 or last > n or first > last:
        print('bad range %d-%d (file has %d lines)' % (first, last, n))
        return 1

    removed = lines[first - 1:last]
    del lines[first - 1:last]

    out = '\n'.join(lines)
    if uses_crlf:
        out = out.replace('\n', '\r\n')

    shutil.copyfile(path, path + '.bak-striplines')
    open(path, 'wb').write(out.encode('latin-1'))

    print('deleted lines %d-%d (%d lines, %d chars)'
          % (first, last, len(removed), sum(len(l) + 1 for l in removed)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
