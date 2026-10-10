r"""Audit data extents: which retail .data/.bss bytes have no source-defined counterpart, and does code reach into them?

  python tools/data_extent_audit.py [--module d3dren] [--map <link map>] [--image <linked image>]
                                    [--min 0x10] [--json out.json] [--hole 0x1004ebb0]

Read-only. Inputs: the retail image (modcfg.IMAGE, or --retail), the source-only link (its map and image: for d3dren
`python tools/source_link_d3dren.py` -> build/release/d3d.ren + scratch/d3d.map; for the engine `python tools/build.py
release` -> build/release/lithtech.exe + lithtech.map), the source annotations and config/<module>/symbols.csv.
Images without base relocations (the /FIXED engine) are paired operand by operand instead of by relocation; data-to-data
pairing and the relocation scan then have nothing to read, so the engine report is coarser.
VC6 keeps unreferenced file-static data and emits no symbol for it: such storage shows up as a hole with no reference.

1. Every data symbol of the source link gets a retail address:
   * from its `// GLOBAL:` annotation (source unit or include/ header), else
   * from code: each annotated FUNCTION/STUB (and libmatch library function) of the link is walked instruction by
     instruction against its retail twin until the first differing instruction (relocated fields and rel32 branch
     displacements masked); each pair of relocated operands names (our target, retail target), else
   * from data: a mapped data symbol whose relocated fields line up with retail's pairs their targets too, else
   * from layout: an unmapped symbol between two symbols of the same object that share one displacement gets it.
   An interior target (g_X+8) maps the symbol base (target - offset); conflicting observations are reported.
2. A symbol's declared size is the distance to the next symbol of the link (alignment padding included).
3. Retail .data/.bss bytes outside every [retail base, retail base + declared size) are holes. Each hole reports
   the preceding mapped symbol, its declared size against the retail span up to the next mapped symbol, whether the
   hole's retail bytes are non-zero (initialised data without an owner), and how retail code reaches it:
   * `direct`: an operand or a base relocation whose target lies in the hole;
   * `indexed`: an operand into the preceding symbol with an index register (disp + reg*scale);
   * `address`: the preceding symbol's address taken as an immediate (pointer walk / memset / memcpy base), with the
     nearest `mov ecx, N` / `push N` constants before a `rep` or call within the next few instructions;
   * `bound`: `cmp reg, N` compares in functions that index the symbol, N * scale beyond its declared size.
   The verdict is evidence for a person to read, not a fix.
"""
import argparse
import bisect
import csv
import json
import os
import re
import struct
import sys
from collections import defaultdict

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)

import modcfg  # noqa: E402  (consumes --module)
import capstone  # noqa: E402
import pefile  # noqa: E402
from capstone import x86 as cx  # noqa: E402

MAP_RE = re.compile(r'^\s*([0-9a-f]{4}):([0-9a-f]{8})\s+(\S+)\s+([0-9a-f]{8})\s+(f\s+)?(i\s+)?(\S+)\s*$')


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path, fast_load=True)
        self.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_BASERELOC']])
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.secs = {}
        for s in self.pe.sections:
            name = s.Name.rstrip(b'\0').decode()
            self.secs[name] = (self.base + s.VirtualAddress, s.Misc_VirtualSize, s.SizeOfRawData)
        self.relocs = set()
        for blk in getattr(self.pe, 'DIRECTORY_ENTRY_BASERELOC', []):
            for e in blk.entries:
                if e.type == 3:
                    self.relocs.add(self.base + e.rva)
        self.reloc_sorted = sorted(self.relocs)

    def read(self, va, n):
        for lo, vsz, raw in self.secs.values():
            if lo <= va < lo + max(vsz, raw):
                rva = va - self.base
                have = max(0, min(n, lo + raw - va))
                head = self.pe.get_data(rva, have) if have else b''
                return head + bytes(n - len(head))
        return bytes(n)

    def dword(self, va):
        return struct.unpack('<I', self.read(va, 4))[0]

    def in_sec(self, va, name):
        lo, vsz, _ = self.secs[name]
        return lo <= va < lo + vsz

    def relocs_in(self, lo, hi):
        i = bisect.bisect_left(self.reloc_sorted, lo)
        out = []
        while i < len(self.reloc_sorted) and self.reloc_sorted[i] < hi:
            out.append(self.reloc_sorted[i])
            i += 1
        return out


