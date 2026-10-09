r"""VC6 inline-budget model and oracle (wave 7; wave 8: hashed cost cache, ICF, STLport/header templates, margins).

  python tools/inline_budget.py <function name or hex address> [options]
      -v          print the whole site tree (default: top level + every site whose decision differs)
      --exe       compare the predicted out-of-line calls with the exe's (default on for annotated functions)
      --sweep     find the budget range for which the model reproduces the exe's out-of-line calls
      --solve     what-if: smallest budget change and/or extra free pending sites (accessors) reproducing the exe
      --no-cost   don't measure callee costs (only B(F) and the tree)
      --summary   one JSON line (SUMMARY {...}): differing calls with their sites, budget ranges, nearest dB, the
                  smallest --solve answers and a confidence (reproduces / undecided / confident / unexplained)
      --no-icf    don't resolve ICF-folded exe calls by comparing code
      -j N        parallel compiles (default 6)
      --alias VA=MANGLED   name an exe address by hand (rarely needed since wave 8: ICF is resolved by code)
      --flags "/O1 /Ob2"   what-if: compile every probe with these flags instead of the unit's // FLAGS: line
      --cost NAME=U        what-if: use cost U for a callee (mangled or undecorated name; repeatable)
  python tools/inline_budget.py --validate [names...]
      run the model on matched functions (default: NOTES.md's inline cases) and score its predictions
  python tools/inline_budget.py --variants <function> <variants.py> [--sweep]
      score vtry-style source variants in memory (the source file is never touched): B, size, model and build
      out-of-line calls vs the exe
  python tools/inline_budget.py --at <function> "<anchor text>"
      remaining top-level budget just before <anchor text> inside the function (probe inserted there; the probe is
      one more pending site for everything before it, so nested shares before the anchor shrink a little)

How it works (all measurements use the real compiler on a temporary copy of the unit, written next to it as
src/<dir>/__ib_*.ibtmp (compiled with /Tp) and deleted afterwards; the unit itself is never edited):
  B(F)    an inline probe made of global stores is put first in F; the largest probe that still inlines is B(F)
          (exact to 1u). size(F) = B/2 - 4 (the probe call) when B > 1000.
  tree    the unit is compiled /Od /Ob0 /FAs: every call of an inline candidate (a COMDAT function) is a site;
          the out-of-line copies give the nested sites. /Od keeps the front end's evaluation order.
  costs   for each callee, a wrapper `inline void R() { <ballast>; <call>; }` called from a function with a known
          budget: the largest ballast at which the callee still inlines gives cost(callee) (exact to ~1u). The
          call expression is built from the undecorated signature; costs are cached in build/inline_costs.json.
  model   the rules below, replayed over the tree; compared with our /O2 build and with the exe.

WAVE 8
  cache   build/inline_costs.json: unit -> "mangled|key" -> cost, where key hashes the callee's own /Od listing
          (its source lines and code, without line numbers and label counters), the unit's flags/defines and
          PROBE_VERSION. Editing a callee or a header it reads changes the key: stale costs are never used. Failed
          probes are cached the same way.
  probes  elaborated-type keywords are dropped from the whole signature (`X<class Y>` was C2908 in every STLport
          probe), names are undecorated with a 64K buffer (2K truncated STLport names), namespace functions
          (_STL::__stl_new) are called as free functions, virtual members by a qualified (direct) call, abstract
          classes' ctors by an explicit `p->C::C()`. STLport members and header templates (CMoArray, LTVector
          operators, BaseNew<>) instantiate in the probe like any other callee.
  header  a STUB whose body is a header template (standalone annotation, e.g. CMoArray<NodeRelation>::GenAppend):
          B = max(1000, 2 x (cost + 4)) from its own cost probe (R4: cost = size), same convention as measure_B.
  ICF     every callee's out-of-line /O2 copy is emitted (a call after an inline ballast that leaves 12u, so it
          is refused; `#pragma inline_depth(0)` would change compiler-generated members' code). Callees with
          identical code and relocation targets are one class; an exe call whose name is no callee (and not a
          function our build calls directly) is matched by code (masked relocations; named relocation targets
          must agree). Model, build and exe are compared per class.
  margin  each charged site's margin = limit - cost. |margin| <= ERR (2u, IB_ERR) is UNDECIDED (shown as
          `inline?`/`refused?` and flagged in the call table); --sweep says how far our B is from the nearest
          reproducing budget and calls it UNDECIDED within 2 x ERR.

THE MODEL (measured with toy programs in wave 7; u = 1/6 of `g[3] = 1;`)
 R1 Size is counted on the front end's tree, before any optimisation: dead stores, `if(0)` bodies, code after
    `return`, empty `{ }` all count; a declaration without initialiser costs 0.
 R2 size(F) = 12u + 1u per parameter + 5u for `this` + the statements' weights (see WEIGHTS below).
 R3 B(F) = max(1000u, 2 x size(F)), from F's own pre-inlining size (all of F).
 R4 cost(site) = size(callee) (its own calls count as call expressions only). Verified exact (toys, 7 shapes).
 R5 cost <= 40u: free (always inlined, never charged, never refused, even with a share <= 0); 41u is charged.
    __forceinline: always inlined, never charged, any size (still a pending site).
 R6 Sites are visited depth first in evaluation order: statements in order; call arguments right to left, and
    before the call that takes them (`R(P())`: P first; `(a - b).MagSqr()`: operator- first); binary operators
    left to right; assignment right side first; if: condition, then, else; for: init, increment, condition,
    body; destructors of locals at each return path and at scope end (the /Od code order).
    Compiler helpers are sites too: `vector constructor iterator' (??_H) costs 49u, `vector destructor
    iterator' (??_I) 64u, scalar deleting destructors (??_G) their own size.
 R7 A top-level site is inlined iff cost <= remaining = B - all charges so far (top level and nested).
    A refused site charges nothing and its body's sites are not visited.
 R8 When a site S is inlined with `avail` at its level, its own sites share
        limit = (avail - cost(S)) / (1 + pending)
    pending = number of inline-candidate calls after S at S's level (free, refused, ctors, dtors, dead ones
    included; non-inline, virtual and function-pointer calls not). Applies recursively; every charge is
    subtracted at every enclosing level.
 R9 Depth limit 8 (#pragma inline_depth).
 R10 A callee compiled earlier in the file costs the same (its pre-optimisation size), but VC6 can delete a call
    of a known side-effect-free function whose result is unused (or a ctor on a dead local) - the tool's cost
    probes therefore use every result.

READING A RESULT
 - Our build vs the model: they agree on 99% of sites (--validate); a disagreement is usually a site within a
   few u of its limit.
 - The exe vs the model: --sweep gives the budget range that reproduces the exe's out-of-line calls with OUR
   costs and tree; --solve adds k extra free pending sites (accessor calls) at each top-level position.
   dB < 0 means the original function was smaller (2u of budget per 1u of own code: look for code that belongs
   in an inline helper, or a macro/expression that was written more cheaply) or charged more before the
   decisive site (a bigger inline callee); extra pending sites are accessors the original called.
 - A pending site after S only divides S's children's share: top-level decisions depend on B and on the charges
   before them, never on pending counts.
 - Top-level sites near the end of a big function see the remaining budget after everything before them: one
   bigger or smaller helper early on flips them.

WEIGHTS (u, toys; for reading code, the tool measures the real thing): constant 2; parameter/local read 3;
global read 5; global store `g[3]=1` 6; local store `l=1` 4; field store via global ptr `gp->a=1` 7; call `f()` 4
(+2 per constant arg, +3 per param arg); `p->m()` 8; virtual call 9; inline call site 4; `if(x) s;` 5+cond+s,
`else` +4; `return;` 1; `while(x) x--;` 12; `for(i=0;i<x;i++) s;` 33-6; `switch` 2 cases 32; empty block 2;
binary operators 0 (operands only); local with inline ctor ~11; local with out-of-line dtor 15.
"""
import concurrent.futures, csv, json, os, re, struct, subprocess, sys, tempfile, threading

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import toolenv  # noqa: E402  (module selection --module d3dren / DECOMP_MODULE, MSYS argument repair, compiler environment)
import build  # noqa: E402
import modcfg  # noqa: E402
import coffobj  # noqa: E402
import ctypes, hashlib  # noqa: E402
import threading as _threading  # noqa: E402
_und_lock = _threading.Lock()
_und_buf = ctypes.create_string_buffer(65536)   # coffobj's 2048-byte buffer truncates STLport names


def undecorate(name, flags=0x1000):
    """MSVC undecorated name with a buffer big enough for STLport names; serialised (the cost probes run in
    threads)."""
    if not name.startswith('?'):
        return name[1:] if name.startswith('_') else name
    with _und_lock:
        n = ctypes.windll.dbghelp.UnDecorateSymbolName(name.encode('latin1'), _und_buf, len(_und_buf), flags)
        return _und_buf.value.decode('latin1') if n else name

