r"""Relink spike: link target (and base) objects with data/import/resource stand-ins into build/relink/lithtech.exe.

  python tools/build.py                       # first (in a worktree: set VC6CL=tools\vc6cl_wt.bat)
  python tools/relink.py                      # every unit from its target object   -> .text/.rdata/.data/.rsrc identical
  python tools/relink.py --mode mixed         # fully matched units from their base objects, with their own .rdata/.data
  python tools/relink.py --mode mixed --standin-data   # ... with every unit's data from the exe (stand-in), as before wave 7
  python tools/relink.py --mode mixed --only client/cnet,shared/nexus [--report-data] [--stage prep] [--show-order]
                                              (--show-order lists each unit's function emission order with the descents marked;
                                               --data-detail lists each unit's data symbols with their exe addresses, '>>' = out of order)
  python tools/relink_cmp.py [new.exe]        # headers, per-section and per-byte comparison with the original

Data (tools/relink_data.py; mixed mode only; --own-data is the default there, --standin-data turns it off):
  --data-units [--write-data-units] [--data-verbose]
        locate every object's data sections in the exe; per-unit .rdata/.data/.bss/.CRT ranges (OUT/data_units.json,
        config/data_units.csv) and the data status of every fully matched unit (OUT/data_status.json): 'match' (its
        .rdata/.data sections, in section-table order, are where LINK would put them and hold the exe's bytes),
        'edge' (they match, but bytes next to them belong to no known object), 'differs' (with the reason).
  --split-standin   the stand-in as one piece per unit and output group (OUT/standin/), in link order
  --own-data [u,..] units with status match/edge keep their own .rdata/.data sections (implies the split stand-in);
                    the default in mixed mode (all such units); pass a list to restrict it
  --standin-data    mixed mode without --own-data: the base objects' data sections are dropped and one stand-in
                    object holds the exe's .rdata/.data/.bss (the pre-wave-7 behaviour)

What it builds in build/relink (RELINK_OUT overrides):
  obj/*.obj      the target objects, made linkable: unique external names (a COMDAT leader gets '@<va>' when its
                 name is used at several addresses, and a leading '_' so that /ORDER can name it), every reference
                 resolved by address (the target objects only know a name that is unique per object), imports
                 renamed to __imp__<name>; library objects whose COMDATs sit between other objects' sections are cut
                 into one object per section so that object order = address order; text that no object covers
                 (import thunks, DirectInput tables, unwind funclets, NOP padding) comes from gap/<va> objects.
  standin.obj    .rdata (after the IAT), .data and .bss copied from the exe, with a public symbol at every address a
                 target or base object refers to.
  idata.obj      .idata$2..$6 with the exe's import tables and an __imp__<name> symbol at every IAT slot.
  rsrc.obj       the exe's .rsrc.
  base__*.obj    (mixed) the unit's base object with its data sections dropped, relocations re-pointed at the
                 address the exe's own bytes imply, functions renamed to the names the target objects use.
LINK: /NODEFAULTLIB /BASE:0x400000 /FIXED /SUBSYSTEM:WINDOWS,4.0 /OPT:REF,NOICF, one /INCLUDE per function.
"""
import argparse
import json
import os
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import coffedit  # noqa: E402
from coffedit import Coff, Sec, Sym  # noqa: E402
import mktarget  # noqa: E402

BUILD = os.path.join(ROOT, 'build')
OUT = os.path.normpath(os.environ.get('RELINK_OUT') or os.path.join(BUILD, 'relink'))     # backslashes: LINK's response file treats '/' as an option
EXE = r'E:\AVP2Source\bin\lithtech.exe'
MSVC = r'E:\MSVC6\VC98\Bin'
LINK = os.path.join(MSVC, 'LINK.EXE')
IMAGE_BASE = 0x400000

RDATA_LO = 0x4C6480        # first byte after the import address table, 16-aligned
RDATA_HI = 0x4CD37C        # start of the import descriptors (.idata$2)
DATA_LO, DATA_SIZE = 0x4CF000, 0x10000
BSS_SIZE = 0x19198 - 0x10000


EXTRA_LINK_FLAGS = []
USE_ORDER = False
RICH = False            # --rich: merge/pad the objects so LINK writes the original's Rich header (tools/richpack.py)
INV_RESULTS = []        # inventory()'s check results (relink_data uses the MATCH functions of partly matched units)


def tool_env():
    env = dict(os.environ)
    env['PATH'] = MSVC + ';E:\\MSVC6\\COMMON\\MSDev98\\Bin;' + env['PATH']
    env['LIB'] = r'E:\MSVC6\VC98\Lib'
    return env


def run(args, **kw):
    r = subprocess.run(args, capture_output=True, text=True, env=tool_env(), **kw)
    return r.returncode, (r.stdout + r.stderr)


def load_json(name):
    with open(os.path.join(BUILD, name)) as f:
        return json.load(f)


def inventory():
    """(units, full, standin): build.py's Unit list (annotations bound to the base objects' symbols), the names of the
    units whose every function matches and that can use their base object, and the units with // STANDIN: code."""
    import io
    import contextlib
    import build as B
    units = B.find_units()
    exe, symtab, libs = B.Exe(B.EXE), B.SymTab(), B.Libraries()
    with contextlib.redirect_stdout(io.StringIO()):
        results, _ = B.run_check(units, exe, symtab, False, '\0nomatch', libs)
    INV_RESULTS[:] = results
    status = {(r.a.unit.name, r.a.va): (r.status, r.a.kind) for r in results}
    objvas = load_json('objvas.json')
    standin = {u.name for u in units if any(B.STANDIN_RE.match(l) for l in open(u.path, encoding='latin1'))}
    full = []
    for u in units:
        vas = objvas.get(u.name, [])
        if vas and u.name not in standin and os.path.exists(u.base_obj) \
                and all(status.get((u.name, va)) == ('MATCH', 'FUNCTION') for va in vas):
            full.append(u.name)
    return units, full, standin


# ------------------------------------------------------------------------------------------ original exe

class Orig:
    def __init__(self):
        import pefile
        self.img = mktarget._image(EXE)
        pe = self.img.pe
        pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        self.dlls = []          # [(dll name, [(slot va, entry name or None, ordinal, hint)])]
        for d in pe.DIRECTORY_ENTRY_IMPORT:
            self.dlls.append((d.dll.decode(), [(i.address, i.name.decode() if i.name else None, i.ordinal, i.hint)
                                               for i in d.imports]))
        self.slots = {s[0]: (dll, s[1], s[2]) for dll, ss in self.dlls for s in ss}
        self.entry = pe.OPTIONAL_HEADER.AddressOfEntryPoint + IMAGE_BASE
        self.text_lo, self.text_hi = self.img.text_lo, self.img.text_hi
        text = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text'][0]
        self.text_end = IMAGE_BASE + text.VirtualAddress + text.Misc_VirtualSize       # real end of code (0x4c54b5)


def imp_symbol(entry):
    return '__imp__' + entry


# ------------------------------------------------------------------------------------------ import libraries

def make_idata_obj(orig):
    """The import tables as one object of .idata$2..$6 sections holding the original's bytes, with an
    __imp__<name> symbol at every IAT slot.

    Why not import libraries (tried first, see the report): LINK lays the IAT out in an order that comes from its
    internal pull order and hash tables, and writes the hint/name pool in pull order too. The thunk objects of a
    LIB /DEF library, LINK's hints (0 instead of the DLL's), and the thunk COMDATs all differ from the original,
    and the IAT order is baked into every `call [slot]` in .text. Taking the tables from the exe makes the
    addresses right; deriving them from real import libraries is the open item."""
    img = orig.img
    F = coffedit
    c = Coff()
    parts = [('.idata$2', 0x4CD37C, 0x4CD41C, 4), ('.idata$3', 0x4CD41C, 0x4CD430, 4),
             ('.idata$4', 0x4CD430, 0x4CD8A4, 4), ('.idata$5', 0x4C6000, 0x4C6474, 4),
             ('.idata$6', 0x4CD8A4, 0x4CEE58, 2)]
    for name, lo, hi, al in parts:
        c.sections.append(Sec(name, img.read(lo, hi - lo), [], F.SCN_CNT_INIT | F.SCN_ALIGN[al] | F.SCN_MEM_READ | F.SCN_MEM_WRITE))
    for i, s in enumerate(c.sections):
        c.syms.append(Sym(s.name, 0, i + 1, 0, F.CLS_STATIC, F.section_aux(s.size, 0)))
        c.syms.append(None)
    for dll, slots in orig.dlls:
        for va, name, ordn, hint in slots:
            c.add_symbol(imp_symbol(name if name else '%s_ord%d' % (dll, ordn)), va - 0x4C6000, 4)
    p = os.path.join(OUT, 'idata.obj')
    c.save(p)
    return p



