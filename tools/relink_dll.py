r"""Relink d3d.ren (module d3dren): target objects, gap objects and stand-ins into build/d3dren/relink/d3d.ren.

  python tools/relink_dll.py --module d3dren [--rich] [--mode targets|mixed] [--splice] [--exclude u,..]
  python tools/relink_cmp.py --module d3dren build/d3dren/relink/d3d.ren --check

The DLL counterpart of tools/relink.py, whose machinery it reuses (target objects made linkable, gap objects for
text no object covers, interleaved library objects sliced, base objects and single-function splicing for mixed
mode). What a DLL needs beyond the engine's fixed-base exe:

  base relocations  LINK writes .reloc from the DIR32 fixups of its inputs. Code objects carry theirs; the data
                    stand-in gets one at every site of the original's .reloc inside it, against a symbol at the
                    address the original's pointer holds (code: the canonical text name; data: a label in the object
                    that holds the address). Sites in .text that no object relocates are reported.
  imports           .idata$2..$6 from the original's bytes, ranges derived from the import directory.
  exports           LINK builds the export directory from a generated .def (config/d3dren/d3dren.def's names and
                    ordinals, bound to the canonical symbols at the original's export addresses).
  resources         the original's .rsrc.
  header            --rich as in relink.py (tools/richpack.py); the timestamp is also written into the export
                    directory, where LINK stores it too.
LINK: /DLL /BASE:<image base> /SUBSYSTEM:WINDOWS,4.0 /ENTRY:<entry> /OPT:REF,NOICF /NODEFAULTLIB /DEF.
"""
import argparse
import json
import os
import struct
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import modcfg  # noqa: E402   (consumes --module; must come first)

if modcfg.NAME != 'd3dren':
    raise SystemExit('relink_dll.py is for --module d3dren (the engine uses tools/relink.py)')

import coffedit  # noqa: E402
from coffedit import Coff, Sec, Sym  # noqa: E402
import relink as R  # noqa: E402
import pefile  # noqa: E402

F = coffedit
R.BUILD = modcfg.BUILD
R.EXE = modcfg.IMAGE
R.OUT = os.path.normpath(os.environ.get('RELINK_OUT') or os.path.join(modcfg.BUILD, 'relink'))
OUT = R.OUT
PE = pefile.PE(modcfg.IMAGE)
BASE = PE.OPTIONAL_HEADER.ImageBase
R.IMAGE_BASE = BASE
R.IMAGE_END = BASE + PE.OPTIONAL_HEADER.SizeOfImage
DIR = PE.OPTIONAL_HEADER.DATA_DIRECTORY


def sec_of(name):
    return [s for s in PE.sections if s.Name.rstrip(b'\0') == name.encode()][0]


def layout():
    """The original's data ranges (VAs): IAT, import tables, the .rdata and .data stand-in ranges, exports."""
    PE.parse_data_directories()
    iat_lo, iat_hi = BASE + DIR[12].VirtualAddress, BASE + DIR[12].VirtualAddress + DIR[12].Size
    imp = BASE + DIR[1].VirtualAddress
    ndll = len(PE.DIRECTORY_ENTRY_IMPORT)
    d2 = (imp, imp + 20 * ndll)
    d3 = (d2[1], d2[1] + 20)
    ilt_end = d3[1]
    names_end = 0
    for e in PE.DIRECTORY_ENTRY_IMPORT:
        n = len(e.imports) + 1
        ilt_end = max(ilt_end, BASE + e.struct.OriginalFirstThunk + 4 * n)
        dll = BASE + e.struct.Name
        names_end = max(names_end, dll + len(e.dll) + 1)
        for i in e.imports:
            if i.name:
                p = BASE + i.hint_name_table_rva
                names_end = max(names_end, p + 2 + len(i.name) + 1)
    d4 = (d3[1], ilt_end)
    d6 = (ilt_end, names_end)
    rdata = sec_of('.rdata')
    data = sec_of('.data')
    exp_lo = BASE + DIR[0].VirtualAddress
    return {
        'iat': (iat_lo, iat_hi),
        'idata': [('.idata$2', d2), ('.idata$3', d3), ('.idata$4', d4), ('.idata$5', (iat_lo, iat_hi)),
                  ('.idata$6', d6)],
        'rdata': (iat_hi, imp),                       # constant data between the IAT and the import descriptors
        'data': (BASE + data.VirtualAddress, BASE + data.VirtualAddress + data.SizeOfRawData),
        'bss': data.Misc_VirtualSize - data.SizeOfRawData,
        'exports': (exp_lo, exp_lo + DIR[0].Size),
        'rdata_end': BASE + rdata.VirtualAddress + rdata.Misc_VirtualSize,
    }