def parse_map(path):
    """[(va, name, is_func, obj)] of every public and static symbol of a LINK 6 map."""
    syms = []
    for line in open(path, encoding='latin1'):
        m = MAP_RE.match(line)
        if m and m.group(1) != '0000':
            syms.append((int(m.group(4), 16), m.group(3), bool(m.group(5)), m.group(7)))
    return syms


def link_objects(mapfile):
    """{map object name: COFF path} of the link's explicit objects (link.rsp next to the map) and of the CRT objects
    (LIBCMT:x.obj -> build/<module>/base/lib/*/x.obj when present: an approximation, checked by placement)."""
    out = {}
    rsp = os.path.join(os.path.dirname(mapfile), 'link.rsp')
    if os.path.exists(rsp):
        for line in open(rsp, encoding='latin1'):
            p = line.strip().strip('"')
            if p.lower().endswith('.obj') and os.path.exists(p):
                out.setdefault(os.path.basename(p), p)
    libdir = os.path.join(modcfg.BUILD, 'base', 'lib')
    if os.path.isdir(libdir):
        for d in sorted(os.listdir(libdir)):
            if 'LIBCMT' not in d:
                continue
            for f in os.listdir(os.path.join(libdir, d)):
                out.setdefault('LIBCMT:' + f, os.path.join(libdir, d, f))
    return out


def code_located_sections(co, oname, pub, img):
    """{section index: base va} of co's non-COMDAT .data/.bss sections, read from the linked image: every DIR32
    relocation of a located code section of co that targets such a section gives base = image dword - addend -
    symbol value (majority over all of them). Exact where code references the section; works without base relocations."""
    from collections import Counter
    from coffobj import REL_DIR32
    votes = defaultdict(Counter)
    for sec in co.sections:
        if not sec.name.startswith('.text'):
            continue
        sbase = None
        for x in sec.syms:
            if x.cls in (2, 3) and not x.is_section_symbol and (oname, x.name) in pub:
                sbase = pub[(oname, x.name)] - x.value
                break
        if sbase is None:
            continue
        for off, si, typ in sec.relocs:
            if typ != REL_DIR32 or off + 4 > len(sec.data):
                continue
            t = co.symbols.get(si)
            if t is None or not (0 < t.secno <= len(co.sections)):
                continue
            tsec = co.sections[t.secno - 1]
            if tsec.name not in ('.data', '.bss') or tsec.flags & 0x1000:
                continue
            addend = struct.unpack_from('<I', sec.data, off)[0]
            votes[t.secno][(img.dword(sbase + off) - addend - t.value) & 0xffffffff] += 1
    return {k: v.most_common(1)[0][0] for k, v in votes.items()}


def add_static_data(syms, mapfile, odata, img=None):
    """The map lists no static data. Locate every .data/.bss section of the link's objects (with the linked image:
    from the code relocations that reach it, see code_located_sections; else from a public symbol of the section,
    else right after the preceding contribution of the same object or link order when it fits before the next
    located one) and add its static symbols. Returns the added symbols and the located sections."""
    from coffobj import CoffObj
    pub = {}
    for va, n, f, o in syms:
        pub[(o, n)] = va
    objs = link_objects(mapfile)
    first = {}
    for va, n, f, o in syms:
        if f:
            first[o] = min(first.get(o, va), va)
    order = []      # (obj, section, base or None), in link order (= the order of the objects' code)
    for oname in sorted((o for o in objs if o in first), key=lambda o: first[o]):
        path = objs[oname]
        try:
            co = CoffObj(path)
        except (OSError, ValueError):
            continue
        exact = code_located_sections(co, oname, pub, img) if img is not None else {}
        for s in co.sections:
            if s.name not in ('.data', '.bss') or s.flags & 0x1000:      # COMDAT literals: public, in the map
                continue
            base = exact.get(s.index)
            for x in ([] if base is not None else s.syms):
                if x.cls == 2 and (oname, x.name) in pub:
                    base = pub[(oname, x.name)] - x.value
                    break
            order.append((oname, s, base))
    located = [(b, b + len(s.data), o, s) for o, s, b in order if b is not None]
    located.sort(key=lambda t: t[0])
    starts = [t[0] for t in located]
    added = []
    for oname, s, base in order:
        if base is None:
            continue
        for x in s.syms:
            if x.cls == 3 and not x.is_section_symbol and x.name[0] != '$' and x.value < len(s.data):
                added.append((base + x.value, x.name, False, oname))
    # sections without a public symbol: right after the previous contribution of the same group in link order
    # (.bss has no COMDATs, so this is exact there; in .data literal COMDATs may intervene), if it fits
    last_end = {}
    for k, (oname, s, base) in enumerate(order):
        if base is None and s.syms and k and s.name in last_end:
            align = 1 << (((s.flags >> 20) & 0xf) - 1) if (s.flags >> 20) & 0xf else 16
            b = (last_end[s.name] + align - 1) & ~(align - 1)
            i = bisect.bisect_left(starts, b)
            nxt = starts[i] if i < len(starts) else odata[1]
            if b + len(s.data) <= nxt:
                base = b
                order[k] = (oname, s, b)
                for x in s.syms:
                    if x.cls in (2, 3) and not x.is_section_symbol and x.value < len(s.data):
                        added.append((b + x.value, x.name, False, oname))
        if base is not None:
            last_end[s.name] = base + len(s.data)
    return added