# ------------------------------------------------------------------------------------------ target objects

def subset_sections(o, secnos):
    """A new object holding only the sections `secnos` (1-based) of `o`. Symbols defined in other sections that these
    refer to become undefined externals."""
    n = Coff()
    newno = {}
    for k in secnos:
        sec = o.sections[k - 1]
        n.sections.append(Sec(sec.name, sec.data, [], sec.flags, sec.nlines))
        n.sections[-1].size = sec.size
        newno[k] = len(n.sections)
    symmap = {}
    import struct
    for idx, s in enumerate(o.syms):
        if s is not None and s.sec in newno:
            symmap[idx] = len(n.syms)
            aux = s.aux
            if is_section_sym(s) and len(aux) >= 18 and aux[14] == 5:
                # associative COMDAT ($T EH tables): renumber the section it belongs to, or make it 'any' if that's gone
                num = struct.unpack_from('<H', aux, 12)[0]
                aux = aux[:12] + struct.pack('<H', newno.get(num, 0)) + (aux[14:] if num in newno else b'\x02' + aux[15:])
            n.syms.append(Sym(s.name, s.value, newno[s.sec], s.typ, s.cls, aux))
            n.syms.extend([None] * s.naux)
    for k in secnos:
        for off, si, t in o.sections[k - 1].relocs:
            if si not in symmap:
                s = o.syms[si]
                symmap[si] = len(n.syms)
                if s.sec > 0:                       # defined in a section that was left out: now an undefined external
                    n.syms.append(Sym(s.name, 0, 0, s.typ, coffedit.CLS_EXTERNAL))
                else:
                    n.syms.append(Sym(s.name, s.value, s.sec, s.typ, s.cls, s.aux))
                    n.syms.extend([None] * s.naux)
            n.sections[newno[k] - 1].relocs.append([off, symmap[si], t])
    return n


def slice_section(o, secno):
    return subset_sections(o, [secno])


class BaseFail(Exception):
    pass


def is_section_sym(s):
    return s.cls == coffedit.CLS_STATIC and s.naux and s.name.startswith('.')


def unit_data_anchors(unit, coff):
    """The defined data symbols for a unit's bound GLOBAL annotations (undefined externs are name hints only)."""
    anchors = {}
    for a in unit.annots:
        if a.kind != 'GLOBAL' or not isinstance(a.symbol, str):
            continue
        matches = [s for s in coff.syms if s is not None and s.name == a.symbol and s.sec > 0
                   and s.sec <= len(coff.sections) and not is_section_sym(s)
                   and not coff.sections[s.sec - 1].flags & coffedit.SCN_CNT_CODE
                   and coff.sections[s.sec - 1].name != '.drectve'
                   and not coff.sections[s.sec - 1].name.startswith('.debug')]
        if len(matches) > 1:
            raise ValueError('%s: GLOBAL %s has multiple defined data symbols' % (unit.name, a.symbol))
        if not matches:
            continue
        if a.symbol in anchors and anchors[a.symbol] != a.va:
            raise ValueError('%s: GLOBAL %s has conflicting addresses' % (unit.name, a.symbol))
        anchors[a.symbol] = a.va
    return anchors