def reloc_sites():
    """VAs of every HIGHLOW base relocation of the original."""
    PE.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_BASERELOC']])
    out = []
    for b in PE.DIRECTORY_ENTRY_BASERELOC:
        for e in b.entries:
            if e.type == 3:
                out.append(BASE + e.rva)
    return sorted(out)


def align_for(va, cap=16):
    al = cap
    while va % al:
        al //= 2
    return al


class DataObj:
    """A data object (stand-in or import tables) whose sections cover fixed VA ranges, with external labels at any
    address on request."""

    def __init__(self, path):
        self.c = Coff()
        self.path = path
        self.ranges = []        # (lo, hi, secno)
        self.labels = {}

    def add(self, name, lo, data, flags, size=None):
        s = Sec(name, data, [], flags)
        if size is not None:
            s.size = size
        self.c.sections.append(s)
        k = len(self.c.sections)
        self.c.syms.append(Sym(name, 0, k, 0, F.CLS_STATIC, F.section_aux(s.size, 0)))
        self.c.syms.append(None)
        self.ranges.append((lo, lo + s.size, k))
        return k

    def holds(self, va, end_ok=False):
        for lo, hi, k in self.ranges:
            if lo <= va < hi or (end_ok and va == hi):
                return k, va - lo
        return None

    def label(self, va, name=None):
        if va not in self.labels:
            k, off = self.holds(va, end_ok=True)
            nm = name or '__va_%08x' % va
            self.c.add_symbol(nm, off, k)
            self.labels[va] = nm
        return self.labels[va]

    def save(self):
        self.c.save(self.path)
        return self.path


