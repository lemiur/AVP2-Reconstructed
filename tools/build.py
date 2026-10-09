r"""decomp build driver for lithtech.exe (default) and d3d.ren (`--module d3dren`, see tools/modcfg.py).

  python tools/build.py                 compile, check, write target objs + objdiff.json, objdiff report
  python tools/build.py check [-v] [F...]  compile and check annotated functions (F = substring of unit/name/addr; several
                                        allowed; a unit filter ends with one summary line per unit); -h/--help: this text
  python tools/build.py diff [-a] [-g] <F>  side-by-side disassembly for matching functions; every diff ends with an
                                        ALIGNED line (instruction mismatches after alignment); -a prints aligned hunks;
                                        a function name/address compiles and prints only the unit that annotates it
                                        (-g/--global: the old behaviour, every unit's notes and compile of all stale units)
  python tools/build.py audit [-v] [-m] [F]  behaviour audit of every non-matching function: call sequence, constants,
                                        strings, globals, jump classes, callee order vs the exe (tools/audit.py);
                                        -m audits the matching functions instead (self-test: expect none)
  python tools/build.py parked [-a]     write PARKED.md: every parked STUB (-a: every STUB) with its scores, audit and notes
  python tools/build.py relink         layout gate: mixed relink of every fully matched unit (tools/relink_gate.py)
  python tools/build.py rules [-v]     code rules: which matched functions also pass the [match] rules of CODE_RULES
                                        (MATCH-PENDING otherwise; tools/rulecheck.py)
  python tools/build.py gate [...]     byte gate: the whole relinked exe must have the original's SHA1 (config/check.sha1),
                                        with every bankable fully matched unit from source; prints GATE GREEN/RED and
                                        the banked counts (tools/byte_gate.py; --allow-dirty, --clean, --no-build)
  python tools/build.py base <obj>      rebuild one base object (objdiff's "custom make" entry point)
  python tools/build.py report          progress/<module version>/report.json from the last full build (decomp.dev)
  python tools/build.py target <F>      rewrite the target object(s) of the unit(s) matching F from the last full build's
                                        namemap (per-unit, safe while others work; the unfiltered build rewrites all)
  --module d3dren                       work on d3d.ren: src/d3dren, config/d3dren, build/d3dren (default module: lithtech;
                                        DECOMP_MODULE=d3dren in the environment does the same)

Source annotations (reccmp style), on the line(s) directly before a definition:
  // FUNCTION: LITHTECH 0x0044cc80 [?mangled]   function that should match byte for byte   (D3DREN 0x10001340 for d3d.ren)
  // STUB: LITHTECH 0x0044cc80 [?mangled]       placeholder: compiled and diffed, not expected to match
  // GLOBAL: LITHTECH 0x004def1c [?mangled]     data symbol (names relocation targets)
A unit may set compiler flags with "// FLAGS: /O2 ..." in its first 30 lines (replaces DEFAULT_OPT).
"""
import hashlib, json, os, re, subprocess, sys, time

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import modcfg  # noqa: E402     (consumes --module <name>; must come before anything reads sys.argv)
from coffobj import CoffObj, undecorate, REL_DIR32, REL_REL32, REL_SIZES  # noqa: E402

EXE = modcfg.IMAGE
VC6CL = modcfg.CL           # lithtech worktrees: set VC6CL=tools\vc6cl_wt.bat
OBJDIFF = os.environ.get('OBJDIFF_CLI') or r'E:\AVP2Source\tools\objdiff\objdiff-cli.exe'
SRC, INC, BUILD = modcfg.SRC, modcfg.INC, modcfg.BUILD
OBJDIFF_DIR = modcfg.OBJDIFF_DIR       # where this module's objdiff.json lives; its paths are relative to it
SYMBOLS_CSV = modcfg.SYMBOLS_CSV
RENAMES_CSV = modcfg.RENAMES_CSV
SPLITS_CSV = modcfg.SPLITS_CSV
SYMBOLS_FALLBACK = r'E:\AVP2Source\out\stage4\dump\avp2_now.tsv' if modcfg.NAME == 'lithtech' else ''
NAMEMAP_JSON = os.path.join(BUILD, 'namemap.json')
LIBRARIES_JSON = modcfg.LIBRARIES_JSON     # tools/libmatch.py output
UNITS_CSV = modcfg.UNITS_CSV   # accepted unit ranges: unit,start,end,confidence


def load_unit_ranges():
    """unit name -> (start, end) from config/units.csv (a unit's functions are [start, end))."""
    import csv
    if not os.path.exists(UNITS_CSV):
        return {}
    with open(UNITS_CSV) as f:
        return {r['unit']: (int(r['start'], 16), int(r['end'], 16)) for r in csv.DictReader(f)}
COMMON_FLAGS = modcfg.COMMON_FLAGS
DEFAULT_OPT = modcfg.DEFAULT_OPT
UNASSIGNED_BLOCK = 0x10000     # target-only units for not-yet-decompiled code, per 64 KB of .text
# Same function under two names (CRT aliases): base undecorated name -> Ghidra name
ALIASES = {'stricmp': '__strcmpi', 'strcmpi': '__strcmpi', 'strnicmp': '__strnicmp', '_chkstk': '__alloca_probe'}


def same_symbol(base, gname):
    """Does base obj symbol `base` plausibly name what Ghidra calls `gname`?"""
    und = undecorate(base)
    if und in (gname, gname.split('::')[-1]) or ALIASES.get(und) == gname:
        return True
    # template spellings: CMoArray<unsigned char,class DefaultCache> vs Ghidra's CMoArray<unsigned_char,DefaultCache>;
    # VC6 mangles template functions without their arguments (BaseDelete vs BaseDelete<CUDPQuery>)
    def norm(x, strip_args):
        x = re.sub(r'\b(class|struct|enum) ', '', x).replace(' ', '').replace('_', '').strip("`'")
        return re.sub(r'<[^<>]*>', '', re.sub(r'<[^<>]*>', '', x)) if strip_args else x
    if norm(und, False) == norm(gname, False) or norm(und, True) == norm(gname, True):
        return True
    if '!' in gname and base.startswith('__imp__'):      # import slot: __imp__LoadStringA@16 vs USER32.DLL!LoadStringA
        imp = gname.split('!', 1)[1]                    # MSS32.DLL!_AIL_lock@0 keeps its decoration
        return base[len('__imp__'):].split('@')[0] == imp or base == '__imp_' + imp
    return False

ANNOT = re.compile(r'^\s*//\s*(FUNCTION|STUB|GLOBAL):\s*' + modcfg.TAG + r'\s+0x([0-9a-fA-F]+)(?:\s+(\S+))?')
FLAGS_RE = re.compile(r'^\s*//\s*FLAGS:\s*(.+)$')


# ---------------------------------------------------------------- exe + symbol table

class Exe:
    def __init__(self, path):
        import pefile
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.image_range = (self.base, self.base + self.pe.OPTIONAL_HEADER.SizeOfImage)
        self.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        self.import_slots = named_import_slots(self.pe)

    def read(self, va, n):
        """n bytes at va.  Inside a section the part past its raw data (the zero-filled, virtual-only tail: .bss, or a
        .data whose VirtualSize exceeds SizeOfRawData) reads as zero bytes, like the loader maps it, so a relocation
        into zero-initialised data (the shared "" literal at the end of d3d.ren's .data) can be verified.  A range that
        leaves the section's virtual extent is read exactly as before (truncated)."""
        rva = va - self.base
        for s in self.pe.sections:
            lo = s.VirtualAddress
            if lo <= rva and rva + n <= lo + max(s.Misc_VirtualSize, s.SizeOfRawData):
                have = lo + s.SizeOfRawData - rva           # bytes of the request that the file backs
                if have >= n:
                    break
                head = self.pe.get_data(rva, have) if have > 0 else b''
                return head + bytes(n - len(head))
        return self.pe.get_data(rva, n)


IMPORT_STDCALL_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)@([0-9]+)$')


def named_import_slots(pe):
    """Named PE imports as export name -> IAT VAs. Ordinal imports have no independent name to bind."""
    slots = {}
    for descriptor in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
        for imp in descriptor.imports:
            name = getattr(imp, 'name', None)
            if name is None:
                continue
            if isinstance(name, bytes):
                name = name.decode('ascii')
            if name:
                slots.setdefault(name, set()).add(imp.address)
    return slots


def import_name_candidates(symbol_name):
    """Strict spellings for an i386 COFF __imp_ reference; retain exact decoration and case."""
    if not symbol_name.startswith('__imp_'):
        return set()
    suffix = symbol_name[len('__imp_'):]
    if not suffix:
        return set()
    spellings = {suffix}
    variants = [suffix]
    if suffix.startswith('_'):
        variants.append(suffix[1:])       # one i386 C symbol underscore
        spellings.add(suffix[1:])
    for candidate in variants:
        match = IMPORT_STDCALL_RE.fullmatch(candidate)
        if match:
            spellings.add(match.group(1))
    return spellings


def bind_import_symbols(exe, objs, name2va):
    """Bind undefined external __imp_ COFF symbols only when their PE name resolves to one IAT slot."""
    pe_slots = getattr(exe, 'import_slots', None)
    if not pe_slots:
        return {}, []
    refs = {}
    for unit_name, obj in objs.items():
        for sym in obj.symbols.values():
            if sym.cls == 2 and sym.secno == 0 and not sym.is_section_symbol and sym.name.startswith('__imp_'):
                refs.setdefault(sym.name, set()).add(unit_name)

    bound, diagnostics = {}, []
    for symbol_name, owners in refs.items():
        candidates = import_name_candidates(symbol_name)
        matches = {name: set(pe_slots[name]) for name in candidates if name in pe_slots}
        slots = set().union(*matches.values()) if matches else set()
        if len(slots) > 1:
            detail = ', '.join('%s=%s' % (name, '/'.join('%08x' % va for va in sorted(vas)))
                               for name, vas in sorted(matches.items()))
            diagnostics.append((owners, 'ambiguous PE import %s: %s' % (symbol_name, detail)))
            continue
        if not slots:
            continue
        slot = next(iter(slots))
        bound[symbol_name] = slot
        old = name2va.get(symbol_name)
        if old is None:
            name2va[symbol_name] = slot
        elif old != slot:
            diagnostics.append((owners, 'PE import slot conflict for %s: namemap %08x, PE %08x (using PE slot for relocations)'
                                % (symbol_name, old, slot)))
    return bound, diagnostics


