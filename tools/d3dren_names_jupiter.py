r"""Cross-match d3d.ren functions that still carry generated names with Jupiter's renderer source (read-only).

  python tools/d3dren_names_jupiter.py [--json OUT] [--all]

Both sides are split into function bodies (top-level definitions: brace matching, comments and strings skipped).
Features of a body: its string literals, the console variables / globals it names (g_CV_*, g_*), the named functions it
calls and the struct members it uses (->m_X / .m_X).  Our side uses only names that are not generated (FUN_/DAT_/m_Unk),
so nothing invented by us can make a match.  Score = weighted overlap / weighted union (strings 6, convars 3,
globals 2, calls 2, members 1).  A candidate is reported when its score is >= 0.25, at least 1.6x the runner-up's and it
shares at least one string, convar or two named calls; --all prints every pending function's best two.

Jupiter: E:\AVP2Source\jupiter\runtime\render_a\src (sys/d3d and shared renderer files)."""
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import d3dren_names_pending as P  # noqa: E402

ROOT = os.path.dirname(HERE)
JUP = r'E:\AVP2Source\jupiter\runtime\render_a\src'
KEYWORDS = set('if while for switch return sizeof catch else do new delete operator defined case'.split())
GENERATED = re.compile(r'(FUN|DAT|PTR|LAB)_[0-9a-f]{6,8}|m_Unk\w*|UnkType_\w*|guess_\w*|_\$E\w*')
STRING = re.compile(r'"((?:\\.|[^"\\\n])*)"')
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)


def strip(text):
    """Comments removed, string contents kept (they are features) but braces inside them hidden."""
    text = COMMENT.sub(' ', text)
    return STRING.sub(lambda m: '"' + m.group(1).replace('{', '\x01').replace('}', '\x02') + '"', text)


def functions(path):
    """[(name, body)] of the top-level function definitions of a C++ file."""
    raw = open(path, encoding='latin1').read()
    text = strip(raw)
    out, depth, start, i = [], 0, 0, 0
    while i < len(text):
        c = text[i]
        if c == '{':
            if depth == 0:
                head = text[start:i]
                m = re.search(r'((?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*)\s*\(([^()]|\([^()]*\))*\)\s*(const\s*)?(:[^{;]*)?$', head.strip())
                body_start = i
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                if m and m.group(1).split('::')[-1] not in KEYWORDS:
                    out.append((m.group(1), text[body_start:i + 1]))
                start = i + 1
                m = None
        elif c == ';' and depth == 0:
            start = i + 1
        i += 1
    return out


def features(body):
    f = {}
    for s in STRING.findall(body):
        s = s.replace('\x01', '{').replace('\x02', '}')
        if len(s) >= 6 and re.search(r'[A-Za-z]{3}', s):
            f['s:' + s] = 6
    for name in re.findall(r'\b(g_CV_\w+|g_\w+)\b', body):
        if not GENERATED.search(name):
            f[('c:' if name.startswith('g_CV_') else 'g:') + name] = 3 if name.startswith('g_CV_') else 2
    for name in re.findall(r'\b([A-Za-z_]\w*)\s*\(', body):
        if name not in KEYWORDS and not GENERATED.search(name) and len(name) > 3:
            f['f:' + name] = 2
    for name in re.findall(r'(?:->|\.)(m_[A-Za-z]\w*)', body):
        if not GENERATED.search(name):
            f['m:' + name] = 1
    for name in re.findall(r'\b([A-Z][A-Z0-9]*_[A-Z0-9_]{2,})\b', body):     # D3D states, enums, flags
        name = name.replace('D3DRENDERSTATE_', 'D3DRS_')                       # D3D7 vs D3D8 spelling
        if not GENERATED.search(name) and name not in ('LTNULL', 'LT_OK'):
            f['k:' + name] = 1
    return f


def score(a, b):
    keys = set(a) | set(b)
    inter = sum(min(a.get(k, 0), b.get(k, 0)) for k in keys)
    union = sum(max(a.get(k, 0), b.get(k, 0)) for k in keys)
    return inter / union if union else 0.0


def main():
    show_all = '--all' in sys.argv
    jup = []
    for p in glob.glob(os.path.join(JUP, '**', '*.cpp'), recursive=True):
        if 'nulldib' in p:
            continue
        for name, body in functions(p):
            fe = features(body)
            if fe:
                jup.append((name, os.path.relpath(p, JUP), fe))
    defs, prop, ghidra = P.load()
    ours_named = {(d['name'] or '').split('::')[-1] for d in defs if d['name'] and not P.GENERATED.match(d['name'].split('::')[-1])}
    pending = {}
    for d in defs:
        short = (d['name'] or '').split('::')[-1]
        if d['kind'] != 'GLOBAL' and P.GENERATED.match(short):
            pending.setdefault(d['va'], d)
    bodies = {}
    for p in set(d['file'] for d in pending.values()):
        for name, body in functions(os.path.join(ROOT, p)):
            bodies.setdefault((p, name.split('::')[-1]), body)
    rows = []
    for va, d in sorted(pending.items()):
        body = bodies.get((d['file'], d['name'].split('::')[-1]))
        if not body:
            continue
        fo = features(body)
        if not fo:
            continue
        ranked = sorted(((score(fo, fj), n, f, fj) for n, f, fj in jup), key=lambda t: -t[0])[:2]
        best, second = ranked[0], ranked[1] if len(ranked) > 1 else (0, '', '', {})
        shared = set(fo) & set(best[3])
        strong = any(k[:2] in ('s:', 'c:') for k in shared) or sum(1 for k in shared if k.startswith('f:')) >= 2
        ok = best[0] >= 0.25 and best[0] >= 1.6 * second[0] and strong
        taken = best[1].split('::')[-1] in ours_named
        if ok or show_all:
            rows.append({'va': '%08x' % va, 'ours': d['name'], 'file': d['file'], 'jupiter': best[1], 'jfile': best[2],
                         'score': round(best[0], 3), 'second': second[1], 'second_score': round(second[0], 3),
                         'accepted': ok and not taken, 'taken': taken,
                         'shared': sorted(k for k in shared if k[:2] in ('s:', 'c:', 'f:'))[:8]})
    for r in rows:
        print('%s %-30s -> %-38s %.2f (2nd %-28s %.2f)%s%s  %s' % (
            r['va'], r['ours'][:30], r['jupiter'][:38], r['score'], r['second'][:28], r['second_score'],
            '' if r['accepted'] else '  [not accepted]', '  [name already used]' if r['taken'] else '', '; '.join(r['shared'])[:140]))
    print('%d accepted of %d pending functions' % (sum(r['accepted'] for r in rows), len(pending)))
    if '--json' in sys.argv:
        json.dump(rows, open(sys.argv[sys.argv.index('--json') + 1], 'w'), indent=1)


if __name__ == '__main__':
    main()
