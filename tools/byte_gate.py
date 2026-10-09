r"""Byte gate: the whole relinked image must have the original's SHA1, with as much of it as possible from source.

  python tools/byte_gate.py [--allow-dirty] [--clean] [--no-build] [--exclude u1,u2] [--max-runs N]
  python tools/build.py gate [...]                 (the same)

A function is banked only when the whole image, linked with that function compiled from our source, still hashes
to the original (config/check.sha1). Every other signal (build.py MATCH, objdiff, gate.py) is a filter feeding this
gate. The gate has two outcomes, GATE GREEN and GATE RED; "banked" may be claimed only from the GATE GREEN line.

What a run does:
  1. contract: config/check.sha1 holds the original's SHA1, file name and size; the configured original
     (modcfg.IMAGE) must still have that hash, or nothing runs;
  2. tree: refuses to run when src/, include/, tools/ or config/ have uncommitted changes (a gate measures
     committed source; --allow-dirty runs anyway and marks the result unofficial);
  3. build: `build.py` (full, content-stamped objects; --clean deletes every unit's stamp first so that every
     object is recompiled), judged by its exit code;
  4. baseline: a relink from the original's bytes only (relink.py --rich): it must hash green, or no verdict about
     source can be made (the gate's own machinery is broken);
  5. bank: a relink with every fully matched unit (relink.py's inventory: every annotated function MATCH) from its
     base object (relink.py --mode mixed --rich, into a fresh directory, output deleted first, linker exit code
     checked). Red: the differing bytes are attributed to units (functions via objvas.json + symbol extents,
     data via data_units.json); those units are excluded and the gate re-runs. When no candidate unit holds a
     differing byte, the candidates are bisected. Ends at the largest green set found;
  6. provenance: every banked unit's base object is in the link, its target object is not, and every function of
     the unit is defined at its original address by the merged object that holds the base object (LINK's map);
  7. count: banked code bytes, functions and units; data bytes of banked units that supply their own .rdata/.data.
     Functions whose body is inline assembly (`__asm`) are "verbatim" and not counted as source. Fully matched
     units the gate could not bank are listed (the second oracle disagrees: a layout or ordering problem).

Writes build/gate/byte_gate.json; an official run (clean tree) also writes progress/<version>/byte_gate.json.
Exit code: 0 green, 1 red, 2 the gate could not run.

Only lithtech.exe has a relink; `--module d3dren` reports that and exits 2.
"""
import argparse
import bisect
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import modcfg  # noqa: E402   (consumes --module)

GATE_DIR = os.path.join(modcfg.BUILD, 'gate')
CONTRACT = os.path.join(modcfg.CONFIG, 'check.sha1')
WATCHED = ('src', 'include', 'tools', 'config')
IMAGE_NAME = modcfg.OUT_IMAGE_NAME


def sha1(path):
    h = hashlib.sha1()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def read_contract():
    with open(CONTRACT) as f:
        fields = f.read().split()
    if len(fields) != 3 or not re.fullmatch(r'[0-9a-f]{40}', fields[0]):
        raise SystemExit('GATE ERROR: %s must hold "<sha1> <file name> <size>"' % CONTRACT)
    return fields[0], fields[1], int(fields[2])


def git(*args):
    r = subprocess.run(['git'] + list(args), cwd=ROOT, capture_output=True, text=True)
    return r.returncode, r.stdout.strip()


def run_relink(out, mode, exclude=()):
    """Fresh relink into `out`; returns (exe path or None, stdout, gate_units dict)."""
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    args = [sys.executable, os.path.join(TOOLS, 'relink.py'), '--rich']
    if mode == 'mixed':
        args += ['--mode', 'mixed']
        if exclude:
            args += ['--exclude', ','.join(sorted(exclude))]
    env = dict(os.environ, RELINK_OUT=os.path.normpath(out))
    r = subprocess.run(args, cwd=ROOT, env=env, capture_output=True, text=True)
    exe = os.path.join(out, IMAGE_NAME)
    log = r.stdout + r.stderr
    with open(os.path.join(out, 'relink.log'), 'w') as f:
        f.write(log)
    ok = r.returncode == 0 and re.search(r'^link rc 0$', r.stdout, re.M) and os.path.exists(exe)
    units = {}
    p = os.path.join(out, 'gate_units.json')
    if os.path.exists(p):
        with open(p) as f:
            units = json.load(f)
    return (exe if ok else None), log, units