class SymTab:
    """Function extents and names from Ghidra (config/symbols.csv, or the older DumpFuncs tsv)."""

    def __init__(self):
        self.funcs = {}     # va -> (end, name)
        self.names = {}     # va -> name (all kinds)
        self.by_name = {}   # name -> [va]
        if os.path.exists(SYMBOLS_CSV):
            import csv
            for r in csv.DictReader(open(SYMBOLS_CSV, encoding='utf-8')):
                va, end = int(r['addr'], 16), int(r['end'], 16)
                if r['kind'] == 'func':
                    self.funcs[va] = (end, r['name'])
                if r['kind'] in ('func', 'data', 'label', 'import'):
                    self.names.setdefault(va, r['name'])
            self.source = SYMBOLS_CSV
        else:
            rows = [l.rstrip('\n').split('\t') for l in open(SYMBOLS_FALLBACK, encoding='utf-8')][1:]
            vas = sorted((int(r[0], 16), r[1]) for r in rows if r[4] == 'false')
            for (va, name), nxt in zip(vas, vas[1:] + [(None, None)]):
                self.funcs[va] = ((nxt[0] if nxt[0] else va + 16), name)
                self.names[va] = name
            self.source = SYMBOLS_FALLBACK
        # Function extents Ghidra merged (config/splits.csv), split as mktarget does.
        import mktarget
        fl = mktarget.apply_splits(sorted((va, e, n) for va, (e, n) in self.funcs.items()), SPLITS_CSV)
        self.funcs = {va: (e, n) for va, e, n in fl}
        for va, e, n in fl:
            self.names.setdefault(va, n)
        # Names proven wrong by matched code, until they're synced back into Ghidra.
        if os.path.exists(RENAMES_CSV):
            import csv
            for r in csv.DictReader(l for l in open(RENAMES_CSV, encoding='utf-8') if not l.startswith('#')):
                va = int(r['addr'], 16)
                self.names[va] = r['name']
                if va in self.funcs:
                    self.funcs[va] = (self.funcs[va][0], r['name'])
        for va, n in self.names.items():
            self.by_name.setdefault(n, []).append(va)


# ---------------------------------------------------------------- units and annotations

class Annot:
    def __init__(self, kind, va, mangled, unit, line, name=None):
        self.kind, self.va, self.mangled, self.unit, self.line, self.name = kind, va, mangled, unit, line, name
        self.symbol = None
        self.error = None
        self.notes = []         # the comment block directly above the annotation
        self.parked = None      # STUB parked by a `// PARKED: <reason>` line in that block

    def where(self):
        return '%s:%d' % (self.unit.rel, self.line)


class Unit:
    def __init__(self, path):
        self.path = path
        self.rel = os.path.relpath(path, SRC).replace('\\', '/')
        self.name = os.path.splitext(self.rel)[0]
        self.base_obj = os.path.join(BUILD, 'base', self.name + '.obj')
        self.target_obj = os.path.join(BUILD, 'target', self.name + '.obj')
        self.flags, self.annots = list(DEFAULT_OPT), []
        with open(path, encoding='latin1') as source:
            lines = source.read().split('\n')
        for i, l in enumerate(lines):
            m = FLAGS_RE.match(l)
            if m and i < 30:
                self.flags = m.group(1).split()
            m = ANNOT.match(l)
            if m:
                a = Annot(m.group(1), int(m.group(2), 16), m.group(3), self, i + 1)
                a.name = _decl_name(lines, i + 1, a.kind == 'GLOBAL')
                k = i - 1
                while k >= 0 and lines[k].lstrip().startswith('//') and not ANNOT.match(lines[k]):
                    k -= 1
                a.notes = [x.strip()[2:].strip() for x in lines[k + 1:i]]
                for n in a.notes:
                    if n.startswith('PARKED:') and a.kind == 'STUB':
                        a.parked = n[len('PARKED:'):].strip() or '(no reason given)'
                self.annots.append(a)


FUNCTION_POINTER_RE = re.compile(
    r'\(\s*(?:(?:__cdecl|__stdcall|__fastcall|__thiscall)\s+)?'
    r'(?:[A-Za-z_][\w:]*::\s*)?\*+\s*(?:(?:const|volatile)\s+)*'
    r'([A-Za-z_][\w:]*)\s*(?:\[[^\]]*\]\s*)*\)\s*\(')


def _function_pointer_name(line):
    """Name in a function-pointer variable declarator, including arrays and member pointers."""
    line = line.split('//')[0].split('=')[0]
    for m in FUNCTION_POINTER_RE.finditer(line):
        prefix = line[:m.start()]
        # A callback parameter inside an ordinary prototype is not a variable declaration.
        # Balanced prefix parentheses allow __declspec(...) on the variable itself.
        if prefix.count('(') == prefix.count(')'):
            return m.group(1)
    return None


def _decl_name(lines, i, is_data):
    """Qualified name declared by the first code line at/after lines[i]."""
    while i < len(lines):
        l = lines[i].split('//')[0].strip()
        i += 1
        if not l or l.startswith('#') or ANNOT.match(lines[i - 1]) or l.startswith('template'):
            continue
        if is_data:
            if '=' in l:
                l = l[:l.index('=')] + '='      # the initializer can't name it: `const float x = 0.1f;`
            pointer_name = _function_pointer_name(l)
            if pointer_name is not None:
                return pointer_name
            m = re.findall(r'([A-Za-z_][\w:]*)\s*(?:\[[^\]]*\])*\s*(?:=|;|$)', l)      # Cls::s_X too
            m = m or re.findall(r'([A-Za-z_]\w*)\s*\(', l)[:1]      # constructor syntax: LTLink g_X(LTLink_Init);
            return m[-1] if m else None
        if '(' not in l:
            continue
        m = re.findall(r'([A-Za-z_~][\w:~]*)\s*$', l[:l.index('(')])
        return m[-1] if m else None
    return None


def header_globals():
    """(va, mangled, declared name, where) for each // GLOBAL: annotation in include/."""
    out = []
    for d, _, files in modcfg.walk(INC):
        for f in sorted(files):
            if not f.lower().endswith('.h'):
                continue
            path = os.path.join(d, f)
            with open(path, encoding='latin1') as source:
                lines = source.read().splitlines()
            for i, l in enumerate(lines):
                m = ANNOT.match(l)
                if m and m.group(1) == 'GLOBAL':
                    out.append((int(m.group(2), 16), m.group(3), _decl_name(lines, i + 1, True),
                                '%s:%d' % (os.path.relpath(path, INC).replace(os.sep, '/'), i + 1)))
    return out


def find_units():
    units = []
    for d, _, files in modcfg.walk(SRC):
        units += [Unit(os.path.join(d, f)) for f in sorted(files) if f.lower().endswith(('.cpp', '.c'))]
    return sorted(units, key=lambda u: u.rel)


# ---------------------------------------------------------------- compile

_HDR_DIGEST = [None]


def _header_digest():
    """Digest of the content of every file under include/ (this module's view of it), computed once per process.
    Content, not mtimes: an object is stale when what it was compiled from changed, not when something was touched."""
    if _HDR_DIGEST[0] is None:
        h = hashlib.sha1()
        for d, dirs, files in modcfg.walk(INC):
            dirs.sort()
            for f in sorted(files):
                p = os.path.join(d, f)
                h.update(os.path.relpath(p, INC).replace('\\', '/').encode('latin1', 'replace') + b'\0')
                try:
                    with open(p, 'rb') as fh:
                        h.update(fh.read())
                except OSError:
                    pass
                h.update(b'\0')
        _HDR_DIGEST[0] = h.hexdigest()
    return _HDR_DIGEST[0]


def _toolchain_digest():
    """Content of the compiler wrapper batch file and tools/modcfg.py (default flags): changing the toolchain makes every object stale."""
    h = hashlib.sha1()
    p = VC6CL if os.path.isabs(VC6CL) else os.path.join(ROOT, VC6CL)
    for q in (p, modcfg.__file__):          # modcfg.py holds the flags
        try:
            with open(q, 'rb') as fh:
                h.update(fh.read())
        except OSError:
            pass
        h.update(b'\0')
    h.update(repr(sorted(modcfg.CL_ENV.items())).encode('latin1', 'replace'))
    return h.hexdigest()


def _unit_stamp(u):
    """What u's object is compiled from: source bytes, flags, headers, toolchain.  Computed BEFORE the compiler runs, so an
    edit that lands while it runs makes the next check recompile instead of trusting an object of the old text."""
    h = hashlib.sha1()
    try:
        with open(u.path, 'rb') as fh:
            h.update(fh.read())
    except OSError:
        pass
    h.update(b'\0' + ' '.join(COMMON_FLAGS + list(u.flags)).encode('latin1', 'replace') + b'\0')
    h.update((_header_digest() + _toolchain_digest()).encode())
    return h.hexdigest()


def _read_stamp(obj):
    try:
        with open(obj + '.stamp') as fh:
            return fh.read().strip()
    except OSError:
        return None


def _write_stamp(obj, stamp):
    tmp = '%s.%d.stamptmp' % (obj, os.getpid())
    with open(tmp, 'w') as fh:
        fh.write(stamp + '\n')
    _replace(tmp, obj + '.stamp')


def _up_to_date(u, stamp=None):
    """Content-hash staleness, no mtime comparison: two edits within one clock tick, or an edit that lands while an
    older compile finishes, leave an object that is newer than the source it was not compiled from."""
    if not os.path.exists(u.base_obj):
        return False
    return _read_stamp(u.base_obj) == (stamp or _unit_stamp(u))


def _lock(path, timeout=900):
    """Exclusive lock file (several agents may check the same unit at once). Returns True once held."""
    t0 = time.time()
    while True:
        try:
            fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            return True
        except FileExistsError:
            try:
                if time.time() - os.path.getmtime(path) > 600:     # a crashed holder: no compile takes 10 minutes
                    os.remove(path)
                    continue
            except OSError:
                pass
            if time.time() - t0 > timeout:
                return False
            time.sleep(0.2)


def _replace(src, dst):
    """os.replace, retried while a reader has dst open (Windows refuses to replace an open file)."""
    for i in range(150):
        try:
            os.replace(src, dst)
            return
        except PermissionError:
            time.sleep(0.2)
    os.replace(src, dst)