class Prepared:
    def __init__(self, orig, library_data=None):
        self.orig = orig
        self.objvas = load_json('objvas.json')
        self.objsym = load_json('objsym.json')
        self.objs = {}
        for name, v in self.objvas.items():
            if v:
                self.objs[name] = Coff.load(os.path.join(BUILD, 'target', name + '.obj'))
        self.library_data = library_data
        self.library_table_name = None
        self.library_native_pieces = []
        self.library_native_outputs = {}
        self.library_native_intervals = []
        if library_data is not None:
            import library_data_relink as LDR
            seed, spec, unit = LDR.make_table_seed(library_data, Coff, subset_sections)
            name = 'library_data/' + unit.metadata['name']
            if name in self.objs:
                raise ValueError('native table seed collides with a target object')
            self.library_table_name = name
            self.objs[name] = seed
            self.objvas[name] = [int(spec.metadata['original_va'], 16)]
            # Resolve the seed's verified GUID references through the regular canonical-data path.
            self.objsym[name] = {r['target_symbol']: int(r['target_va'], 16)
                                 for r in spec.metadata['native_relocations']}
        self.unit_of, self.orig_vas = {}, {}
        self.add_gaps()
        self.order = sorted((min(v), k) for k, v in self.objvas.items() if v and k in self.objs)

    def add_gaps(self):
        """Text bytes that no target object covers (import-library thunks and data, unwind funclets) become
        pseudo-function sections 'gap/<va>', cut from the exe with recovered relocations. Padding-only gaps are
        left to the linker."""
        import build as B
        st = B.symtab_for_mktarget()
        namemap = {int(k, 16): v for k, v in load_json('namemap.json').items()}
        img = self.orig.img
        iv = sorted((va, va + len(self.objs[n].sections[i].data)) for n, vas in self.objvas.items() if n in self.objs
                    for i, va in enumerate(vas))
        pos, gaps = img.text_lo, []
        end = self.orig.text_end
        for lo, hi in iv + [(end, end)]:
            lo = min(lo, end)
            # INT3 padding is what LINK itself writes between sections; anything else (NOP fill, thunks, data) is kept
            if lo > pos and img.read(pos, lo - pos).strip(b'\xcc'):
                gaps.append((pos, lo))
            pos = max(pos, hi)
        self.gaps = gaps
        d = os.path.join(OUT, 'gap')
        os.makedirs(d, exist_ok=True)
        for lo, hi in gaps:
            name = 'gap/%08x' % lo
            path = os.path.join(d, '%08x.obj' % lo)
            info = mktarget.write_target_sections(path, [(lo, hi, [(0, 'gap_%08x' % lo, 2, 0x20)])], EXE, st, namemap)
            o = Coff.load(path)
            for sec in o.sections:
                sec.flags = (sec.flags & ~0x00F00000) | coffedit.SCN_ALIGN[1]
            self.objs[name] = o
            self.objvas[name] = [lo]
            self.objsym[name] = {k: v for k, v in info['name2va'].items() if not (k.startswith('$L') and len(k) >= 8)}

    def sections(self, name):
        """[(va, section index 1-based, size)] in object order."""
        o = self.objs[name]
        vas = self.objvas[name]
        assert len(vas) == len(o.sections), (name, len(vas), len(o.sections))
        return [(va, i + 1, len(o.sections[i].data)) for i, va in enumerate(vas)]

    def run(self):
        orig = self.orig
        text_lo, text_hi = orig.text_lo, orig.text_hi
        # 1. every defined, non-label symbol -> (obj, sym index, va)
        defs = []
        for name, o in self.objs.items():
            secva = {i + 1: va for i, va in enumerate(self.objvas[name])}
            for idx, s in enumerate(o.syms):
                if s is None or s.sec <= 0 or is_section_sym(s) or s.cls not in (coffedit.CLS_EXTERNAL, coffedit.CLS_STATIC):
                    continue
                if s.name.startswith('$L') and s.typ != 0x20:       # mktarget's local labels ($L<hex va>)
                    continue
                defs.append((name, idx, secva[s.sec] + s.value))
        # 2. unique external names. A name used for several VAs gets '@<va>'.
        by_name, first_sym = {}, {}          # first_sym: the COMDAT symbol = first symbol of the section in table order
        for n, idx, va in sorted(defs, key=lambda x: (x[0], x[1])):
            first_sym.setdefault((n, self.objs[n].syms[idx].sec), idx)
        for n, idx, va in defs:
            by_name.setdefault(self.objs[n].syms[idx].name, set()).add(va)
        self.leader = {}                 # (obj, section no) -> final leader name
        self.text_name = {}              # va -> canonical external name
        renames = 0
        for n, idx, va in sorted(defs, key=lambda x: x[2]):
            s = self.objs[n].syms[idx]
            is_leader = (n, s.sec) not in self.leader and idx == first_sym[(n, s.sec)]
            if len(by_name[s.name]) > 1:
                s.name = '%s@%08x' % (s.name, va)
                renames += 1
            if is_leader:
                s.cls = coffedit.CLS_EXTERNAL
                if not s.name.startswith(('?', '_')):       # /ORDER wants C names undecorated: LINK adds the '_'
                    s.name = '_' + s.name
                self.leader[(n, s.sec)] = s.name
            if s.cls == coffedit.CLS_EXTERNAL or is_leader:
                self.text_name.setdefault(va, s.name)
        self.renamed = renames
        # 3. references
        self.unresolved = {}
        data_names = {}              # va -> {name: count}
        for name, o in self.objs.items():
            osym = self.objsym.get(name, {})
            for idx, s in enumerate(o.syms):
                if s is None or s.sec != 0 or s.cls != coffedit.CLS_EXTERNAL:
                    continue
                va = osym.get(s.name)
                if va is None:
                    self.unresolved.setdefault(s.name, []).append(name)
                    continue
                if va in orig.slots:
                    dll, ent, ordn = orig.slots[va]
                    s.name = imp_symbol(ent if ent else '%s_ord%d' % (dll, ordn))
                elif text_lo <= va < text_hi:
                    if va not in self.text_name:
                        self.add_code_symbol(va)
                    s.name = self.text_name[va]
                else:
                    data_names.setdefault(va, {}).setdefault(s.name, 0)
                    data_names[va][s.name] += 1
                    s.name = '\0D%08x' % va         # placeholder, canonical name chosen below
        # 4. canonical data names
        used = set(self.text_name.values())
        self.data_name = {}
        for va, names in sorted(data_names.items()):
            cands = sorted(names.items(), key=lambda kv: (kv[0].startswith(('dat_', 'DAT_')), -kv[1], kv[0]))
            nm = cands[0][0]
            if nm in used:
                nm = '%s@%08x' % (nm, va)
            used.add(nm)
            self.data_name[va] = nm
        for o in self.objs.values():
            for s in o.syms:
                if s is not None and s.name.startswith('\0D'):
                    s.name = self.data_name[int(s.name[2:], 16)]

    def add_code_symbol(self, va):
        """A reference to code that no object defines at that address (a label inside a function): add a
        symbol to the section that contains it."""
        for name in self.objs:
            for sva, secno, size in self.sections(name):
                if sva <= va < sva + size:
                    nm = 'fn_%08x' % va
                    self.objs[name].add_symbol(nm, va - sva, secno, 0x20, coffedit.CLS_EXTERNAL)
                    self.text_name[va] = nm
                    return
        raise SystemExit('no section contains code address %08x' % va)

    def slice_interleaved(self):
        """Library objects whose sections sit between other objects' sections (COMDATs the original linker placed
        elsewhere) are cut into one object per section, so that link order = address order."""
        allsec = sorted((va, n) for n, v in self.objvas.items() if n in self.objs for va in v)
        first, last = {}, {}
        for i, (va, n) in enumerate(allsec):
            first.setdefault(n, i)
            last[n] = i
        hit = [n for n in first if any(allsec[i][1] != n for i in range(first[n] + 1, last[n]))]
        self.sliced = sorted(hit)
        for name in self.sliced:
            o = self.objs.pop(name)
            vas = self.objvas.pop(name)
            self.orig_vas[name] = vas
            # a static symbol that another section of the object refers to must become external
            for idx, s in enumerate(o.syms):
                if s is not None and s.cls == coffedit.CLS_STATIC and s.sec > 0 and not is_section_sym(s) \
                        and not s.name.startswith('$L') and s.typ != 0x20:
                    s.cls = coffedit.CLS_EXTERNAL
                    s.name = '%s@%08x' % (s.name, vas[s.sec - 1] + s.value)
            for i, va in enumerate(vas):
                new = slice_section(o, i + 1)
                key = '%s#%08x' % (name, va)
                self.objs[key] = new
                self.unit_of[key] = name
                self.objvas[key] = [va]
                self.leader[(key, 1)] = self.leader[(name, i + 1)]
        self.order = sorted((min(v), k) for k, v in self.objvas.items() if v and k in self.objs)

    # ---------------------------------------------------------------------------------------- base objects
    def canon_target(self, tva):
        """Symbol name for the address a relocation of base code points at (None if it can't be named)."""
        orig = self.orig
        if tva in orig.slots:
            dll, ent, ordn = orig.slots[tva]
            return imp_symbol(ent if ent else '%s_ord%d' % (dll, ordn))
        if orig.text_lo <= tva < orig.text_hi:
            return self.text_name.get(tva)
        nm = self.data_name.get(tva)
        if nm is None:
            nm = 'dat_%08x' % tva
            while nm in self.used_names:
                nm += '_'
            self.data_name[tva] = nm
            self.used_names.add(nm)
        return nm

    def use_base(self, units, full, only=None):
        """Replace the target objects of fully matched units by their (preprocessed) base objects.

        Base code keeps its bytes. Every relocation that leaves the object is re-pointed at the symbol the exe's own
        bytes imply (target address = field - addend), named like the target objects name that address; the object's
        data sections are dropped (the stand-in holds the data); its functions are renamed to the canonical names."""
        img = self.orig.img
        self.used_names = set(self.text_name.values()) | set(self.data_name.values())
        self.base = {}                # unit name -> (Coff, [function names to /INCLUDE])
        self.base_failed = {}
        self.order_report = {}
        self.data_refs = {}
        byname = {u.name: u for u in units}
        claimed = {self.leader[(n, k)] for n in self.objs if self.unit_of.get(n, n) not in full
                   for k in range(1, len(self.objs[n].sections) + 1) if (n, k) in self.leader}
        for name in full:
            if only and name not in only:
                continue
            u = byname[name]
            try:
                self.base[name] = self.prep_base_unit(u, claimed)
            except BaseFail as e:
                self.base_failed[name] = str(e)
        self.order = [(va, k) for va, k in self.order if self.unit_of.get(k, k) not in self.base]
        self.order += [(min(self.orig_vas.get(n) or self.objvas[n]), n) for n in self.base]
        self.order.sort()

    def prep_base_unit(self, u, claimed):
        img = self.orig.img
        o = Coff.load(u.base_obj)
        eh_plan = getattr(self, 'source_eh_plan', None)
        if eh_plan is not None:
            eh_plan.assert_object_unchanged(u.name, u.base_obj)
        F = coffedit
        code = [i + 1 for i, s in enumerate(o.sections) if s.flags & F.SCN_CNT_CODE]
        if not code:
            raise BaseFail('no code section')
        # function symbols with an annotation -> VA
        fva = {}
        for a in u.annots:
            if a.kind == 'FUNCTION' and a.symbol is not None:
                fva[a.symbol.name] = a.va
        funcs = {}                    # section no -> sorted [(offset, symbol name, va)]
        for idx, s in enumerate(o.syms):
            if s is not None and s.sec in code and s.typ == 0x20 and not is_section_sym(s):
                if s.name in fva:
                    funcs.setdefault(s.sec, []).append((s.value, s.name, fva[s.name]))
        for k in funcs:
            funcs[k].sort()
        # code sections without an annotated function of this unit are copies the exe doesn't have there
        # (unreferenced inline functions, or functions the exe has in a library object / another unit's range)
        own = set(self.orig_vas.get(u.name) or self.objvas[u.name])
        code = [k for k in code if any(va in own for _, _, va in funcs.get(k, []))]
        if not code:
            raise BaseFail('no annotated function in any code section')
        retained = eh_plan.extend_base_unit(self, u.name, o, code, funcs) if eh_plan else list(code)

        def va_at(secno, off):
            best = None
            for value, nm, va in funcs.get(secno, []):
                if value <= off:
                    best = (value, va)
            if best is None:
                raise BaseFail('relocation at section %d +%x precedes every annotated function' % (secno, off))
            return best[1] + off - best[0]

        newsym = {}
        data_refs = []

        def sym_for(name, typ=0):
            if name not in newsym:
                newsym[name] = len(o.syms)
                o.syms.append(Sym(name, 0, 0, typ, F.CLS_EXTERNAL))
            return newsym[name]

        import struct
        for secno in code:
            sec = o.sections[secno - 1]
            for rel in sec.relocs:
                off, si, t = rel
                S = o.syms[si]
                if t not in (mktarget.IMAGE_REL_I386_DIR32, mktarget.IMAGE_REL_I386_REL32):
                    raise BaseFail('relocation type %x' % t)
                if S.sec in retained or S.sec == -1 or S.name == '__except_list':
                    continue          # switch tables, calls between COMDAT functions, absolute symbols (__except_list = fs:[0])
                fva_ = va_at(secno, off)
                field = struct.unpack('<I', img.read(fva_, 4))[0]
                addend = struct.unpack('<I', sec.data[off:off + 4])[0]
                if t == mktarget.IMAGE_REL_I386_DIR32:
                    tva = (field - addend) & 0xffffffff
                else:
                    tva = (fva_ + 4 + struct.unpack('<i', struct.pack('<I', field))[0] - struct.unpack('<i', struct.pack('<I', addend))[0]) & 0xffffffff
                if tva < IMAGE_BASE or tva >= 0x500000:
                    raise BaseFail('reloc +%x in section %d (%s) to %s has address %08x (field %08x, addend %08x)' % (
                        off, secno, o.sections[secno - 1].name, S.name, tva, field, addend))
                nm = self.canon_target(tva)
                if nm is None:
                    raise BaseFail('reloc +%x in section %d targets %08x, which no target object names' % (off, secno, tva))
                if S.sec > 0 and not (o.sections[S.sec - 1].flags & F.SCN_CNT_CODE):
                    data_refs.append((S.sec, S.value, tva, S.name))
                rel[1] = sym_for(nm, 0x20 if t == mktarget.IMAGE_REL_I386_REL32 else 0)

        # The ordinary function code above keeps the original routing and
        # relocation policy. Verified native EH sections are appended after it;
        # references that leave the retained unit use their proof-derived VA.
        if eh_plan and eh_plan.unit(u.name):
            for secno in eh_plan.section_numbers(u.name):
                sec = o.sections[secno - 1]
                for rel in sec.relocs:
                    off, si, t = rel
                    S = o.syms[si]
                    if t not in (mktarget.IMAGE_REL_I386_DIR32, mktarget.IMAGE_REL_I386_REL32):
                        raise BaseFail('source EH relocation type %x' % t)
                    if S.sec in retained or S.sec == -1 or S.name == '__except_list':
                        continue
                    info = eh_plan.relocation(u.name, secno, off)
                    tva = info['target_va']
                    nm = self.canon_target(tva)
                    if nm is None or nm != info['target_name']:
                        raise BaseFail('source EH reloc +%x in section %d targets %08x without its verified canonical name' % (
                            off, secno, tva))
                    if S.sec > 0 and not (o.sections[S.sec - 1].flags & F.SCN_CNT_CODE):
                        data_refs.append((S.sec, S.value, tva, S.name))
                    rel[1] = sym_for(nm, 0x20 if t == mktarget.IMAGE_REL_I386_REL32 else 0)
        # does the object emit its functions in the original's address order? (COMDAT sections are placed in object
        # order, a plain .text keeps source order)
        emit = sorted((k, value, va) for k, lst in funcs.items() if k in code for value, nm, va in lst if va in own)
        descents = sum(1 for x, y in zip(emit, emit[1:]) if y[2] < x[2])
        self.order_report[u.name] = (len(emit), descents)
        names = {(k, value): nm for k, lst in funcs.items() for value, nm, va in lst}
        self.order_detail = getattr(self, 'order_detail', {})
        self.order_detail[u.name] = [(va, names[(k, value)]) for k, value, va in emit]
        # canonical names for the functions this object defines
        include = []
        for idx, s in enumerate(o.syms):
            if s is None or s.sec not in code or is_section_sym(s) or s.typ != 0x20:
                continue
            source_name = s.name
            va = fva.get(source_name)
            cn = self.text_name.get(va) if va else None
            if cn and cn not in claimed:
                claimed.add(cn)
                s.name = cn
            else:
                s.name = '%s@%s' % (s.name, u.name.replace('/', '_'))
            s.cls = F.CLS_EXTERNAL
            if eh_plan and va is not None:
                eh_plan.record_function_symbol(u.name, source_name, va, s.name)
            include.append((s.name, va if cn and s.name == cn else None))
        self.data_refs[u.name] = (o, data_refs)
        self.base_code = getattr(self, 'base_code', {})
        self.base_code[u.name] = retained
        result = subset_sections(o, retained)
        return result, include, fva

    def own_data(self, name, x, layout):
        """--own-data: let base unit `name` keep its .rdata/.data sections (the chain relink_data found for it, `x`).
        Relocations in those sections that leave the kept sections are re-pointed at the names the exe's bytes imply;
        the sections' own symbols get unit-unique names, and every canonical data name inside them becomes an alias
        there (the stand-in pieces no longer cover these bytes). Kept COMDATs are /INCLUDEd: stand-in data that refers
        to them carries no relocations, so /OPT:REF would drop them."""
        import relink_data as RD
        img = self.orig.img
        o, _ = self.data_refs[name]
        code = self.base_code[name]
        kept = {}
        for g in ('.rdata', '.data'):
            for k, va in x.chain[g].owned:
                kept[k] = va
        keep_all = set(code) | set(kept)
        newsym = {}

        def sym_for(nm):
            if nm not in newsym:
                newsym[nm] = len(o.syms)
                o.syms.append(Sym(nm, 0, 0, 0, coffedit.CLS_EXTERNAL))
            return newsym[nm]

        for k, va in kept.items():
            sec = o.sections[k - 1]
            for rel in sec.relocs:
                off, si, t = rel
                S = o.syms[si]
                if S.sec in keep_all:
                    continue
                tva = RD.reloc_target(img, va + off, sec.data, off, t)
                nm = self.canon_target(tva)
                if nm is None:
                    raise BaseFail('own data: reloc +%x in section %d targets %08x, which nothing names' % (off, k, tva))
                rel[1] = sym_for(nm)
        suffix = '@' + name.replace('/', '_')
        for s in o.syms:
            if s is not None and s.sec in kept and not is_section_sym(s):
                s.name = s.name + suffix          # unit-unique: nothing outside refers to these names any more
        spans = sorted((va, va + o.sections[k - 1].size, k) for k, va in kept.items())
        self.own_spans = getattr(self, 'own_spans', [])
        self.own_names = getattr(self, 'own_names', set())
        aliases = 0
        for va, nm in sorted(self.data_name.items()):
            for lo, hi, k in spans:
                if lo <= va < hi:
                    o.add_symbol(nm, va - lo, k)
                    self.own_names.add(va)
                    aliases += 1
                    break
        include = []
        for k in sorted(kept):
            if o.sections[k - 1].flags & coffedit.SCN_LNK_COMDAT:
                ext = [s.name for s in o.syms if s is not None and s.sec == k and s.cls == coffedit.CLS_EXTERNAL]
                if ext:
                    include.append(ext[0])
        self.own_spans += [(lo, hi, name, k) for lo, hi, k in spans]
        sub = subset_sections(o, code + sorted(kept))
        self.own_secno = getattr(self, 'own_secno', {})
        for i, k in enumerate(sorted(kept)):
            self.own_secno[(name, k)] = len(code) + i + 1      # section number of kept section k in `sub`
        obj, inc, fva = self.base[name]
        self.base[name] = (sub, inc + [(n, None) for n in include], fva)
        return len(kept), aliases

    def own_data_late_aliases(self):
        """Data names created after a unit's own_data() ran (a later unit's kept data pointing into it, e.g. at a
        string literal COMDAT the earlier unit supplies) become aliases in that unit's object too."""
        n = 0
        for va, nm in sorted(self.data_name.items()):
            if va in self.own_names:
                continue
            for lo, hi, name, k in getattr(self, 'own_spans', []):
                if lo <= va < hi:
                    self.base[name][0].add_symbol(nm, va - lo, self.own_secno[(name, k)])
                    self.own_names.add(va)
                    n += 1
                    break
        return n

    def write(self):
        d = os.path.join(OUT, 'obj')
        os.makedirs(d, exist_ok=True)
        self.paths = {}
        for name, o in self.objs.items():
            if name == self.library_table_name:
                # This one native .text data section is retained only as the real
                # coverage anchor in objvas; its paired .rdata section is not a
                # function VA and the native flags/section order remain untouched.
                p = os.path.join(d, name.replace('/', '__') + '.obj')
                o.save(p)
                self.paths[name] = p
                continue
            for va, sec in zip(self.objvas[name], o.sections):
                if va + len(sec.data) > self.orig.text_end:      # last text extent may reach page end
                    keep = self.orig.text_end - va
                    assert not sec.data[keep:].strip(b'\0') and all(r[0] < keep for r in sec.relocs), name
                    sec.data, sec.size = sec.data[:keep], keep
                # sections that the original linker placed at an unaligned address (static initialisers, library
                # code) get the alignment their address allows; padding is whatever the extents already contain
                al = 16
                while va % al:
                    al //= 2
                sec.flags = (sec.flags & ~0x00F00000) | coffedit.SCN_ALIGN[al]
            p = os.path.join(d, name.replace('/', '__') + '.obj')
            o.save(p)
            self.paths[name] = p
        for name, (o, include, fva) in getattr(self, 'base', {}).items():
            p = os.path.join(d, 'base__' + name.replace('/', '__') + '.obj')
            o.save(p)
            self.paths[name] = p
        order = []
        for name in self.objs:
            if self.unit_of.get(name, name) in getattr(self, 'base', {}):
                continue
            if name == self.library_table_name:
                continue
            for sva, secno, size in self.sections(name):
                order.append((sva, self.leader[(name, secno)]))
        order.sort()
        self.leaders = order
        self.includes = [n for _, n in order] + [n for (o, inc, fva) in getattr(self, 'base', {}).values() for n, _ in inc]
        order += [(va, n) for (o, inc, fva) in getattr(self, 'base', {}).values() for n, va in inc if va]
        order.sort()
        with open(os.path.join(OUT, 'order.txt'), 'w', newline='') as f:
            for _, n in order:
                f.write((n if n.startswith('?') else n[1:]) + '\n')
        with open(os.path.join(OUT, 'leaders.json'), 'w') as f:
            json.dump(order, f)
        for name, (obj, comdat_symbols, path, _key) in getattr(self, 'library_native_outputs', {}).items():
            obj.save(path)
            self.paths[name] = path
            self.includes.extend(comdat_symbols)

    def install_library_data(self):
        """Install selected native COFF data after source-owned aliases are complete."""
        if self.library_data is None:
            return
        import library_data_relink as LDR
        verified = self.library_data
        aliases = LDR.native_aliases(verified)
        used = set(self.text_name.values()) | set(self.data_name.values())
        # Give every selected .rdata symbol a canonical external name before retargeting native relocs.
        for sec in verified.sections():
            if sec.metadata['original_output_group'] != '.rdata':
                continue
            for symbol in sec.metadata['symbols']:
                va = int(symbol['va'], 16)
                preferred = aliases[va]
                if va in self.data_name:
                    continue
                name = preferred
                if name in used:
                    name = '%s@%08x' % (preferred, va)
                self.data_name[va] = name
                used.add(name)
        table_va = LDR.TEXT_ANCHOR
        if table_va not in self.text_name:
            raise ValueError('native DirectInput table seed did not create its canonical text name')

        outputs, native_pieces = {}, []
        self.native_data_spans = []
        self.own_names = getattr(self, 'own_names', set())
        for ordinal, unit, sections in LDR.section_specs(verified):
            obj, newno = LDR.copy_selected(unit, Coff, Sym, subset_sections, self.canon_target)
            manifest_name = unit.metadata['name']
            unit_key = 'library_data/' + manifest_name
            comdat = []
            selected_by_old = {int(s.metadata['coff_section_index']): s for s in sections}
            for oldno, secmeta in selected_by_old.items():
                outno = newno[oldno]
                va0 = int(secmeta.metadata['original_va'], 16)
                hi = va0 + int(secmeta.metadata['size'])
                if secmeta.metadata['original_output_group'] == '.rdata':
                    for va, nm in sorted(self.data_name.items()):
                        if va0 <= va < hi:
                            if not any(s is not None and s.cls == coffedit.CLS_EXTERNAL and s.name == nm
                                       and s.sec == outno and s.value == va - va0 for s in obj.syms):
                                obj.add_symbol(nm, va - va0, outno)
                            self.own_names.add(va)
                    self.native_data_spans.append((va0, hi, unit_key))
                else:
                    # The table is data in .text; the external alias keeps references resolvable,
                    # but it never enters the FUNCTION inventory.
                    nm = self.text_name[table_va]
                    if not any(s is not None and s.cls == coffedit.CLS_EXTERNAL and s.name == nm
                               and s.sec == outno and s.value == table_va - va0 for s in obj.syms):
                        obj.add_symbol(nm, table_va - va0, outno)
                for sym in secmeta.metadata['symbols']:
                    va = int(sym['va'], 16)
                    if va == table_va:
                        continue
                    nm = self.data_name[va]
                    if not any(s is not None and s.cls == coffedit.CLS_EXTERNAL and s.name == nm
                               and s.sec == outno and s.value == int(sym['offset']) for s in obj.syms):
                        obj.add_symbol(nm, int(sym['offset']), outno)
                if obj.sections[outno - 1].flags & coffedit.SCN_LNK_COMDAT:
                    leaders = [s.name for s in obj.syms if s is not None and s.sec == outno
                               and s.cls == coffedit.CLS_EXTERNAL and not is_section_sym(s)]
                    if not leaders:
                        raise ValueError('%s: selected COMDAT section has no external leader' % manifest_name)
                    comdat.append(leaders[0])
            if manifest_name == 'DX81/dilib3':
                if unit_key != self.library_table_name:
                    raise ValueError('native DirectInput table unit does not match its coverage seed')
                self.objs[unit_key] = obj
                self.library_native_outputs = getattr(self, 'library_native_outputs', {})
            else:
                va = min(int(s.metadata['original_va'], 16) for s in sections)
                # The common tuple prefix is the real table address; ordinal only orders native members
                # for the legacy scalar relink-data API and is never interpreted as a function VA.
                key = (LDR.TEXT_ANCHOR, 3, va)
                path = os.path.join(OUT, 'obj', unit_key.replace('/', '__') + '.obj')
                outputs[unit_key] = (obj, comdat, path, key)
                native_pieces.append((key, path))
        for pva, psize, _ in LDR.verified_padding(verified):
            if any(pva <= va < pva + psize for va in self.data_name):
                raise ValueError('canonical data reference overlaps native alignment padding at %08x' % pva)
        self.library_native_outputs = outputs
        self.library_native_pieces = native_pieces
        self.library_native_intervals = LDR.selected_intervals(verified)