def undecorate_bare(name):
    """Bare identifier of a mangled data/function name: ?g_X@@3HA -> g_X, ?m@Cls@@2HA -> Cls::m, _x -> x."""
    if name.startswith('?'):
        parts = name[1:].split('@@')[0].split('@')
        if parts and parts[0] and not parts[0].startswith('?'):
            return '::'.join(reversed(parts))
        return name
    return name[1:] if name.startswith('_') else name


def load_annotations():
    """(retail va, kind, mangled or None, bare name) for every FUNCTION/STUB/GLOBAL annotation of the module."""
    import build
    out = []
    for u in build.find_units():
        for a in u.annots:
            out.append((a.va, a.kind, a.mangled, a.name, os.path.basename(u.name) + '.obj'))
    for va, mangled, name, _ in build.header_globals():
        out.append((va, 'GLOBAL', mangled, name, None))
    return out


def retail_functions():
    funcs = {}
    for r in csv.DictReader(open(modcfg.SYMBOLS_CSV, encoding='utf-8')):
        if r['kind'] == 'func':
            a, e = int(r['addr'], 16), int(r['end'], 16)
            if e > a:
                funcs[a] = (e, r['name'])
    return funcs


def retail_names():
    names = {}
    for r in csv.DictReader(open(modcfg.SYMBOLS_CSV, encoding='utf-8')):
        names.setdefault(int(r['addr'], 16), r['name'])
    return names


def library_functions():
    path = os.path.join(modcfg.CONFIG, 'libraries.json')
    out = {}
    if os.path.exists(path):
        for u in json.load(open(path)).get('units', []):
            for va, name in u.get('functions', {}).items():
                out[name] = int(va, 16)
    return out