def compile_unit(u, force=False, quiet=False):
    """Compile u unless its object is current.  quiet: print nothing and list no failure (units nobody asked for)."""
    # The compiler writes a private temporary object that is renamed into place, under a per-unit lock, so a
    # concurrent check never reads a half-written object and two compiles of one unit don't interleave.
    os.makedirs(os.path.dirname(u.base_obj), exist_ok=True)
    if not force and _up_to_date(u):
        return True
    lock = u.base_obj + '.lock'
    if not _lock(lock):
        if not quiet:
            print('COMPILE FAILED: %s (timed out waiting for %s)' % (u.rel, lock))
            COMPILE_FAILED.append(u.rel)
        return False
    try:
        stamp = _unit_stamp(u)
        if not force and _up_to_date(u, stamp):       # another process compiled it while we waited
            return True
        tmp = '%s.%d.tmp.obj' % (u.base_obj[:-4], os.getpid())
        args = [VC6CL] + COMMON_FLAGS + u.flags + ['/Fo' + tmp, u.path]
        r = subprocess.run(['cmd', '/c'] + args, capture_output=True, text=True, cwd=os.path.dirname(u.path),
                           env=dict(os.environ, **modcfg.CL_ENV))
        out = '\n'.join(l for l in r.stdout.splitlines() if l.strip() and l.strip() != os.path.basename(u.path))
        if out and not quiet:
            print(out)
        if r.returncode != 0 or not os.path.exists(tmp):
            # never check a stale object: its functions report ERROR until the unit compiles again
            for p in (tmp, u.base_obj, u.base_obj + '.stamp'):
                if os.path.exists(p):
                    os.remove(p)
            if not quiet:
                print('COMPILE FAILED: %s' % u.rel)
                COMPILE_FAILED.append(u.rel)
            return False
        _replace(tmp, u.base_obj)
        _write_stamp(u.base_obj, stamp)
        return True
    finally:
        try:
            os.remove(lock)
        except OSError:
            pass


COMPILE_FAILED = []


# ---------------------------------------------------------------- check

class Result:
    def __init__(self, a):
        self.a = a
        self.status, self.detail, self.size, self.diffs = 'ERROR', '', 0, 0
        self.target_size = None    # split-aware symbol extent; size remains the compiled body length
        self.unverified = []    # (va, base symbol name) reloc targets not confirmed by name or content
        self.bad_relocs = []    # (offset, base name, expected va, exe va)
        self.interior = []      # (referenced va, symbol va) for relocations with a nonzero addend (g_Render+0x58)


def bind_symbols(units, obj):
    """Attach the compiled obj symbol to each annotation (by undecorated name, or the explicit mangled name)."""
    for u in units:
        o = obj.get(u.name)
        if not o:
            continue
        for a in u.annots:
            if a.kind == 'GLOBAL':
                cands = [s for s in o.symbols.values() if not s.is_section_symbol and s.cls in (2, 3)
                         and (s.name == a.mangled if a.mangled else undecorate(s.name) == a.name)]
                names = sorted({s.name for s in cands})
                a.symbol = names[0] if len(names) == 1 else None
                a.static = bool(cands) and all(s.cls == 3 for s in cands)     # file-static data: per-unit name
                a.error = None if a.symbol else 'GLOBAL %s: %d symbol candidates %s' % (a.name, len(names), names[:4])
                continue
            cands = [s for s in o.functions() if (s.name == a.mangled if a.mangled else undecorate(s.name) == a.name)]
            a.symbol = cands[0] if len(cands) == 1 else None
            a.error = None if a.symbol else '%s: %d function symbols match %r %s (add the mangled name to the annotation)' % (
                a.where(), len(cands), a.name, [c.name for c in cands][:4])


class Libraries:
    """Prebuilt library objects found in the exe by tools/libmatch.py (config/libraries.json)."""

    def __init__(self):
        self.units, self.names = [], {}
        self.code_aliases, self.code_alias_problems = {}, []
        if os.path.exists(LIBRARIES_JSON):
            with open(LIBRARIES_JSON) as f:
                j = json.load(f)
            self.units = j['units']
            self.names = {int(k, 16): v for k, v in j['names'].items()}
        for u in self.units:
            u['base_obj'] = os.path.join(BUILD, 'base', u['name'] + '.obj')
            u['target_obj'] = os.path.join(BUILD, 'target', u['name'] + '.obj')
            # code sections placed in .text (.text$x unwind funclets have no symbols for objdiff)
            u['text'] = sorted((int(va, 16), n) for va, (_, n, _, _, sec, _) in u['sections'].items() if sec == '.text')
            u['funclets'] = sorted((int(va, 16), n) for va, (_, n, _, _, sec, _) in u['sections'].items() if sec != '.text')
        self._load_code_aliases()

    def _load_code_aliases(self):
        """Load additional names only for corroborated external function symbols at known library entries.

        ``names`` stays the canonical VA-to-name map used for ownership and target generation.  This separate
        name-to-VA map is only for relocation verification; it accepts aliases from exact/verified .text rows when
        the configured original COFF object proves both the alias and the row's canonical function entry.
        """
        candidates, original_objects = {}, {}
        canonical_addrs = {}
        for va, name in self.names.items():
            canonical_addrs.setdefault(name, set()).add(va)

        def problem(message):
            self.code_alias_problems.append(message)

        def original_object(unit):
            path = unit.get('obj')
            if path not in original_objects:
                try:
                    original_objects[path] = CoffObj(path)
                except Exception as exc:  # malformed, missing, or replaced original object: fail closed
                    original_objects[path] = None
                    problem('cannot verify library code aliases in %s (%s): %s' %
                            (unit.get('name', path), path, exc))
            return original_objects[path]

        for unit in self.units:
            functions = {int(va, 16): name for va, name in unit.get('functions', {}).items()}
            for base_text, row in unit.get('sections', {}).items():
                try:
                    base_va = int(base_text, 16)
                    secno, size, grade, _, section_name, symbol_rows = row
                    secno, size = int(secno), int(size)
                except (TypeError, ValueError, IndexError):
                    continue
                if grade not in ('exact', 'verified') or section_name != '.text':
                    continue

                obj = original_object(unit)
                if obj is None:
                    continue
                if secno <= 0 or secno > len(obj.sections):
                    problem('library code alias section missing in %s: section %s for %08x' %
                            (unit.get('name'), secno, base_va))
                    continue
                sec = obj.sections[secno - 1]
                if sec.index != secno or sec.name != section_name or len(sec.data) != size or \
                        (sec.flags & 0x20) == 0 or (sec.flags & 0x20000000) == 0:
                    problem('library code alias section mismatch in %s @%08x: metadata %s/%s/%d, COFF %s/%s/%d flags %08x' %
                            (unit.get('name'), base_va, secno, section_name, size,
                             sec.index, sec.name, len(sec.data), sec.flags))
                    continue

                for symbol_row in symbol_rows:
                    try:
                        offset, name, cls, typ = symbol_row
                        offset, cls, typ = int(offset), int(cls), int(typ)
                    except (TypeError, ValueError, IndexError):
                        continue
                    # COFF aliases must be external, typed function symbols in executable section bytes.
                    if cls != 2 or typ != 0x20:
                        continue
                    if offset < 0 or offset >= size:
                        problem('library code alias %s has out-of-bounds offset %s in %s @%08x size %d' %
                                (name, offset, unit.get('name'), base_va, size))
                        continue

                    entry_va = base_va + offset
                    canonical_name = functions.get(entry_va)
                    if not canonical_name:
                        problem('library code alias %s is not at a canonical function entry in %s @%08x+%x' %
                                (name, unit.get('name'), base_va, offset))
                        continue

                    def has_external_function(symbol_name):
                        return any(s.name == symbol_name and s.secno == secno and s.value == offset and
                                   s.cls == 2 and s.typ == 0x20 for s in obj.symbols.values())

                    if not has_external_function(canonical_name):
                        problem('library canonical function symbol mismatch in %s @%08x: %s is not external type 32 at section %d+%x' %
                                (unit.get('name'), entry_va, canonical_name, secno, offset))
                        continue
                    if not has_external_function(name):
                        problem('library code alias symbol mismatch in %s @%08x: metadata %s is not external type 32 at section %d+%x' %
                                (unit.get('name'), entry_va, name, secno, offset))
                        continue
                    canonical_known = canonical_addrs.get(canonical_name, set())
                    if canonical_known and canonical_known != {entry_va}:
                        problem('library canonical entry %s @%08x conflicts with canonical library name at %s' %
                                (canonical_name, entry_va,
                                 ', '.join('%08x' % va for va in sorted(canonical_known))))
                        continue
                    if name == canonical_name:
                        continue

                    known = canonical_addrs.get(name, set())
                    if known and known != {entry_va}:
                        problem('library code alias %s @%08x conflicts with canonical library name at %s' %
                                (name, entry_va, ', '.join('%08x' % va for va in sorted(known))))
                        continue
                    if known == {entry_va}:
                        continue
                    provenance = '%s section %d+%x' % (unit.get('name'), secno, offset)
                    candidates.setdefault(name, []).append((entry_va, provenance))

        for name, rows in sorted(candidates.items()):
            vas = sorted({va for va, _ in rows})
            if len(vas) != 1:
                detail = ', '.join('%08x (%s)' % (va, source) for va, source in sorted(set(rows)))
                problem('ambiguous library code alias %s: %s' % (name, detail))
                continue
            self.code_aliases[name] = vas[0]

    def code_bytes(self):
        return sum(n for u in self.units for _, n in u['text'] + u['funclets'])


ICF_CSV = os.path.join(modcfg.CONFIG, 'icf.csv')     # this module's ICF-folded addresses (absent for lithtech: nothing accepted)


def load_icf(path=None):
    """config/<module>/icf.csv (addr,note[,names]): addresses where the linker folded identical COMDATs (/OPT:ICF), so several
    source symbols legitimately live at one address.  {va: None | set of mangled names accepted there}.  `names` (optional,
    ';'-separated) limits the silent acceptance to those symbols: anything else at that address is still a clash."""
    out = {}
    path = path or ICF_CSV
    if os.path.exists(path):
        import csv
        for r in csv.DictReader(l for l in open(path, encoding='utf-8') if not l.startswith('#')):
            if not (r.get('addr') or '').strip():
                continue
            names = {x.strip() for x in (r.get('names') or '').split(';') if x.strip()}
            out[int(r['addr'], 16)] = names or None
    return out


def icf_accepts(icf, va, *names):
    """Is it expected that all of `names` (mangled) name the code at va?"""
    if va not in icf:
        return False
    allowed = icf[va]
    return allowed is None or all(n in allowed for n in names)


def build_namemap(units, symtab, icf=None):
    """va -> mangled base name, from annotations; name -> va for external symbols; and per-unit
    name -> va for static (file-local) symbols such as VC6's _$E1 static initialisers.  `icf` (load_icf): at those addresses
    several annotated names are expected (the first stays the namemap's; every name verifies relocations)."""
    icf = icf or {}
    va2name, name2va, problems = {}, {}, []
    local = {}
    for u in units:
        mine = local.setdefault(u.name, {})
        for a in u.annots:
            n = a.symbol.name if hasattr(a.symbol, 'name') else a.symbol
            if not n:
                continue
            is_static = getattr(a.symbol, 'cls', 2) == 3 or getattr(a, 'static', False)
            names = mine if is_static else name2va
            bad_va, bad_name = va2name.get(a.va, n) != n, names.get(n, a.va) != a.va
            if bad_va and not bad_name and icf_accepts(icf, a.va, va2name[a.va], n):
                names[n] = a.va         # a second name of an ICF-folded address
                continue
            if bad_va or bad_name:
                problems.append('%s: %s @%08x conflicts with %s @%08x' % (a.where(), n, a.va, va2name.get(a.va), names.get(n, 0)))
            va2name[a.va], names[n] = n, a.va
    return va2name, name2va, local, problems


