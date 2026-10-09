r"""Which compiler flag set reproduces the matched functions?  (evidence for the module's default flags)

  python tools/flagscan.py [--module d3dren] <unit filter> [--variants "/O2" "/Ox" ...] [--detail] [--fn VA,VA,...]

Works from Git Bash too (the MSYS-converted `/O1` is repaired; for `/I<path>` tokens also set MSYS2_ARG_CONV_EXCL='*');
needs no extra environment (DX8INC and the compiler wrapper come from the module).  Compiles every unit matching the
filter (`*` = every unit) once per flag variant into build/<module>/flagscan/<variant>/ (never into the real base
objects), checks all FUNCTION annotations against the image and prints, per variant, how many functions are
MATCH / DIFF / SIZE ...  Safe to run while others work.

Variant forms:
  native                  each unit's own `// FLAGS:` line (or the module default)
  /O2 /Ob1 ...            replaces every selected unit's optimisation flags (the common flags /c /nologo /MT /W3
                          /DWIN32 /DNDEBUG /I include stay)
  native -/Ob2 +/Ob1      edits each unit's own flags: `-X` drops the token X, `+X` appends X (so the STLport /D and /I
                          tokens of a unit survive).  `+/I<dir>` puts a header overlay before the SDK/VC98 INCLUDE path
                          (a header experiment without touching include/ or the SDK).
  native ~/O1=/O2         replaces the token /O1 by /O2 where the unit has it (others unchanged)

--detail (the inline/call-set view; implies a `native` reference run first): per variant, besides the status counts,
  FUNCTION annotations that lost MATCH against native ("broken"), STUBs that became MATCH ("gained"), and over all
  STUBs the sum of ALIGNED mismatches and the sum of call-sequence differences against the exe (difflib hunks, the
  `calls` measure of tools/audit.py).  A flag or header change that removes an inline/call-set wall shows up as a
  falling call sum.
--fn VA,...: implies --detail; per variant one row per listed function: status, ALIGNED mismatches, call differences.
"""
import contextlib
import collections
import difflib
import io
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import toolenv  # noqa: E402  (module selection, MSYS argument repair: `/O1` from Git Bash arrives as a path)
import build  # noqa: E402
import modcfg  # noqa: E402

DEFAULT_VARIANTS = ['native', '/O2', '/Ox', '/O2 /Gy-', '/O1', '/Od', '/O2 /Ob2', '/O2 /Ob0', '/O2 /G6', '/O2 /GX', '/Ox /Gy',
                    '/Og /Oi /Ot /Oy /Ob1 /Gs', '/Ot /Og /Oy /Oi /Ob2']


def variant_flags(variant, own):
    """The flag list a unit whose own flags are `own` gets under `variant` (see the module docstring)."""
    toks = variant.split()
    if not toks or toks[0] != 'native':
        return toks
    flags = list(own)
    for t in toks[1:]:
        if t.startswith('+') and len(t) > 1:
            flags.append(t[1:])
        elif t.startswith('-') and len(t) > 1:
            flags = [f for f in flags if f != t[1:]]
        elif t.startswith('~') and '=' in t:
            old, new = t[1:].split('=', 1)
            flags = [new if f == old else f for f in flags]
        else:
            raise ValueError('variant %r: token %r is not +X, -X or ~X=Y' % (variant, t))
    return flags


def call_diffs(aud, r, o, symtab):
    """difflib hunk size between our and the exe's call sequences for one result (audit.py's `calls` count)."""
    a = r.a
    sec, start, end = o.extent(a.symbol)
    ours, n = aud.ours(o, sec, start, end)
    ext = symtab.funcs.get(a.va)
    theirs = aud.theirs(a.va, (ext[0] - a.va) if ext else n)
    A, B = [t for _, t in ours.calls], [t for _, t in theirs.calls]
    sm = difflib.SequenceMatcher(None, A, B, autojunk=False)
    return sum(max(i2 - i1, j2 - j1) for tag, i1, i2, j1, j2 in sm.get_opcodes() if tag != 'equal')