def data_report(prep):
    """How do the base objects' own data sections compare with the original's data? Section addresses come from the
    exe itself (target address of every code relocation that points into the section, minus the symbol's offset)."""
    img = prep.orig.img
    F = coffedit
    out, tot = {}, {}

    def bump(k, n=1):
        tot[k] = tot.get(k, 0) + n

    for name, (o, refs) in sorted(prep.data_refs.items()):
        secva, conflicts = {}, 0
        for secno, value, tva, sname in refs:
            va = tva - value
            if secno in secva and secva[secno] != va:
                conflicts += 1
            secva.setdefault(secno, va)
        rows = []
        for i, sec in enumerate(o.sections, 1):
            if sec.flags & F.SCN_CNT_CODE or sec.name == '.drectve' or sec.name.startswith('.debug'):
                continue
            uninit = bool(sec.flags & F.SCN_CNT_UNINIT)
            size = sec.size
            va = secva.get(i)
            kind = 'comdat' if sec.flags & F.SCN_LNK_COMDAT else 'plain'
            row = dict(sec=i, name=sec.name, size=size, kind=kind, uninit=uninit, va=va)
            if va is not None and not uninit and size:
                try:
                    got = img.read(va, size)
                    masked = set()
                    for off, si, t in sec.relocs:
                        masked.update(range(off, off + 4))
                    bad = [k for k in range(size) if k not in masked and got[k] != sec.data[k]]
                    row['bad'] = len(bad)
                    row['first_bad'] = bad[0] if bad else None
                except ValueError:
                    row['bad'] = -1
            rows.append(row)
            bump('sections_' + kind)
            bump('bytes_' + kind, size)
            if va is None:
                bump('unlocated_sections')
                bump('unlocated_bytes', size)
            elif uninit or not size:
                bump('located_bss_sections')
            elif row.get('bad') == 0:
                bump('content_identical_sections')
                bump('content_identical_bytes', size)
            else:
                bump('content_differs_sections')
                bump('content_differs_bytes', size)
        loc = sorted((r['va'], r['size'], r['sec']) for r in rows if r['va'] is not None and r['size'])
        inversions = sum(1 for a, b in zip(sorted(loc, key=lambda x: x[2]), sorted(loc, key=lambda x: x[2])[1:]) if b[0] < a[0])
        out[name] = dict(sections=rows, conflicts=conflicts, order_inversions=inversions,
                         located=len(loc), total=len(rows))
        bump('unit_section_order_inversions', inversions)
    return out, tot


