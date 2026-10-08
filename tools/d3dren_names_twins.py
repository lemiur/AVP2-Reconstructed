r"""Engine twins of d3d.ren functions that still carry generated names (read-only).

  python tools/d3dren_names_twins.py [--json OUT]

For every generated d3d.ren function (tools/d3dren_names_pending.py) look for a lithtech.exe function that is
  exact  - the same bytes with in-image addresses and call/jump displacements masked (tools/d3dren_names_xmatch.py), or
  mnem   - the same mnemonic sequence (registers / operands may differ),
and that is the only such function on both sides.  The engine name comes from the engine module's last full build
(E:\AVP2Source\decomp\build\namemap.json, names proven by matching code) or else its Ghidra export (config/symbols.csv)."""
import csv
import hashlib
import json
import os
import re
import sys

import capstone
import pefile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import d3dren_names_pending as P  # noqa: E402
import d3dren_names_xmatch as X  # noqa: E402

ROOT = os.path.dirname(HERE)
ENGINE = r'E:\AVP2Source\decomp'
REN = r'E:\AVP2Source\bin\talon\d3d.ren'
EXE = r'E:\AVP2Source\bin\lithtech.exe'
DEFAULT = re.compile(r'^(FUN|DAT|PTR|LAB|thunk_FUN)_')


def funcs(symcsv):
    return {a: v for a, v in X.load_funcs(symcsv).items()}


def sigs(img, fl):
    pe = pefile.PE(img, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    lo, hi = base, base + pe.OPTIONAL_HEADER.SizeOfImage
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    out = {}
    for fa, (fe, fn) in fl.items():
        try:
            h, n = X.masked_hash(pe, base, lo, hi, fa, fe, md)
            code = pe.get_data(fa - base, fe - fa)
        except Exception:
            continue
        mn = [i.mnemonic for i in md.disasm(code, fa) if i.mnemonic not in ('nop', 'int3')]
        out[fa] = (h, hashlib.md5(' '.join(mn).encode()).hexdigest(), fe - fa, len(mn))
    return out


def main():
    defs, prop, ghidra = P.load()
    pending = {d['va'] for d in defs if P.GENERATED.match((d['name'] or '').split('::')[-1]) and d['kind'] != 'GLOBAL'}
    rf = funcs(os.path.join(ROOT, 'config', 'd3dren', 'symbols.csv'))
    ef = funcs(os.path.join(ENGINE, 'config', 'symbols.csv'))
    rs, es = sigs(REN, rf), sigs(EXE, ef)
    nm = {int(a, 16): n for a, n in json.load(open(os.path.join(ENGINE, 'build', 'namemap.json'))).items()}
    lib = json.load(open(os.path.join(ROOT, 'config', 'd3dren', 'libraries.json')))
    libf = {int(a, 16) for u in lib['units'] for a in u['functions']}
    rows = []
    for key, idx in (('exact', 0), ('mnem', 1)):
        eby, rby = {}, {}
        for a, s in es.items():
            eby.setdefault(s[idx], []).append(a)
        for a, s in rs.items():
            rby.setdefault(s[idx], []).append(a)
        for a in sorted(pending):
            if a not in rs or a in libf or any(r['va'] == a for r in rows):
                continue
            s = rs[a]
            if s[3] < 4:
                continue        # too small to identify
            cands = eby.get(s[idx], [])
            if len(cands) != 1 or len(rby[s[idx]]) != 1:
                continue
            e = cands[0]
            name = nm.get(e) or ef[e][1]
            proven = e in nm
            if DEFAULT.match(name.split('::')[-1]):
                continue
            rows.append({'va': a, 'exe': e, 'how': key, 'size': s[2], 'exe_size': es[e][2], 'name': name, 'proven': proven})
    for r in rows:
        print('%08x %-5s %4d  exe %08x %4d  %s%s' % (r['va'], r['how'], r['size'], r['exe'], r['exe_size'], r['name'],
                                                     '' if r['proven'] else '   (Ghidra name, not proven by matching code)'))
    print('%d twins for %d generated functions' % (len(rows), len(pending)))
    if len(sys.argv) > 2 and sys.argv[1] == '--json':
        json.dump(rows, open(sys.argv[2], 'w'), indent=1)


if __name__ == '__main__':
    main()