def measure(results, exe, symtab, namemap):
    """(va, unit, line) -> (kind, status, aligned, calls, name) for every FUNCTION/STUB result (None: not measurable)."""
    import audit
    aud = audit.Auditor(exe, namemap, build.ALL_NAMES)
    objs = dict(build.LAST_OBJS)
    out = {}
    for r in results:
        if r.a.kind not in ('FUNCTION', 'STUB'):
            continue
        o = objs.get(r.a.unit.name)
        al = cd = None
        if r.status == 'MATCH':
            al = cd = 0
        elif o is not None and r.a.symbol is not None:
            try:
                al = build.aligned_counts(r, o, exe)[0]
                cd = call_diffs(aud, r, o, symtab)
            except Exception:       # an extent the disassembler can't read: no score
                pass
        out[(r.a.va, r.a.unit.name, r.a.line)] = (r.a.kind, r.status, al, cd, r.a.mangled or r.a.name or "?")
    return out


def compare(ref, m):
    """(broken FUNCTION keys, gained STUB keys, sum of STUB ALIGNED, sum of STUB call differences)."""
    broken = sorted(k for k, x in m.items() if x[0] == 'FUNCTION' and ref.get(k, (0, 'MATCH'))[1] == 'MATCH'
                    and x[1] != 'MATCH')
    gained = sorted(k for k, x in m.items() if x[0] == 'STUB' and x[1] == 'MATCH')
    stubs = [x for x in m.values() if x[0] == 'STUB']
    return broken, gained, sum(x[2] or 0 for x in stubs), sum(x[3] or 0 for x in stubs)


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    detail = '--detail' in args
    fns = set()
    if '--fn' in args:
        i = args.index('--fn')
        fns = {int(x, 16) for x in args[i + 1].split(',') if x}
        del args[i:i + 2]
        detail = True
    args = [a for a in args if a != '--detail']
    filt = args[0]
    variants = args[args.index('--variants') + 1:] if '--variants' in args else DEFAULT_VARIANTS
    if detail and variants[0] != 'native':
        variants = ['native'] + [v for v in variants if v != 'native']
    units = build.find_units() if filt in ('', '*') else [u for u in build.find_units() if filt in u.name]
    if not units:
        print('no unit matches', filt)
        return 1
    own = {u.name: list(u.flags) for u in units}
    exe, symtab = build.Exe(build.EXE), build.SymTab()
    libs = build.Libraries()
    head = ('MATCH', 'DIFF', 'SIZE', 'RELOC', 'ERROR') + (('broken', 'gained', 'stubALN', 'stubCALL') if detail else ())
    print('%-34s %s' % ('variant', ' '.join('%-9s' % s for s in head)))
    ref = None
    for v in variants:
        tag = ''.join(c if c.isalnum() else '_' for c in v)[:80]
        for u in units:
            u.flags = variant_flags(v, own[u.name])
            u.base_obj = os.path.join(build.BUILD, 'flagscan', tag, u.name + '.obj')
            os.makedirs(os.path.dirname(u.base_obj), exist_ok=True)
            with contextlib.redirect_stdout(io.StringIO()):
                build.compile_unit(u, force=True)
        with contextlib.redirect_stdout(io.StringIO()):
            results, namemap = build.run_check(units, exe, symtab, False, None, libs)
        c = collections.Counter(r.status for r in results if r.a.kind in ('FUNCTION', 'STUB'))
        row = [c.get(k, 0) for k in ('MATCH', 'DIFF', 'SIZE', 'RELOC', 'ERROR')]
        if detail:
            m = measure(results, exe, symtab, namemap)
            ref = ref or m
            broken, gained, aln, calls = compare(ref, m)
            row += [len(broken), len(gained), aln, calls]
        print('%-34s %s' % (v[:34], ' '.join('%-9d' % n for n in row)))
        if detail:
            for k in broken:
                print('    broken  %08x %-40s %s ALIGNED %s calls %s' % (k[0], m[k][4][:40], m[k][1], m[k][2], m[k][3]))
            for k in gained:
                print('    gained  %08x %s' % (k[0], m[k][4][:60]))
            for k in sorted(m):
                if k[0] in fns:
                    rk = ref.get(k)
                    print('    fn      %08x %-40s %-5s ALIGNED %-5s calls %-4s (native %s/%s)'
                          % (k[0], m[k][4][:40], m[k][1], m[k][2], m[k][3], rk and rk[2], rk and rk[3]))
        sys.stdout.flush()
    return 0


if __name__ == '__main__':
    sys.exit(main())