def data_detail(prep, only=None):
    """For every unit (or those in `only`): the data symbols the unit's code refers to, in (section, offset) order, with the
    address the exe's code implies for each. Within one plain .data/.bss section the addresses must ascend; a '>>' marks
    a symbol that sits below its predecessor in the exe, i.e. VC6 would have to define it earlier in the source."""
    F = coffedit
    for name, (o, refs) in sorted(prep.data_refs.items()):
        if only and name not in only:
            continue
        syms = {}
        for secno, value, tva, sname in refs:
            syms[(secno, value, sname)] = tva
        rows = sorted(syms.items())
        if not rows:
            continue
        print('== data symbols of %s' % name)
        prev, prevsec = 0, None
        for (secno, value, sname), tva in rows:
            sec = o.sections[secno - 1]
            if secno != prevsec:
                prev, prevsec = 0, secno
            kind = 'bss' if sec.flags & F.SCN_CNT_UNINIT else 'data'
            cls = 'comdat' if sec.flags & F.SCN_LNK_COMDAT else 'plain'
            print('  %s sec%-3d %-6s %-6s +%-5x %08x  %s' % ('>>' if tva < prev and cls == 'plain' else '  ', secno, sec.name, cls, value, tva, sname))
            prev = max(prev, tva) if cls == 'plain' else prev