class Audit:
    def __init__(self, retail, ours, mapfile, data_sec='.data'):
        self.R, self.O = Image(retail), Image(ours)
        self.syms = parse_map(mapfile)
        lo, vsz, _ = self.O.secs[data_sec]
        self.syms += add_static_data(self.syms, mapfile, (lo, lo + vsz), self.O)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        lo, vsz, _ = self.O.secs[data_sec]
        self.odata = (lo, lo + vsz)
        lo, vsz, _ = self.R.secs[data_sec]
        self.rdata = (lo, lo + vsz)
        rlo, rvsz, _ = self.R.secs['.rdata']
        self.rrdata = (rlo, rlo + rvsz)
        olo, ovsz, _ = self.O.secs['.rdata']
        self.ordata = (olo, olo + ovsz)
        # every symbol of the link, sorted, with its size (distance to the next symbol in the same section)
        self.by_va = sorted({(va, n, f, o) for va, n, f, o in self.syms})
        self.vas = [s[0] for s in self.by_va]
        self.size = {}
        for i, (va, n, f, o) in enumerate(self.by_va):
            nxt = self.vas[i + 1] if i + 1 < len(self.vas) else None
            sec_end = self.odata[1] if self.odata[0] <= va < self.odata[1] else (
                self.ordata[1] if self.ordata[0] <= va < self.ordata[1] else None)
            end = min(x for x in (nxt, sec_end) if x is not None) if (nxt or sec_end) else va
            if sec_end is not None and nxt is not None and nxt > sec_end:
                end = sec_end
            self.size[(va, n, o)] = end - va
        self.obs = defaultdict(lambda: defaultdict(list))   # (va, name, obj) -> retail base -> [evidence]
        self.how = {}

    # -------------------------------------------------------------- symbol lookup
    def our_sym_at(self, va):
        """(va, name, obj) of the link symbol containing va (data sections only)."""
        if not (self.odata[0] <= va < self.odata[1] or self.ordata[0] <= va < self.ordata[1]):
            return None
        i = bisect.bisect_right(self.vas, va) - 1
        while i >= 0 and self.by_va[i][2]:
            i -= 1
        if i < 0:
            return None
        s = self.by_va[i]
        # several names at one address (ICF, aliases): the first is the representative
        return (s[0], s[1], s[3])

    def observe(self, ours_va, retail_va, why):
        s = self.our_sym_at(ours_va)
        if s is None:
            return
        if not (self.rdata[0] <= retail_va < self.rdata[1] or self.rrdata[0] <= retail_va < self.rrdata[1]):
            return
        off = ours_va - s[0]
        self.obs[s][retail_va - off].append(why)

    # -------------------------------------------------------------- pairing
    def walk_pair(self, ova, rva, rend, label):
        """Walk our function at ova against retail [rva, rend) until the first differing instruction."""
        n = rend - rva
        rb, ob = self.R.read(rva, n), self.O.read(ova, n + 16)
        ri = list(self.md.disasm(rb, rva))
        oi = list(self.md.disasm(ob, ova))
        use_relocs = bool(self.R.relocs) and bool(self.O.relocs)
        for a, b in zip(ri, oi):
            if a.size != b.size or a.id != b.id:
                return
            pairs = self.operand_pairs(a, b) if not use_relocs else self.reloc_pairs(a, b)
            if pairs is None:
                return
            for ours, theirs in pairs:
                self.observe(ours, theirs, '%s+%x' % (label, a.address - rva))

    def reloc_pairs(self, a, b):
        """(our target, retail target) of the relocated fields of two instructions, None when they differ."""
        rrel = self.R.relocs_in(a.address, a.address + a.size)
        orel = self.O.relocs_in(b.address, b.address + b.size)
        if [x - a.address for x in rrel] != [x - b.address for x in orel]:
            return None
        mask = set()
        for x in rrel:
            mask.update(range(x - a.address, x - a.address + 4))
        if a.group(capstone.CS_GRP_JUMP) or a.group(capstone.CS_GRP_CALL):
            if a.operands and a.operands[0].type == cx.X86_OP_IMM:
                mask.update(range(a.size - 4 if a.size >= 5 else a.size - 1, a.size))
        if any(a.bytes[k] != b.bytes[k] for k in range(a.size) if k not in mask):
            return None
        return [(self.O.dword(y), self.R.dword(x)) for x, y in zip(rrel, orel)]

    def operand_pairs(self, a, b):
        """Images without base relocations (a /FIXED exe): operands must agree except image addresses."""
        if a.group(capstone.CS_GRP_JUMP) or a.group(capstone.CS_GRP_CALL):
            if a.operands and a.operands[0].type == cx.X86_OP_IMM:
                return []
        if len(a.operands) != len(b.operands):
            return None
        out = []
        for x, y in zip(a.operands, b.operands):
            if x.type != y.type:
                return None
            if x.type == cx.X86_OP_REG:
                if x.reg != y.reg:
                    return None
            elif x.type == cx.X86_OP_MEM:
                if (x.mem.base, x.mem.index, x.mem.scale, x.mem.segment) != (y.mem.base, y.mem.index, y.mem.scale, y.mem.segment):
                    return None
                dx, dy = x.mem.disp & 0xffffffff, y.mem.disp & 0xffffffff
                if dx != dy:
                    if not (self.r_image(dx) and self.o_image(dy)):
                        return None
                    out.append((dy, dx))
                elif self.r_image(dx):
                    out.append((dy, dx))
            elif x.type == cx.X86_OP_IMM:
                vx, vy = x.imm & 0xffffffff, y.imm & 0xffffffff
                if vx != vy:
                    if not (self.r_image(vx) and self.o_image(vy)):
                        return None
                    out.append((vy, vx))
        return out

    def r_image(self, v):
        return self.R.base + 0x1000 <= v < self.rdata[1]

    def o_image(self, v):
        return self.O.base + 0x1000 <= v < self.odata[1]

    def pair_code(self, annots, rfuncs):
        by_name = defaultdict(list)
        for va, n, f, o in self.syms:
            if f:
                for k in (n, undecorate_bare(n)):
                    by_name[k].append(va)
                    by_name[(k, o)].append(va)
        done = set()
        for rva, kind, mangled, name, obj in annots:
            if kind == 'GLOBAL' or rva not in rfuncs:
                continue
            key = mangled if mangled else (name or '')
            cands = set(by_name.get(key, []))
            if len(cands) != 1:
                cands = set(by_name.get((key, obj), []))
            if len(cands) != 1:
                continue
            ova = cands.pop()
            if (ova, rva) in done:
                continue
            done.add((ova, rva))
            self.walk_pair(ova, rva, rfuncs[rva][0], name or mangled)
        for mangled, rva in library_functions().items():
            cands = set(by_name.get(mangled, []))
            if len(cands) == 1 and rva in rfuncs:
                ova = cands.pop()
                if (ova, rva) not in done:
                    done.add((ova, rva))
                    self.walk_pair(ova, rva, rfuncs[rva][0], mangled)

    def pair_globals(self, annots):
        by_name = defaultdict(set)
        for va, n, f, o in self.syms:
            if not f:
                for k in (n, undecorate_bare(n)):
                    by_name[k].add((va, n, o))
        for rva, kind, mangled, name, obj in annots:
            if kind != 'GLOBAL':
                continue
            cands = by_name.get(mangled, set()) if mangled else by_name.get(name or '', set())
            if len({c[0] for c in cands}) > 1 and obj:
                cands = {c for c in cands if c[2] == obj}
            vas = {c[0] for c in cands}
            if len(vas) == 1:
                self.observe(vas.pop(), rva, 'GLOBAL')

    def resolve(self):
        """symbol -> retail base (majority; conflicts kept)."""
        self.where, self.conflicts = {}, {}
        for s, bases in self.obs.items():
            best = max(bases.items(), key=lambda kv: (('GLOBAL' in kv[1]), len(kv[1])))
            self.where[s] = best[0]
            if len(bases) > 1:
                self.conflicts[s] = {hex(b): len(v) for b, v in bases.items()}
            self.how[s] = 'GLOBAL' if 'GLOBAL' in best[1] else 'code'

    def pair_data(self):
        """Mapped data symbols pair their relocated fields with retail's (same offset in both)."""
        changed = True
        seen = set()
        while changed:
            changed = False
            self.resolve()
            for s, rb in list(self.where.items()):
                if s in seen:
                    continue
                seen.add(s)
                sz = self.size.get(s, 0)
                orel = self.O.relocs_in(s[0], s[0] + sz)
                rrel = set(self.R.relocs_in(rb, rb + sz))
                for x in orel:
                    y = rb + (x - s[0])
                    if y in rrel:
                        before = len(self.obs)
                        t = self.O.dword(x)
                        s2 = self.our_sym_at(t)
                        if s2 is not None and s2 not in self.where:
                            changed = True
                        self.observe(t, self.R.dword(y), 'data:%s' % s[1])
                        del before
        self.resolve()

    def pair_literals(self):
        """Unmapped string literal COMDATs (??_C@) found once, byte for byte, in retail .data."""
        lo, vsz, raw = self.R.secs['.data']
        blob = self.R.read(lo, min(vsz, raw))
        for va, n, f, o in self.by_va:
            s = (va, n, o)
            if f or not n.startswith('??_C@') or s in self.where:
                continue
            data = self.O.read(va, max(1, self.size.get(s, 1)))
            end = data.find(b'\0')
            if end < 0:
                continue
            lit = data[:end + 1]
            i = blob.find(lit)
            if i >= 0 and blob.find(lit, i + 1) < 0:
                self.where[s] = lo + i
                self.how[s] = 'content'

    def infer_layout(self):
        """Unmapped symbol between two symbols of the same object with one shared displacement gets it."""
        data = [(va, n, o) for va, n, f, o in self.by_va if not f and self.odata[0] <= va < self.odata[1]]
        reps = []
        last = None
        for s in data:
            if last and last[0] == s[0]:
                continue
            reps.append(s)
            last = s
        changed = True
        while changed:
            changed = False
            for i, s in enumerate(reps):
                if s in self.where:
                    continue
                prev = next((reps[j] for j in range(i - 1, -1, -1) if reps[j] in self.where), None)
                nxt = next((reps[j] for j in range(i + 1, len(reps)) if reps[j] in self.where), None)
                if prev and nxt and prev[2] == s[2] == nxt[2]:
                    d1, d2 = self.where[prev] - prev[0], self.where[nxt] - nxt[0]
                    if d1 == d2:
                        self.where[s] = s[0] + d1
                        self.how[s] = 'layout'
                        changed = True

    # -------------------------------------------------------------- retail code references
    def scan_retail(self, rfuncs):
        """Every operand of retail code that names a .data address: (target, insn va, func, kind, scale, text)."""
        self.refs = []
        self.insns = {}
        lo, hi = self.rdata
        for fva, (end, fname) in sorted(rfuncs.items()):
            if not self.R.in_sec(fva, '.text'):
                continue
            ins = list(self.md.disasm(self.R.read(fva, end - fva), fva))
            self.insns[fva] = ins
            for k, i in enumerate(ins):
                for op in i.operands:
                    if op.type == cx.X86_OP_MEM:
                        t = op.mem.disp & 0xffffffff
                        if lo <= t < hi:
                            kind = 'indexed' if op.mem.index or (op.mem.base and t) else 'mem'
                            self.refs.append((t, i.address, fva, kind, op.mem.scale, '%s %s' % (i.mnemonic, i.op_str), k))
                    elif op.type == cx.X86_OP_IMM:
                        t = op.imm & 0xffffffff
                        if lo <= t < hi and (not self.R.relocs or self.R.relocs_in(i.address, i.address + i.size)):
                            self.refs.append((t, i.address, fva, 'address', 0, '%s %s' % (i.mnemonic, i.op_str), k))
        self.refs.sort()
        self.ref_vas = [r[0] for r in self.refs]
        # every retail base relocation (code or data) that points into .data: catches code outside known extents
        self.data_ptrs = []
        for x in self.R.reloc_sorted:
            t = self.R.dword(x)
            if lo <= t < hi:
                self.data_ptrs.append((t, x))
        self.data_ptrs.sort()

    def refs_in(self, a, b):
        i = bisect.bisect_left(self.ref_vas, a)
        out = []
        while i < len(self.refs) and self.refs[i][0] < b:
            out.append(self.refs[i])
            i += 1
        return out

    def size_hints(self, ref):
        """Constants near an address-of reference: rep counts, pushed sizes, loop bounds."""
        t, iva, fva, kind, scale, text, k = ref
        ins = self.insns[fva]
        hints = []
        for j in range(max(0, k - 6), min(len(ins), k + 10)):
            i = ins[j]
            s = '%s %s' % (i.mnemonic, i.op_str)
            if i.mnemonic.startswith('rep'):
                hints.append(s)
            if i.mnemonic in ('push', 'mov', 'cmp') and i.operands and i.operands[-1].type == cx.X86_OP_IMM:
                v = i.operands[-1].imm & 0xffffffff
                if 4 <= v <= 0x100000 and not (self.rdata[0] <= v < self.rdata[1]):
                    if i.mnemonic == 'push' or (i.operands[0].type == cx.X86_OP_REG and i.mnemonic != 'mov') or \
                            (i.mnemonic == 'mov' and i.operands[0].type == cx.X86_OP_REG and i.reg_name(i.operands[0].reg) == 'ecx'):
                        hints.append(s)
            if i.mnemonic == 'call':
                hints.append(s)
        return hints

    def loop_bounds(self, fva, base, declared, scale):
        """cmp/jcc constants in a function that indexes base: values whose scaled extent passes the declared size."""
        out = []
        for i in self.insns.get(fva, []):
            if i.mnemonic == 'cmp' and len(i.operands) == 2 and i.operands[1].type == cx.X86_OP_IMM:
                v = i.operands[1].imm & 0xffffffff
                if 1 < v < 0x10000 and v * max(scale, 1) > declared:
                    out.append('%08x: cmp %s' % (i.address, i.op_str))
        return out

    # -------------------------------------------------------------- holes
    def holes(self, min_size):
        cov = []
        for s, rb in self.where.items():
            if self.rdata[0] <= rb < self.rdata[1]:
                cov.append((rb, rb + max(self.size.get(s, 0), 1), s))
        cov.sort()
        self.cov = cov
        out, pos = [], self.rdata[0]
        for a, b, s in cov:
            if a > pos and a - pos >= min_size:
                out.append((pos, a))
            pos = max(pos, b)
        if self.rdata[1] - pos >= min_size:
            out.append((pos, self.rdata[1]))
        return out

    def describe(self, h0, h1):
        prev = [c for c in self.cov if c[0] < h0]
        prev = max(prev, key=lambda c: (c[1], c[0])) if prev else None
        nxt = min((c for c in self.cov if c[0] >= h1), default=None)
        raw_end = self.rdata[0] + self.R.secs['.data'][2]
        nz = sum(1 for x in self.R.read(h0, h1 - h0) if x) if h0 < raw_end else 0
        d = {'hole': '%08x..%08x' % (h0, h1), 'size': h1 - h0, 'nonzero_bytes': nz, 'bss': h0 >= raw_end}
        direct = self.refs_in(h0, h1)
        d['direct'] = ['%08x %s (%08x)' % (r[1], r[5], r[0]) for r in direct]
        d['data_ptrs'] = ['%08x <- %08x%s' % (t, x, ' (code)' if self.R.in_sec(x, '.text') else '')
                          for t, x in self.data_ptrs if h0 <= t < h1]
        if prev:
            p0, p1, s = prev
            d['prev'] = {'name': s[1], 'obj': s[2], 'retail': '%08x' % p0, 'declared': self.size.get(s, 0),
                         'retail_span': (nxt[0] if nxt else self.rdata[1]) - p0, 'how': self.how.get(s)}
            pr = self.refs_in(p0, p1)
            d['indexed'] = ['%08x %s [%s]' % (r[1], r[5], self.fname(r[2])) for r in pr if r[3] == 'indexed']
            d['address'] = []
            for r in pr:
                if r[3] == 'address' and r[0] == p0:
                    d['address'].append({'at': '%08x %s [%s]' % (r[1], r[5], self.fname(r[2])),
                                         'near': self.size_hints(r)})
            bounds = set()
            for r in pr:
                if r[3] == 'indexed':
                    for b in self.loop_bounds(r[2], p0, self.size.get(s, 0), r[4]):
                        bounds.add('%s [%s]' % (b, self.fname(r[2])))
            d['bounds'] = sorted(bounds)
        if nxt:
            d['next'] = {'name': nxt[2][1], 'retail': '%08x' % nxt[0]}
        return d

    def fname(self, fva):
        return self.rnames.get(fva, '%08x' % fva)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--retail', default=modcfg.IMAGE)
    rel = os.path.join(ROOT, 'build', 'release')
    ap.add_argument('--image', default=os.path.join(rel, 'd3d.ren' if modcfg.NAME == 'd3dren' else 'lithtech.exe'))
    ap.add_argument('--map', default=os.path.join(rel, 'scratch', 'd3d.map') if modcfg.NAME == 'd3dren' else os.path.join(rel, 'lithtech.map'))
    ap.add_argument('--min', type=lambda x: int(x, 0), default=0x10, help='smallest hole reported (default 0x10)')
    ap.add_argument('--hole', type=lambda x: int(x, 16), help='only the hole containing this address')
    ap.add_argument('--json', help='write the full report as JSON')
    ap.add_argument('--symbols', action='store_true', help='also list every mapped data symbol')
    args = ap.parse_args(argv)
    for p in (args.retail, args.image, args.map):
        if not os.path.exists(p):
            raise SystemExit('missing %s (link the source build first)' % p)

    au = Audit(args.retail, args.image, args.map)
    annots = load_annotations()
    rfuncs = retail_functions()
    au.rnames = retail_names()
    au.pair_globals(annots)
    au.pair_code(annots, rfuncs)
    au.pair_data()
    au.pair_literals()
    au.infer_layout()
    au.scan_retail(rfuncs)
    holes = au.holes(args.min)
    if args.hole is not None:
        holes = [h for h in holes if h[0] <= args.hole < h[1]]

    data_syms = [(va, n, o) for va, n, f, o in au.by_va if not f and au.odata[0] <= va < au.odata[1]]
    unmapped = [s for s in data_syms if s not in au.where and au.size.get(s, 0)]
    report = {
        'retail_data': '%08x..%08x' % au.rdata, 'ours_data': '%08x..%08x' % au.odata,
        'retail_vsize': au.rdata[1] - au.rdata[0], 'ours_vsize': au.odata[1] - au.odata[0],
        'mapped': len(au.where), 'unmapped': len(unmapped),
        'hole_bytes': sum(b - a for a, b in holes),
        'holes': [au.describe(a, b) for a, b in holes],
        'unmapped_symbols': [{'ours': '%08x' % s[0], 'name': s[1], 'obj': s[2], 'size': au.size[s]} for s in unmapped],
        'conflicts': {'%s (%s)' % (s[1], s[2]): v for s, v in au.conflicts.items()},
    }
    # mapped symbols whose declared size runs over the next mapped retail symbol (oversized, or misplaced)
    starts = sorted(set(rb for rb in au.where.values()))
    over = []
    for s_, rb in au.where.items():
        if not (au.rdata[0] <= rb < au.rdata[1]):
            continue
        i = bisect.bisect_right(starts, rb)
        if i < len(starts):
            span = starts[i] - rb
            if au.size.get(s_, 0) > span + 3:
                over.append(('%08x' % rb, s_[1], s_[2], au.size[s_], span))
    report['overruns'] = sorted(over)
    if args.symbols:
        report['symbols'] = sorted(('%08x' % rb, s[1], s[2], au.size.get(s, 0), au.how.get(s)) for s, rb in au.where.items())
    if args.json:
        with open(args.json, 'w') as f:
            json.dump(report, f, indent=1)

    print('retail .data %s (VirtualSize 0x%x), source link %s (0x%x)' % (
        report['retail_data'], report['retail_vsize'], report['ours_data'], report['ours_vsize']))
    print('data symbols mapped to retail: %d; unmapped (no evidence): %d; conflicting: %d' % (
        report['mapped'], report['unmapped'], len(report['conflicts'])))
    print('holes >= 0x%x: %d, 0x%x bytes' % (args.min, len(holes), report['hole_bytes']))
    for d in sorted(report['holes'], key=lambda d: -d['size']):
        p = d.get('prev')
        print('\n%s  0x%x bytes%s%s' % (d['hole'], d['size'], ' .bss' if d['bss'] else '',
                                        ', %d nonzero retail bytes' % d['nonzero_bytes'] if d['nonzero_bytes'] else ''))
        if p:
            print('  after %s (%s) @%s [%s]: declared 0x%x, retail span to next 0x%x' % (
                p['name'], p['obj'], p['retail'], p['how'], p['declared'], p['retail_span']))
        if 'next' in d:
            print('  before %s @%s' % (d['next']['name'], d['next']['retail']))
        for k in ('direct', 'data_ptrs', 'indexed', 'bounds'):
            for x in d.get(k, [])[:12]:
                print('  %-8s %s' % (k, x))
            if len(d.get(k, [])) > 12:
                print('  %-8s ... %d more' % (k, len(d[k]) - 12))
        for x in d.get('address', [])[:8]:
            print('  address  %s' % x['at'])
            for h in x['near']:
                print('             %s' % h)
    if report['unmapped_symbols']:
        print('\nunmapped source data symbols (no retail evidence):')
        for u in report['unmapped_symbols']:
            print('  %s 0x%-6x %s (%s)' % (u['ours'], u['size'], u['name'], u['obj']))
    if report['conflicts']:
        print('\nconflicting retail bases:')
        for k, v in report['conflicts'].items():
            print('  %s %s' % (k, v))


if __name__ == '__main__':
    main()