def literal_bytes(o, sym):
    """Data of a COMDAT literal (string / float constant) symbol defined in obj o, else None."""
    if sym.secno <= 0:
        return None
    sec = o.section_of(sym)
    return sec.data[sym.value:]


def check_function(a, o, exe, symtab, name2va, ghidra_conflicts, local=None, import_bindings=None):
    r = Result(a)
    if not a.symbol:
        r.detail = a.error
        return r
    sec, start, end = o.extent(a.symbol)
    base = sec.data[start:end]
    r.size = len(base)
    ext = symtab.funcs.get(a.va)
    exe_len = (ext[0] - a.va) if ext else None
    r.target_size = exe_len
    target = exe.read(a.va, len(base))
    relocs = o.relocs_in(sec, start, end)
    mask = bytearray(len(base))
    for off, _, typ, _ in relocs:
        for k in range(REL_SIZES.get(typ, 4)):
            if off + k < len(mask):
                mask[off + k] = 1
    diffs = [i for i in range(len(base)) if not mask[i] and base[i] != target[i]]
    r.diffs = len(diffs)
    for off, s, typ, addend in relocs:
        if typ not in (REL_DIR32, REL_REL32) or off + 4 > len(base):
            continue
        field = target[off:off + 4]
        if typ == REL_DIR32:
            tva = int.from_bytes(field, 'little') - addend
        else:
            tva = a.va + off + 4 + int.from_bytes(field, 'little', signed=True) - addend
        if s.is_section_symbol or (s.secno == a.symbol.secno):     # own section: switch tables, local labels
            expect = a.va + (s.value if not s.is_section_symbol else 0) - start
            if tva != expect:
                r.bad_relocs.append((off, s.name, expect, tva))
            continue
        if addend and typ == REL_DIR32:
            r.interior.append((tva + addend, tva))
        if s.cls == 2 and s.secno == 0 and s.name in (import_bindings or {}):
            known = import_bindings[s.name]
        else:
            known = (local or {}).get(s.name) if s.cls == 3 else name2va.get(s.name)
        if known is not None:
            if known != tva:
                r.bad_relocs.append((off, s.name, known, tva))
            continue
        lit = literal_bytes(o, s) if s.name.startswith(('??_C@', '__real@')) else None
        if lit is not None:
            try:
                got = exe.read(tva, len(lit))
            except Exception:     # target outside the image: the reloc is simply wrong
                got = None
            if got != lit:
                r.bad_relocs.append((off, s.name, None, tva))
            continue
        gname = symtab.names.get(tva)
        if gname and not gname.startswith(('FUN_', 'DAT_', 'LAB_', 'PTR_', 'switchD', 'caseD', 's_', 'u_')) \
                and not same_symbol(s.name, gname):
            ghidra_conflicts.append((a, s.name, tva, gname))
        r.unverified.append((tva, s.name))
    # the linker pads between COMDATs with int3 (0xCC) when the object's own padding is shorter (e.g. /Od units)
    pad_only = exe_len is not None and exe_len > len(base) and \
        all(b in (0xCC, 0x90) for b in exe.read(a.va + len(base), exe_len - len(base)))
    if len(base) > len(target) or (exe_len is not None and exe_len != len(base) and not pad_only):
        r.status = 'SIZE'
        r.detail = 'base %d bytes, Ghidra extent %s' % (len(base), exe_len)
        if diffs or r.bad_relocs:
            r.detail += ', %d bytes differ' % len(diffs)
    elif diffs:
        r.status, r.detail = 'DIFF', '%d/%d bytes differ (first +0x%x)' % (len(diffs), len(base), diffs[0])
    elif r.bad_relocs:
        r.status = 'RELOC'
        r.detail = ', '.join('+0x%x %s: want %s got %08x' % (off, n, ('%08x' % e) if e else 'literal', g)
                             for off, n, e, g in r.bad_relocs[:3])
    else:
        r.status = 'MATCH'
        if r.unverified:
            r.detail = '%d reloc target(s) named by this match' % len(r.unverified)
    return r


LAST_OBJS = {}     # unit name -> CoffObj of the last run_check (build.py audit reads them)
ALL_NAMES = {}     # every name -> va the last run_check knows, ICF-folded duplicates included (build.py audit)


def _filters(filt):
    """A filter argument (None, one substring, or a list of them) as a list of non-empty substrings."""
    if filt is None:
        return []
    return [f for f in ([filt] if isinstance(filt, str) else filt) if f]


def _wanted(a, filts):
    """Does annotation a match any filter (substring of its unit name, its name, or its address)?"""
    return not filts or any(f in a.unit.name or f in (a.name or '') or f in '%08x' % a.va for f in filts)


def run_check(units, exe, symtab, verbose=False, filt=None, libs=None, scope=None):
    """Check every annotated function.  Prints the rows of the functions matching `filt` (a substring or a list of them)
    and the NAME?/CLASH/MULTI/ANNOTATION CONFLICT notes; `scope` (a set of unit names) limits those notes to those units
    (the checking itself always covers every unit that has an object: names come from all of them)."""
    filts = _filters(filt)
    scope_rels = None if scope is None else {u.rel for u in units if u.name in scope}
    in_scope = (lambda a: True) if scope is None else (lambda a: a.unit.name in scope)
    icf = load_icf()
    objs = {u.name: CoffObj(u.base_obj) for u in units if os.path.exists(u.base_obj)}
    LAST_OBJS.clear()
    LAST_OBJS.update(objs)
    bind_symbols(units, objs)
    va2name, name2va, local, problems = build_namemap(units, symtab, icf)
    # GLOBAL annotations in include/ name the symbol in every object that references it
    for va, mangled, name, where in header_globals():
        syms = {s.name for o in objs.values() for s in o.symbols.values() if not s.is_section_symbol and s.cls == 2
                and (s.name == mangled if mangled else undecorate(s.name) == name)}
        for n in syms:
            if name2va.setdefault(n, va) != va or va2name.setdefault(va, n) != n:
                if icf_accepts(icf, va, va2name.get(va), n) and name2va.get(n) == va:
                    continue
                problems.append('include/%s: %s @%08x conflicts with %s @%08x' % (where, n, va, va2name.get(va), name2va.get(n, 0)))
    # library names verify engine relocations too (CRT calls, __imp__X@n import slots)
    lib_names = libs.names if libs else {}
    for va, n in lib_names.items():
        if name2va.setdefault(n, va) != va:
            problems.append('library name %s @%08x conflicts with annotation @%08x' % (n, va, name2va[n]))
    lib_code_aliases = getattr(libs, 'code_aliases', {}) if libs else {}
    for name, va in sorted(lib_code_aliases.items()):
        old = name2va.get(name)
        if old is not None and old != va:
            problems.append('library code alias %s @%08x conflicts with annotation @%08x' % (name, va, old))
        else:
            name2va.setdefault(name, va)
    for p in getattr(libs, 'code_alias_problems', []) if libs else []:
        print('LIBRARY CODE ALIAS: ' + p)
    import_bindings, import_problems = bind_import_symbols(exe, objs, name2va)
    for p in problems:
        if p.startswith('library code alias '):
            print('ANNOTATION CONFLICT: ' + p)
        elif scope_rels is not None:
            if p.split(':', 1)[0] in scope_rels:
                print('ANNOTATION CONFLICT: ' + p)
        elif not filts or not p.startswith('library name'):
            print('ANNOTATION CONFLICT: ' + p)
    visible_units = {u.name for u in units if not filts or any(f in u.name for f in filts) or
                     any(_wanted(a, filts) for a in u.annots)}
    if scope is not None:
        visible_units.intersection_update(scope)
    for owners, detail in import_problems:
        relevant = sorted(owners & visible_units)
        if relevant:
            print('IMPORT %s: %s' % (', '.join(relevant), detail))
    results, ghidra_conflicts = [], []
    for u in units:
        for a in u.annots:
            if a.kind == 'GLOBAL':
                if a.error and in_scope(a):
                    print('%s: %s' % (a.where(), a.error))
                continue
            if u.name not in objs:
                r = Result(a)
                r.detail = 'no object: %s did not compile' % u.rel
                results.append(r)
                continue
            r = check_function(a, objs[u.name], exe, symtab, name2va, ghidra_conflicts,
                               local.get(u.name), import_bindings)
            results.append(r)
    # names learned from fully matching functions name the targets of their relocations
    learned, clash, learned_at, icf_seen = {}, [], {}, {}
    INTERIOR.clear()
    for r in results:
        if r.status != 'MATCH':
            continue
        for va, sva in r.interior:
            INTERIOR.setdefault(va, sva)
        for tva, n in r.unverified:
            if va2name.get(tva, n) != n or learned.get(tva, n) != n:
                other = va2name.get(tva) or learned.get(tva)
                if icf_accepts(icf, tva, other, n):      # ICF-folded: another name of the same code, expected
                    icf_seen.setdefault(tva, {other}).add(n)
                    learned_at.setdefault(n, {}).setdefault(tva, r.a)
                    continue
                clash.append((r.a, tva, n, other))
                continue
            learned[tva] = n
            learned_at.setdefault(n, {}).setdefault(tva, r.a)
    for r in results:
        if not _wanted(r.a, filts):
            continue
        flag = '' if r.a.kind == 'FUNCTION' else ' (stub)' + (' parked' if r.a.parked else '')
        print('%-6s %08x %5d  %-45s %s%s' % (r.status, r.a.va, r.size, (r.a.mangled or r.a.name or '?')[:45], r.detail, flag))
        if verbose and r.status in ('DIFF', 'SIZE', 'RELOC'):
            print_diff(r, objs[r.a.unit.name], exe)
    # Only byte-identical functions pair relocations reliably, and ICF-folded targets are already CLASHes.
    exact = {id(r.a) for r in results if r.status in ('MATCH', 'RELOC')}
    folded = {tva for _, tva, _, _ in clash} | set(icf_seen)
    seen = set()
    for a, n, tva, g in ghidra_conflicts:
        if id(a) not in exact or tva in folded or (a.where(), n) in seen or not in_scope(a):
            continue
        seen.add((a.where(), n))
        print('NAME?  %s references %s at %08x, Ghidra calls it %s' % (a.where(), n, tva, g))
    for a, tva, n, other in clash:
        if in_scope(a):
            print('CLASH  %s: %08x is %s here but %s elsewhere' % (a.where(), tva, n, other))
    # one symbol learned at several addresses: a member offset or addend is wrong in one of the matches
    for n, at in sorted(learned_at.items()):
        if len(at) > 1 and (scope is None or any(in_scope(a) for a in at.values())):
            print('MULTI  %s learned at %s' % (n, ', '.join('%08x (%s)' % (v, a.where()) for v, a in sorted(at.items()))))
    ALL_NAMES.clear()
    ALL_NAMES.update(name2va)
    for a, tva, n, other in clash:
        ALL_NAMES.setdefault(n, tva)
    for tva, ns in icf_seen.items():
        for n in ns:
            ALL_NAMES.setdefault(n, tva)
    for tva, n in learned.items():
        ALL_NAMES.setdefault(n, tva)
    # Imports were independently proven from the PE directory, so publish them even though their relocations
    # are no longer learned from matching code. This also repairs stale name -> VA entries after reporting conflicts.
    ALL_NAMES.update(import_bindings)
    namemap = dict(va2name)
    namemap.update({k: v for k, v in learned.items() if k not in namemap})
    namemap.update({k: v for k, v in lib_names.items() if k not in namemap})
    # The target-object namemap is address -> one name; keep all aliases in ALL_NAMES, and preserve an existing
    # name here only if it independently maps back to this same slot.
    for name, slot in sorted(import_bindings.items()):
        for old_slot, old_name in list(namemap.items()):
            if old_name == name and old_slot != slot:
                del namemap[old_slot]
        current = namemap.get(slot)
        if current is None or ALL_NAMES.get(current) != slot:
            namemap[slot] = name
    return results, namemap


