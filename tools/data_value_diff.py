r"""Compare the initial VALUES of the source link's data with retail: every mapped data symbol, byte for byte.

  python tools/data_value_diff.py [--module d3dren|lithtech] [--image I] [--map M] [--json out.json]
                                  [--obj substring] [--all] [-v]

Read-only. The byte gates take .data/.rdata from retail, so the initial values of source-defined globals (console
variable tables, vtables, function-pointer tables, constant tables, strings, FP constants) are compared nowhere else.
Inputs as for tools/data_extent_audit.py (it pairs every data symbol of the link with a retail address; this tool
reuses that pairing): the release image and its map (d3dren: `python tools/source_link_d3dren.py`; engine:
`python tools/build.py release`).

For every data symbol of the link in .data or .rdata that has a retail address, the bytes of
[symbol, symbol + n) are compared with retail, where n is the declared size (distance to the next symbol of the link)
capped by the retail span to the next mapped retail symbol:
  * a relocated field (a base relocation of the link; for an image without relocations, a dword that is an image
    address on both sides) is a pointer and is compared by TARGET: our target symbol (data symbol + offset, function,
    import slot) is mapped to its retail address, which must equal retail's value. A target without a retail address
    is `unverified`, not a mismatch;
  * a pointer on one side only is a mismatch (`pointer/value`);
  * every other byte is compared raw.
Symbols in the zero-filled tail (.bss) read as zero, so `.bss here, initialised in retail` and the reverse show as
value mismatches; they are labelled. String literals paired by content are equal by construction and only counted.
Mismatches are grouped by the owning object (unit). Exit status 0; the report is evidence, not a verdict.
"""
import argparse
import bisect
import json
import os
import struct
import sys
from collections import defaultdict

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)

import modcfg  # noqa: E402  (consumes --module)
import data_extent_audit as dea  # noqa: E402