class Attribution:
    """Maps a VA to the unit that owns it: functions through objvas.json and symbol extents, data through the
    relink's data_units.json."""

    def __init__(self, symtab):
        self.funcs = symtab.funcs
        self.starts = sorted(self.funcs)
        with open(os.path.join(modcfg.BUILD, 'objvas.json')) as f:
            self.objvas = json.load(f)
        self.owner = {va: u for u, vas in self.objvas.items() for va in vas}

    def func_at(self, va):
        k = bisect.bisect_right(self.starts, va) - 1
        if k < 0:
            return None
        f = self.starts[k]
        return f if va < self.funcs[f][0] else None

    def units_of_diff(self, orig, exe, out):
        """{unit: differing byte count} and {section: differing byte count} for one relinked image."""
        import pefile
        a, b = pefile.PE(orig), pefile.PE(exe)
        base = a.OPTIONAL_HEADER.ImageBase
        sb = {s.Name.rstrip(b'\0').decode('latin1'): s for s in b.sections}
        data_ranges = []
        p = os.path.join(out, 'data_units.json')
        if os.path.exists(p):
            with open(p) as f:
                for r in json.load(f):
                    data_ranges.append((int(r['start'], 16), int(r['end'], 16), r['unit']))
        data_ranges.sort()
        per_unit, per_sec = {}, {}
        for s in a.sections:
            n = s.Name.rstrip(b'\0').decode('latin1')
            da = s.get_data()
            db = sb[n].get_data() if n in sb else b''
            diff = [i for i in range(min(len(da), len(db))) if da[i] != db[i]]
            diff += list(range(min(len(da), len(db)), max(len(da), len(db))))
            per_sec[n] = len(diff)
            for i in diff:
                va = base + s.VirtualAddress + i
                if n == '.text':
                    f = self.func_at(va)
                    u = self.owner.get(f, '(no unit)') if f is not None else '(padding)'
                else:
                    k = bisect.bisect_right(data_ranges, (va, 0xFFFFFFFF, '')) - 1
                    u = data_ranges[k][2] if k >= 0 and data_ranges[k][0] <= va < data_ranges[k][1] else '(no unit)'
                per_unit[u] = per_unit.get(u, 0) + 1
        hdr = sum(1 for x, y in zip(open(orig, 'rb').read(0x400), open(exe, 'rb').read(0x400)) if x != y)
        if hdr:
            per_sec['(header)'] = hdr
        return per_unit, per_sec


