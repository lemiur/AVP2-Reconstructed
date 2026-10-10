r"""Source-only link of d3d.ren: build/release/d3d.ren from source-built objects and prebuilt libraries.

  python tools/source_link_d3dren.py [--no-icf] [--force] [--list] [--lanes A.csv B.csv ...]

Inputs (no byte of the retail d3d.ren is read):
  * the renderer objects compiled from src/d3dren (build/d3dren/base, every unit not under lib/);
  * the prebuilt StdLith objects named by config/d3dren/libraries.json (LT_StdLith: dynarray, l_allocator, struct_bank);
  * the VC6 RTM libraries (LIBCMT, KERNEL32, USER32, WINMM, OLDNAMES) and the DX 8.0 DDRAW/DXGUID libraries;
  * config/d3dren/d3dren.def (exports and ordinals) and config/d3dren/d3dren.rc (RC + CVTRES).

The report lists the unresolved externals LINK prints, grouped as data / functions / library. Data names that one of
the data lanes (sessions/d3dren_data_lane{A,B,C}.csv) owns are tagged with the lane, so a rerun after merging shows
what is left. With unresolved externals LINK writes no image unless --force (/FORCE:UNRESOLVED) is given; a forced
image is a diagnostic, not a build.

/OPT:ICF is the default: the original folded identical COMDATs (config/d3dren/icf.csv).  Measured against the original's
.text size (0x44890): ICF 0x44670, NOICF 0x450f0.  The one known over-fold is of the 1- and 3-byte `ret` / `xor eax,eax; ret`
functions of different units (d3d_BeginModelProjectionNoOp 0x1000dd18, d3d_NullCallback 0x100235e1, vq_DefaultFn1/2 0x1003451a,
d3d_PortalFileCommand 0x1002d07c, LightmapPage::IsRTexture 0x10034277), which the original kept apart.
"""
import argparse
import csv
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
os.environ['DECOMP_MODULE'] = 'd3dren'
sys.path.insert(0, str(ROOT / 'tools'))

import modcfg  # noqa: E402,F401
import build  # noqa: E402

MSVC = Path(r'E:\Program Files (x86)\Microsoft Visual Studio\VC98')
LINK = MSVC / 'Bin' / 'LINK.EXE'
CVTRES = MSVC / 'Bin' / 'CVTRES.EXE'
RC = Path(r'E:\Program Files (x86)\Microsoft Visual Studio\Common\MSDev98\Bin\RC.EXE')
DX8_LIB = Path(r'E:\AVP2Source\directx8-msdx8\lib')
OUT = ROOT / 'build' / 'release'
SCRATCH = OUT / 'scratch'
DEFAULT_LANES = [Path(r'E:\AVP2Source\sessions') / ('d3dren_data_lane%s.csv' % c) for c in 'ABC']
LINK_LIBS = ['LIBCMT.LIB', 'KERNEL32.LIB', 'USER32.LIB', 'WINMM.LIB', 'OLDNAMES.LIB', 'DDRAW.LIB', 'DXGUID.LIB']
ENTRY = '_DllMainCRTStartup@12'


def run(cmd, env):
    r = subprocess.run([str(c) for c in cmd], env=env, cwd=str(SCRATCH), stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, errors='replace')
    return r.returncode, r.stdout


def tool_env():
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join([str(LINK.parent), str(RC.parent), env.get('PATH', '')])
    # DX 8.0 import/GUID libraries first, then the VC6 libraries
    env['LIB'] = os.pathsep.join([str(DX8_LIB), str(MSVC / 'Lib')])
    return env


def source_objects():
    objs = []
    for u in build.find_units():
        if u.name.startswith('lib/'):
            continue
        if not os.path.exists(u.base_obj):
            raise SystemExit('missing object %s: run python tools/build.py --module d3dren first' % u.base_obj)
        objs.append(os.path.abspath(u.base_obj))
    return objs


def library_objects():
    """Prebuilt StdLith objects the original linked (config/d3dren/libraries.json); CRT objects come from LIBCMT.LIB."""
    data = json.load(open(ROOT / 'config' / 'd3dren' / 'libraries.json'))
    return [u['obj'] for u in data['units'] if u['lib'] == 'LT_StdLith']


def load_lanes(paths):
    owner = {}
    for p in paths:
        p = Path(p)
        if not p.exists():
            continue
        lane = p.stem[-1].upper()
        with open(p, newline='') as f:
            for row in csv.DictReader(f):
                owner[row['mangled']] = lane
    return owner


UNRES = re.compile(r'LNK2001|LNK2019')
DUP = re.compile(r'LNK2005')
MANGLED = re.compile(r'\((\?[^\s()]+)\)')
CNAME = re.compile(r'(?<![\w@?$])(_[A-Za-z0-9_$@]+)\s*$|(__imp_\S+)')


def symbol_of(line):
    """The decorated symbol of one LINK diagnostic. LINK's prose is localized, so only the symbol text is parsed:
    the mangled name in parentheses, else the plain C name."""
    body = line.split('error', 1)[-1]
    m = MANGLED.search(body)
    if m:
        return m.group(1)
    m = re.search(r'(__imp_\S+|(?<![\w@?$])_[A-Za-z0-9_$@]+)', body.split(':', 1)[-1])
    return m.group(1) if m else None