def imports(img):
    """{IAT slot va: 'dll!name'} of a PE image."""
    pe = img.pe
    pe.parse_data_directories(directories=[dea.pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
    out = {}
    for d in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
        for i in d.imports:
            nm = i.name.decode() if i.name else '#%d' % i.ordinal
            out[i.address] = '%s!%s' % (d.dll.decode().lower(), nm)
    return out


def pair_objsym(au):
    """Data symbols named in the checker's per-object name map (build/<module>/objsym.json: target object ->
    {symbol: retail va}, learned from the relocations of matching code and from Ghidra) get that retail address."""
    path = os.path.join(modcfg.BUILD, 'objsym.json')
    if not os.path.exists(path):
        return 0
    objsym = json.load(open(path))
    by_obj = defaultdict(list)
    for unit in objsym:
        by_obj[os.path.basename(unit) + '.obj'].append(unit)
    n = 0
    for va, name, f, o in au.syms:
        if f:
            continue
        units = by_obj.get(o, [])
        if len(units) != 1:
            continue
        rva = objsym[units[0]].get(name)
        if rva is not None:
            au.observe(va, rva, 'objsym')
            n += 1
    return n


class ValueDiff:
    def __init__(self, au, annots, rfuncs):
        self.au = au
        self.R, self.O = au.R, au.O
        self.fmap = defaultdict(set)        # our function va -> {retail va}
        by_name = defaultdict(set)
        for va, n, f, o in au.syms:
            if f:
                for k in (n, dea.undecorate_bare(n)):
                    by_name[k].add(va)
                    by_name[(k, o)].add(va)
        for rva, kind, mangled, name, obj in annots:
            if kind == 'GLOBAL':
                continue
            key = mangled if mangled else (name or '')
            c = by_name.get(key, set())
            if len(c) != 1:
                c = by_name.get((key, obj), set())
            if len(c) == 1:
                self.fmap[next(iter(c))].add(rva)
        for mangled, rva in dea.library_functions().items():
            c = by_name.get(mangled, set())
            if len(c) == 1:
                self.fmap[next(iter(c))].add(rva)
        self.ofuncs = {va: n for va, n, f, o in au.syms if f}
        ri, oi = imports(self.R), imports(self.O)
        rname = {v: k for k, v in ri.items()}
        self.imap = {va: rname.get(n) for va, n in oi.items()}
        self.iname = oi
        rtext = self.R.secs['.text']
        self.rtext = (rtext[0], rtext[0] + rtext[1])
        otext = self.O.secs['.text']
        self.otext = (otext[0], otext[0] + otext[1])
        # the link's own .bss start (the map's section table), else the raw end of .data
        self.obss = self.O.secs['.data'][0] + self.O.secs['.data'][2]
        self.rbss = self.R.secs['.data'][0] + self.R.secs['.data'][2]
        self.rel_none = not self.R.relocs and not self.O.relocs
        self.rimg = (self.R.base, self.R.base + self.R.pe.OPTIONAL_HEADER.SizeOfImage)
        self.oimg = (self.O.base, self.O.base + self.O.pe.OPTIONAL_HEADER.SizeOfImage)

    def set_bss_start(self, mapfile):
        for line in open(mapfile, encoding='latin1'):
            p = line.split()
            if len(p) >= 3 and p[2] == '.bss' and ':' in p[0]:
                sec, off = p[0].split(':')
                idx = int(sec, 16) - 1
                s = self.O.pe.sections[idx]
                self.obss = self.O.base + s.VirtualAddress + int(off, 16)
                return

    def target(self, t):
        """Retail address(es) our pointer value t stands for, and a label; (None, label) when unknown."""
        if t in self.imap:
            r = self.imap[t]
            return ({r} if r else None), 'import %s' % self.iname[t]
        if self.otext[0] <= t < self.otext[1]:
            if t in self.fmap:
                return self.fmap[t], 'func %s' % self.ofuncs.get(t, '%08x' % t)
            return None, 'code %s' % self.ofuncs.get(t, '%08x' % t)
        s = self.au.our_sym_at(t)
        if s is None:
            return None, 'outside %08x' % t
        if s in self.au.where:
            return {self.au.where[s] + (t - s[0])}, '%s+%x' % (s[1], t - s[0])
        return None, 'unmapped %s+%x' % (s[1], t - s[0])

    def same_string(self, ov, rv):
        """Both pointers reach the same NUL-terminated text (an unnamed literal, $SG): equal by content."""
        a, b = self.O.read(ov, 256), self.R.read(rv, 256)
        ea, eb = a.find(b'\0'), b.find(b'\0')
        return ea > 0 and ea == eb and a[:ea] == b[:eb] and all(32 <= c < 127 or c in (9, 10, 13) for c in a[:ea])

    def init_table(self, au):
        """The C++ dynamic initialiser table (___xc_a .. ___xc_z): our initialisers as retail addresses against
        retail's table, as sets (the order is the link order of the objects)."""
        names = {n: va for va, n, f, o in au.syms}
        if '___xc_a' not in names or '___xc_z' not in names:
            return None
        lo, hi = names['___xc_a'], names['___xc_z']
        ours = [self.O.dword(x) for x in range(lo, hi, 4) if self.O.dword(x)]
        rb = au.where.get(next((s for s in au.where if s[1] == '___xc_a'), None))
        if rb is None:
            return None
        theirs = []
        x = rb
        while True:
            x += 4
            v = self.R.dword(x)
            if not v:
                break
            theirs.append(v)
        mapped, extra = [], []
        for v in ours:
            want, lab = self.target(v)
            if want:
                mapped.append(min(want))
            else:
                extra.append((v, lab))
        rset = set(theirs)
        missing = [v for v in theirs if v not in set(mapped)]
        wrong = [v for v in mapped if v not in rset]
        same_order = [v for v in mapped if v in rset] == [v for v in theirs if v in set(mapped)]
        return {'ours': len(ours), 'retail': len(theirs), 'unmapped_ours': extra, 'retail_missing': missing,
                'not_in_retail': wrong, 'same_order': same_order}

    def collapse_order(self, s, rb, diffs):
        """A pointer table whose wrong targets are a permutation of retail's values: one `order` entry."""
        bad = [d for d in diffs if d[1] == 'pointer']
        if len(bad) < 2:
            return diffs
        ours = []
        for d in bad:
            want, _ = self.target(d[2])
            ours.append(min(want))
        if sorted(ours) != sorted(d[3] for d in bad):
            return diffs
        rest = [d for d in diffs if d[1] != 'pointer']
        return [(bad[0][0], 'order', len(bad), len(bad),
                 '%d pointers: retail targets in another order (link order)' % len(bad))] + rest

    def r_image(self, v):
        return self.rimg[0] + 0x1000 <= v < self.rimg[1]

    def o_image(self, v):
        return self.oimg[0] + 0x1000 <= v < self.oimg[1]

    def compare(self, s, rb, n):
        """[(offset, kind, ours, retail, note)] for symbol s at retail base rb over n bytes; plus counters."""
        out, stats = [], defaultdict(int)
        ob = self.O.read(s[0], n)
        rbb = self.R.read(rb, n)
        if self.rel_none:
            # no base relocations: an aligned dword that is an image address on both sides and differs is a pointer
            # (string literals hold no pointers)
            optr = set()
            if not s[1].startswith('??_C@'):
                for k in range((-s[0]) & 3, n - 3, 4):
                    ov = struct.unpack_from('<I', ob, k)[0]
                    rv = struct.unpack_from('<I', rbb, k)[0]
                    if ov != rv and self.o_image(ov) and self.r_image(rv):
                        optr.add(k)
            rptr = set(optr)
        else:
            optr = {x - s[0] for x in self.O.relocs_in(s[0], s[0] + n) if x + 4 <= s[0] + n}
            rptr = {x - rb for x in self.R.relocs_in(rb, rb + n) if x + 4 <= rb + n}
        covered = set()
        for k in sorted(optr | rptr):
            covered.update(range(k, k + 4))
            ov = struct.unpack_from('<I', ob, k)[0]
            rv = struct.unpack_from('<I', rbb, k)[0]
            if k in optr and k in rptr:
                want, lab = self.target(ov)
                if want is None and self.same_string(ov, rv):
                    stats['ptr_ok_content'] += 1
                elif want is None:
                    stats['unverified'] += 1
                    out.append((k, 'unverified', ov, rv, lab))
                elif rv in want:
                    stats['ptr_ok'] += 1
                else:
                    stats['ptr_bad'] += 1
                    out.append((k, 'pointer', ov, rv, '%s -> retail %s' % (lab, '/'.join('%08x' % w for w in sorted(want)))))
            else:
                stats['ptr_one_side'] += 1
                lab = self.target(ov)[1] if k in optr else 'retail pointer'
                out.append((k, 'pointer/value', ov, rv, ('ours is a pointer (%s)' % lab) if k in optr else
                            'retail is a pointer, ours a value'))
        run = None
        for k in range(n):
            if k in covered:
                continue
            if ob[k] != rbb[k]:
                if run and run[1] == k:
                    run[1] = k + 1
                else:
                    run = [k, k + 1]
                    out.append(run)
        res = []
        for x in out:
            if isinstance(x, list):
                a, b = x
                res.append((a, 'value', ob[a:b].hex(), rbb[a:b].hex(), ''))
            else:
                res.append(x)
        res.sort(key=lambda t: t[0])
        return res, stats


def run(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--retail', default=modcfg.IMAGE)
    rel = os.path.join(dea.ROOT, 'build', 'release')
    d3d = modcfg.NAME == 'd3dren'
    ap.add_argument('--image', default=os.path.join(rel, 'd3d.ren' if d3d else 'lithtech.exe'))
    ap.add_argument('--map', default=os.path.join(rel, 'scratch', 'd3d.map') if d3d else os.path.join(rel, 'lithtech.map'))
    ap.add_argument('--json', help='write the full report as JSON')
    ap.add_argument('--obj', help='only objects whose name contains this')
    ap.add_argument('--all', action='store_true', help='also list unverified pointers')
    ap.add_argument('--lib', action='store_true', help='also compare library objects (LIBCMT:x.obj, import libraries)')
    ap.add_argument('-v', action='store_true', help='print every differing field, not just the first 8 per symbol')
    args = ap.parse_args(argv)
    for p in (args.retail, args.image, args.map):
        if not os.path.exists(p):
            raise SystemExit('missing %s (link the source build first)' % p)

    au = dea.Audit(args.retail, args.image, args.map)
    annots = dea.load_annotations()
    rfuncs = dea.retail_functions()
    au.rnames = dea.retail_names()
    au.pair_globals(annots)
    pair_objsym(au)
    au.pair_code(annots, rfuncs)
    au.pair_data()
    au.pair_literals()
    au.infer_layout()
    vd = ValueDiff(au, annots, rfuncs)
    vd.set_bss_start(args.map)

    # retail starts of every mapped symbol: the span cap
    prebuilt = {o for o, p in dea.link_objects(args.map).items()
                if not os.path.normcase(os.path.abspath(p)).startswith(os.path.normcase(os.path.join(dea.ROOT, 'build')))}
    rstarts = sorted(set(au.where.values()))
    imports_r = imports(au.R)
    rsec = {n: (lo, lo + vsz) for n, (lo, vsz, raw) in au.R.secs.items()}

    def rsec_of(va):
        for n, (lo, hi) in rsec.items():
            if lo <= va < hi:
                return n, hi
        return None, va

    groups = defaultdict(list)
    totals = defaultdict(int)
    seen_va = set()
    covered = set()
    for s, rb in sorted(au.where.items()):
        va, name, obj = s
        if va in seen_va:
            continue
        if not (au.odata[0] <= va < au.odata[1] or au.ordata[0] <= va < au.ordata[1]):
            continue
        if args.obj and args.obj.lower() not in obj.lower():
            continue
        seen_va.add(va)
        rs, rend = rsec_of(rb)
        if rs not in ('.data', '.rdata'):
            continue
        i = bisect.bisect_right(rstarts, rb)
        rnext = min(rstarts[i] if i < len(rstarts) else rend, rend)
        n = min(au.size.get(s, 0), rnext - rb)
        if n <= 0:
            continue
        totals['symbols'] += 1
        totals['bytes'] += n
        covered.update(range(rb, rb + n))
        if au.how.get(s) == 'content':
            totals['literal_by_content'] += 1
            continue
        if va in vd.iname:
            totals['imports'] += 1
            rn = imports_r.get(rb)
            if rn != vd.iname[va]:
                groups[obj].append({'name': name, 'ours': '%08x' % va, 'retail': '%08x' % rb, 'size': 4,
                                    'sections': '.rdata/.rdata', 'how': au.how.get(s),
                                    'diffs': [{'off': 0, 'kind': 'import', 'ours': vd.iname[va], 'retail': str(rn),
                                               'note': ''}]})
                totals['mismatched_symbols'] += 1
            continue
        if name in ('___xc_a', '___xc_z'):
            continue
        if (':' in obj or obj in prebuilt) and not args.lib:
            totals['library_skipped'] += 1
            continue
        diffs, st = vd.compare(s, rb, n)
        diffs = vd.collapse_order(s, rb, diffs)
        for k, v in st.items():
            totals[k] += v
        osec = '.rdata' if au.ordata[0] <= va < au.ordata[1] else ('.bss' if va >= vd.obss else '.data')
        rsec_l = '.rdata' if rs == '.rdata' else ('.bss' if rb >= vd.rbss else '.data')
        real = [d for d in diffs if d[1] != 'unverified']
        if osec != rsec_l and (osec == '.rdata' or rsec_l == '.rdata'):
            real.insert(0, (0, 'section', osec, rsec_l, 'ours %s, retail %s' % (osec, rsec_l)))
        if not real and not (args.all and diffs):
            continue
        if real:
            totals['mismatched_symbols'] += 1
        groups[obj].append({'name': name, 'ours': '%08x' % va, 'retail': '%08x' % rb, 'size': n,
                            'sections': '%s/%s' % (osec, rsec_l), 'how': au.how.get(s),
                            'diffs': [{'off': d[0], 'kind': d[1],
                                       'ours': d[2] if isinstance(d[2], str) else '%08x' % d[2],
                                       'retail': d[3] if isinstance(d[3], str) else '%08x' % d[3],
                                       'note': d[4]} for d in (diffs if args.all else real)]})

    print('%s: %d data symbols compared (%d bytes); %d literals equal by content' % (
        modcfg.NAME, totals['symbols'], totals['bytes'], totals['literal_by_content']))
    print('imports compared by name: %d; library-object symbols skipped: %d (--lib)' % (
        totals['imports'], totals['library_skipped']))
    # coverage: retail's initialised non-zero bytes of .data and .rdata (import tables excluded) that a compared
    # symbol spans; the rest has no paired source symbol and was not compared
    for sec in ('.data', '.rdata'):
        lo, vsz, raw = au.R.secs[sec]
        blob = au.R.read(lo, min(vsz, raw))
        skip = set()
        if sec == '.rdata':
            pe = au.R.pe
            for d in (1, 12):       # import directory + names, IAT
                dd = pe.OPTIONAL_HEADER.DATA_DIRECTORY[d]
                skip.update(range(au.R.base + dd.VirtualAddress, au.R.base + dd.VirtualAddress + dd.Size))
            imp = pe.OPTIONAL_HEADER.DATA_DIRECTORY[1]
            if imp.Size:            # hint/name tables follow the descriptors up to the export directory
                ex = pe.OPTIONAL_HEADER.DATA_DIRECTORY[0]
                end = au.R.base + (ex.VirtualAddress if ex.Size else imp.VirtualAddress + 0x800)
                skip.update(range(au.R.base + imp.VirtualAddress, end))
                skip.update(range(au.R.base + ex.VirtualAddress, au.R.base + ex.VirtualAddress + ex.Size))
        nz = [lo + k for k, b in enumerate(blob) if b and lo + k not in skip]
        cov = sum(1 for x in nz if x in covered)
        print('retail %s: %d of %d initialised non-zero bytes lie in a compared symbol (%.1f%%)' % (
            sec, cov, len(nz), 100.0 * cov / max(1, len(nz))))
        totals['coverage' + sec] = [cov, len(nz)]
    print('pointers: %d by target OK (+%d unnamed literals equal by content), %d wrong target, %d pointer on one side only, %d unverified (target unmapped)' % (
        totals['ptr_ok'], totals['ptr_ok_content'], totals['ptr_bad'], totals['ptr_one_side'], totals['unverified']))
    it = vd.init_table(au)
    if it:
        print('initialiser table: ours %d, retail %d; same relative order: %s; retail initialisers we lack: %s; ours not '
              'in retail: %s; ours without a retail address: %s' % (
                  it['ours'], it['retail'], it['same_order'],
                  ' '.join('%08x' % v for v in it['retail_missing']) or '-',
                  ' '.join('%08x' % v for v in it['not_in_retail']) or '-',
                  ' '.join(l for v, l in it['unmapped_ours']) or '-'))
        totals['init_table'] = {k: (v if not isinstance(v, list) else len(v)) for k, v in it.items()}
    print('symbols with a mismatch: %d in %d objects' % (totals['mismatched_symbols'],
                                                         sum(1 for g in groups.values() if any(
                                                             any(d['kind'] != 'unverified' for d in e['diffs']) for e in g))))
    for obj in sorted(groups):
        print('\n== %s' % obj)
        for e in groups[obj]:
            print('  %s  ours %s retail %s  0x%x bytes  %s  [%s]' % (e['name'], e['ours'], e['retail'], e['size'],
                                                                    e['sections'], e['how']))
            ds = e['diffs'] if args.v else e['diffs'][:8]
            for d in ds:
                print('    +%-5x %-13s ours %-18s retail %-18s %s' % (d['off'], d['kind'], d['ours'][:18], d['retail'][:18],
                                                                    d['note']))
            if len(e['diffs']) > len(ds):
                print('    ... %d more' % (len(e['diffs']) - len(ds)))
    if args.json:
        with open(args.json, 'w') as f:
            json.dump({'totals': totals, 'objects': groups}, f, indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(run())