# ------------------------------------------------------------------------------------------ data stand-in

def make_rsrc_obj(orig):
    """The resources: the original's .rsrc bytes (resource data entries hold RVAs, which stay valid because the
    section lands at the same address)."""
    sec = [x for x in orig.img.pe.sections if x.Name.rstrip(b'\0') == b'.rsrc'][0]
    F = coffedit
    c = Coff()
    c.sections.append(Sec('.rsrc$01', sec.get_data()[:sec.Misc_VirtualSize], [],
                          F.SCN_CNT_INIT | F.SCN_ALIGN[4] | F.SCN_MEM_READ))
    c.syms.append(Sym('.rsrc$01', 0, 1, 0, F.CLS_STATIC, F.section_aux(c.sections[0].size, 0)))
    c.syms.append(None)
    p = os.path.join(OUT, 'rsrc.obj')
    c.save(p)
    return p


def make_standin(prep):
    img = prep.orig.img
    F = coffedit
    c = Coff()
    c.sections.append(Sec('.rdata', img.read(RDATA_LO, RDATA_HI - RDATA_LO), [],
                          F.SCN_CNT_INIT | F.SCN_ALIGN[16] | F.SCN_MEM_READ))
    c.sections.append(Sec('.data', img.read(DATA_LO, DATA_SIZE), [],
                          F.SCN_CNT_INIT | F.SCN_ALIGN[16] | F.SCN_MEM_READ | F.SCN_MEM_WRITE))
    bss = Sec('.bss', b'', [], F.SCN_CNT_UNINIT | F.SCN_ALIGN[16] | F.SCN_MEM_READ | F.SCN_MEM_WRITE)
    bss.size = BSS_SIZE
    c.sections.append(bss)
    for i, s in enumerate(c.sections):      # section symbols
        c.syms.append(Sym(s.name, 0, i + 1, 0, F.CLS_STATIC, F.section_aux(s.size, 0)))
        c.syms.append(None)
    c.syms.append(Sym('__except_list', 0, -1, 0, F.CLS_EXTERNAL))      # fs:[0], defined in LIBCMT's exsup.obj
    bad = []
    for va, nm in sorted(prep.data_name.items()):
        if RDATA_LO <= va < RDATA_HI:
            c.add_symbol(nm, va - RDATA_LO, 1)
        elif DATA_LO <= va < DATA_LO + DATA_SIZE:
            c.add_symbol(nm, va - DATA_LO, 2)
        elif DATA_LO + DATA_SIZE <= va < DATA_LO + DATA_SIZE + BSS_SIZE:
            c.add_symbol(nm, va - DATA_LO - DATA_SIZE, 3)
        else:
            bad.append((va, nm))
    p = os.path.join(OUT, 'standin.obj')
    c.save(p)
    return p, bad


def build_layout(prep, units, full):
    """relink_data.Layout over source objects, libraries, and selected native data members."""
    import relink_data as RD
    import library_data_relink as LDR
    good = {}
    for r in INV_RESULTS:
        if r.status == 'MATCH' and r.a.kind == 'FUNCTION' and r.a.symbol is not None:
            good.setdefault(r.a.unit.name, {})[r.a.symbol.name] = r.a.va
    objs = []
    for u in units:
        if os.path.exists(u.base_obj):
            coff = Coff.load(u.base_obj)
            explicit_data = unit_data_anchors(u, coff)
            objs.append(RD.Obj(u.name, coff, good.get(u.name, {}),
                               'full' if u.name in full else 'base', explicit_data))
    libs = json.load(open(os.path.join(ROOT, 'config', 'libraries.json')))
    for L in libs['units']:
        objs.append(RD.Obj(L['name'], Coff.load(L['obj']), {nm: int(va, 16) for va, nm in L['functions'].items()}, 'lib'))
    link_of = {}
    for n, vas in list(prep.objvas.items()) + list(prep.orig_vas.items()):
        u = prep.unit_of.get(n, n.split('#')[0])
        if vas and not n.startswith('gap/'):
            link_of[u] = min(link_of.get(u, 1 << 40), min(vas))
    if prep.library_data is not None:
        for ordinal, unit, sections in LDR.section_specs(prep.library_data):
            obj, _ = LDR.copy_selected(unit, Coff, Sym, subset_sections, prep.canon_target)
            name = 'library_data/' + unit.metadata['name']
            table_fva, explicit = {}, {}
            for sec in sections:
                for symbol in sec.metadata['symbols']:
                    va = int(symbol['va'], 16)
                    if sec.metadata['original_output_group'] == '.text':
                        table_fva[symbol['name']] = va
                    elif sec.metadata['original_output_group'] == '.rdata':
                        explicit[symbol['name']] = va
            objs.append(RD.Obj(name, obj, table_fva, 'lib', explicit))
            # RD.Layout has a scalar ordering API. Fractional tie-breaks preserve
            # the shared real link anchor without encoding function VAs.
            link_of[name] = LDR.TEXT_ANCHOR + ordinal / 16.0
    symva = load_json('symva.json')
    byname = {v: int(k, 16) for k, v in load_json('namemap.json').items()}
    # // GLOBAL: annotations (bound by inventory's check) also name globals that no code refers to
    byname.update({a.symbol: a.va for u in units for a in u.annots if a.kind == 'GLOBAL' and isinstance(a.symbol, str)})
    if prep.library_data is not None:
        native_aliases = LDR.native_aliases(prep.library_data)
        native_names = {va: prep.data_name.get(va, nm) for va, nm in native_aliases.items()
                        if va in prep.data_name}
        name_to_va = {}
        for va, nm in list(native_aliases.items()) + list(native_names.items()):
            if nm in name_to_va and name_to_va[nm] != va:
                raise ValueError('native library-data alias %s names multiple VAs' % nm)
            name_to_va[nm] = va
        byname.update(name_to_va)

    def va_of_name(n):
        return symva.get(n, byname.get(n))

    lay = RD.Layout(prep.orig.img, objs, link_of)
    lay.run(va_of_name)
    return lay