def parse_unresolved(text):
    """{symbol: [referencing object, ...]} from LINK's output (decorated names)."""
    found = {}
    for line in text.splitlines():
        if UNRES.search(line) and 'error' in line:
            sym = symbol_of(line)
            if sym:
                found.setdefault(sym, []).append(line.split(':')[0].strip())
    return found


def parse_duplicates(text):
    found = {}
    for line in text.splitlines():
        if DUP.search(line) and 'error' in line:
            sym = symbol_of(line)
            found.setdefault(sym, []).append(line.split(':')[0].strip())
    return found


def classify(name, lanes):
    """data / functions / library."""
    if name in lanes or '@@3' in name:
        return 'data'
    if name.startswith(('__imp_', '_IID_', '_CLSID_', '??2', '??3', '??_U', '??_V', '?sb_', '?MoArray_')):
        return 'library'
    if re.match(r'^_[A-Za-z0-9_]+(@\d+)?$', name):      # plain C names: CRT / DirectDraw / dxguid
        return 'library'
    return 'functions'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--no-icf', action='store_true', help='link with /OPT:REF,NOICF instead of the default /OPT:REF,ICF')
    ap.add_argument('--force', action='store_true',
                    help='/FORCE:UNRESOLVED: write a diagnostic image even with unresolved externals')
    ap.add_argument('--lanes', nargs='*', default=[str(p) for p in DEFAULT_LANES],
                    help='data-lane csv files (column "mangled")')
    ap.add_argument('--out', default=str(OUT / 'd3d.ren'))
    ap.add_argument('--list', action='store_true', help='also list every unresolved data name with its lane')
    a = ap.parse_args()

    for p in (LINK, CVTRES, RC):
        if not p.is_file():
            raise SystemExit('missing tool: %s' % p)
    SCRATCH.mkdir(parents=True, exist_ok=True)
    env = tool_env()
    out_img = Path(a.out)
    if out_img.exists():
        out_img.unlink()

    res = SCRATCH / 'd3dren.res'
    rsrc = SCRATCH / 'd3dren_rsrc.obj'
    rc_, o = run([RC, '/r', '/fo', res, ROOT / 'config' / 'd3dren' / 'd3dren.rc'], env)
    if rc_:
        print(o)
        raise SystemExit('RC failed')
    rc_, o = run([CVTRES, '/MACHINE:IX86', '/OUT:' + str(rsrc), res], env)
    if rc_:
        print(o)
        raise SystemExit('CVTRES failed')

    src = source_objects()
    lib = library_objects()
    opt = '/OPT:REF,NOICF' if a.no_icf else '/OPT:REF,ICF'
    rsp = ['/NOLOGO', '/DLL', '/NODEFAULTLIB', '/OUT:' + str(out_img), '/BASE:0x10000000',
           '/SUBSYSTEM:WINDOWS,4.0', '/ENTRY:' + ENTRY, opt, '/SECTION:.rsrc,R',
           '/DEF:' + str(ROOT / 'config' / 'd3dren' / 'd3dren.def'), '/MAP:' + str(SCRATCH / 'd3d.map')]
    if a.force:
        rsp.append('/FORCE:UNRESOLVED')
    rsp += ['"%s"' % o for o in src + lib + [str(rsrc)]] + LINK_LIBS
    (SCRATCH / 'link.rsp').write_text('\n'.join(rsp) + '\n')
    rc_, text = run([LINK, '@' + str(SCRATCH / 'link.rsp')], env)
    (SCRATCH / 'link.log').write_text(text)

    unresolved = parse_unresolved(text)
    lanes = load_lanes(a.lanes)
    groups = {'data': {}, 'functions': {}, 'library': {}}
    for n, who in unresolved.items():
        groups[classify(n, lanes)][n] = who

    print('objects: %d source, %d prebuilt StdLith, 1 resource; libraries: %s; %s' %
          (len(src), len(lib), ' '.join(LINK_LIBS), opt))
    for l in text.splitlines():
        if re.search(r'LNK\d+', l) and not UNRES.search(l) and not DUP.search(l):
            print('  ' + l.strip())
    dups = parse_duplicates(text)
    for n, who in sorted(dups.items()):
        print('  duplicate definition: %s  <- %s' % (n, ','.join(sorted(set(who)))))
    print('LINK exit %d; image %s' % (rc_, 'written' if out_img.exists() else 'NOT written'))
    print('unresolved externals: %d total  data %d  functions %d  library %d' %
          (len(unresolved), len(groups['data']), len(groups['functions']), len(groups['library'])))
    if lanes:
        left, total = {}, {}
        for lane in lanes.values():
            total[lane] = total.get(lane, 0) + 1
        for n in groups['data']:
            left[lanes.get(n, '-')] = left.get(lanes.get(n, '-'), 0) + 1
        for lane in sorted(total):
            print('  data lane %s: %d of %d still unresolved' % (lane, left.get(lane, 0), total[lane]))
        print('  data owned by no lane: %d' % left.get('-', 0))
        for n in sorted(n for n in groups['data'] if n not in lanes):
            print('    unowned: %s' % n)
    for g in ('functions', 'library'):
        for n, who in sorted(groups[g].items()):
            names = sorted(set(os.path.basename(w) for w in who))
            print('  %s: %s  <- %s' % (g, n, ','.join(names)[:100]))
    if a.list:
        for n in sorted(groups['data']):
            print('  data[%s]: %s' % (lanes.get(n, '-'), n))
    return 0 if rc_ == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