def save_namemap(namemap):
    os.makedirs(BUILD, exist_ok=True)
    tmp = NAMEMAP_JSON + '.%d.tmp' % os.getpid()
    json.dump({'%08x' % k: v for k, v in sorted(namemap.items())}, open(tmp, 'w'), indent=1)
    os.replace(tmp, NAMEMAP_JSON)


def show_todo(filt, units, symtab, results):
    """Functions in the units matching filt (a substring or a list of them; config/units.csv ranges), with their annotation status."""
    filts = _filters(filt)
    status = {r.a.va: r.status for r in results}
    ranges = load_unit_ranges()
    for name, (lo, hi) in sorted(ranges.items(), key=lambda x: x[1]):
        if filts and not any(f in name for f in filts):
            continue
        vas = sorted(va for va in symtab.funcs if lo <= va < hi)
        done = sum(1 for va in vas if status.get(va) == 'MATCH')
        print('%s  %08x-%08x  %d functions, %d bytes, %d matching' % (name, lo, hi, len(vas), hi - lo, done))
        for va in vas:
            end, n = symtab.funcs[va]
            print('  %-6s %08x %6d  %s' % (status.get(va, '-'), va, end - va, n))


def matched_function_coverage(results):
    """Unique target addresses covered by matching FUNCTION annotations, with their byte extents."""
    coverage = {}
    for r in results:
        if r.a.kind != 'FUNCTION' or r.status != 'MATCH':
            continue
        coverage[r.a.va] = max(coverage.get(r.a.va, 0), r.size)
    return coverage


def unit_summaries(sel, results, symtab, skip_empty=False):
    """One line per selected unit: functions (its config/units.csv range; for a unit without a range, the functions it
    annotates), MATCH, STUB, not annotated (by any unit), bytes matched.  A function counts as MATCH when a FUNCTION
    annotation of it (in any unit) says MATCH."""
    ranges = load_unit_ranges()
    by_va = {}
    for r in results:
        by_va.setdefault(r.a.va, []).append(r)
    out = []
    for u in sel:
        if u.name in ranges:
            lo, hi = ranges[u.name]
            vas = sorted(va for va in symtab.funcs if lo <= va < hi)
            where = '%08x-%08x' % (lo, hi)
        else:
            lo = hi = None
            vas = sorted({a.va for a in u.annots if a.kind != 'GLOBAL'})
            where = 'no units.csv range: the functions it annotates'
            if skip_empty and not vas:
                continue
        match = [va for va in vas if any(r.a.kind == 'FUNCTION' and r.status == 'MATCH' for r in by_va.get(va, []))]
        mset = set(match)
        selected = set(vas)
        stub = [va for va in vas if va not in mset and any(r.a.kind == 'STUB' for r in by_va.get(va, []))]
        bad = [r for r in results if r.a.kind == 'FUNCTION' and r.status != 'MATCH' and r.a.va in selected]
        none = [va for va in vas if va not in by_va]
        mb = sum(matched_function_coverage(r for r in results if r.a.va in selected).values())
        line = 'unit %s (%s): %d functions, %d MATCH, %d STUB, %d not annotated, %d bytes matched' % (
            u.name, where, len(vas), len(match), len(stub), len(none), mb)
        if hi is not None:
            line += ' of %d' % (hi - lo)
        if bad:
            bad_vas = sorted({r.a.va for r in bad})
            line += ' (+%d FUNCTION annotations not MATCH: %s)' % (
                len(bad), ' '.join('%08x' % va for va in bad_vas[:6]) + (' ...' if len(bad_vas) > 6 else ''))
        out.append(line)
    return out


def print_diff(r, o, exe):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    sec, start, end = o.extent(r.a.symbol)
    base = sec.data[start:end]
    target = exe.read(r.a.va, r.target_size if r.target_size is not None else max(r.size, len(base)))
    relocs = {off: s.name for off, s, _, _ in o.relocs_in(sec, start, end)}
    left = [(i.address, i.size, _InsnText(i)) for i in md.disasm(base, 0)]
    right = [(i.address, i.size, _InsnText(i)) for i in md.disasm(target, r.a.va)]
    for k in range(max(len(left), len(right))):
        la, ls, lt = left[k] if k < len(left) else (None, 0, '')
        ra, rs, rt = right[k] if k < len(right) else (None, 0, '')
        rel = [relocs[o_] for o_ in range(la, la + ls) if o_ in relocs] if la is not None else []
        if rel and lt.split()[0] in ('call', 'jmp') or (rel and lt.startswith('j')):
            lt = lt.split()[0] + ' ' + rel[0][:40]
        elif rel:
            lt = re.sub(r'0x0\b|0x[0-9a-f]+(?=\])', rel[0][:28], lt, count=1) if '0x' in lt else lt + ' ; ' + rel[0][:28]
        same = la is not None and ra is not None and base[la:la + ls] == target[ra - r.a.va:ra - r.a.va + rs]
        mark = ' ' if same or (rel and ls == rs and lt.split()[0] == rt.split()[0]) else '*'
        if not ALIGNED_ONLY:
            print('   %s %04x %-50s | %s' % (mark, la if la is not None else 0, lt[:50], ('%04x ' % (ra - r.a.va) if ra else '') + rt[:50]))
    print_aligned(r, left, right, relocs, exe.image_range)


ALIGNED_ONLY = False    # diff -a: print the aligned hunks instead of the index-by-index listing


_ACTIVE_IMAGE_RANGE = None


class _InsnText(str):
    """Keep the existing (address, size, text) API while retaining encoded operand offsets."""
    def __new__(cls, insn):
        text = str.__new__(cls, '%s %s' % (insn.mnemonic, insn.op_str))
        text.disp_offset, text.imm_offset = insn.disp_offset, insn.imm_offset
        return text


_NUMBER_RE = re.compile(r'(?<![\w*])(?:0x[0-9a-f]+|\d+)\b')


def _norm_reloc_fields(t, offsets):
    """Mask exactly the encoded fields covered by relocations, keeping other operand literals."""
    disp_offset, imm_offset = t.disp_offset, t.imm_offset
    text = str(t)
    memory = re.search(r'\[([^\]]*)\]', text)
    if memory and disp_offset and disp_offset in offsets:
        operand = memory.group(1)
        numbers = list(_NUMBER_RE.finditer(operand))
        if numbers:
            number = numbers[-1]    # index scales are excluded by _NUMBER_RE
            prefix = operand[:number.start()]
            # Relocatable negative addends become positive image addresses after linking.
            prefix = re.sub(r'-\s*$', '+ ', prefix)
            operand = prefix + 'A' + operand[number.end():]
        else:
            # Capstone omits an encoded zero displacement: [eax] or [eax*4].
            operand += ' + A'
        text = text[:memory.start(1)] + operand + text[memory.end(1):]
    if imm_offset and imm_offset in offsets:
        # x86 has at most one relocatable immediate here; it follows any memory operand.
        memory = re.search(r'\[[^\]]*\]', text)
        numbers = [m for m in _NUMBER_RE.finditer(text)
                   if memory is None or not memory.start() <= m.start() < memory.end()]
        if numbers:
            number = numbers[-1]
            text = text[:number.start()] + 'A' + text[number.end():]
    return text


def _active_image_range():
    global _ACTIVE_IMAGE_RANGE
    if _ACTIVE_IMAGE_RANGE is None:
        _ACTIVE_IMAGE_RANGE = Exe(EXE).image_range
    return _ACTIVE_IMAGE_RANGE


def _norm_insn(t, has_reloc, stack, image_range=None):
    if re.match(r'^(j\w+|call|loop\w*)\b', t):
        return t.split()[0]
    if has_reloc:
        if isinstance(t, _InsnText) and not isinstance(has_reloc, bool):
            t = _norm_reloc_fields(t, has_reloc)
        else:
            # Compatibility for plain-text listings (e.g. the permuter). Normal diff/count
            # listings carry Capstone metadata and do not need this operand heuristic.
            count = 1 if isinstance(has_reloc, bool) else len(has_reloc)
            stack_spans = [m.span() for m in re.finditer(r'\[(?:esp|ebp)\b[^\]]*\]', t)]
            numbers = [m for m in _NUMBER_RE.finditer(t)
                       if not any(lo <= m.start() < hi for lo, hi in stack_spans)][:count]
            for m in reversed(numbers):
                t = t[:m.start()] + 'A' + t[m.end():]
    lo, hi = image_range if image_range is not None else _active_image_range()
    t = re.sub(r'\b0x[0-9a-f]+\b', lambda m: 'A' if lo <= int(m.group(), 16) < hi else m.group(), t)
    if stack:
        t = re.sub(r'\[(esp|ebp) [+-] (?:0x[0-9a-f]+|\d+)\]', r'[\1 S]', t)
    return t