def report_layout(lay, full, write_csv=False, verbose=False):
    """Per-unit data ranges (OUT/data_units.json, config/data_units.csv with --write-data-units) and the data status
    of every fully matched unit (OUT/data_status.json)."""
    import relink_data as RD
    rows = []
    for g in RD.GROUPS:
        for u, (lo, hi, how, n) in sorted(lay.range[g].items(), key=lambda kv: kv[1][0]):
            rows.append((u, g, lo, hi, how, n))
    for u, ents in sorted(lay.crt_entries().items(), key=lambda kv: min(kv[1])):
        lo = min(va for va, size, nm in ents)
        hi = max(va + size for va, size, nm in ents)
        rows.append((u, '.CRT', lo, hi, 'exact', len(ents)))
    json.dump([dict(unit=u, group=g, start='%08x' % lo, end='%08x' % hi, source=how, anchors=n) for u, g, lo, hi, how, n in rows],
              open(os.path.join(OUT, 'data_units.json'), 'w'), indent=0)
    if write_csv:
        with open(os.path.join(ROOT, 'config', 'data_units.csv'), 'w', newline='') as f:
            f.write('unit,group,start,end,source,anchors\n')
            for u, g, lo, hi, how, n in sorted(rows, key=lambda r: (r[1], r[2])):
                f.write('%s,%s,%08x,%08x,%s,%d\n' % (u, g, lo, hi, how, n))
    print('data ranges: %s' % ', '.join('%s %d units (%d anchors against link order)' % (
        g, len(lay.range[g]), len(lay.rejected[g])) for g in RD.GROUPS))
    for g in RD.GROUPS:
        for lo, hi, u, w in lay.rejected[g]:
            if w >= 1000 or verbose:
                print('  %s anchor against link order: %s %08x-%08x' % (g, u, lo, hi))
    status = {}
    for x in lay.objs:
        if x.kind != 'full':
            continue
        st, iss = lay.status(x)
        info = {g: ('%08x-%08x' % (x.chain[g].lo, x.chain[g].hi) if x.chain[g].owned else '-') for g in RD.GROUPS}
        info['pooled'] = sum(len(x.chain[g].pooled) for g in ('.rdata', '.data'))
        status[x.unit] = dict(status=st, issues=['%s %s: %s' % i for i in iss], bss=RD.bss_order(x), **info)
    json.dump(status, open(os.path.join(OUT, 'data_status.json'), 'w'), indent=1)
    good = sorted(u for u, s in status.items() if s['status'] == 'match')
    edge = sorted(u for u, s in status.items() if s['status'] == 'edge')
    print('fully matched units whose .rdata/.data match: %d of %d (+%d whose own sections match, with unexplained bytes '
          'next to them)' % (len(good), len(status), len(edge)))
    for u, s in sorted(status.items()):
        if s['status'] != 'match' or verbose:
            print('  %-42s %s' % (u, s['status']))
            for t in s['issues'][:8]:
                print('      ' + t)
    return status


def make_standin_pieces(prep, layout, own):
    """--split-standin: the stand-in cut into one object per unit and output group (relink_data.pieces), plus a head
    piece (the merged .CRT tables at the start of .data) and a tail piece (.rdata$r/.xdata$x, as the last '.rdata').
    Returns ([(sort key, path)], names that no piece or own-data section could hold)."""
    import relink_data as RD
    img = prep.orig.img
    d = os.path.join(OUT, 'standin')
    os.makedirs(d, exist_ok=True)
    for f in os.listdir(d):
        os.remove(os.path.join(d, f))
    ps = RD.pieces(layout, own)
    ps = [(key, g, lo, hi, u) for key, g, lo, hi, u in ps if hi > lo]
    eh_plan = getattr(prep, 'source_eh_plan', None)
    if eh_plan is not None:
        ps = eh_plan.subtract_xdata_spans(ps)
    if prep.library_data is not None:
        import library_data_relink as LDR
        ps = LDR.subtract_native_spans(ps, prep.library_native_intervals,
                                       LDR.verified_padding(prep.library_data))
    spans = [((-1, 0, RD.CRT_LO), '.data', RD.CRT_LO, RD.CRT_HI, '(crt)')]
    tail_end = eh_plan.xdata_start if eh_plan is not None else RDATA_HI
    if layout.rtail > tail_end:
        raise ValueError('stand-in .rdata tail starts at %08x after native source EH metadata at %08x' %
                         (layout.rtail, tail_end))
    if tail_end > layout.rtail:
        spans.append(((1 << 40, 0, layout.rtail), '.rdata', layout.rtail, tail_end, '(rtail)'))
    spans += ps
    names = {i: [] for i in range(len(spans))}
    starts = sorted((lo, i) for i, (key, g, lo, hi, u) in enumerate(spans))
    import bisect
    los = [s[0] for s in starts]
    own_names = getattr(prep, 'own_names', set())
    bad = []
    for va, nm in sorted(prep.data_name.items()):
        if va in own_names:
            continue
        j = bisect.bisect_right(los, va) - 1
        if j < 0:
            bad.append((va, nm))
            continue
        i = starts[j][1]
        key, g, lo, hi, u = spans[i]
        if va >= hi and va - hi >= 16:      # (within 16: the alignment gap before .bss, or one past a group's end)
            bad.append((va, nm))
            continue
        names[i].append((va, nm))
    out = []
    for i, (key, g, lo, hi, u) in enumerate(spans):
        c = RD.make_piece_obj(img, g, lo, hi, names[i], prep.text_name)     # code pointers relocated
        if u == '(crt)':
            c.syms.append(Sym('__except_list', 0, -1, 0, coffedit.CLS_EXTERNAL))      # fs:[0], see make_standin
        p = os.path.join(d, '%08x_%s.obj' % (lo, (u or 'none').replace('/', '__').strip('()')))
        c.save(p)
        out.append((key, p))
    out.extend(prep.library_native_pieces)
    return out, bad, ps


# ------------------------------------------------------------------------------------------ link

