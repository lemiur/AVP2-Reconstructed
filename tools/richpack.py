r"""Reproduce the original image's Rich header in a relink: merge the link's objects and give each one @comp.id.

LINK 6 writes the Rich header from its input objects: every object counts once, under its last `@comp.id`
symbol (an object without one counts as (0, 0)), and the entries appear in the order of each id's *last*
occurrence on the command line. The original lithtech.exe has 15 entries over 745 objects; the relink feeds LINK
900-1,300 objects (target objects, gap objects, sliced library objects, base objects, data pieces).

pack() therefore
  1. merges consecutive objects of the link order into at most `total` objects (only neighbours are merged, so the
     order of every section contribution is unchanged: LINK orders contributions of one output section by object,
     then by section-table position);
  2. pads with empty objects (no sections, nothing but `@comp.id`) up to exactly `total`;
  3. assigns the ids by position, in the original's entry order, so that counts and last-occurrence order match.

The ids are linker metadata copied from the original image, not a claim about which tool built which piece: the
relink's objects are cut from the image and do not correspond one to one to the original objects. Any
`@comp.id` an input object already carries (VC6 sets it in every compiled object) is renamed so that only the
assigned one counts.
"""
import os
import struct

import coffedit
from coffedit import Coff, Sym

CLS_EXTERNAL, CLS_STATIC, CLS_FUNCTION, CLS_FILE, CLS_WEAK = 2, 3, 101, 103, 105
SEL_ASSOCIATIVE = 5


def rich_entries(path):
    """[(comp.id value, count)] of an image's Rich header, in header order."""
    import pefile
    pe = pefile.PE(path)
    r = pe.RICH_HEADER
    if r is None:
        return []
    v = r.values
    return [(v[i], v[i + 1]) for i in range(0, len(v), 2)]


def merge(coffs):
    """One Coff holding every section and symbol of `coffs`, in order (section numbers and symbol indices rebased)."""
    out = Coff()
    for c in coffs:
        soff, yoff = len(out.sections), len(out.syms)
        for s in c.sections:
            n = coffedit.Sec(s.name, s.data, [[o, si + yoff, t] for o, si, t in s.relocs], s.flags, 0)
            n.size = s.size
            out.sections.append(n)
        for s in c.syms:
            if s is None:
                out.syms.append(None)
                continue
            sec, aux, name = s.sec, s.aux, s.name
            if sec > 0:
                sec += soff
            if name == '@comp.id':
                name = '@comp.ix'           # keep the slot (indices), stop LINK counting it
            if aux:
                aux = bytearray(aux)
                if s.cls == CLS_STATIC and s.sec > 0 and s.typ == 0 and s.naux == 1 and s.value == 0:
                    # section definition aux: Length, NRelocs, NLines, CheckSum, Number, Selection
                    number, sel = struct.unpack_from('<HB', aux, 12)
                    if sel == SEL_ASSOCIATIVE and number:
                        struct.pack_into('<H', aux, 12, number + soff)
                elif s.cls == CLS_WEAK:
                    tag = struct.unpack_from('<I', aux, 0)[0]
                    struct.pack_into('<I', aux, 0, tag + yoff)
                elif s.cls == CLS_EXTERNAL and (s.typ >> 4) == 2 and s.naux == 1:
                    tag, size, lines, nxt = struct.unpack_from('<IIII', aux, 0)
                    struct.pack_into('<IIII', aux, 0, tag + yoff if tag else 0, size, 0, nxt + yoff if nxt else 0)
                elif s.cls == CLS_FUNCTION:
                    nxt = struct.unpack_from('<I', aux, 12)[0]
                    if nxt:
                        struct.pack_into('<I', aux, 12, nxt + yoff)
                aux = bytes(aux)
            out.syms.append(Sym(name, s.value, sec, s.typ, s.cls, aux))
    return out


def pack(paths, entries, outdir, tail=()):
    """Merge/pad the ordered object list `paths` into exactly sum(counts) objects carrying `entries`' ids.

    `tail`: objects linked last, unmerged, each taking the next id from the end of the sequence (a DLL's export
    object, which LINK would otherwise build itself and stamp with its own build). Returns the new ordered path
    list. Raises ValueError when the entries are empty."""
    total = sum(n for _, n in entries) - len(tail)
    if not total:
        raise ValueError('the original image has no Rich header to reproduce')
    if os.path.isdir(outdir):
        for f in os.listdir(outdir):
            os.remove(os.path.join(outdir, f))
    os.makedirs(outdir, exist_ok=True)
    ids = [v for v, n in entries for _ in range(n)]
    tail_ids = ids[len(ids) - len(tail):] if tail else []
    ids = ids[:len(ids) - len(tail)] if tail else ids
    # leave room for at least one padding object per id that would otherwise land on a real group: not needed,
    # the ids are positional, so real groups simply take the first slots
    groups = []
    n = len(paths)
    k = min(n, total)
    for g in range(k):                          # k groups as even as possible, neighbours only
        lo, hi = g * n // k, (g + 1) * n // k
        groups.append(paths[lo:hi])
    out, manifest = [], {}
    for i in range(total):
        c = merge([Coff.load(p) for p in groups[i]]) if i < len(groups) else Coff()
        c.add_symbol('@comp.id', ids[i], -1, 0, CLS_STATIC)
        p = os.path.join(outdir, 'rich%04d.obj' % i)
        c.save(p)
        out.append(p)
        manifest[os.path.basename(p)] = groups[i] if i < len(groups) else []
    for i, (t, v) in enumerate(zip(tail, tail_ids)):
        c = Coff.load(t)
        for sym in c.syms:
            if sym is not None and sym.name == '@comp.id':
                sym.name = '@comp.ix'
        c.add_symbol('@comp.id', v, -1, 0, CLS_STATIC)
        p = os.path.join(outdir, 'tail%02d_%s' % (i, os.path.basename(t)))
        c.save(p)
        out.append(p)
        manifest[os.path.basename(p)] = [t]
    import json
    with open(os.path.join(outdir, 'manifest.json'), 'w') as f:       # merged object -> its input objects
        json.dump(manifest, f, indent=0)
    return out


def set_timestamp(path, stamp):
    """Write the PE file header's TimeDateStamp; return the old value. Nothing else in the file is touched."""
    with open(path, 'r+b') as f:
        d = f.read(0x400)
        pe_off = struct.unpack_from('<I', d, 0x3c)[0]
        assert d[pe_off:pe_off + 4] == b'PE\0\0', path
        old = struct.unpack_from('<I', d, pe_off + 8)[0]
        f.seek(pe_off + 8)
        f.write(struct.pack('<I', stamp))
    return old