class Banker:
    def __init__(self, want, orig, attr, max_runs, log):
        self.want, self.orig, self.attr, self.max_runs, self.log = want, orig, attr, max_runs, log
        self.runs = 0
        self.history = []

    def attempt(self, include, all_full):
        """One mixed relink with exactly `include` from base objects. Returns (green, exe, units, per_unit)."""
        self.runs += 1
        out = os.path.join(GATE_DIR, 'run%02d' % self.runs)
        exclude = set(all_full) - set(include)
        t = time.time()
        exe, log, units = run_relink(out, 'mixed', exclude)
        if exe is None:
            self.log('  run %d: relink FAILED (%d units); see %s' % (self.runs, len(include), os.path.join(out, 'relink.log')))
            self.history.append({'run': self.runs, 'units': len(include), 'result': 'relink failed'})
            return False, None, units, {}
        h = sha1(exe)
        green = h == self.want
        per_unit = {}
        if not green:
            per_unit, per_sec = self.attr.units_of_diff(self.orig, exe, out)
        else:
            per_sec = {}
        self.log('  run %d: %d units from source: %s (%.0f s)%s' % (
            self.runs, len(include), 'GREEN' if green else 'red', time.time() - t,
            '' if green else '; differing bytes: ' + ', '.join('%s %d' % kv for kv in sorted(per_sec.items()) if kv[1])))
        self.history.append({'run': self.runs, 'units': len(include), 'result': 'green' if green else 'red',
                             'sections': per_sec, 'units_with_diffs': per_unit})
        return green, exe, units, per_unit

    def find(self, cands, all_full, rejected):
        """Largest green subset of `cands` found by attribution and bisection; fills `rejected` {unit: reason}."""
        cands = sorted(cands)
        while True:
            if self.runs >= self.max_runs:
                self.log('  run budget (%d) exhausted' % self.max_runs)
                return None
            green, exe, units, per_unit = self.attempt(cands, all_full)
            if green:
                return cands
            fell = set(units.get('base_failed', []))
            culprits = {u for u in per_unit if u in cands} | (fell & set(cands))
            if not culprits:
                culprits = set(units.get('out_of_order', [])) & set(cands)
                why = 'emits functions out of address order (no differing byte attributed)'
            else:
                why = None
            if culprits:
                for u in culprits:
                    rejected[u] = why or ('base object fell back to its target' if u in fell else
                                          '%d differing bytes attributed' % per_unit.get(u, 0))
                self.log('  excluding %d: %s' % (len(culprits), ', '.join(sorted(culprits))))
                cands = [u for u in cands if u not in culprits]
                continue
            if len(cands) <= 1:
                for u in cands:
                    rejected[u] = 'red on its own (no attribution)'
                return []
            half = len(cands) // 2
            self.log('  no unit attributed: bisecting %d candidates' % len(cands))
            a = self.find(cands[:half], all_full, rejected)
            b = self.find(cands[half:], all_full, rejected)
            if a is None or b is None:
                return None
            if self.runs < self.max_runs:
                g, _, _, _ = self.attempt(a + b, all_full)
                if g:
                    return sorted(a + b)
            keep, drop = (a, b) if len(a) >= len(b) else (b, a)
            for u in drop:
                rejected.setdefault(u, 'conflicts with another green half')
            return keep


def parse_map(path):
    """{va: object file} for every public in LINK's map."""
    out = {}
    pat = re.compile(r'^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+(?:f\s+)?(?:i\s+)?(\S+)\s*$')
    with open(path, errors='replace') as f:
        for line in f:
            m = pat.match(line)
            if m:
                out.setdefault(int(m.group(2), 16), set()).add(m.group(3))
    return out