def do_link(prep, standin, idata, mode, pieces=None):
    if pieces is None:
        objs = [idata, standin, make_rsrc_obj(prep.orig)] + [prep.paths[n] for _, n in prep.order]
    else:
        # data pieces sort by (unit link position, 0 piece / 3 gap after the unit, address), code objects by (address, 2)
        code = [((va, 2, va), prep.paths[n]) for va, n in prep.order]
        objs = [idata, make_rsrc_obj(prep.orig)] + [p for _, p in sorted(pieces + code, key=lambda x: x[0])]
    if RICH:
        import richpack
        objs = richpack.pack(objs, richpack.rich_entries(EXE), os.path.join(OUT, 'rich'))
    entry = prep.text_name[prep.orig.entry]
    entry = entry[1:] if entry.startswith('_') else entry       # LINK adds the decoration itself
    rsp = ['/NOLOGO', '/NODEFAULTLIB', '/OUT:' + os.path.join(OUT, 'lithtech.exe'),
           '/BASE:0x400000', '/FIXED', '/SUBSYSTEM:WINDOWS,4.0', '/ENTRY:' + entry,
           '/OPT:REF,NOICF', '/MAP:' + os.path.join(OUT, 'lithtech.map')]
    if USE_ORDER:
        rsp.append('/ORDER:@' + os.path.join(OUT, 'order.txt'))
    # /OPT:REF drops the import libraries' unreferenced thunks (the original has few of them); /INCLUDE keeps every
    # target function, whose callers are partly stand-in data (vtables) that carries no relocations
    rsp += EXTRA_LINK_FLAGS
    rsp += ['/INCLUDE:' + n for n in prep.includes]
    rsp += ['"%s"' % o for o in objs]
    with open(os.path.join(OUT, 'link.rsp'), 'w', newline='') as f:
        f.write('\n'.join(rsp) + '\n')
    return run([LINK, '@' + os.path.join(OUT, 'link.rsp')])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mode', default='targets')
    ap.add_argument('--stage', default='all')
    ap.add_argument('--link-flag', action='append', default=[], help='extra LINK flag (repeatable)')
    ap.add_argument('--report-data', action='store_true', help='mixed mode: compare the base objects' + chr(39) + ' data sections with the exe')
    ap.add_argument('--only', help='mixed mode: comma-separated unit names allowed to use their base object')
    ap.add_argument('--data-detail', action='store_true', help='mixed mode: per unit, every data symbol the code refers to, in the base object section/offset order with the address the exe gives it; ">>" marks a symbol whose address is below the previous one (the variable order differs from the original)')
    ap.add_argument('--show-order', action='store_true', help='mixed mode: list, per unit, the base objects function emission order (VA, name) with ">>" at every descent')
    ap.add_argument('--order', action='store_true', help='pass /ORDER (not needed: object order = address order)')
    ap.add_argument('--data-units', action='store_true', help='mixed mode: locate every object' + chr(39) + 's data sections, print the per-unit data ranges (OUT/data_units.json) and the data status of each fully matched unit (OUT/data_status.json); see tools/relink_data.py')
    ap.add_argument('--write-data-units', action='store_true', help='with --data-units: also write config/data_units.csv')
    ap.add_argument('--data-verbose', action='store_true', help='with --data-units: list every unit, and the weak anchors against link order')
    ap.add_argument('--split-standin', action='store_true', help='mixed mode: one stand-in piece per unit and output group, placed in link order (OUT/standin/)')
    ap.add_argument('--own-data', nargs='?', const='', default=None, help='mixed mode: fully matched units whose .rdata/.data match supply their own sections (all of them, or the comma-separated list); implies --split-standin')
    ap.add_argument('--own-data-force', action='store_true', help='with --own-data: also units whose data status is not "match" (to see the byte differences)')
    ap.add_argument('--exclude', help='mixed mode: comma-separated fully matched units to keep as target objects (unlike --only, the default path with source EH helpers and native library data stays enabled; the byte gate uses this)')
    ap.add_argument('--rich', action='store_true', help='merge/pad the objects so that LINK writes the original' + chr(39) + 's Rich header, and set the original' + chr(39) + 's TimeDateStamp after the link (the byte gate uses both; tools/richpack.py)')
    ap.add_argument('--standin-data', action='store_true', help='mixed mode: take every unit' + chr(39) + 's .rdata/.data from the exe (one stand-in object) instead of the default --own-data')
    a = ap.parse_args()
    if a.mode == 'mixed' and a.own_data is None and not a.standin_data:
        a.own_data = ''         # default: fully matched units whose data matches supply their own sections
    if a.standin_data:
        a.own_data = None
    EXTRA_LINK_FLAGS.extend(a.link_flag)
    global USE_ORDER, RICH
    USE_ORDER = a.order
    RICH = a.rich
    os.makedirs(OUT, exist_ok=True)
    orig = Orig()
    library_data = None
    if a.mode == 'mixed':
        import library_data_relink as LDR
        if LDR.enabled(a.mode, a.own_data, a.standin_data, a.only):
            library_data = LDR.verify_default(os.path.join(BUILD, 'library_data'))
            if len(library_data.units) != 6 or len(library_data.sections()) != 38 \
                    or library_data.payload_bytes != 1304 or library_data.padding_bytes != 4:
                raise ValueError('verified native library-data inventory differs from the supported 6-unit manifest')
            print('verified native library data: %d units, %d sections, %d payload bytes, %d alignment bytes' % (
                len(library_data.units), len(library_data.sections()), library_data.payload_bytes,
                library_data.padding_bytes))
    prep = Prepared(orig, library_data)
    print('gap objects %d (%d bytes)' % (len(prep.gaps), sum(h - l for l, h in prep.gaps)))
    prep.run()
    prep.slice_interleaved()
    eh_plan = None
    if a.mode == 'mixed':
        units, full, standin = inventory()
        if a.exclude:
            ex = set(a.exclude.split(','))
            full = [u for u in full if u not in ex]
            print('excluded from base objects: %d: %s' % (len(ex), ', '.join(sorted(ex))))
        print('fully matched units: %d (stand-in units kept as targets: %s)' % (len(full), sorted(standin)))
        import source_eh_relink as SER
        eh_ok = SER.enabled(a.mode, a.own_data, a.standin_data, a.only)
        if eh_ok and a.exclude and set(a.exclude.split(',')) & set(SER.UNIT_ORDER):
            # the source EH plan is a fixed, fail-closed manifest of its units: with one of them excluded, every EH
            # helper comes from the target bytes instead
            print('source EH helpers from the target bytes (excluded EH unit: %s)' % ', '.join(
                sorted(set(a.exclude.split(',')) & set(SER.UNIT_ORDER))))
            eh_ok = False
        if eh_ok:
            eh_plan = SER.build_plan(prep, units, full)
            prep.source_eh_plan = eh_plan
            eh_plan.remove_target_tail(prep, subset_sections)
            print('verified source EH: %d .text$x sections/%d bytes, %d .xdata$x sections/%d bytes, '
                  '%d root + %d helper relocations' % (
                      eh_plan.text_helper_sections, eh_plan.text_helper_bytes,
                      eh_plan.xdata_sections_count, eh_plan.xdata_bytes,
                      eh_plan.root_relocation_count, eh_plan.helper_relocation_count))
        prep.use_base(units, full, set(a.only.split(',')) if a.only else None)
        if eh_plan is not None:
            eh_plan.verify_base_coverage(prep)
        print('base objects used: %d; fell back to target: %s' % (len(prep.base), prep.base_failed))
        if a.report_data:
            rep, tot = data_report(prep)
            json.dump(rep, open(os.path.join(OUT, 'data_report.json'), 'w'), indent=1)
            print('data sections of the base objects: %s' % json.dumps(tot, sort_keys=True))
        if a.data_detail:
            data_detail(prep, set(a.only.split(',')) if a.only else None)
        bad = {n: r for n, r in prep.order_report.items() if r[1]}
        print('units whose base object emits functions out of address order: %d of %d: %s' % (
            len(bad), len(prep.order_report), ', '.join('%s(%d/%d)' % (n, r[1], r[0]) for n, r in sorted(bad.items()))))
        if a.show_order:
            for n in sorted(bad):
                print('== %s (emission order; >> = address goes down)' % n)
                prev = 0
                for va, nm in prep.order_detail[n]:
                    print('  %s %08x %s' % ('>>' if va < prev else '  ', va, nm))
                    prev = va
    lay, own = None, []
    if a.mode == 'mixed' and (a.data_units or a.write_data_units or a.split_standin or a.own_data is not None):
        lay = build_layout(prep, units, full)
        status = report_layout(lay, full, a.write_data_units, a.data_verbose)
        if a.own_data is not None:
            want = set(a.own_data.split(',')) if a.own_data else None
            import relink_data as RD
            for x in lay.objs:
                if x.unit not in prep.base or (want is not None and x.unit not in want):
                    continue
                if status.get(x.unit, {}).get('status') not in ('match', 'edge') and not a.own_data_force:
                    continue
                # the unit's range must be exactly its chain (an anchor against link order has no range)
                if any(x.chain[g].owned and lay.range[g].get(x.unit, [None])[:2] != [x.chain[g].lo, x.chain[g].hi]
                       for g in ('.rdata', '.data')):
                    print('  own data: %s skipped (its range is not its chain)' % x.unit)
                    continue
                try:
                    nk, nal = prep.own_data(x.unit, x, lay)
                except BaseFail as e:
                    print('  own data: %s skipped: %s' % (x.unit, e))
                    continue
                own.append(x.unit)
            late = prep.own_data_late_aliases() if own else 0
            print('units supplying their own .rdata/.data: %d: %s' % (len(own), ', '.join(sorted(own))))
            if late:
                print('  late data aliases in own sections: %d' % late)
    if library_data is not None:
        prep.install_library_data()
        print('native library data installed: %d .rdata spans; DirectInput table remains a data section in .text' %
              len(prep.native_data_spans))
    print("sliced %d interleaved library objects" % len(prep.sliced))
    prep.write()
    print('objects %d, renamed %d, unresolved %d, data symbols %d' % (
        len(prep.objs), prep.renamed, len(prep.unresolved), len(prep.data_name)))
    for n, objs in list(prep.unresolved.items())[:20]:
        print('  unresolved', n, objs[:3])
    pieces = None
    if lay is not None and (a.split_standin or own or library_data is not None):
        pieces, bad, ps = make_standin_pieces(prep, lay, set(own))
        print('stand-in pieces: %d' % len(pieces))
        if library_data is not None:
            print('native data pieces: %d' % len(prep.library_native_pieces))
        standin = None
    else:
        standin, bad = make_standin(prep)
    for va, nm in bad[:20]:
        print('  data symbol outside stand-in sections: %08x %s' % (va, nm))
    idata = make_idata_obj(orig)
    with open(os.path.join(OUT, 'gate_units.json'), 'w') as f:      # read by tools/byte_gate.py
        json.dump({'mode': a.mode, 'base': sorted(getattr(prep, 'base', {})), 'own_data': sorted(own),
                   'out_of_order': sorted(n for n, r in getattr(prep, 'order_report', {}).items() if r[1]),
                   'base_failed': sorted(getattr(prep, 'base_failed', {}) or [])}, f, indent=1)
    if a.stage == 'prep':
        return
    rc, out = do_link(prep, standin, idata, a.mode, pieces)
    print(out[-6000:])
    print('link rc', rc)
    if rc == 0 and RICH:
        import richpack
        stamp = orig.img.pe.FILE_HEADER.TimeDateStamp
        old = richpack.set_timestamp(os.path.join(OUT, 'lithtech.exe'), stamp)
        print('TimeDateStamp %08x -> %08x (the only post-link edit)' % (old, stamp))
    if rc == 0 and eh_plan is not None:
        eh_plan.verify_linked_output(os.path.join(OUT, 'lithtech.exe'),
                                     os.path.join(OUT, 'lithtech.map'), prep)
        print('native source EH: linked anchors, bytes, padding, and root/helper relocations verified')
    if rc == 0 and library_data is not None:
        import library_data_relink as LDR
        LDR.verify_linked_output(os.path.join(OUT, 'lithtech.exe'), library_data)
        print('native library data: linked payload and 4 alignment bytes verified')


if __name__ == '__main__':
    main()
