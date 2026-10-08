r"""Which d3d.ren definitions still carry generated names (FUN_/DAT_) and what names are proposed for them (read-only).

  python tools/d3dren_names_pending.py [--json OUT]

For every // FUNCTION: / // STUB: / // GLOBAL: D3DREN annotation in src/d3dren the defined identifier is parsed from the
definition that follows it.  A definition is "generated" when that identifier is FUN_xxxxxxxx / DAT_xxxxxxxx.  Each one is
listed with the config/d3dren/names_proposal.csv row of its address (name, confidence, provenance) and the name in
config/d3dren/symbols.csv (the Ghidra export), when that is not a default name."""
import csv
import glob
import json
import os
import re
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANN = re.compile(r'^// (FUNCTION|STUB|GLOBAL): D3DREN 0x([0-9a-f]+)[ \t]*(\S*)[^\n]*$', re.M)
GENERATED = re.compile(r'^(FUN|DAT)_[0-9a-f]{8}$')
DEFAULT = re.compile(r'^(FUN|DAT|PTR|LAB|thunk_FUN|switchD|caseD)_')


def defined_name(kind, line):
    """Identifier a definition line defines: the (possibly Class::) name before '(' for functions; for data the
    declarator name before '[', '=', '(' (constructor arguments) or ';'."""
    line = line.split('//')[0].strip()
    if kind == 'GLOBAL':
        m = re.search(r'([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*(?:=|\(|;|$)', line)
        return m.group(1) if m else None
    m = re.search(r'((?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*)\s*\(', line)
    return m.group(1) if m else None


def load():
    defs = []
    for p in sorted(glob.glob(os.path.join(ROOT, 'src', 'd3dren', '**', '*.cpp'), recursive=True)):
        text = open(p, encoding='latin1').read()
        for m in ANN.finditer(text):
            kind, va, explicit = m.group(1), int(m.group(2), 16), m.group(3)
            if explicit:            # the annotation names its symbol (a COMDAT copy or a mangled name): not generated
                name = explicit
            else:                   # the definition after the block of comment lines
                rest = text[m.end() + 1:].split('\n')
                line = next((l for l in rest if not l.lstrip().startswith('//') and l.strip()), '')
                name = defined_name(kind, line)
            defs.append({'va': va, 'kind': kind, 'file': os.path.relpath(p, ROOT).replace('\\', '/'), 'name': name})
    prop = {int(r['address'], 16): r for r in csv.DictReader(open(os.path.join(ROOT, 'config', 'd3dren', 'names_proposal.csv'), encoding='utf-8'))}
    ghidra = {}
    for r in csv.reader(open(os.path.join(ROOT, 'config', 'd3dren', 'symbols.csv'), encoding='utf-8')):
        if len(r) > 3 and r[2] in ('func', 'data'):
            try:
                ghidra.setdefault(int(r[0], 16), r[3])
            except ValueError:
                pass
    return defs, prop, ghidra


def main():
    defs, prop, ghidra = load()
    out, stats = [], Counter()
    for d in defs:
        short = (d['name'] or '').split('::')[-1]
        if not GENERATED.match(short):
            continue
        p = prop.get(d['va'])
        g = ghidra.get(d['va'], '')
        row = dict(d, va='%08x' % d['va'],
                   proposal=p['name'] if p else '', confidence=p['confidence'] if p else '', provenance=p['provenance'] if p else '',
                   evidence=p['evidence'] if p else '', ghidra=g if g and not DEFAULT.match(g.split('::')[-1]) else '')
        out.append(row)
        stats[(d['kind'], row['provenance'] or '-', row['confidence'] or '-')] += 1
    print('%d generated definitions (%d functions, %d globals)' % (len(out), sum(1 for r in out if r['kind'] != 'GLOBAL'),
                                                                 sum(1 for r in out if r['kind'] == 'GLOBAL')))
    for k, v in sorted(stats.items(), key=lambda kv: -kv[1]):
        print('  %-7s %-12s %-7s %d' % (k + (v,))[:4] if False else '  %-7s %-12s %-7s %d' % (k[0], k[1], k[2], v))
    print('  with a non-default Ghidra name: %d' % sum(1 for r in out if r['ghidra']))
    if len(sys.argv) > 2 and sys.argv[1] == '--json':
        json.dump(out, open(sys.argv[2], 'w'), indent=1)


if __name__ == '__main__':
    main()