def reloc_pad_obj(n):
    """An object whose only section is an unreferenced COMDAT holding `n` DIR32 fixups: /OPT:REF discards it, so
    it adds no bytes and no base relocation, but LINK counts its fixups when it sizes .reloc."""
    c = Coff()
    s = Sec('.rdata', b'\0' * (4 * n), [[4 * i, 2, 6] for i in range(n)],
            F.SCN_CNT_INIT | F.SCN_LNK_COMDAT | F.SCN_ALIGN[4] | F.SCN_MEM_READ)
    c.sections.append(s)
    c.syms.append(Sym('.rdata', 0, 1, 0, F.CLS_STATIC, F.section_aux(s.size, n, selection=2)))
    c.syms.append(None)
    c.add_symbol('__reloc_pad', 0, 1, 0, F.CLS_EXTERNAL)
    p = os.path.join(OUT, 'relocpad.obj')
    c.save(p)
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mode', default='targets', choices=('targets', 'mixed'))
    ap.add_argument('--rich', action='store_true')
    ap.add_argument('--splice', action='store_true')
    ap.add_argument('--exclude', default='', help='accepted for the byte gate; d3d.ren banks single functions only')
    ap.add_argument('--splice-exclude', help='a JSON file of ["unit", "hex va"] pairs not to splice')
    ap.add_argument('--splice-force', help='also splice this STUB (hex VA): the byte gate' + chr(39) + 's negative control')
    ap.add_argument('--stage', default='all')
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    lay = layout()
    img_orig = R.Orig()
    prep = R.Prepared(img_orig)
    print('gap objects %d (%d bytes)' % (len(prep.gaps), sum(h - l for l, h in prep.gaps)))
    prep.run()
    prep.slice_interleaved()
    sites = reloc_sites()
    text_lo, text_hi = img_orig.text_lo, img_orig.text_hi
    img = img_orig.img

    if a.mode == 'mixed':
        units, full, standin = R.inventory()
        # d3d.ren's units are not contiguous (a unit's COMDATs sit among other units' code), so a whole base object
        # cannot take its target object's place: every MATCH function, of fully and of partly matched units, is
        # spliced into the target objects instead, which keep the original's order
        prep.base, prep.base_failed, prep.order_report = {}, {}, {}
        if a.splice:
            picks = {}
            skip = set()
            if a.splice_exclude:
                with open(a.splice_exclude) as f:
                    skip = {(u, int(v, 16)) for u, v in json.load(f)}
            for r in R.INV_RESULTS:
                un = r.a.unit.name
                if r.status == 'MATCH' and r.a.kind == 'FUNCTION' and (un in prep.objs or un in prep.orig_vas) \
                        and (un, r.a.va) not in skip:
                    picks.setdefault(un, set()).add(r.a.va)
            kinds = ('FUNCTION',)
            if a.splice_force:
                fv = int(a.splice_force, 16)
                hit = [r for r in R.INV_RESULTS if r.a.va == fv and r.a.kind == 'STUB']
                if not hit:
                    raise SystemExit('--splice-force %s: no STUB at that address' % a.splice_force)
                picks.setdefault(hit[0].a.unit.name, set()).add(fv)
                kinds = ('FUNCTION', 'STUB')
            done, failed, sizes = prep.splice_functions(units, picks, kinds)
            print('spliced functions: %d in %d partly matched units (%d not spliceable)' % (
                sum(len(v) for v in done.values()), len(done), len(failed)))
            prep.splice_report = {'spliced': {u: ['%08x' % va for va in sorted(v)] for u, v in done.items()},
                                  'failed': {'%s@%08x' % k: why for k, why in sorted(failed.items())},
                                  'sizes': {'%s@%08x' % k: list(v) for k, v in sorted(sizes.items())}}

    # ---- data objects: import tables, stand-in .rdata/.data/.bss
    idata = DataObj(os.path.join(OUT, 'idata.obj'))
    for name, (lo, hi) in lay['idata']:
        idata.add(name, lo, img.read(lo, hi - lo),
                  F.SCN_CNT_INIT | F.SCN_ALIGN[2 if name == '.idata$6' else 4] | F.SCN_MEM_READ | F.SCN_MEM_WRITE)
    iat_k = [k for (n, _), (lo, hi, k) in zip(lay['idata'], idata.ranges) if n == '.idata$5'][0]
    for dll, slots in img_orig.dlls:
        for va, name, ordn, hint in slots:
            idata.c.add_symbol(R.imp_symbol(name if name else '%s_ord%d' % (dll, ordn)), va - lay['iat'][0], iat_k)

    st = DataObj(os.path.join(OUT, 'standin.obj'))
    rlo, rhi = lay['rdata']
    st.add('.rdata', rlo, img.read(rlo, rhi - rlo), F.SCN_CNT_INIT | F.SCN_ALIGN[align_for(rlo)] | F.SCN_MEM_READ)
    dlo, dhi = lay['data']
    st.add('.data', dlo, img.read(dlo, dhi - dlo),
           F.SCN_CNT_INIT | F.SCN_ALIGN[align_for(dlo)] | F.SCN_MEM_READ | F.SCN_MEM_WRITE)
    st.add('.bss', dhi, b'', F.SCN_CNT_UNINIT | F.SCN_ALIGN[16] | F.SCN_MEM_READ | F.SCN_MEM_WRITE, size=lay['bss'])
    st.c.syms.append(Sym('__except_list', 0, -1, 0, F.CLS_EXTERNAL))

    # base relocations inside the stand-in: every original site becomes a DIR32 against a symbol at its target
    data_objs = (st, idata)
    missing_text = []
    st_relocs = 0
    for s in sites:
        hit = st.holds(s)
        if hit is None:
            if not (text_lo <= s < text_hi) and idata.holds(s) is None:
                missing_text.append(s)
            continue
        k, off = hit
        v = struct.unpack('<I', img.read(s, 4))[0]
        if text_lo <= v < text_hi:
            if v not in prep.text_name:
                prep.add_code_symbol(v)
            nm = prep.text_name[v]
        else:
            holder = next((o for o in data_objs if o.holds(v, end_ok=True)), None)
            if holder is None:
                raise SystemExit('stand-in pointer at %08x -> %08x: no object holds the target' % (s, v))
            nm = holder.label(v)
        sec = st.c.sections[k - 1]
        sec.data = sec.data[:off] + b'\0\0\0\0' + sec.data[off + 4:]
        i = st.c.find(nm)
        if i is None or st.c.syms[i].sec != 0 and st.c.syms[i].cls != F.CLS_EXTERNAL:
            i = st.c.add_symbol(nm, 0, 0, 0, F.CLS_EXTERNAL) if st.c.find(nm) is None else st.c.find(nm)
        sec.relocs.append([off, i, 6])       # IMAGE_REL_I386_DIR32
        st_relocs += 1
    for sec in st.c.sections:
        sec.relocs.sort()
    # canonical data names the code objects refer to
    bad = []
    for va, nm in sorted(prep.data_name.items()):
        holder = next((o for o in data_objs if o.holds(va, end_ok=True)), None)
        if holder is None:
            bad.append((va, nm))
        elif va not in holder.labels:
            holder.label(va, nm)
        else:
            k, off = holder.holds(va, end_ok=True)
            holder.c.add_symbol(nm, off, k)
    print('stand-in relocations: %d; sites outside text/stand-in/idata: %d; data names without a holder: %d' % (
        st_relocs, len(missing_text), len(bad)))
    for va, nm in bad[:10]:
        print('  no holder for %08x %s' % (va, nm))
    prep.write()
    idata_p, st_p = idata.save(), st.save()
    rsrc_p = R.make_rsrc_obj(img_orig)

    # exports: names and ordinals from the original, bound to the canonical symbols
    PE.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
    exp = PE.DIRECTORY_ENTRY_EXPORT
    dll_name = PE.get_string_at_rva(exp.struct.Name).decode()
    lines = ['LIBRARY "%s"' % dll_name, 'EXPORTS']
    for e in exp.symbols:
        sym = prep.text_name[BASE + e.address]
        internal = sym[1:] if sym.startswith('_') and '@' not in sym else sym
        lines.append('    %s=%s @%d' % (e.name.decode(), internal, e.ordinal))
    def_p = os.path.join(OUT, 'exports.def')
    with open(def_p, 'w', newline='') as f:
        f.write('\n'.join(lines) + '\n')

    with open(os.path.join(OUT, 'gate_units.json'), 'w') as f:
        json.dump({'mode': a.mode, 'base': sorted(getattr(prep, 'base', {})), 'own_data': [],
                   'out_of_order': sorted(n for n, r in getattr(prep, 'order_report', {}).items() if r[1]),
                   'base_failed': sorted(getattr(prep, 'base_failed', {}) or []),
                   'splice': getattr(prep, 'splice_report', None)}, f, indent=1)
    if a.stage == 'prep':
        return 0
    inputs = [idata_p, st_p, rsrc_p] + [prep.paths[n] for _, n in prep.order]
    entry = prep.text_name[img_orig.entry]
    entry = entry[1:] if entry.startswith('_') else entry
    out_img = os.path.join(OUT, modcfg.OUT_IMAGE_NAME)
    # the export object: LIB builds it from the .def (LINK would build the same itself from /DEF, but stamp it with
    # its own @comp.id); it is linked last, as LINK places its own
    exp_lib = os.path.join(OUT, 'exports.lib')
    for p in (exp_lib, exp_lib[:-4] + '.exp'):
        if os.path.exists(p):
            os.remove(p)
    rc, out = R.run([os.path.join(R.MSVC, 'LIB.EXE'), '/NOLOGO', '/MACHINE:IX86', '/DEF:' + def_p, '/OUT:' + exp_lib])
    exp_obj = exp_lib[:-4] + '.exp'
    if rc != 0 or not os.path.exists(exp_obj):
        print(out)
        raise SystemExit('LIB could not build the export object')
    rsp = ['/NOLOGO', '/NODEFAULTLIB', '/DLL', '/OUT:' + out_img, '/BASE:0x%x' % BASE,
           '/SUBSYSTEM:WINDOWS,4.0', '/ENTRY:' + entry, '/OPT:REF,NOICF',
           '/MAP:' + os.path.join(OUT, 'd3d.map')]
    rsp += ['/INCLUDE:' + n for n in prep.includes]

    def link(objs):
        if a.rich:
            import richpack
            objs = richpack.pack(objs, richpack.rich_entries(R.EXE), os.path.join(OUT, 'rich'), tail=[exp_obj])
        else:
            objs = list(objs) + [exp_obj]
        if os.path.exists(out_img):
            os.remove(out_img)
        with open(os.path.join(OUT, 'link.rsp'), 'w', newline='') as f:
            f.write('\n'.join(rsp + ['"%s"' % o for o in objs]) + '\n')
        return R.run([R.LINK, '@' + os.path.join(OUT, 'link.rsp')])

    rc, out = link(inputs)
    if rc == 0:
        # LINK reserves .reloc for every DIR32 fixup it reads, 2 bytes each, including those in sections that
        # /OPT:REF later discards, and writes fewer. The original's inputs held more such fixups than ours do: the
        # size is reproduced with an unreferenced COMDAT of fixups (no bytes and no base relocation in the image).
        want = sec_of('.reloc').Misc_VirtualSize
        got = [s for s in pefile.PE(out_img, fast_load=True).sections
               if s.Name.rstrip(b'\0') == b'.reloc'][0].Misc_VirtualSize
        if got != want:
            if got > want or (want - got) % 2:
                print('.reloc reserves %#x bytes, the original %#x: cannot pad to it' % (got, want))
            else:
                pad = (want - got) // 2
                print('.reloc reserves %#x bytes, the original %#x: %d discarded fixups added' % (got, want, pad))
                rc, out = link(inputs + [reloc_pad_obj(pad)])
    print(out[-4000:])
    print('link rc', rc)
    if rc == 0 and a.rich:
        import richpack
        stamp = PE.FILE_HEADER.TimeDateStamp
        old = richpack.set_timestamp(out_img, stamp)
        # LINK writes the same stamp into the export directory
        new = pefile.PE(out_img, fast_load=True)
        new.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
        off = new.get_offset_from_rva(new.OPTIONAL_HEADER.DATA_DIRECTORY[0].VirtualAddress) + 4
        new.close()
        with open(out_img, 'r+b') as f:
            f.seek(off)
            f.write(struct.pack('<I', stamp))
        print('TimeDateStamp %08x -> %08x in the file header and the export directory (the only post-link edit)' % (
            old, stamp))
    return 0 if rc == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