def aligned_score(left, right, relocs, stack, image_range=None):
    """Instruction mismatches after aligning the two listings (difflib), so one inserted instruction counts once
    instead of shifting every later row. Branch/call targets and relocated addresses are ignored."""
    import difflib
    image_range = image_range if image_range is not None else _active_image_range()
    A = [_norm_insn(t, [o - a for o in range(a, a + s) if o in relocs], stack, image_range) for a, s, t in left]
    B = [_norm_insn(t, False, stack, image_range) for a, s, t in right]
    sm = difflib.SequenceMatcher(None, A, B, autojunk=False)
    return sum(max(i2 - i1, j2 - j1) for tag, i1, i2, j1, j2 in sm.get_opcodes() if tag != 'equal'), sm


def aligned_counts(r, o, exe):
    """(mismatches, mismatches ignoring stack offsets, our instructions, exe instructions) for a result."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    sec, start, end = o.extent(r.a.symbol)
    base = sec.data[start:end]
    target = exe.read(r.a.va, r.target_size if r.target_size is not None else max(r.size, len(base)))
    relocs = {off: s.name for off, s, _, _ in o.relocs_in(sec, start, end)}
    left = [(i.address, i.size, _InsnText(i)) for i in md.disasm(base, 0)]
    right = [(i.address, i.size, _InsnText(i)) for i in md.disasm(target, r.a.va)]
    return aligned_score(left, right, relocs, False, exe.image_range)[0], aligned_score(left, right, relocs, True, exe.image_range)[0], len(left), len(right)


def print_aligned(r, left, right, relocs, image_range=None):
    n, sm = aligned_score(left, right, relocs, False, image_range)
    ns, _ = aligned_score(left, right, relocs, True, image_range)
    if ALIGNED_ONLY:
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == 'equal':
                continue
            print('   %s ours %04x..  exe %04x..' % (tag, left[i1][0] if i1 < len(left) else 0,
                                                    (right[j1][0] - r.a.va) if j1 < len(right) else 0))
            for k in range(i1, i2):
                print('      - %s' % left[k][2])
            for k in range(j1, j2):
                print('      + %s' % right[k][2])
    print('   ALIGNED %s: %d instruction mismatches, %d ignoring stack offsets (%d vs %d instructions)' % (
        r.a.mangled or r.a.name or r.a.symbol, n, ns, len(left), len(right)))


# ---------------------------------------------------------------- targets + objdiff

def apply_library_extents(libs, exe_path):
    """Function extents for library code = the library object's function symbols: Ghidra entries
    inside a library function are dropped (catch blocks, split tails), merged ones are split."""
    import mktarget
    st = symtab_for_mktarget()
    img = mktarget._image(exe_path)
    for u in libs.units:
        fstarts = sorted(int(k, 16) for k in u['functions'])
        for sva, n in u['text']:
            inside = [va for va in fstarts if sva <= va < sva + n]
            if not inside or inside[0] != sva:
                inside.insert(0, sva)
            # past the section: alignment padding, then whatever Ghidra merged into the extent
            last = st.containing_func(inside[-1])
            p, end = sva + n, (last[1] if last else sva + n)
            while p < end and img.read(p, 1) in (b'\xcc', b'\x90'):
                p += 1
            if p < end:
                st.split_at(p)
            # each library function is exactly its object bytes (padding is not part of it)
            for va, nxt in zip(inside, inside[1:] + [sva + n]):
                st.set_body(va, nxt)
        for sva, n in u['funclets']:
            # unwind funclets: claimed (verified bytes) but not emitted; split them off their hosts
            st.split_at(sva)
            st.split_at(sva + n)


def library_funclet_vas(libs):
    st = symtab_for_mktarget()
    out = set()
    for u in libs.units:
        for sva, n in u['funclets']:
            out.update(f[0] for f in st.funcs if sva <= f[0] < sva + n)
    return out


def write_library_units(libs, namemap):
    import mktarget
    import shutil
    out_units, claimed = [], set()
    st = symtab_for_mktarget()
    stamp = max(_inputs_mtime(), os.path.getmtime(LIBRARIES_JSON), os.path.getmtime(os.path.abspath(__file__)))
    for u in libs.units:
        vas = sorted({f[0] for sva, n in u['text'] for f in st.funcs if sva <= f[0] < sva + n})
        claimed.update(vas)
        if not vas:
            continue
        os.makedirs(os.path.dirname(u['base_obj']), exist_ok=True)
        if not os.path.exists(u['base_obj']) or os.path.getmtime(u['base_obj']) < os.path.getmtime(u['obj']):
            shutil.copyfile(u['obj'], u['base_obj'])
        nm = dict(namemap)
        nm.update({int(k, 16): v for k, v in u['data'].items()})
        nm.update({int(k, 16): v for k, v in u['functions'].items()})
        os.makedirs(os.path.dirname(u['target_obj']), exist_ok=True)
        if True:     # always rewritten: SYMVA (build/symva.json, used by tools/relink.py) needs every object's names
            base = CoffObj(u['obj'])
            secs = []
            for sva, n in u['text']:
                secno, _, _, _, _, syms = u['sections']['%08x' % sva]
                offs = {off for off, _, _ in base.sections[secno - 1].relocs}
                secs.append((sva, sva + n, [tuple(x) for x in syms], offs))
            info = mktarget.write_target_sections(u['target_obj'], secs, EXE, st, nm)
            _note_names(info['name2va'], SYMVA, u['name'])
            OBJVAS[u['name']] = [sva for sva, n in u['text']]
            for bad in mktarget.verify(u['target_obj'], EXE, info['name2va']):
                print('TARGET ROUND-TRIP FAILED: ' + bad)
        out_units.append({'name': u['name'], 'target_path': rel(u['target_obj']), 'base_path': rel(u['base_obj']),
                          'metadata': {'complete': True}})
    return out_units, claimed


def matched_body_ends(results, symtab, base_objects, exe_path):
    """Return verified end VAs for matched FUNCTION bodies followed by linker padding."""
    if results is None:
        return {}
    import mktarget
    img = mktarget._image(exe_path)
    body_ends = {}
    for r in results:
        if r.a.kind != 'FUNCTION' or r.status != 'MATCH' or not r.a.symbol:
            continue
        o = base_objects.get(r.a.unit.name)
        extent = symtab.funcs.get(r.a.va)
        if o is None or extent is None:
            continue
        _, start, end = o.extent(r.a.symbol)
        body_end = r.a.va + (end - start)
        original_end = extent[0]
        if body_end < original_end:
            mktarget.validate_body_end(r.a.va, original_end, body_end, img)
            body_ends[r.a.va] = body_end
    return body_ends


def library_claims(libs):
    """Function entries inside the code sections of library objects (what write_library_units claims)."""
    st = symtab_for_mktarget()
    return {f[0] for u in libs.units for sva, n in u['text'] for f in st.funcs if sva <= f[0] < sva + n}


def write_targets(units, symtab, namemap, libs=None, results=None, only=None):
    """Write the target objects (and objdiff entries).  `only` = unit-name substring: refresh just those units'
    target objects (build.py target <F>): no library objects, no unassigned blocks, nothing else shared is written."""
    try:
        import mktarget
    except ImportError:
        print('tools/mktarget.py not available yet: skipping target objects')
        return None
    claimed = set()
    out_units = []
    lib_units = []
    if libs and libs.units:
        apply_library_extents(libs, EXE)
        if only is None:
            lib_units, lib_claimed = write_library_units(libs, namemap)
        else:
            lib_claimed = library_claims(libs)
        claimed |= lib_claimed | library_funclet_vas(libs)
    ranges = load_unit_ranges()
    body_ends = matched_body_ends(results, symtab, LAST_OBJS, EXE)
    src_units = {u.name: u for u in units}
    mk_funcs = symtab_for_mktarget().func_addrs
    for name in sorted(set(ranges) | set(src_units)):
        u = src_units.get(name)
        vas = set(a.va for a in (u.annots if u else []) if a.kind in ('FUNCTION', 'STUB') and a.va in symtab.funcs)
        if name in ranges:
            lo, hi = ranges[name]
            vas |= set(va for va in mk_funcs if lo <= va < hi)
        vas -= claimed
        claimed.update(vas)
        if not vas or (only is not None and only not in name):
            continue
        target = os.path.join(BUILD, 'target', name + '.obj')
        os.makedirs(os.path.dirname(target), exist_ok=True)
        unit_body_ends = {va: end for va, end in body_ends.items() if va in vas}
        tmp = '%s.%d.tmp.obj' % (target[:-4], os.getpid())      # written privately, renamed into place (atomic)
        info = mktarget.write_target_obj(tmp, sorted(vas), EXE, symtab_for_mktarget(), namemap,
                                         body_ends=unit_body_ends)
        _note_names(info['name2va'], SYMVA, name)
        OBJVAS[name] = sorted(vas)
        for bad in mktarget.verify(tmp, EXE, info['name2va']):
            print('TARGET ROUND-TRIP FAILED: ' + bad)
        _replace(tmp, target)
        if only is not None:
            print('target %s: %d functions, %s' % (name, len(vas), rel(target)))
        entry = {'name': name, 'target_path': rel(target), 'metadata': {'complete': False}}
        if u and os.path.exists(u.base_obj):
            entry['base_path'] = rel(u.base_obj)
            entry['metadata']['source_path'] = rel(u.path)
        out_units.append(entry)
    out_units += lib_units
    if only is not None:
        return out_units
    blocks = {}
    for va in symtab_for_mktarget().func_addrs:
        if va not in claimed:
            blocks.setdefault(va & ~(UNASSIGNED_BLOCK - 1), []).append(va)
    for blk, vas in sorted(blocks.items()):
        name = 'unassigned/%08x' % blk
        path = os.path.join(BUILD, 'target', name + '.obj')
        os.makedirs(os.path.dirname(path), exist_ok=True)
        info = mktarget.write_target_obj(path, vas, EXE, symtab_for_mktarget(), namemap)
        _note_names(info['name2va'], SYMVA, name)
        OBJVAS[name] = sorted(vas)
        out_units.append({'name': name, 'target_path': rel(path), 'metadata': {'auto_generated': True}})
    return out_units


def _note_names(name2va, into, obj=None):
    for k, v in name2va.items():
        if k.startswith('$L') and len(k) >= 8:      # mktarget's '$L<hex va>' labels, not Ghidra's '$L4470'
            continue
        if into.get(k, v) != v:
            SYMCONFLICT.setdefault(k, set()).update((into[k], v))
        into[k] = v
        if obj:
            OBJSYM.setdefault(obj, {})[k] = v


OBJVAS = {}         # target object name (unit / lib/... / unassigned/...) -> section VAs in object order
OBJSYM = {}         # target object name -> {symbol name: VA} (names are unique per object, not across objects)
SYMCONFLICT = {}    # symbol name -> VAs, for names that mean different addresses in different objects
SYMVA = {}          # symbol name -> VA for every name used in a target object (not the '$L' labels)
_mk_symtab = None
INTERIOR = {}       # VA -> start VA of the symbol a matching function references it through (member of a struct global)


def symtab_for_mktarget():
    global _mk_symtab
    if _mk_symtab is None:
        import mktarget
        _mk_symtab = mktarget.SymTab.load(SYMBOLS_CSV if os.path.exists(SYMBOLS_CSV) else SYMBOLS_FALLBACK, mktarget._image(EXE))
        _mk_symtab.interior = INTERIOR
    return _mk_symtab


def _inputs_mtime():
    return max(os.path.getmtime(p) for p in [NAMEMAP_JSON, LIBRARIES_JSON, SYMBOLS_CSV if os.path.exists(SYMBOLS_CSV) else SYMBOLS_FALLBACK,
                                             os.path.join(TOOLS, 'mktarget.py')] if os.path.exists(p))


def rel(p):
    return os.path.relpath(p, OBJDIFF_DIR).replace('\\', '/')


PROGRESS_CATEGORIES = [
    {'id': 'engine', 'name': 'Engine'},             # src/ units and the unassigned blocks
    {'id': 'lithshared', 'name': 'lithshared'},     # src/lithshared: RezMgr, StdLith, ... compiled by the engine build
    {'id': 'wonapi', 'name': 'WONAPI'},             # prebuilt library objects (tools/libmatch.py)
    {'id': 'crt', 'name': 'VC6 CRT'},
]


def unit_category(name):
    if name.startswith('lib/'):
        return 'wonapi' if 'WONAPI' in name else 'crt'
    if name.startswith('lithshared/'):
        return 'lithshared'
    return 'engine'


def mark_complete(units_json, results):
    """A source unit is complete when every function in its target object matches (decomp.dev's 'complete'
    measures; prebuilt library units are always complete)."""
    matched_by_unit = {}
    matched_at = {}     # va -> (unit name, symbol) of the annotation that matched it
    for r in results:
        if r.a.kind == 'FUNCTION' and r.status == 'MATCH' and r.a.symbol:
            matched_by_unit.setdefault(r.a.unit.name, set()).add(r.a.va)
            matched_at[r.a.va] = (r.a.unit.name, r.a.symbol)
    for e in units_json:
        md = e.setdefault('metadata', {})
        md['progress_categories'] = [unit_category(e['name'])]
        if 'source_path' in md:
            vas = OBJVAS.get(e['name'], [])
            matched = matched_by_unit.get(e['name'], set())
            md['complete'] = bool(vas) and all(va in matched or _same_copy(e['name'], matched_at.get(va))
                                               for va in vas)


def _function_body(obj, sym):
    """A function's bytes with relocation fields zeroed, and its relocations as (offset, target name, type)."""
    sec, start, end = obj.extent(sym)
    body = bytearray(sec.data[start:end])
    rels = []
    for off, s, typ, _ in obj.relocs_in(sec, start, end):
        body[off:off + REL_FIELD.get(typ, 4)] = bytes(REL_FIELD.get(typ, 4))
        rels.append((off, s.name, typ))
    return bytes(body), rels