VC6CL = modcfg.CL                       # the module's compiler wrapper (d3dren: tools\vc6cl_d3dren.bat); VC6CL (lithtech) or D3DRENCL (d3dren) in the environment overrides
COST_CACHE = os.path.join(build.BUILD, 'inline_costs.json')       # per module: build/inline_costs.json, build/d3dren/inline_costs.json
NAMEMAPS = [os.path.join(build.BUILD, 'namemap.json')] + (
    [r'E:\AVP2Source\decomp\build\namemap.json'] if modcfg.NAME == 'lithtech' else [])
PROBE_TIMEOUT = 180                     # seconds per probe compile (a hung CL.EXE is killed)
FLAGS_OVERRIDE = []                     # --flags "/O1 /Ob2": replaces the unit's optimisation flags for every probe (what-if)
FLOOR, FREE, MAXDEPTH = 1000, 40, 8
ERR = float(os.environ.get('IB_ERR', 2))          # u: decisions within +-ERR of their limit are "undecided"
BCORR = int(os.environ.get('IB_BCORR', 0))     # experiment: subtract the probe call's 2 x 4u from a measured B
INTDIV = bool(os.environ.get('IB_INTDIV'))     # experiment: truncate every share to an integer
# compiler-generated inline candidates whose cost can't be probed with a call expression (toy-measured)
FIXED_COSTS = {'??_H@YGXPAXIHP6EX0@Z@Z': 49,     # `vector constructor iterator' (arrays of classes with ctors)
               '??_I@YGXPAXIHP6EX0@Z@Z': 64}     # `vector destructor iterator' (arrays of classes with dtors)
OPT_FLAGS = re.compile(r'^/(O[12xdgitysab]\w*|Gy|Ob\d)$')
_ctr = [0]
_ctr_lock = threading.Lock()

# ----------------------------------------------------------------------------- compiling

def _tmpname(unit_dir):
    with _ctr_lock:
        _ctr[0] += 1
        n = _ctr[0]
    return '__ib_%d_%d' % (os.getpid(), n)


def effective_flags(unit, flags=None):
    """The flags a probe is compiled with: `flags` (the /Od tree), else --flags, else the unit's own // FLAGS: line."""
    if flags is not None:
        return list(flags)
    return list(FLAGS_OVERRIDE) if FLAGS_OVERRIDE else list(unit.flags)


def cost_flags(unit):
    """Flags of the callee-cost probes.  A callee's cost is its front-end size, the same under every optimisation switch, but
    the probe harness (ballast of ~6000u) needs the speed-optimised budget B = max(1000u, 2 x size): with /O1 (/Os, /Ob1) the
    budget of every function is ~53u (measured: any function of a /O1 unit), every callee then 'costs' ~9000u.  So units that
    are not /O2 or /Ox are probed with /O2 /Ob2 instead (the unit's other flags, /D and /I, are kept); B(F) itself is always
    measured with the real flags."""
    fl = effective_flags(unit)
    if any(re.match(r'^/O[2x]$', f) for f in fl) and any(re.match(r'^/O[2x]$|^/Ob2$', f) for f in fl):
        return fl
    return [f for f in fl if not OPT_FLAGS.match(f)] + ['/O2', '/Ob2']


def compile_asm(unit, text, flags=None, want_obj=False):
    """Compile `text` as if it were the unit's source, with the module's wrapper, environment and the unit's flags; return
    the /FAs listing (with want_obj, (listing, CoffObj)).  Raises CompileError (never returns a partial listing) when the compile fails or hangs.  The temporary
    copy lives in build/<module>/scratch/ib/ (its relative #include "..." lines point at the original files)."""
    tmpd = tempfile.mkdtemp(prefix='ib_', dir=toolenv.scratch_dir('ib'))
    asm = os.path.join(tmpd, 'a.asm')
    errs, out = [], ''
    try:
        fl = effective_flags(unit, flags)
        for attempt in range(3):
            if os.path.exists(asm):
                os.remove(asm)
            obj, out = toolenv.compile_tu(unit.path, text, tmpd, fl, timeout=PROBE_TIMEOUT, asm=asm,
                                          name=_tmpname('') + '.cpp')
            text_asm = open(asm, encoding='latin1', errors='replace').read() if os.path.exists(asm) else ''
            if obj and text_asm.rstrip().endswith('END'):
                return (text_asm, coffobj.CoffObj(obj)) if want_obj else text_asm
            errs = toolenv.compile_errors(out, 8) or [l for l in out.splitlines() if l.strip()][-5:]
            if errs and not any('C1001' in e or 'Broken pipe' in e or 'TIMEOUT' in e for e in errs):
                raise CompileError(errs)
        raise CompileError(errs or ['compile produced no usable listing (%s)' % ' '.join(effective_flags(unit, flags))])
    finally:
        import shutil
        shutil.rmtree(tmpd, ignore_errors=True)


class CompileError(Exception):
    pass


def parse_listing(asm):
    """{mangled: {'comdat': bool, 'calls': [mangled...], 'desc': str}} from a /FAs listing (call/jmp to symbols)."""
    funcs, cur, body = {}, None, None
    for line in asm.splitlines():
        m = re.match(r'^(\S+)\s+PROC NEAR(.*)$', line)
        if m:
            cur = m.group(1)
            funcs[cur] = {'comdat': 'COMDAT' in m.group(2), 'calls': [], 'desc': m.group(2).strip(' ;\t')}
            body = hashlib.sha1()
            continue
        if re.match(r'^\S+\s+ENDP', line):
            if cur:
                funcs[cur]['h'] = body.hexdigest()[:16]
            cur = None
            continue
        if cur:
            body.update(_norm_line(line).encode('latin1') + b'\n')
            m = re.match(r'^\s+(call|jmp)\s+(?:DWORD PTR\s+)?(\?\S+|_\w\S*)', line)
            if m and not m.group(2).startswith('__imp_'):
                funcs[cur]['calls'].append(m.group(2))
    return funcs


def _norm_line(line):
    """A listing line without what changes when unrelated code moves: source line numbers and the TU-wide label and
    temporary counters ($L123, $SG123, $T123, $E123). What is left is the function's source text and /Od code."""
    line = re.sub(r'^;\s*\d+\s*:', ';:', line)
    return re.sub(r'\$(L|SG|T|E)\d+', r'$\1', line).rstrip()


def desc_name(desc):
    return desc.split(',')[0].strip()

# ----------------------------------------------------------------------------- locating the function

def find_function(key):
    """(unit, annotation) for a name (undecorated, qualified or not) or hex address."""
    units = [u for u in build.find_units() if not os.path.basename(u.path).startswith('__ib_')]   # our temp copies
    va = None
    if re.match(r'^(0x)?[0-9a-fA-F]{6,8}$', key):
        va = int(key, 16)
    cands = []
    for u in units:
        for a in u.annots:
            if a.kind == 'GLOBAL':
                continue
            if va is not None and a.va == va or va is None and a.name and (
                    a.name == key or a.name.split('::')[-1] == key):
                cands.append((u, a))
    if not cands:
        raise SystemExit('no annotated function %s' % key)
    # exact (qualified) name first; of several annotations naming one definition (a standalone annotation of a
    # compiler-generated copy just above it), the one closest to the definition
    cands.sort(key=lambda ua: (ua[1].name != key, -ua[1].line if va is None else 0))
    return cands[0]


def body_start(text, line_no):
    """Offset just after the '{' that opens the definition starting at 1-based line_no (skips the parameter list
    and initialiser lists)."""
    lines = text.split('\n')
    off = sum(len(l) + 1 for l in lines[:line_no - 1])
    depth = 0
    i = off
    while i < len(text):
        c = text[i]
        if text.startswith('//', i):
            i = text.index('\n', i)
            continue
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        elif c == '{' and depth == 0:
            return i + 1
        i += 1
    raise ValueError('no body')


def fn_symbol(listing, unit_ann_name, mangled=None):
    if mangled and mangled in listing:
        return mangled
    hits = [k for k, v in listing.items() if desc_name(v['desc']) == unit_ann_name]
    if not hits:
        hits = [k for k, v in listing.items() if desc_name(v['desc']).split('::')[-1] == unit_ann_name.split('::')[-1]]
    return hits[0] if hits else None

# ----------------------------------------------------------------------------- probes

PROBE_DATA = 'int __ib_g[8000];\nvoid *__ib_p;\n'


def fine_body(W, arr='__ib_g', base=0):
    """Statements of exactly W u (W >= 0; W = 1,3,5,7,9,11 are rounded down)."""
    for a in (0, 1):
        for b in range(6):
            rem = W - 13 * a - 2 * b
            if rem >= 0 and rem % 6 == 0:
                K = rem // 6
                return (''.join('\t%s[%d]=%d;\n' % (arr, base + i, i + 1) for i in range(K)) +
                        ('\tif(0) %s[7999]=1;\n' % arr) * a + '\t{ }\n' * b)
    return fine_body(W - 1, arr, base)


def probe_def(name, W):
    return 'inline void %s() {\n%s}\n' % (name, fine_body(W))


