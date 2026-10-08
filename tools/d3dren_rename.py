r"""Rename identifiers in the d3d.ren sources (src/d3dren, include): whole-word replacement, code and comments alike.

  python tools/d3dren_rename.py RENAMES.json [--dry-run]

RENAMES.json: [{"old": "FUN_10023d80", "new": "d3d_DrawLineSystem", "va": "10023d80", "why": "..."}, ...]
Refuses a rename whose new name is already an identifier in the code (comments and strings aside) unless "allow": true,
and an old name that occurs nowhere.  Files keep their bytes apart from the replaced words (line endings untouched).
Afterwards: python tools/build.py --module d3dren check d3dren, python tools/gate.py, then the Ghidra sync."""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COM = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
STR = re.compile(r'"(?:\\.|[^"\\\n])*"')


def files():
    out = glob.glob(os.path.join(ROOT, 'src', 'd3dren', '**', '*.cpp'), recursive=True)
    out += glob.glob(os.path.join(ROOT, 'include', '**', '*.h'), recursive=True)
    return sorted(out)


def main():
    renames = json.load(open(sys.argv[1]))
    dry = '--dry-run' in sys.argv
    texts = {p: open(p, 'rb').read().decode('latin1') for p in files()}
    code_words = set()
    for t in texts.values():
        code_words.update(re.findall(r'[A-Za-z_]\w*', STR.sub('""', COM.sub(' ', t))))
    bad = []
    olds = {r['old'] for r in renames}
    for r in renames:
        if r['new'] in code_words and r['new'] not in olds and not r.get('allow'):
            bad.append('%s -> %s: new name already used in the code' % (r['old'], r['new']))
        if not any(re.search(r'\b%s\b' % re.escape(r['old']), t) for t in texts.values()):
            bad.append('%s: not found' % r['old'])
    news = [r['new'] for r in renames]
    for n in set(news):
        same = [r for r in renames if r['new'] == n]
        if len({r['old'] for r in same}) > 1 and not all(r.get('allow') for r in same):   # e.g. a virtual and its override
            bad.append('%s: target of several renames' % n)
    if bad:
        print('\n'.join(bad))
        sys.exit(1)
    pat = re.compile(r'\b(%s)\b' % '|'.join(re.escape(r['old']) for r in sorted(renames, key=lambda r: -len(r['old']))))
    to = {r['old']: r['new'] for r in renames}
    changed, total = 0, 0
    for p, t in texts.items():
        new, n = pat.subn(lambda m: to[m.group(1)], t)
        if n:
            changed += 1
            total += n
            if not dry:
                open(p, 'wb').write(new.encode('latin1'))
    print('%d renames, %d occurrences in %d files%s' % (len(renames), total, changed, ' (dry run)' if dry else ''))


if __name__ == '__main__':
    main()