REL_FIELD = {0x0A: 2}    # IMAGE_REL_I386_SECTION is 2 bytes; the rest are 4


def _same_copy(unit, owner):
    """A function annotated in another unit counts for this one when this unit's object defines the same symbol
    with the same body (an inline or template copy the original placed in this unit's range)."""
    if owner is None:
        return False
    owner_unit, sym = owner
    mine, theirs = LAST_OBJS.get(unit), LAST_OBJS.get(owner_unit)
    if mine is None or theirs is None:
        return False
    copy = next((s for s in mine.symbols.values() if s.name == sym.name and s.is_function), None)
    return copy is not None and _function_body(mine, copy) == _function_body(theirs, sym)


def write_objdiff_json(units_json):
    if modcfg.NAME == 'lithtech':
        make_args, watch = ['tools/build.py', 'base'], ['src/**/*.cpp', 'src/**/*.c', 'include/**/*.h']
    else:       # project dir is build/d3dren: absolute script path, watch patterns relative to the project dir
        make_args = [os.path.join(TOOLS, 'build.py'), '--module', modcfg.NAME, 'base']
        up = os.path.relpath(ROOT, OBJDIFF_DIR).replace('\\', '/')
        watch = [up + '/src/d3dren/**/*.cpp', up + '/src/d3dren/**/*.c', up + '/include/**/*.h']
    cfg = {
        '$schema': 'https://raw.githubusercontent.com/encounter/objdiff/main/config.schema.json',
        'custom_make': 'python',
        'custom_args': make_args,
        'build_target': False,
        'build_base': True,
        'watch_patterns': watch,
        'units': units_json,
        'progress_categories': PROGRESS_CATEGORIES,
    }
    os.makedirs(OBJDIFF_DIR, exist_ok=True)
    tmp = os.path.join(OBJDIFF_DIR, 'objdiff.json.%d.tmp' % os.getpid())
    json.dump(cfg, open(tmp, 'w'), indent=2)
    _replace(tmp, os.path.join(OBJDIFF_DIR, 'objdiff.json'))


REPORT_VERSION = modcfg.REPORT_VERSION     # decomp.dev version id: the CI artifact is <REPORT_VERSION>_report
PUBLISHED_REPORT = os.path.join(ROOT, 'progress', REPORT_VERSION, 'report.json')


def publish_report():
    """build.py report: regenerate the objdiff report from the last full build into progress/<version>/report.json
    (committed; .github/workflows/progress.yml uploads it for decomp.dev, since CI can't build without VC6 and
    lithtech.exe). Each module has its own version (`--module d3dren report` publishes the renderer)."""
    if not os.path.exists(os.path.join(OBJDIFF_DIR, 'objdiff.json')):
        print('no objdiff.json: run a full `python tools/build.py` first')
        return 1
    os.makedirs(os.path.dirname(PUBLISHED_REPORT), exist_ok=True)
    rep = objdiff_report(PUBLISHED_REPORT, 'json')
    if rep is None:
        return 1
    print('wrote %s: commit and push it to update decomp.dev' % rel(PUBLISHED_REPORT))
    return 0