def verbatim_functions(units, names):
    """Annotated function VAs whose body contains inline assembly, per unit name in `names`."""
    out = {}
    ann = re.compile(r'^\s*//\s*(?:FUNCTION|STUB):\s*%s\s+0x([0-9a-fA-F]+)' % modcfg.TAG)
    by_name = {u.name: u for u in units}
    for n in names:
        u = by_name.get(n)
        if u is None:
            continue
        last = None
        with open(u.path, encoding='latin1') as f:
            for line in f:
                m = ann.match(line)
                if m:
                    last = int(m.group(1), 16)
                elif re.search(r'\b_*asm\b', line) and not line.lstrip().startswith('//') and last is not None:
                    out.setdefault(n, set()).add(last)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--allow-dirty', action='store_true', help='run on uncommitted changes (the result is unofficial)')
    ap.add_argument('--clean', action='store_true', help='recompile every unit (delete the objects' + chr(39) + ' stamps first)')
    ap.add_argument('--no-build', action='store_true', help='use the objects of the last build.py run')
    ap.add_argument('--exclude', default='', help='units never to take from source (comma-separated)')
    ap.add_argument('--max-runs', type=int, default=24, help='relink budget for banking (default 24)')
    a = ap.parse_args()
    t0 = time.time()
    lines = []

    def log(s):
        print(s, flush=True)
        lines.append(s)

    if modcfg.NAME != 'lithtech':
        print('GATE ERROR: %s has no relink yet; the byte gate covers lithtech.exe only' % modcfg.NAME)
        return 2
    want, name, size = read_contract()
    if name != IMAGE_NAME:
        print('GATE ERROR: the contract is for %s, this module builds %s' % (name, IMAGE_NAME))
        return 2
    import relink as R
    for p in (modcfg.IMAGE, R.EXE):
        if os.path.getsize(p) != size or sha1(p) != want:
            print('GATE ERROR: %s does not match the contract (%s, %d bytes)' % (p, want, size))
            return 2
    log('contract: %s %s %d bytes (original verified)' % (want, name, size))

    rc, head = git('rev-parse', 'HEAD')
    rc, dirty = git('status', '--porcelain', '--', *WATCHED)
    official = not dirty
    if dirty and not a.allow_dirty:
        print('GATE ERROR: uncommitted changes under %s (commit first, or --allow-dirty for an unofficial run):\n%s'
              % ('/, '.join(WATCHED) + '/', dirty))
        return 2
    log('source: %s%s' % (head[:10], '' if official else ' + uncommitted changes (UNOFFICIAL)'))

    if not a.no_build:
        import build as B
        if a.clean:
            n = 0
            for u in B.find_units():
                st = u.base_obj + '.stamp'
                if os.path.exists(st):
                    os.remove(st)
                    n += 1
            log('clean: %d object stamps removed (every unit recompiles)' % n)
        env = dict(os.environ, DECOMP_LEAD='1')
        t = time.time()
        r = subprocess.run([sys.executable, os.path.join(TOOLS, 'build.py')], cwd=ROOT, env=env,
                           capture_output=True, text=True)
        os.makedirs(GATE_DIR, exist_ok=True)
        with open(os.path.join(GATE_DIR, 'build.log'), 'w') as f:
            f.write(r.stdout + r.stderr)
        summary = [l for l in r.stdout.splitlines() if 'annotated functions match' in l]
        if r.returncode != 0:
            print('GATE ERROR: build.py failed (exit %d); see %s' % (r.returncode, os.path.join(GATE_DIR, 'build.log')))
            return 2
        log('build: exit 0 (%.0f s)%s' % (time.time() - t, '; ' + summary[0] if summary else ''))

    import build as B
    import io
    import contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        units, full, standin = R.inventory()
        symtab = B.SymTab()
    results = list(R.INV_RESULTS)
    status = {}
    for r in results:
        status[r.a.va] = r.status
    attr = Attribution(symtab)
    never = set(x for x in a.exclude.split(',') if x)
    cands = [u for u in full if u not in never]
    log('fully matched units (every annotated function MATCH): %d; candidates: %d' % (len(full), len(cands)))

    # baseline: the relink of the original's bytes must be green, or nothing measured below means anything
    t = time.time()
    exe, rlog, _ = run_relink(os.path.join(GATE_DIR, 'baseline'), 'targets')
    if exe is None or sha1(exe) != want:
        print('GATE RED: the baseline relink (original bytes only) is %s: the gate itself is broken; see %s' % (
            'not linked' if exe is None else 'not identical', os.path.join(GATE_DIR, 'baseline', 'relink.log')))
        return 1
    log('baseline: original bytes only: GREEN (%.0f s)' % (time.time() - t))

    rejected = {}
    banker = Banker(want, R.EXE, attr, a.max_runs, log)
    banked = banker.find(cands, full, rejected)
    if banked is None:
        print('GATE RED: no green set found within %d relinks' % a.max_runs)
        return 1
    last = os.path.join(GATE_DIR, 'run%02d' % banker.runs)
    # the last run is green for `banked` unless the bisection merged halves without a final run: re-run then
    if banker.history[-1]['result'] != 'green' or banker.history[-1]['units'] != len(banked):
        green, exe, _, _ = banker.attempt(banked, full)
        last = os.path.join(GATE_DIR, 'run%02d' % banker.runs)
        if not green:
            print('GATE RED: the final banked set did not reproduce the hash')
            return 1
    exe = os.path.join(last, IMAGE_NAME)
    if sha1(exe) != want:
        print('GATE RED: final image hash differs')
        return 1
    with open(os.path.join(last, 'gate_units.json')) as f:
        gu = json.load(f)

    # provenance: from what LINK actually received and where it put each function
    with open(os.path.join(last, 'rich', 'manifest.json')) as f:
        manifest = json.load(f)
    group_of = {}
    for g, members in manifest.items():
        for m in members:
            group_of[os.path.normcase(os.path.basename(m))] = g
    publics = parse_map(os.path.join(last, IMAGE_NAME.rsplit('.', 1)[0] + '.map'))
    bad = []
    for u in banked:
        if u not in gu.get('base', []):
            bad.append('%s: not linked from its base object' % u)
            continue
        base = os.path.normcase('base__' + u.replace('/', '__') + '.obj')
        targ = os.path.normcase(u.replace('/', '__') + '.obj')
        if base not in group_of:
            bad.append('%s: base object missing from the link' % u)
        if targ in group_of:
            bad.append('%s: target object also linked' % u)
        g = group_of.get(base)
        for va in attr.objvas.get(u, []):
            if g not in publics.get(va, set()):
                bad.append('%s: function %08x not defined by the object holding its base object' % (u, va))
    if bad:
        print('GATE RED: provenance check failed:\n  ' + '\n  '.join(bad[:30]))
        return 1

    # counting, from the source of the green image
    verb = verbatim_functions(units, banked)
    funcs = attr.funcs
    total_code = sum(end - va for va, (end, _) in funcs.items())
    n_f = code = 0
    v_f = v_b = 0
    for u in banked:
        for va in attr.objvas.get(u, []):
            if va not in funcs:
                continue
            sz = funcs[va][0] - va
            if va in verb.get(u, set()):
                v_f += 1
                v_b += sz
                continue
            n_f += 1
            code += sz
    data = 0
    own = set(gu.get('own_data', [])) & set(banked)
    p = os.path.join(last, 'data_units.json')
    if os.path.exists(p):
        with open(p) as f:
            for r in json.load(f):
                if r['unit'] in own and r['group'] in ('.rdata', '.data'):
                    data += int(r['end'], 16) - int(r['start'], 16)
    not_banked = sorted(set(full) - set(banked))
    for u in not_banked:
        rejected.setdefault(u, 'excluded by --exclude' if u in never else 'not banked')

    log('GATE GREEN sha1=%s' % want)
    log('banked from source: %d units, %d functions, %d code bytes (%.3f%% of %d function bytes); '
        '%d own-data units, %d .rdata/.data bytes' % (len(banked), n_f, code, 100.0 * code / total_code, total_code,
                                                    len(own), data))
    if v_f:
        log('verbatim (inline assembly, not counted): %d functions, %d bytes: %s' % (
            v_f, v_b, ', '.join('%08x' % va for s in verb.values() for va in sorted(s))))
    if not_banked:
        log('fully matched but not banked (second oracle disagrees: a layout or ordering problem): %d' % len(not_banked))
        for u in not_banked:
            log('  %-40s %s' % (u, rejected[u]))
    log('relinks: %d (+ baseline), %.0f s' % (banker.runs, time.time() - t0))

    result = {
        'module': modcfg.NAME, 'contract': want, 'image': name, 'commit': head, 'official': official,
        'date': datetime.datetime.now().strftime('%Y-%m-%d %H:%M'),
        'banked_units': banked, 'functions': n_f, 'code_bytes': code, 'function_bytes_total': total_code,
        'own_data_units': sorted(own), 'data_bytes': data,
        'verbatim': {u: ['%08x' % va for va in sorted(s)] for u, s in verb.items()},
        'not_banked': {u: rejected[u] for u in not_banked},
        'relinks': banker.history,
    }
    with open(os.path.join(GATE_DIR, 'byte_gate.json'), 'w') as f:
        json.dump(result, f, indent=1, sort_keys=True)
    if official:
        d = os.path.join(ROOT, 'progress', modcfg.REPORT_VERSION)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, 'byte_gate.json'), 'w', newline='\n') as f:
            json.dump(result, f, indent=1, sort_keys=True)
            f.write('\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