def bsearch_parallel(jobs, lo, hi, run_round):
    """jobs: list of keys. run_round({key: W}) -> {key: inlined?}. Monotone: inlined for W <= W*. Returns {key: W*}
    (None when not inlined even at lo, hi when inlined at hi)."""
    st = {k: [lo, hi] for k in jobs}
    first = run_round({k: lo for k in jobs})
    res = {}
    for k in jobs:
        if not first.get(k):
            res[k] = None
            st.pop(k)
    if st:
        top = run_round({k: hi for k in st})
        for k in list(st):
            if top.get(k):
                res[k] = hi
                st.pop(k)
    while st:
        mids = {k: (a + b) // 2 for k, (a, b) in st.items()}
        r = run_round(mids)
        for k, m in mids.items():
            if r.get(k):
                st[k][0] = m
            else:
                st[k][1] = m
            if st[k][1] - st[k][0] <= 1:
                res[k] = st[k][0]
                st.pop(k)
    return res


def measure_B(unit, ann, text):
    """Exact budget B(F) with a probe as F's first statement."""
    pos = body_start(text, ann.line)
    anno_off = sum(len(l) + 1 for l in text.split('\n')[:ann.line - 1])
    # the probe definition goes before the annotation line (outside any function)

    def src(W):
        t = text[:pos] + '\n\t__ib_P0();' + text[pos:]
        return t[:anno_off] + PROBE_DATA + probe_def('__ib_P0', W) + t[anno_off:]
    sym = [None]

    def rnd(ws):
        W = ws['B']
        lst = parse_listing(compile_asm(unit, src(W)))
        if sym[0] is None:
            sym[0] = fn_symbol(lst, ann.name, ann.mangled)
        return {'B': '?__ib_P0@@YAXXZ' not in lst.get(sym[0], {'calls': []})['calls']}
    r = bsearch_parallel(['B'], 0, 30000, rnd)['B']
    return None if r is None else r + 12

# ----------------------------------------------------------------------------- call expressions for cost probes

UND_FULL = 0


def full_signature(mangled):
    return undecorate(mangled, UND_FULL)


def split_params(s):
    out, depth, cur = [], 0, ''
    for c in s:
        if c in '<(':
            depth += 1
        elif c in '>)':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


SCALARS = ('char', 'short', 'int', 'long', 'float', 'double', 'bool', 'unsigned', 'signed', 'enum ', '__int64')


def arg_expr(t):
    t = re.sub(r'\b(class|struct|union) ', '', t).strip()
    if t.endswith('&'):
        return '*(%s *)__ib_p' % t[:-1].strip()
    if t.endswith('*') or '(' in t:
        return '(%s)__ib_p' % t if '(' not in t else '0'
    if any(t.startswith(s) or (' ' + s) in (' ' + t) for s in SCALARS):
        return '(%s)0' % t
    return '*(%s *)__ib_p' % t


def call_expr(mangled):
    """C++ statement calling `mangled` with dummy arguments, or None."""
    alts = call_exprs(mangled)
    return alts[0] if alts else None


def call_exprs(mangled):
    """Alternative C++ statements calling `mangled` with dummy arguments, best first (the probes fall back to the
    next one when an expression does not compile)."""
    sig = full_signature(mangled).strip()
    if sig == mangled or sig.startswith("void __stdcall `"):
        return []
    # `class X<class Y>` in a qualified name reads as an explicit specialisation to VC6 (C2908): drop the
    # elaborated-type keywords everywhere (STLport names are full of them)
    sig = re.sub(r'\b(class|struct|union|enum) ', '', sig)
    sig = re.sub(r'\bconst ,', 'const,', sig).replace(' const >', ' const>')
    m = re.match(r'^(?:(public|protected|private): )?(static |virtual )?(.*?)(__thiscall|__cdecl|__stdcall|__fastcall) '
                 r'(.+?)\((.*)\)(const)?$', sig)
    if not m:
        return []
    access, static, ret, cc, qname, params, const = m.groups()
    params = [] if params.strip() in ('void', '') else split_params(params)
    if '...' in params:
        params = [p for p in params if p != '...']
    args = ', '.join(arg_expr(p) for p in params)
    # split qualified name at the last :: outside <>
    depth, cut = 0, -1
    for i, c in enumerate(qname):
        if c == '<':
            depth += 1
        elif c == '>':
            depth -= 1
        elif c == ':' and depth == 0 and qname[i:i + 2] == '::':
            cut = i
    if cut < 0 or not access:          # a free function, maybe in a namespace (_STL::__stl_new)
        call = '%s(%s)' % (qname, args)
    else:
        cls, name = qname[:cut], qname[cut + 2:]
        base = re.sub(r'<.*>$', '', cls).split('::')[-1]
        if name == cls.split('::')[-1] or name == base or re.sub(r'<.*>$', '', name) == base:      # constructor
            # the object escapes: VC6 deletes a known side-effect-free ctor call on a dead local. An abstract
            # class (ILTServer) can't be a local: call the constructor explicitly (an MS extension VC6 accepts)
            return [('{ %s __ib_o(%s); __ib_p = &__ib_o; }' % (cls, args)) if args else
                    '{ %s __ib_o; __ib_p = &__ib_o; }' % cls,
                    '((%s *)__ib_p)->%s::%s(%s);' % (cls, cls, base, args)]
        if name.startswith('~'):
            if '<' in cls:    # a qualified template dtor name trips VC6 (C2908): call it unqualified
                return ['((%s *)__ib_p)->~%s();' % (cls, base)]
            return ['((%s *)__ib_p)->%s::%s();' % (cls, cls, name)]
        if name == "`scalar deleting destructor'":
            return ['delete (%s *)__ib_p;' % cls]
        if (static or '').strip() == 'static':
            call = '%s::%s(%s)' % (cls, name, args)
        elif (static or '').strip() == 'virtual':
            # a qualified call is direct (an inline candidate); an unqualified one is a virtual call
            call = '((%s *)__ib_p)->%s::%s(%s)' % (cls, cls, name, args)
        else:
            call = '((%s *)__ib_p)->%s(%s)' % (cls, name, args)
    # use the result: VC6 deletes a call of a side-effect-free function whose result is unused
    ret = re.sub(r'\b(class|struct|union) ', '', (ret or '').strip())
    if ret in ('', 'void'):
        return [call + ';']
    if ret.endswith('&'):
        return ['__ib_p = (void *)&%s;' % call]
    if ret.endswith('*'):
        return ['__ib_p = (void *)%s;' % call]
    if any(ret.startswith(s) or (' ' + s) in (' ' + ret) for s in SCALARS):
        return ['__ib_g[7998] = (int)%s;' % call]
    return [call + ';']


def _load_all():
    try:
        d = json.load(open(COST_CACHE))
        return d if all(isinstance(v, dict) and 'cost' not in v for v in d.values()) else {}
    except (OSError, ValueError):
        return {}


PROBE_VERSION = 'w8.2'      # bump when call_expr or the probe layout changes: every cached entry is re-measured


def cost_key(unit, mangled, body_hash):
    """Cache key: the callee's name plus a hash of its own /Od listing (its source lines and code, without line
    numbers or label counters), the unit's flags/defines and the probe version. Editing the callee (or a header
    it reads) changes the key, so stale costs are never used."""
    h = hashlib.sha1(('%s|%s|%s' % (PROBE_VERSION, ' '.join(unit.flags), body_hash)).encode('latin1'))
    return '%s|%s' % (mangled, h.hexdigest()[:12])


def load_costs(unit):
    """Costs are cached per unit: one mangled name can have different bodies in different units (headers that
    mirror each other, e.g. CMoArray/BaseNew in load_pcx.h vs ltdynarray.h). Keys are cost_key()s; wave 7's
    name-only entries are ignored."""
    return {k: v for k, v in _load_all().get(unit.name, {}).items() if '|' in k}


def save_costs(unit, c):
    """Merge into the cache file (several runs may share it) and replace it atomically. Failed probes are cached
    too (their key changes with the callee's source and PROBE_VERSION), except transient whole-batch failures."""
    os.makedirs(os.path.dirname(COST_CACHE), exist_ok=True)
    allc = _load_all()
    cur = allc.setdefault(unit.name, {})
    for k in [k for k in cur if '|' not in k]:       # wave 7 name-only entries: stale by construction
        del cur[k]
    for k, v in c.items():
        if '|' in k and (v.get('cost') is not None or v.get('why') not in (None, 'compile failed')):
            cur[k] = v
    tmp = COST_CACHE + '.%d.tmp' % os.getpid()
    json.dump(allc, open(tmp, 'w'), indent=1, sort_keys=True)
    os.replace(tmp, COST_CACHE)


BW_STORES = 500                   # wrapper ballast: B(W) = 2*(12 + 6*500 + 4) = 6032
BW = 2 * (12 + 6 * BW_STORES + 4)


def measure_costs(unit, text, callees, hashes=None, log=print, jobs=6):
    """{mangled: cost} for callees, via wrappers appended to the unit. hashes: {mangled: hash of the callee's /Od
    listing} (parse_listing's 'h'); the cache is keyed by cost_key(name, that hash, unit flags)."""
    hashes = hashes or {}
    keyof = {c: cost_key(unit, c, hashes.get(c, '?')) for c in callees}
    stored = load_costs(unit)
    cache = {c: stored[keyof[c]] for c in callees if keyof[c] in stored}
    cache.update({k: {'cost': v, 'why': 'fixed (toy-measured)'} for k, v in FIXED_COSTS.items()})
    todo = [c for c in callees if c not in cache]
    exprs, alts = {}, {}
    for c in todo:
        alts[c] = call_exprs(c)
        if alts[c]:
            exprs[c] = alts[c].pop(0)
        else:
            cache[c] = {'cost': None, 'why': 'no call expression'}
    if not exprs:
        save_costs(unit, {keyof[c]: cache[c] for c in callees if c in cache and c not in FIXED_COSTS and c in hashes})
        return {c: cache[c] for c in callees}
    # drop expressions that don't compile (one compile per round of removals)
    tail ='\n#pragma inline_depth()\n' + PROBE_DATA
    lift = ''.join('\t__ib_g[%d]=%d;\n' % (4000 + i, i + 1) for i in range(BW_STORES))
    # private/protected callees: open the classes up (only in this measurement copy)
    head = ['#define private public\n#define protected public\n']
    try:
        compile_asm(unit, head[0] + text, cost_flags(unit))
    except CompileError:
        head[0] = ''

    def text_for(Ws, Wp):
        t = head[0] + text + tail
        for i, c in enumerate(exprs):
            if c in Ws:
                t += 'inline void __ib_R%d() {\n%s\t%s\n}\nvoid __ib_W%d() {\n%s\t__ib_R%d();\n}\n' % (
                    i, fine_body(Ws[c]), exprs[c], i, lift, i)
            if c in Wp:
                t += probe_def('__ib_Q%d' % i, Wp[c])
                t += 'void __ib_S%d() {\n\t__ib_Q%d();\n%s\t%s\n}\n' % (i, i, lift, exprs[c])
        return t
    for _ in range(200):
        try:
            compile_asm(unit, text_for({c: 0 for c in exprs}, {c: 0 for c in exprs}), cost_flags(unit))
            break
        except CompileError as e:
            lines = text_for({c: 0 for c in exprs}, {c: 0 for c in exprs}).split('\n')
            bad = set()
            for err in e.args[0]:
                m = re.search(r'\((\d+)\)\s*:', err)
                if m:
                    ln = int(m.group(1))
                    for j in range(ln - 1, -1, -1):
                        mm = re.match(r'^(?:inline )?void __ib_[RS](\d+)\(\)', lines[j])
                        if mm:
                            bad.add(list(exprs)[int(mm.group(1))])
                            break
            if not bad:
                raise CompileError(['the cost probes do not compile (not one callee call expression: nothing was measured)'] +
                                   list(e.args[0][:6]))
            for c in bad:
                if alts.get(c):              # try the next form (keeps the order of exprs for the indices)
                    exprs[c] = alts[c].pop(0)
                    continue
                cache[c] = {'cost': None, 'why': 'call expression does not compile: ' + exprs[c]}
                del exprs[c]
    if exprs:
        idx = {c: i for i, c in enumerate(exprs)}

        def rnd_cost(ws):
            lst = parse_listing(compile_asm(unit, text_for(ws, {}), cost_flags(unit)))
            out = {}
            for c in ws:
                w = lst.get('?__ib_W%d@@YAXXZ' % idx[c])
                # `#define protected public` changes the access code in the mangled name: compare without it
                called = {undecorate(x, 0x80) for x in w['calls']} if w is not None else set()
                out[c] = (w is not None and undecorate(c, 0x80) not in called and
                          '?__ib_R%d@@YAXXZ' % idx[c] not in w['calls'])
            return out

        def rnd_expr(ws):
            lst = parse_listing(compile_asm(unit, text_for({}, ws), cost_flags(unit)))
            out = {}
            for c in ws:
                s = lst.get('?__ib_S%d@@YAXXZ' % idx[c])
                out[c] = s is not None and '?__ib_Q%d@@YAXXZ' % idx[c] not in s['calls']
            return out
        keys = list(exprs)
        log('measuring %d callee costs (%d cached) ...' % (len(keys), len(callees) - len(todo)))
        # split into batches compiled in parallel
        nb = max(1, min(jobs, (len(keys) + 7) // 8))
        batches = [keys[i::nb] for i in range(nb)]
        with concurrent.futures.ThreadPoolExecutor(int(os.environ.get('IB_THREADS', nb * 2))) as ex:
            fc = [ex.submit(bsearch_parallel, b, 0, BW - 42, rnd_cost) for b in batches]
            fe = [ex.submit(bsearch_parallel, b, 0, 20000, rnd_expr) for b in batches]
            wc, we = {}, {}
            for f in fc:
                wc.update(f.result())
            for f in fe:
                we.update(f.result())
        for c in keys:
            if wc.get(c) is None or we.get(c) is None:
                cache[c] = {'cost': None, 'why': 'never inlined'}
                continue
            sizeS = (we[c] + 12) / 2.0                    # B(S) = 2 * size(S)
            wexpr = sizeS - 12 - 4 - 6 * BW_STORES        # the call expression's own weight in R / S
            costR = 12 + wc[c] + wexpr
            cost = BW - costR
            cache[c] = {'cost': int(round(cost)), 'expr': exprs[c], 'wexpr': wexpr}
            if wc[c] >= BW - 42 or cost <= FREE:       # inlined with a limit <= 30u: free (or __forceinline)
                cache[c]['cost'] = min(int(round(cost)), FREE)
                cache[c]['free'] = True
    save_costs(unit, {keyof[c]: cache[c] for c in callees if c in cache and c not in FIXED_COSTS and c in hashes})
    return {c: cache[c] for c in callees}

# ----------------------------------------------------------------------------- the model

class Site:
    def __init__(self, callee, depth):
        self.callee, self.depth = callee, depth
        self.children = []
        self.cost = None
        self.limit = None
        self.decision = None       # 'inline' / 'free' / 'force' / 'refused' / 'depth' / 'unknown'
        self.pending = 0
        self.margin = None         # limit - cost for a charged (> 40u) site: >= 0 inlined, < 0 refused


def build_tree(lst, root, depth=0, stack=()):
    sites = []
    if depth >= 12:
        return sites
    for c in lst[root]['calls']:
        if c in lst and lst[c]['comdat'] and (not c.startswith('??_') or c in FIXED_COSTS or
                                                    c.startswith('??_G')):   # helpers: ??_H, ??_G
            s = Site(c, depth + 1)
            if c not in stack:
                s.children = build_tree(lst, c, depth + 1, stack + (c,))
            sites.append(s)
    return sites


def simulate(sites, limit, costs, depth=1):
    used = 0
    for i, s in enumerate(sites):
        avail = limit - used
        s.pending = len(sites) - i - 1
        s.limit = avail
        info = costs.get(s.callee) or {}
        cost = info.get('cost')
        s.cost = cost
        s.margin = (avail - cost) if cost is not None and cost > FREE else None
        if depth > MAXDEPTH:
            s.decision = 'depth'
            continue
        if cost is None:
            s.decision, charge = 'unknown', 0
        elif cost <= FREE:
            s.decision, charge = 'free', 0
        elif cost <= avail:
            s.decision, charge = 'inline', cost
        else:
            s.decision = 'refused'
            continue
        used += charge
        child = (avail - charge) / (1.0 + s.pending)
        if INTDIV:
            child = int(child)
        used += simulate(s.children, child, costs, depth + 1)
    return used


def refused_multiset(sites, out=None):
    out = {} if out is None else out
    for s in sites:
        if s.decision in ('refused', 'depth'):
            out[s.callee] = out.get(s.callee, 0) + 1
        elif s.decision != 'unknown':
            refused_multiset(s.children, out)
    return out


def undecided(s):
    """A decision within the model's error (ERR u: B and costs are exact to ~1u, shares are fractions): a charged
    site within ERR of its limit, or a site whose cost is within ERR of the 40u free threshold and whose limit
    would refuse it if it were charged."""
    if s.cost is not None and s.limit is not None and abs(s.cost - (FREE + 0.5)) <= ERR and s.cost > s.limit:
        return s.decision in ('free', 'refused')
    return s.margin is not None and s.decision in ('inline', 'refused') and -ERR <= s.margin <= ERR


def walk(sites):
    for s in sites:
        yield s
        if s.decision not in ('refused', 'depth'):
            for x in walk(s.children):
                yield x

# ----------------------------------------------------------------------------- exe side

_EXE_NAMES = []


def exe_names():
    """{va: name} from build/namemap.json (the last full check's names)."""
    if not _EXE_NAMES:
        names = {}
        for p in NAMEMAPS:
            if os.path.exists(p):
                for k, v in json.load(open(p)).items():
                    names.setdefault(int(k, 16), v)
        _EXE_NAMES.append(names)
    return _EXE_NAMES[0]


def exe_calls(va, symtab):
    import pefile, capstone
    exe = build.Exe(build.EXE)
    end = symtab.funcs[va][0]
    data = exe.pe.get_memory_mapped_image()[va - exe.base:end - exe.base]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    names = exe_names()
    out = []
    for ins in md.disasm(data, va):
        if ins.mnemonic in ('call', 'jmp') and ins.op_str.startswith('0x'):
            t = int(ins.op_str, 16)
            if ins.mnemonic == 'jmp' and va <= t < end:
                continue
            out.append((t, ALIASES.get(t) or names.get(t) or symtab.names.get(t) or '%08x' % t))
    return out


COST_OVERRIDES = {}
ALIASES = {}     # exe address -> mangled name, for out-of-line copies no matched function has named yet (--alias)


def _norm(n):
    n = undecorate(n) if n.startswith('?') else n
    n = re.sub(r'<[^<>]*>', '', re.sub(r'<[^<>]*>', '', n))
    return n.replace(' ', '')


def map_exe(ex_calls, callees):
    """Exe call names -> our callee keys (mangled when known; Ghidra names matched by unqualified-template name)."""
    by_norm = {}
    for c in callees:
        by_norm.setdefault(_norm(c), []).append(c)
    out = {}
    for n, k in ex_calls.items():
        if n in callees:
            key = n
        else:
            hits = by_norm.get(_norm(n), [])
            if len(hits) != 1:
                continue
            key = hits[0]
        out[key] = out.get(key, 0) + k
    return out


# ----------------------------------------------------------------------------- ICF (identical code folding)
# The linker folds byte-identical functions (/OPT:ICF): the exe then calls one surviving address whose name (from a
# matched function) can be a different function than our callee (??0RayTri for ContainerPhysics' ctor,
# BaseNew<uint32> for another BaseNew<>). The tool compiles every callee's out-of-line /O2 copy, groups callees with
# identical code into classes, resolves unnamed or foreign exe targets by comparing bytes, and compares the model
# with the exe per class.

def _fn_image(o, sym):
    """(code with relocation fields zeroed, ((offset, target name), ...)) of a function in a CoffObj."""
    sec, a, b = o.extent(sym)
    data = bytearray(sec.data[a:b])
    rel = []
    for off, rs, typ, add in o.relocs_in(sec, a, b):
        for k in range(coffobj.REL_SIZES.get(typ, 4)):
            if off + k < len(data):
                data[off + k] = 0
        rel.append((off, typ, '$sec' if rs.is_section_symbol or rs.name.startswith('$') else rs.name))
    return bytes(data), tuple(rel)


def _prune(unit, exprs, text_for, tag, log, alts=None):
    """Drop call expressions that don't compile (their function is named __ib_<tag><i>), or replace them by their
    next alternative."""
    exprs, alts = dict(exprs), alts or {}
    for _ in range(200):
        t = text_for(exprs)
        try:
            return exprs, compile_asm(unit, t, want_obj=True)
        except CompileError as e:
            lines, bad = t.split('\n'), set()
            keys = list(exprs)
            for err in e.args[0]:
                m = re.search(r'\((\d+)\)\s*:', err)
                if m:
                    for j in range(int(m.group(1)) - 1, -1, -1):
                        mm = re.match(r'^void __ib_%s(\d+)\(\)' % tag, lines[j])
                        if mm:
                            bad.add(keys[int(mm.group(1))])
                            break
            if not bad:
                log('ICF: emitter does not compile: %s' % e.args[0][:2])
                return {}, None
            for c in bad:
                if alts.get(c):
                    exprs[c] = alts[c].pop(0)
                else:
                    del exprs[c]
    return {}, None


def emit_copies(unit, text, callees, log=print):
    """{mangled: (code, relocs)} of each callee's out-of-line /O2 copy: the unit is compiled at its own flags with
    one extra function per callee that exhausts its budget first, so the callee is refused and emitted with its
    normal code."""
    alts = {c: call_exprs(c) for c in callees}
    exprs = {c: a.pop(0) for c, a in alts.items() if a}
    res = None
    for head in ('#define private public\n#define protected public\n', ''):
        def text_for(ex, head=head):
            # each callee is called after an inline ballast that leaves 12u of the 1000u budget, so every
            # charged (> 40u) callee is refused and emitted. (`#pragma inline_depth(0)` would also emit them, but
            # it changes the code of compiler-generated members like an implicit operator=.)
            t = head + text + '\n#pragma inline_depth()\n' + PROBE_DATA + probe_def('__ib_X', FLOOR - 24)
            return t + ''.join('void __ib_E%d() {\n\t__ib_X();\n\t%s\n}\n' % (i, ex[c]) for i, c in enumerate(ex))
        ok, res = _prune(unit, exprs, text_for, 'E', log, {c: list(a) for c, a in alts.items()})
        if res:
            break
    if not res:
        return {}
    _, obj = res
    by_und = {}
    for sym in obj.functions():
        by_und.setdefault(undecorate(sym.name, 0x80), sym)
    out = {}
    for c in callees:
        sym = by_und.get(undecorate(c, 0x80))
        if sym is not None:
            out[c] = _fn_image(obj, sym)
    return out


def icf_classes(copies, prefer=()):
    """{callee: representative}: callees with identical code and relocation targets are one class. The
    representative is the first of `prefer` (callees called out of line) in the class, else the first by name."""
    rank = {c: i for i, c in enumerate(prefer)}
    groups = {}
    for c in sorted(copies):
        groups.setdefault(copies[c], []).append(c)
    out = {}
    for g in groups.values():
        r = min(g, key=lambda c: (rank.get(c, len(rank)), c))
        out.update({c: r for c in g})
    return out


def _exe_matches(exe, symtab, va, img):
    code, rel = img
    try:
        got = bytearray(exe.read(va, len(code)))
    except Exception:
        return False
    for off, typ, _ in rel:
        for k in range(coffobj.REL_SIZES.get(typ, 4)):
            if off + k < len(got):
                got[off + k] = 0
    # a REL_SECTION (2-byte) field is zeroed in ours only; compare where ours is not a zeroed reloc field
    if bytes(got) != code:
        return False
    ext = symtab.funcs.get(va)
    if ext:        # the exe function must not be longer (padding aside): a short body can be a prefix of another
        tail = exe.read(va + len(code), max(0, ext[0] - va - len(code)))
        if any(b not in (0xcc, 0x90) for b in tail):
            return False
    # the linker folds only functions whose relocations name the same targets: where the exe's target has a name,
    # it must be ours (operator new's own `jmp` is not __stl_new's)
    names = exe_names()
    raw = exe.read(va, len(code))
    for off, typ, n in rel:
        if n == '$sec' or typ not in (coffobj.REL_DIR32, coffobj.REL_REL32) or off + 4 > len(raw):
            continue
        field = struct.unpack_from('<i' if typ == coffobj.REL_REL32 else '<I', raw, off)[0]
        tva = (va + off + 4 + field) & 0xffffffff if typ == coffobj.REL_REL32 else field
        tn = names.get(tva) or symtab.names.get(tva)
        if tn and tn != n and _norm(tn) != _norm(n):
            return False
    return True


def map_exe_icf(ex_list, callees, copies, canon, symtab, direct=()):
    """[(va, name)] of the exe's calls -> ({class representative: count}, notes). Names first (map_exe's rules);
    a target whose name is no callee is compared byte for byte with every callee's /O2 copy, unless our build calls
    that name directly too (`direct`: a non-inline function both sides call)."""
    exe = build.Exe(build.EXE)
    exact = set(direct)
    direct = exact | {_norm(x) for x in direct}
    by_norm = {}
    for c in callees:
        by_norm.setdefault(_norm(c), []).append(c)
    out, notes, cache = {}, [], {}
    for va, n in ex_list:
        if va not in cache:
            key = n if n in callees else None
            if key is None and n not in exact:      # a function our build calls directly is no inline candidate
                hits = by_norm.get(_norm(n), [])
                key = hits[0] if len(hits) == 1 else None
            if key is None and n not in direct and _norm(n) not in direct:
                hits = [c for c in sorted(copies) if _exe_matches(exe, symtab, va, copies[c])]
                if hits:
                    key = hits[0]
                    notes.append('%08x %s = %s (identical code%s)' % (
                        va, short(n)[:50], short(key)[:60], '' if len({canon.get(h, h) for h in hits}) == 1
                        else '; ambiguous: ' + ', '.join(short(h)[:40] for h in hits[1:4])))
            cache[va] = key
        key = cache[va]
        if key is not None:
            k = canon.get(key, key)
            out[k] = out.get(k, 0) + 1
    return out, notes


def fold(d, canon):
    out = {}
    for k, v in d.items():
        k = canon.get(k, k)
        out[k] = out.get(k, 0) + v
    return out


def count(lst):
    d = {}
    for x in lst:
        d[x] = d.get(x, 0) + 1
    return d

# ----------------------------------------------------------------------------- driver

def tree_flags(unit):
    return [f for f in effective_flags(unit) if not OPT_FLAGS.match(f)] + ['/Od', '/Ob0']


def short(m):
    u = undecorate(m)
    return u.replace('_CVector<float>', 'LTVector')


def apply_variant(text, ann, repls):
    """Apply (old, new) replacements in memory; returns (text, annotation with its new line number)."""
    import copy
    for old, new in repls:
        if text.count(old) != 1:
            raise SystemExit('variant: %r occurs %d times' % (old[:60], text.count(old)))
        text = text.replace(old, new)
    a = copy.copy(ann)
    for i, l in enumerate(text.split('\n')):
        m = build.ANNOT.match(l)
        if m and int(m.group(2), 16) == ann.va:
            a.line = i + 1
            break
    return text, a


def header_body(unit, ann, text):
    """True when the annotation names a function whose body is not written after it: a template member defined in
    a header (CMoArray<NodeRelation>::GenAppend), annotated standalone among other annotations."""
    if not ann.mangled:
        return False
    if not ann.name:
        try:
            pos = body_start(text, ann.line)
        except ValueError:
            return True
        lines = text[:pos].split('\n')[ann.line:]
        return any(build.ANNOT.match(l) for l in lines)

    def last(n):
        n = re.sub(r'<[^<>]*>', '', re.sub(r'<[^<>]*>', '', n)).replace(' ', '')
        return n.split('::')[-1]
    return last(undecorate(ann.mangled)) != last(ann.name)


def analyse(key, verbose=False, use_exe=True, sweep=False, costs_on=True, jobs=6, quiet=False, variant=None,
            solve=False, icf=True, summary=False):
    log = (lambda *a: None) if quiet else print
    unit, ann = find_function(key)
    text = open(unit.path, encoding='latin1').read()
    hdr = header_body(unit, ann, text)
    if variant:
        if hdr:
            raise SystemExit('%s: the body is in a header: variants of the unit text cannot change it' % ann.mangled)
        text, ann = apply_variant(text, ann, variant)
    if hdr:
        import copy
        ann = copy.copy(ann)
        ann.name = undecorate(ann.mangled)
    log('%s  (%s:%d, flags: %s%s)%s' % (ann.name, unit.rel, ann.line, ' '.join(effective_flags(unit)),
                                        ' [--flags override]' if FLAGS_OVERRIDE else '',
                                        '  [body in a header: B from its own cost]' if hdr else ''))
    log('compiler: %s%s' % (VC6CL, '' if modcfg.NAME == 'lithtech' else '  (module %s, DX8INC=%s)' % (modcfg.NAME, modcfg.CL_ENV.get('DX8INC'))))
    if not any(re.match(r'^/O[2x]$|^/Ob2$', f) for f in effective_flags(unit)):
        log('NOTE: these flags give inline expansion /Ob1 (only functions declared inline and class-body functions; /O1 implies /Ob1, '
            '/O2 and /Ox imply /Ob2).  B(F) is measured with the real flags of the unit (a /O1 unit has a budget of ~53u for every function); callee costs '
            '(front-end sizes) are measured with /O2 /Ob2, the only setting the probe harness is calibrated for; the 1000u floor and B = 2 x size '
            'of the model were measured for /O2 only.')
    # our /O2 build and the /Od /Ob0 tree, in parallel with B
    with concurrent.futures.ThreadPoolExecutor(3) as ex:
        f_o2 = ex.submit(compile_asm, unit, text)
        f_tree = ex.submit(compile_asm, unit, text, tree_flags(unit))
        f_B = None if hdr else ex.submit(measure_B, unit, ann, text)
        o2, tl = parse_listing(f_o2.result()), parse_listing(f_tree.result())
        B = None if hdr else f_B.result()
        if B and BCORR and B > FLOOR:      # experiment: B without the probe's own call
            B = max(FLOOR, B - BCORR)
    root = fn_symbol(tl, ann.name, ann.mangled)
    root_o2 = fn_symbol(o2, ann.name, ann.mangled)
    if root is None or root_o2 is None:
        raise SystemExit('function not found in the listings')
    if hdr:
        # R4: a site costs the callee's size, so the function's own cost probe gives size(F); B as measure_B
        # reports it (with its probe call's 4u: 2 x (size + 4))
        own = measure_costs(unit, text, [root], {root: tl[root].get('h')}, log=log, jobs=jobs)[root]
        if own.get('cost') is None:
            raise SystemExit('size of %s not measurable: %s' % (ann.name, own.get('why')))
        B = max(FLOOR, 2 * (own['cost'] + 4)) if not own.get('free') else FLOOR
    sites = build_tree(tl, root)
    callees = sorted({s.callee for s in walk_all(sites)})
    size = None if B is None or B <= FLOOR else B / 2.0 - 4
    log('B(F) = %s u  -> size(F) = %s u%s' % (B, size if size is not None else '<= %d' % (FLOOR // 2 - 4),
                                                '' if size is not None else ' (budget at the 1000u floor)'))
    costs = measure_costs(unit, text, callees, {c: tl[c].get('h') for c in callees}, log=log,
                          jobs=jobs) if costs_on else {}
    for k, v in COST_OVERRIDES.items():           # --cost: what-if costs (mangled or undecorated name)
        for c in callees:
            if c == k or undecorate(c) == k:
                costs[c] = {'cost': v}
                log('cost override: %s = %d' % (short(c), v))
    simulate(sites, B if B else FLOOR, costs)
    pred = refused_multiset(sites)
    ours = {k: v for k, v in count(o2[root_o2]['calls']).items() if k in tl and tl[k]['comdat'] or k in pred}
    res = {'name': ann.name, 'va': ann.va, 'B': B, 'size': size, 'pred': pred, 'ours': ours, 'sites': sites,
           'costs': costs, 'canon': {}, 'icf': []}
    if use_exe:
        st = build.SymTab()
        if ann.va in st.funcs:
            ex_list = exe_calls(ann.va, st)
            copies = emit_copies(unit, text, callees, log) if icf else {}
            canon = icf_classes(copies, list(ours) + [c for c in pred if c not in ours] +
                                [s.callee for s in walk(sites) if s.decision in ('inline', 'refused')])
            res['canon'] = {k: v for k, v in canon.items() if k != v}
            direct = [c for c in o2[root_o2]['calls'] if not (c in tl and tl[c]['comdat'])]
            res['exe'], res['icf'] = map_exe_icf(ex_list, callees, copies, canon, st, direct)
    if not quiet:
        report(res, verbose)
    if sweep and 'exe' in res:
        do_sweep(res, sites, costs, B)
    if solve and 'exe' in res:
        do_solve(res, sites, costs, B)
    if summary and 'exe' in res:
        print('SUMMARY ' + json.dumps(summarize(res)), flush=True)
    return res


def _model(sites, b, costs, canon):
    simulate(sites, b, costs)
    return fold(refused_multiset(sites), canon)


def _miss(p, want):
    return sum(abs(p.get(k, 0) - want.get(k, 0)) for k in set(p) | set(want))


def do_solve(res, sites, costs, B, max_extra=6, db_range=300, quiet=False):
    """What-if search: the budget changed by dB (a different own size or extra charges before the first site) and/or
    k extra free pending sites (accessor calls) inserted before top-level site j. Lists the smallest changes that make
    the model reproduce the exe's out-of-line calls. Returns [(k, dB lo, dB hi, [j...])] (smallest first)."""
    want = res['exe']
    canon = res.get('canon', {})
    B = B or FLOOR
    sols = []
    fake = 'FREE@__ib_pending'
    costs = dict(costs)
    costs[fake] = {'cost': 0}
    n = len(sites)
    for k in range(0, max_extra + 1):
        for j in (range(n + 1) if k else [n]):
            trial = sites[:j] + [Site(fake, 1) for _ in range(k)] + sites[j:]
            for db in range(-db_range, db_range + 1, 2):
                p = _model(trial, max(FLOOR, B + db), costs, canon)
                p.pop(fake, None)
                if _miss(p, want) == 0:
                    sols.append((k, abs(db), db, j))
        if len(sols) >= 1 and k >= 3 + min(s[0] for s in sols):
            break
    simulate(sites, B, costs)
    if not sols:
        if not quiet:
            print('solve: no combination of <= %d extra pending sites and |dB| <= %d reproduces the exe' % (
                max_extra, db_range))
        return []
    sols.sort()
    shown = {}
    for k, adb, db, j in sols:
        key = (k, j)
        if key in shown:
            shown[key][1] = min(shown[key][1], db)
            shown[key][2] = max(shown[key][2], db)
            continue
        shown[key] = [k, db, db, j]
    groups = {}
    for k, lo, hi, j in shown.values():
        groups.setdefault((k, lo, hi), []).append(j)
    out = sorted(([k, lo, hi, sorted(js)] for (k, lo, hi), js in groups.items()),
                 key=lambda g: (g[0], 0 if g[1] <= 0 <= g[2] else min(abs(g[1]), abs(g[2]))))
    if quiet:
        return out
    print('solve: changes that reproduce the exe (k extra free pending sites before top-level site j, budget change dB;'
          ' an accessor call itself adds ~4-8u of own size):')
    for k, lo, hi, js in out:
        rngs = []
        for j in js:
            if rngs and j == rngs[-1][1] + 1:
                rngs[-1][1] = j
            else:
                rngs.append([j, j])
        where = ', '.join(('%d-%d' % (a, b) if a != b else '%d' % a) for a, b in rngs) if k else '-'
        first = js[0]
        print('  k=%d dB in [%d, %d] (own size %+d..%+d u)  before top-level sites %s  (site %d = %s)%s' % (
            k, lo, hi, lo // 2, hi // 2, where, first, short(sites[first].callee) if first < n else 'end',
            '   UNDECIDED: within the model error' if k == 0 and min(abs(lo), abs(hi)) <= 2 * ERR and not lo <= 0 <= hi
            else ''))
    return out


def walk_all(sites):
    for s in sites:
        yield s
        for x in walk_all(s.children):
            yield x


def label(k, names):
    """short(k), with the parameter list when several keys have the same short name (overloads)."""
    n = short(k)
    if names.get(n, 0) > 1:
        m = re.search(r'\(.*\)', full_signature(k))
        n += (m.group(0) if m else '').replace('class ', '').replace('struct ', '').replace('_CVector<float>',
                                                                                            'LTVector')
    return n


def site_label(s):
    d = s.decision
    if undecided(s):
        d += '?'
    return d


def report(res, verbose):
    def show(sites, ind=0):
        for s in sites:
            interesting = verbose or s.depth == 1 or s.decision in ('refused', 'depth', 'unknown') or undecided(s)
            if interesting:
                print('%s%-9s %-55s cost %5s  limit %7.1f  pending %d%s' % (
                    '  ' * (s.depth - 1), site_label(s), short(s.callee)[:55], s.cost, s.limit, s.pending,
                    '  margin %+.1f' % s.margin if s.margin is not None and s.decision in ('inline', 'refused')
                    else ''))
            if s.decision not in ('refused', 'depth'):
                show(s.children, ind + 1)
    show(res['sites'])
    canon = res.get('canon', {}) if 'exe' in res else {}
    pred, ours = fold(res['pred'], canon), fold(res['ours'], canon)
    close = {}
    for s in walk(res['sites']):
        if undecided(s):
            k = canon.get(s.callee, s.callee)
            close[k] = min(close.get(k, 99), abs(s.limit - s.cost))
    keys = sorted(set(pred) | set(ours) | set(res.get('exe', {})) | set(close))
    names = count([short(k) for k in keys])
    print('\nout-of-line calls of inline candidates:   predicted / our build%s' % (' / exe' if 'exe' in res else ''))
    for k in keys:
        p, o, e = pred.get(k, 0), ours.get(k, 0), res.get('exe', {}).get(k, 0)
        flag = '' if p == o else '   <-- model != build'
        if 'exe' in res and o != e:
            flag += '   <-- build != exe'
        if k in close:
            flag += '   (UNDECIDED: a site within %gu of its limit or of the 40u free threshold)' % ERR
        print('  %3d %3d %s  %s%s' % (p, o, ('%3d' % e) if 'exe' in res else '', label(k, names)[:90], flag))
    groups = {}
    for c, r in canon.items():
        groups.setdefault(r, []).append(c)
    for r, cs in sorted(groups.items()):
        print('ICF: identical code, counted as one: %s = %s' % (short(r)[:50], ', '.join(short(c)[:50] for c in cs)))
    for n in res.get('icf', []):
        print('ICF: exe %s' % n)
    unknown = [k for k, v in res['costs'].items() if v.get('cost') is None]
    if unknown:
        print('cost unknown (treated as inlined, not charged): %s' % ', '.join(short(k) for k in unknown))


def do_sweep(res, sites, costs, B, quiet=False):
    """Budgets for which the model reproduces the exe's out-of-line calls. Returns (ranges, closest (miss, b))."""
    want = res['exe']
    canon = res.get('canon', {})
    ok = []
    best = (10 ** 9, None)
    top = max(3 * (B or FLOOR), 4000)
    for b in range(FLOOR, top, 4):
        miss = _miss(_model(sites, b, costs, canon), want)
        if miss < best[0] or (miss == best[0] and abs(b - (B or FLOOR)) < abs(best[1] - (B or FLOOR))):
            best = (miss, b)
        if miss == 0:
            ok.append(b)
    rngs = []
    for b in ok:
        if rngs and b - rngs[-1][1] <= 4:
            rngs[-1][1] = b
        else:
            rngs.append([b, b])
    # exact edges (1u)
    for r in rngs:
        while r[0] > FLOOR and _miss(_model(sites, r[0] - 1, costs, canon), want) == 0:
            r[0] -= 1
        while _miss(_model(sites, r[1] + 1, costs, canon), want) == 0 and r[1] < top + 8:
            r[1] += 1
    simulate(sites, B or FLOOR, costs)
    if quiet:
        return rngs, best
    if not ok:
        p = _model(sites, best[1], costs, canon)
        print('closest budget: %s u (%d out-of-line calls differ from the exe: %s)' % (best[1], best[0], ', '.join(
            '%s model %d exe %d' % (short(k), p.get(k, 0), want.get(k, 0)) for k in sorted(set(p) | set(want))
            if p.get(k, 0) != want.get(k, 0))))
        simulate(sites, B or FLOOR, costs)
        print('no budget in [1000, %d] reproduces the exe with these costs/sites' % top)
    else:
        print('budgets that reproduce the exe: %s  (ours %s)%s' % (', '.join('%d-%d' % tuple(r) for r in rngs), B,
                                                                   _sweep_verdict(rngs, B)))
    return rngs, best


def _sweep_verdict(rngs, B):
    B = B or FLOOR
    if any(lo <= B <= hi for lo, hi in rngs):
        d = min(min(B - lo, hi - B) for lo, hi in rngs if lo <= B <= hi)
        return '  -> ours reproduces it, %du from the edge%s' % (d, ' (UNDECIDED: within the model error)'
                                                                 if d <= 2 * ERR else '')
    need = min((lo - B if lo > B else hi - B for lo, hi in rngs), key=abs)
    return '  -> nearest: dB %+d (own size %+.1fu)%s' % (need, need / 2.0, ' (UNDECIDED: within the model error)'
                                                          if abs(need) <= 2 * ERR else '')


def summarize(res):
    """Machine-readable verdict for --summary: what the exe's out-of-line calls need from the model, and how sure."""
    want = res['exe']
    canon = res['canon']
    B = res['B'] or FLOOR
    sites, costs = res['sites'], res['costs']
    pred, ours = _model(sites, B, costs, canon), fold(res['ours'], canon)
    close = {}
    for s in walk(sites):
        if undecided(s):
            k = canon.get(s.callee, s.callee)
            close[k] = min(close.get(k, 99), round(abs(s.limit - s.cost), 1))
    keys = sorted(set(pred) | set(want) | set(ours))
    names = count([short(k) for k in keys])
    where = {}
    for s in walk(sites):
        if s.decision in ('inline', 'refused', 'depth'):
            where.setdefault(canon.get(s.callee, s.callee), []).append(
                [s.depth, s.decision, s.cost, round(s.limit, 1), None if s.margin is None else round(s.margin, 1)])
    diffs = [{'callee': label(k, names)[:150], 'model': pred.get(k, 0), 'build': ours.get(k, 0), 'exe': want.get(k, 0),
              'undecided_margin': close.get(k), 'sites[depth,decision,cost,limit,margin]': where.get(k, [])[:8]}
             for k in keys if pred.get(k, 0) != want.get(k, 0) or ours.get(k, 0) != want.get(k, 0)]
    rngs, best = do_sweep(res, sites, costs, res['B'], quiet=True)
    sols = do_solve(res, sites, costs, res['B'], quiet=True)
    simulate(sites, B, costs)
    nearest = None
    if rngs:
        nearest = 0 if any(lo <= B <= hi for lo, hi in rngs) else min(
            (lo - B if lo > B else hi - B for lo, hi in rngs), key=abs)
    n = len(sites)
    sol_out = [{'k': k, 'dB': [lo, hi], 'own_size_u': [lo / 2.0, hi / 2.0],
                'before_sites': js[:12], 'first_site': short(sites[js[0]].callee) if k and js[0] < n else 'end'}
               for k, lo, hi, js in sols[:4]]
    miss = _miss(pred, want)
    if miss == 0:
        conf = 'undecided' if any(d['undecided_margin'] is not None for d in diffs) or close else 'reproduces'
        edge = min((min(B - lo, hi - B) for lo, hi in rngs if lo <= B <= hi), default=None)
        if edge is not None and edge <= 2 * ERR:
            conf = 'undecided'
    elif nearest is not None and abs(nearest) <= 2 * ERR:
        conf = 'undecided'
    elif sols or rngs:
        conf = 'confident'
        if any(d['undecided_margin'] is not None for d in diffs):
            conf = 'undecided'
    else:
        conf = 'unexplained'
    return {'va': '%08x' % res['va'], 'name': res['name'], 'B': res['B'], 'size': res['size'], 'floor': B <= FLOOR,
            'model_vs_exe': miss, 'build_vs_exe': _miss(ours, want), 'diffs': diffs,
            'budget_ranges': rngs, 'nearest_dB': nearest,
            'closest_budget': None if rngs else {'B': best[1], 'miss': best[0]},
            'solve': sol_out, 'undecided_sites': len(close),
            'unknown_costs': sorted(short(k) for k, v in costs.items() if v.get('cost') is None),
            'icf': res['icf'], 'confidence': conf}


def measure_at(key, anchor):
    unit, ann = find_function(key)
    text = open(unit.path, encoding='latin1').read()
    start = body_start(text, ann.line)
    pos = text.find(anchor, start)
    if pos < 0:
        raise SystemExit('anchor not found after the function start')
    anno_off = sum(len(l) + 1 for l in text.split('\n')[:ann.line - 1])

    def src(W):
        t = text[:pos] + '__ib_P0();\n\t' + text[pos:]
        return t[:anno_off] + PROBE_DATA + probe_def('__ib_P0', W) + t[anno_off:]
    sym = [None]

    def rnd(ws):
        lst = parse_listing(compile_asm(unit, src(ws['a'])))
        if sym[0] is None:
            sym[0] = fn_symbol(lst, ann.name, ann.mangled)
        return {'a': '?__ib_P0@@YAXXZ' not in lst[sym[0]]['calls']}
    r = bsearch_parallel(['a'], 0, 30000, rnd)['a']
    print('remaining before %r: %s u' % (anchor, None if r is None else r + 12))


VALIDATE = ['SweptSphereOrient', 'FillSoundTrackPacketFromInfo', '4995f0', 'ThreadLoadFile', 'UnloadFile',
            'TransferNetDriver', 'StartHMessageWrite', 'ftc_ProcessPacket', 'OnLoadWorldPacket',
            'sm_TellClientAboutGlobalLight', 'ModelExtraInit', 'StairStep', 'ClipBoxIntoTree', 'AddMovement',
            'CSoundMgr::Update', 'w_LoadWorldBsp', 'MoveObject', 'DoNonsolidCollision', 'GrowDim',
            'RotateWorldModel', 'ChangeObjectDimensions', 'CollideAgainstWorld', 'GetPushawayPos',
            'CreateServerMgr', 'ReallySendPacket', 'CSoundMgr::Init', 'dsi_LoadServerObjects', 'ClientLoadChildModelCB',
            'sm_WriteLightAnimInfo', 'AddDataToGroupPacket', 'SetObjectFilenames', 'LockTexture']


def run_variants(key, path, jobs, sweep, solve=False):
    """Score source variants in memory (vtry-style variants file: VARIANTS = [(label, old, new), ...], old/new may be
    lists): B, size, and how many of the exe's out-of-line calls the model reproduces, plus our build's calls."""
    import runpy
    vs = runpy.run_path(path)['VARIANTS']
    pre = runpy.run_path(path).get('PRE', [])
    for label, old, new in [('base', [], [])] + list(vs):
        olds = old if isinstance(old, list) else [old]
        news = new if isinstance(new, list) else [new]
        try:
            r = analyse(key, use_exe=True, jobs=jobs, quiet=True, variant=list(pre) + list(zip(olds, news)))
        except (SystemExit, CompileError) as e:
            print('%-24s %s' % (label, str(e)[:200]))
            continue
        want = r.get('exe', {})
        pred, ours = fold(r['pred'], r['canon']), fold(r['ours'], r['canon'])
        dm, db = _miss(pred, want), _miss(ours, want)
        print('%-24s B=%-5s size=%-7s model-vs-exe %d  build-vs-exe %d   build: %s' % (
            label, r['B'], r['size'], dm, db, ', '.join('%s %d' % (short(k)[:28], v) for k, v in sorted(r['ours'].items()))),
            flush=True)
        if sweep:
            do_sweep(r, r['sites'], r['costs'], r['B'])
        if solve:
            do_solve(r, r['sites'], r['costs'], r['B'])


def validate(names, jobs):
    tot = good = 0
    exact = 0
    und_bad = und_all = unknown = 0
    alt = {}         # budget offset -> mispredicted sites (is the measured B, which includes the probe's call, right?)
    for n in names:
        try:
            r = analyse(n, use_exe=False, jobs=jobs, quiet=True)
        except SystemExit as e:
            print('%-32s skipped: %s' % (n, e))
            continue
        except CompileError as e:
            print('%-32s compile error: %s' % (n, e.args[0][:2]))
            continue
        sites = list(walk(r['sites']))
        # per-site agreement: predicted refusals vs our build's out-of-line counts, by callee
        keys = set(r['pred']) | set(r['ours'])
        nsite = len(sites)
        bad = sum(abs(r['pred'].get(k, 0) - r['ours'].get(k, 0)) for k in keys)
        tot += nsite
        good += max(0, nsite - bad)
        exact += (bad == 0)
        und = {s.callee for s in sites if undecided(s)}
        badk = {k for k in keys if r['pred'].get(k, 0) != r['ours'].get(k, 0)}
        und_all += len(und)
        und_bad += len(und & badk)
        unk = sum(1 for s in sites if s.decision == 'unknown')
        unknown += unk
        for d in (-8, -4, 4):
            b2 = max(FLOOR, (r['B'] or FLOOR) + d) if (r['B'] or FLOOR) > FLOOR else FLOOR
            simulate(r['sites'], b2, r['costs'])
            p2 = refused_multiset(r['sites'])
            alt[d] = alt.get(d, 0) + _miss(p2, r['ours'])
        simulate(r['sites'], r['B'] or FLOOR, r['costs'])
        print('%-32s B=%-6s sites %3d  mispredicted %d%s%s%s' % (
            n, r['B'], nsite, bad, '' if bad == 0 else '  ' + ', '.join(
                '%s %d/%d%s' % (short(k)[:30], r['pred'].get(k, 0), r['ours'].get(k, 0), ' (undecided)' if k in und
                                else '') for k in sorted(badk)),
            '  [%d undecided]' % len(und) if und else '', '  [%d cost unknown]' % unk if unk else ''), flush=True)
    print('\n%d of %d sites predicted (%.1f%%); %d of %d functions exact; %d sites cost unknown' % (
        good, tot, 100.0 * good / max(tot, 1), exact, len(names), unknown))
    print('undecided callees (a site within %gu of its limit): %d, of which mispredicted: %d' % (ERR, und_all, und_bad))
    print('mispredicted sites with B changed by %s (B > 1000 only): %s  (as measured: %d)' % (
        '/'.join('%+d' % d for d in sorted(alt)), '/'.join(str(alt[d]) for d in sorted(alt)), tot - good))


def main(argv):
    jobs = 6
    while '--cost' in argv:           # --cost IsWorldModel=41  (what-if; all overloads with that name)
        i = argv.index('--cost')
        a, v = argv[i + 1].rsplit('=', 1)
        COST_OVERRIDES[a] = int(v)
        del argv[i:i + 2]
    while '--alias' in argv:          # --alias 45e960=?IsWorldModel@@YAIPAVLTObject@@@Z
        i = argv.index('--alias')
        a, m = argv[i + 1].split('=', 1)
        ALIASES[int(a, 16)] = m
        del argv[i:i + 2]
    if '-j' in argv:
        i = argv.index('-j')
        jobs = int(argv[i + 1])
        del argv[i:i + 2]
    while '--flags' in argv:           # --flags "/O1 /Ob2": replace the unit's optimisation flags for every probe (what-if)
        i = argv.index('--flags')
        FLAGS_OVERRIDE[:] = argv[i + 1].split()
        del argv[i:i + 2]
    if argv and argv[0] == '--validate':
        validate(argv[1:] or VALIDATE, jobs)
        return
    if argv and argv[0] == '--variants':
        run_variants(argv[1], argv[2], jobs, '--sweep' in argv, '--solve' in argv)
        return
    if argv and argv[0] == '--at':
        measure_at(argv[1], argv[2])
        return
    flags = {a for a in argv if a.startswith('-')}
    keys = [a for a in argv if not a.startswith('-')]
    if not keys:
        print(__doc__)
        return
    for k in keys:
        try:
            analyse(k, verbose='-v' in flags, sweep='--sweep' in flags, costs_on='--no-cost' not in flags, jobs=jobs,
                    solve='--solve' in flags, icf='--no-icf' not in flags, summary='--summary' in flags,
                    quiet='--summary' in flags and '-v' not in flags)
        except CompileError as e:      # loud: a probe that does not compile means the numbers would be meaningless
            print('FATAL: a probe compile failed (compiler %s, flags %s):' % (VC6CL, ' '.join(FLAGS_OVERRIDE) or 'the unit\'s own'))
            for l in e.args[0][:8]:
                print('   ' + str(l)[:220])
            return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