def objdiff_report(out=None, fmt='json-pretty'):
    out = out or os.path.join(BUILD, 'report.json')
    r = subprocess.run([OBJDIFF, 'report', 'generate', '-p', OBJDIFF_DIR, '-o', out, '-f', fmt],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print('objdiff report failed:\n' + r.stderr[-2000:])
        return None
    rep = json.load(open(out))
    m = rep.get('measures', {})
    print('objdiff: code %s/%s bytes matched (%.2f%%), functions %s/%s matched, fuzzy %.2f%%' % (
        m.get('matched_code', 0), m.get('total_code', 0), m.get('matched_code_percent', 0),
        m.get('matched_functions', 0), m.get('total_functions', 0), m.get('fuzzy_match_percent', 0)))
    return rep


# ---------------------------------------------------------------- lint

PROTO_RE = re.compile(r'^(?:extern\s+)?(?!typedef|return|else|delete|friend|#)([A-Za-z_][\w\s\*&<>,:]*?[\s\*&])'
                      r'([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*;')
STANDIN_RE = re.compile(r'^\s*//\s*STANDIN:')


def _proto_params(p):
    p = re.sub(r'=[^,]*', '', re.sub(r'/\*.*?\*/', '', p))
    if p.strip() in ('', 'void'):
        return ()
    out = []
    for a in (x.strip() for x in p.split(',')):
        # drop the parameter name: "const LTVector &vPos" -> "const LTVector&"
        a = re.sub(r'^((?:const\s+)?(?:unsigned\s+|signed\s+)?[A-Za-z_][\w:<>]*)\s*([\*&\s]*)\s*([A-Za-z_]\w*)\s*(\[\s*\w*\s*\])?$',
                   lambda m: m.group(0) if m.group(3) in ('int', 'char', 'short', 'long') else
                   m.group(1) + m.group(2).replace(' ', '') + ('*' if m.group(4) else ''), a)
        out.append(re.sub(r'\s+', ' ', a).replace(' *', '*').replace(' &', '&').replace('size_t', 'unsigned int'))
    return tuple(out)


def lint(scope_units=None):
    """Warnings for the problems integration kept fixing by hand. Returns the stand-in count.  scope_units (Unit list):
    print only the warnings that concern those units' files (or headers in include/); the stand-in count stays global."""
    decls, standins, crlf = {}, 0, []
    for root in (INC, SRC):
        for d, _, files in modcfg.walk(root):
            for f in sorted(files):
                if not f.lower().endswith(('.h', '.cpp', '.c')):
                    continue
                path = os.path.join(d, f)
                where = os.path.relpath(path, ROOT).replace(os.sep, '/')
                with open(path, 'rb') as source:
                    data = source.read()
                if b'\r\n' in data:
                    crlf.append(where)
                for i, l in enumerate(data.decode('latin1').split('\n')):
                    if STANDIN_RE.match(l):
                        standins += 1
                    if _function_pointer_name(l) is not None:
                        continue    # variable declarators are not function prototypes
                    m = PROTO_RE.match(l)
                    if m and 'static' not in m.group(1) and 'inline' not in m.group(1):
                        ret = re.sub(r'\s+', '', m.group(1).replace('extern', ''))
                        decls.setdefault(m.group(2), []).append((ret, _proto_params(m.group(3)), '%s:%d' % (where, i + 1)))
    mine = None if scope_units is None else {os.path.relpath(u.path, ROOT).replace(os.sep, '/') for u in scope_units}

    def wanted(wheres):
        return mine is None or any(w.split(':')[0] in mine or w.startswith('include/') for w in wheres)
    for where in crlf:
        if wanted([where]):
            print('WARNING CRLF: %s (write files with newline=\'\' and LF line endings)' % where)
    # a .cpp prototype that disagrees with the header's, or with another unit's (headers may overload)
    for name, ds in sorted(decls.items()):
        hdr = {(r, p) for r, p, w in ds if w.startswith('include/')}
        src = {(r, p) for r, p, w in ds if w.startswith('src/')}
        if ((src - hdr) if hdr else len(src) > 1) and (mine is None or any(w.split(':')[0] in mine for r, p, w in ds)):
            print('WARNING PROTOTYPE %s declared differently: %s' % (
                name, '; '.join('%s %s(%s) at %s' % (r, name, ', '.join(p), w) for r, p, w in ds)))
    return standins


def normalize_filter(f):
    """`0x10017150` and `10017D50`-style spellings of an address match the lower-case 8-digit form the rows print."""
    if re.fullmatch(r'0[xX][0-9a-fA-F]+', f):
        f = f[2:]
    return f.lower() if re.fullmatch(r'[0-9a-fA-F]{6,8}', f) else f


def resolve_units(units, filts):
    """The units a filter list is about, in order: a filter that is a substring of a unit name selects those units (as it
    always did); otherwise it selects the units that annotate a function whose name or hex address contains it.
    Returns (selected units, filters that matched nothing)."""
    sel, unresolved = [], []
    for f in filts:
        hit = [u for u in units if f in u.name]
        if not hit:
            hit = sorted({a.unit for u in units for a in u.annots
                          if a.kind != 'GLOBAL' and (f in (a.name or '') or f in '%08x' % a.va)}, key=lambda u: u.rel)
        if not hit:
            unresolved.append(f)
        for u in hit:
            if u not in sel:
                sel.append(u)
    return sel, unresolved


# ---------------------------------------------------------------- main

def full_build_refused():
    """Unfiltered builds on the main checkout, from a Claude Code shell, need DECOMP_LEAD=1 (agents share master's
    build/; a worktree's is private). DECOMP_AGENT=1 refuses them anywhere."""
    if os.environ.get('DECOMP_AGENT'):
        return True
    main_checkout = os.path.normcase(ROOT) in [os.path.normcase(x) for x in modcfg.SHARED_CHECKOUTS]
    return main_checkout and bool(os.environ.get('CLAUDECODE')) and not os.environ.get('DECOMP_LEAD')


COMMANDS = ('all', 'check', 'diff', 'todo', 'audit', 'parked', 'target', 'relink', 'base', 'report', 'gate', 'rules')
HELP_FLAGS = ('-h', '--help', '-?', '/?', 'help')


def usage():
    print(__doc__.rstrip())
    print("""
Filters (check/diff/todo/audit/target): any number for check/diff/todo.  A filter is a substring of a unit name (all
those units are compiled), else of an annotated function's name or hex address (only the unit(s) that annotate it
are compiled).  `diff` prints the rows and differences of the functions that match.
  -v                 check: print the instruction diff of every non-matching function
  -a                 diff: only the aligned differing hunks
  -m                 audit: audit the matching functions (self-test)
  -g, --global       old behaviour: notes (NAME?/CLASH/ANNOTATION CONFLICT/WARNING) of every unit, and a filter that names
                     no unit compiles all stale units
  check <unit> ends with one summary line per selected unit: functions in its config/units.csv range, MATCH, STUB, not
  annotated (by any unit), bytes matched.  `check %s` (the module name as the filter) checks every unit: all rows, and a
  summary line for each unit.
  --help / -h        this text.  Active module: %s (--module d3dren or DECOMP_MODULE=d3dren selects d3d.ren)""" % (modcfg.NAME, modcfg.NAME))


def main(argv):
    if any(a in HELP_FLAGS for a in argv):
        usage()
        return 0
    cmd = argv[0] if argv else 'all'
    if cmd not in COMMANDS:
        print('build.py: unknown command %r' % cmd)
        usage()
        return 2
    if cmd == 'relink':         # layout gate over the last full build's objects (tools/relink_gate.py)
        if modcfg.NAME != 'lithtech':
            print('relink is only implemented for the lithtech module (no relink spike for d3d.ren yet)')
            return 2
        import relink_gate
        return relink_gate.main()
    if cmd == 'rules':          # code-rule checker: which matches also pass the [match] rules (tools/rulecheck.py)
        import rulecheck
        sys.argv = [os.path.join(TOOLS, 'rulecheck.py')] + list(argv[1:])
        return rulecheck.main()
    if cmd == 'gate':           # byte gate: whole-image SHA1 with every bankable unit from source (tools/byte_gate.py)
        import byte_gate
        sys.argv = [os.path.join(TOOLS, 'byte_gate.py')] + list(argv[1:])
        return byte_gate.main()
    if cmd == 'report':         # progress/<version>/report.json for decomp.dev, from the last full build
        return publish_report()
    if cmd == 'base':           # objdiff passes the base object path
        want = os.path.normcase(os.path.abspath(os.path.join(OBJDIFF_DIR, argv[1])))
        if want.startswith(os.path.normcase(os.path.join(BUILD, 'base', 'lib') + os.sep)):
            return 0            # prebuilt library object: nothing to build
        for u in find_units():
            if os.path.normcase(os.path.abspath(u.base_obj)) == want:
                return 0 if compile_unit(u, force=True) else 1
        print('no unit builds ' + argv[1])
        return 1
    t0 = time.time()
    units = find_units()
    verbose = '-v' in argv
    global ALIGNED_ONLY
    ALIGNED_ONLY = '-a' in argv
    glob = '-g' in argv or '--global' in argv
    rest = [x for x in argv[1:] if x not in ('-v', '-a', '-m', '-g', '--global')]
    filts = [normalize_filter(x) for x in rest]
    whole = any(f.lower() == modcfg.NAME for f in filts)       # `check d3dren`: the whole module, rows and summaries of every unit
    if whole:
        filts = []
    filt = filts[0] if filts else None
    # a filtered check/diff/todo only compiles the units it names (several agents may run checks at once;
    # other units' existing objects are still read for names)
    if cmd != 'parked' and (cmd == 'all' or not (filt or whole)) and full_build_refused():     # parked compiles nothing
        print('build.py %s without a filter rewrites the shared outputs on master: agents always pass a unit or '
              'function filter (the lead runs full builds with DECOMP_LEAD=1)' % cmd)
        return 2
    if cmd == 'target' and not filt:
        print('build.py target needs a unit filter (the unfiltered build writes every target object)')
        return 2
    scoped = cmd in ('check', 'diff', 'todo', 'audit', 'target') and bool(filts) and not glob
    scope = None
    if scoped:
        mine, unresolved = resolve_units(units, filts)
        if cmd == 'todo':       # todo also names units that have no source (config/units.csv ranges)
            unresolved = [f for f in unresolved if not any(f in n for n in load_unit_ranges())]
        for f in unresolved:
            print('no unit or annotated function matches %r' % f)
        scope = {u.name for u in mine}
    elif cmd in ('check', 'diff', 'todo', 'audit', 'target') and filts:
        mine = [u for u in units if any(f in u.name for f in filts)]
        if cmd in ('check', 'diff', 'todo', 'audit') and not mine:
            print('no unit matches %r: checking the existing objects without compiling' % ' '.join(filts))
    else:
        mine = []
    ok = all([compile_unit(u) for u in (mine or ([] if cmd == 'parked' or scoped else units))])
    if scoped:      # units nobody asked for are not compiled, except those that have no object at all (their names verify relocations)
        for u in units:
            if u not in mine and not os.path.exists(u.base_obj):
                compile_unit(u, quiet=True)
    standins = lint(mine if scoped else None) if cmd in ('all', 'check') else 0
    exe, symtab = Exe(EXE), SymTab()
    libs = Libraries()
    if cmd == 'diff':
        run_check(units, exe, symtab, verbose=True, filt=filts, libs=libs, scope=scope)
        if COMPILE_FAILED:
            print('*** COMPILE FAILED: %s ***' % ', '.join(COMPILE_FAILED))
        return 0
    if cmd == 'parked':
        import io, contextlib, audit, parked
        with contextlib.redirect_stdout(io.StringIO()):
            results, namemap = run_check(units, exe, symtab, False, None, libs)
        summaries = {}
        audit.run(results, namemap, exe, symtab, LAST_OBJS, None, False, False, ALL_NAMES, out=summaries)
        return parked.write(results, LAST_OBJS, exe, symtab, summaries, aligned_counts, '-a' in argv)
    if cmd == 'audit':
        import io, contextlib, audit
        with contextlib.redirect_stdout(io.StringIO()):
            results, namemap = run_check(units, exe, symtab, False, None, libs)
        return audit.run(results, namemap, exe, symtab, LAST_OBJS, filt, verbose, '-m' in argv, ALL_NAMES)
    results, namemap = run_check(units, exe, symtab, verbose, filts if cmd in ('check', 'todo') else None, libs, scope=scope)
    if cmd == 'target':
        for f in filts:
            write_targets(units, symtab, namemap, libs, only=f)
        return 0 if ok else 1
    if cmd == 'todo':
        show_todo(filts, units, symtab, results)
        return 0
    n = sum(1 for r in results if r.a.kind == 'FUNCTION')
    m = sum(1 for r in results if r.a.kind == 'FUNCTION' and r.status == 'MATCH')
    coverage = matched_function_coverage(results)
    mb = sum(coverage.values())
    total = sum(e - va for va, (e, _) in symtab.funcs.items())
    aliases = max(0, m - len(coverage))
    print('%d/%d annotated functions match; %d of %d .text function bytes (%.3f%%)  [symbols: %s]' % (
        m, n, mb, total, 100.0 * mb / max(total, 1), os.path.basename(symtab.source)))
    print('%d unique matched addresses (%d excess matching aliases)' % (len(coverage), aliases))
    stubs = [r for r in results if r.a.kind == 'STUB']
    if stubs and not filt:
        print('%d STUBs (%d bytes), %d of them parked (build.py parked lists them)' % (
            len(stubs), sum(r.size for r in stubs), sum(1 for r in stubs if r.a.parked)))
    if libs.units:
        lb = libs.code_bytes()
        print('libraries: %d prebuilt objects, %d functions, %d code bytes (%.3f%%) matched by tools/libmatch.py' % (
            len(libs.units), sum(len(u['functions']) for u in libs.units), lb, 100.0 * lb / max(total, 1)))
    if standins:
        print('%d stand-ins not in %s (// STANDIN: lines; a relink cannot contain them)' % (standins, modcfg.OUT_IMAGE_NAME))
    if cmd == 'check' and (filts or whole):
        for line in unit_summaries(units if whole else mine, results, symtab, skip_empty=whole):
            print(line)
    if cmd == 'all':
        save_namemap(namemap)
        units_json = write_targets(units, symtab, namemap, libs, results=results)
        if units_json is not None:
            mark_complete(units_json, results)
            write_objdiff_json(units_json)
            json.dump({k: v for k, v in sorted(SYMVA.items()) if not (k.startswith('$L') and len(k) >= 8)},
                      open(os.path.join(BUILD, 'symva.json'), 'w'), indent=0)
            json.dump(OBJVAS, open(os.path.join(BUILD, 'objvas.json'), 'w'), indent=0)
            json.dump(OBJSYM, open(os.path.join(BUILD, 'objsym.json'), 'w'), indent=0)
            json.dump({k: sorted(v) for k, v in SYMCONFLICT.items()}, open(os.path.join(BUILD, 'symconflict.json'), 'w'), indent=0)
            objdiff_report()
    print('done in %.1fs' % (time.time() - t0))
    if COMPILE_FAILED:
        print('*** COMPILE FAILED: %s -- their functions were not checked ***' % ', '.join(COMPILE_FAILED))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
