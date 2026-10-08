r"""Randomised source permuter for one STUB (in the spirit of decomp-permuter, for VC6 C++).

  python tools/permute.py [--module d3dren] [<src file>] <hex address | Class::Method | name> [--iters N] [--jobs J] [--seed S]
                          [--temp T] [--ops a,b,..] [--timeout SECONDS] [--cut] [--minutes M] [--resume]

The target is a hex address, or a (qualified) function name such as `ModelDraw::FUN_10002050` that exactly one FUNCTION/STUB
annotation carries; without a source file the unit that annotates it is found.  Works on any function of a big unit file and
on member functions and constructors.  With --module d3dren (or DECOMP_MODULE=d3dren) everything comes from the module: the
compiler wrapper and flags, DX8INC, the unit's // FLAGS: line, the module's namemap, and the output directory.  Run from Git
Bash or PowerShell, no extra environment.

Applies random source mutations to the function's body, compiles every candidate privately (a copy of the unit in
build/permute/<address>/w<slot>/, build/d3dren/permute/... for d3dren; its relative #include "..." lines are rewritten to point at
the originals; never the unit's own object or source, so it is safe next to other checks and never writes into src/) and scores the
function against the exe: instruction mismatches after alignment (ignoring stack offsets, then exact), then
size and differing bytes. It walks by simulated annealing from the current source.  Every compile has a timeout (default 90 s,
--timeout): a mutated source that hangs CL.EXE is killed together with its C1/C2 children and counts as a failed candidate.
--cut compiles only the part of the unit up to the end of the function (faster for a big unit; the whole file is used when that
does not compile): inlining decisions only depend on what is defined before the function.

The mutations are NOT all semantics-preserving (type changes, operand swaps, statement moves without a dependency
test). That is deliberate: the only result it reports as solved is a byte-identical function, and identical code
is identical behaviour. An improved-but-not-matching candidate must be read before it is adopted.

Output (build/permute/<address>/, build/d3dren/permute/<address>/ for d3dren):
  best.cpp     the best-scoring version of the whole source file so far
  match.cpp    the first byte-identical version (the search stops); verify with `build.py check <unit>` after
               copying it over the source (relocation targets and the unit's other functions are checked there)
  log.txt      every improvement: score and the mutations that led to it

  result.json  start/best ALIGNED, the mutations in the best candidate, compile failures per mutation
  hints.txt    ftype probe hits (a member zeroed as int that compiles closer as a float, or the reverse)

  python tools/permute.py [<src file>] <hex address | name> --minimize
reverts every hunk of match.cpp that the match does not need (writes min.cpp, prints the remaining diff).

  python tools/permute.py <src> <addr> --text <scratch copy> --outdir <dir> [--include <private include/>]
permutes a scratch copy of the source (the unit file still gives the flags and include directory) and writes
elsewhere: nothing under src/ or include/ is ever written. --sample N [-v] shows what each mutation does and its
compile-failure rate; --ftype-only runs just the ftype probes. Mutation list: --help.
  python tools/permute_apply.py [--module d3dren] <src file> <hex address> [old=new ...]     copies min.cpp/match.cpp's body into the source
"""
import hashlib, math, os, random, re, shutil, subprocess, sys, time

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import modcfg  # noqa: E402  (module tag LITHTECH or D3DREN; --module is consumed here)
import toolenv  # noqa: E402  (module defaults: compiler environment, MSYS argument repair, timeouts)

COMPILE_TIMEOUT = 90        # seconds per candidate compile; a mutated source that hangs CL.EXE is killed with its children

# ---------------------------------------------------------------- mutations

INTS = ['int', 'uint32', 'int32', 'uint16', 'uint8', 'long', 'short', 'unsigned short', 'unsigned long', 'LTBOOL',
        'int16', 'unsigned int', 'unsigned char', 'char']
OPND = r'(?:[A-Za-z_]\w*(?:(?:->|\.)\w+|\[\w+\])*|\d[\w.]*)'
DECL = re.compile(r'^(\t+)((?:const |static |unsigned |struct |class )*[A-Za-z_][\w:]*(?:<[^;=()]*>)?)\s+([*&\s]*[A-Za-z_]\w*(?:\[\w*\])*'
                  r'(?:\s*,\s*[*&\s]*[A-Za-z_]\w*(?:\[\w*\])*)*)\s*;\s*$')
DECL_INIT = re.compile(r'^(\t+)((?:const |unsigned |struct |class )*[A-Za-z_][\w:]*(?:<[^;=()]*>)?)\s+([*&]*\s*)([A-Za-z_]\w*)\s*=\s*(.+);\s*$')
NOT_TYPE = {'return', 'delete', 'goto', 'else', 'case', 'new', 'throw', 'break', 'continue', 'if', 'while', 'for', 'do'}
CTRL = re.compile(r'^(return|break|continue|goto|for|if|while|else|do|case|default|switch)\b')


def indent(l):
    return len(l) - len(l.lstrip('\t'))


def is_simple(l):
    t = l.strip()
    return bool(t) and t.endswith(';') and '{' not in t and '}' not in t and not t.startswith(('//', '#')) \
        and not CTRL.match(t) and not re.match(r'^\w+:$', t)


def is_decl(l):
    m = DECL.match(l)
    return bool(m) and m.group(2).split()[-1] not in NOT_TYPE and '(' not in l


def block_end(L, i):
    """L[i] is a line that is just `{`: index of its closing `}` line (same indentation)."""
    ind = indent(L[i])
    for j in range(i + 1, len(L)):
        if L[j].strip() in ('}', '};') and indent(L[j]) == ind:
            return j
        if L[j].strip().startswith('}') and indent(L[j]) == ind:
            return j
    return None


def stmt_span(L, i):
    """The statement starting at line i: (i, j) with j exclusive. A braced block, a control statement with its
    body, or one simple line. None if it can't be delimited."""
    if i >= len(L):
        return None
    t = L[i].strip()
    if t == '{':
        e = block_end(L, i)
        return (i, e + 1) if e is not None else None
    if re.match(r'^(if|for|while|else)\b', t) and not t.endswith(';'):
        s = stmt_span(L, i + 1)
        if not s:
            return None
        j = s[1]
        if t.startswith('if') and j < len(L) and L[j].strip().startswith('else') and indent(L[j]) == indent(L[i]):
            if L[j].strip() == 'else':
                s2 = stmt_span(L, j + 1)
            elif not L[j].strip().endswith(';'):
                s2 = stmt_span(L, j)
            else:
                s2 = (j, j + 1)
            if not s2:
                return None
            j = s2[1]
        return (i, j)
    if t.endswith(';') and not t.startswith('}'):
        return (i, i + 1)
    return None


def runs(L):
    """Maximal runs of consecutive simple lines with the same indentation: list of (a, b)."""
    out, i = [], 0
    while i < len(L):
        if is_simple(L[i]):
            j = i
            while j < len(L) and is_simple(L[j]) and indent(L[j]) == indent(L[i]):
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def m_move_stmt(L, rng, ctx):
    rs = [r for r in runs(L) if r[1] - r[0] >= 2]
    if not rs:
        return None
    a, b = rng.choice(rs)
    i = rng.randrange(a, b)
    j = rng.randrange(a, b)
    if i == j:
        return None
    if rng.random() < 0.6:      # mostly neighbours
        j = i + rng.choice((-1, 1))
        if not (a <= j < b):
            return None
    x = L.pop(i)
    L.insert(j, x)
    return 'move %r %+d' % (x.strip()[:40], j - i)


def m_swap_decl(L, rng, ctx):
    ds = [i for i, l in enumerate(L) if is_decl(l) and indent(l) == 1]
    if len(ds) < 2:
        return None
    i, j = rng.sample(ds, 2)
    L[i], L[j] = L[j], L[i]
    return 'swap decl %r / %r' % (L[i].strip()[:30], L[j].strip()[:30])


def m_split_multi_decl(L, rng, ctx):
    ds = [i for i, l in enumerate(L) if is_decl(l) and ',' in DECL.match(l).group(3)]
    if not ds:
        return None
    i = rng.choice(ds)
    m = DECL.match(L[i])
    parts = [p.strip() for p in m.group(3).split(',')]
    if rng.random() < 0.5:
        rng.shuffle(parts)
        L[i] = '%s%s %s;' % (m.group(1), m.group(2), ', '.join(parts))
        return 'shuffle decl list %r' % L[i].strip()[:40]
    L[i:i + 1] = ['%s%s %s;' % (m.group(1), m.group(2), p) for p in parts]
    return 'split decl %r' % m.group(3)[:40]


def m_split_init(L, rng, ctx):
    ds = [i for i, l in enumerate(L) if DECL_INIT.match(l) and DECL_INIT.match(l).group(2).split()[-1] not in NOT_TYPE]
    if not ds:
        return None
    i = rng.choice(ds)
    m = DECL_INIT.match(L[i])
    ind, typ, ptr, name, expr = m.groups()
    if typ.startswith('const') or '&' in ptr:
        return None
    L[i:i + 1] = ['%s%s %s%s;' % (ind, typ, ptr, name), '%s%s = %s;' % (ind, name, expr)]
    if rng.random() < 0.5:      # hoist the bare declaration to the top of the function
        d = L.pop(i)
        L.insert(0, '\t' + d.strip())
    return 'split init %s' % name


def m_decl_to_use(L, rng, ctx):
    ds = [i for i, l in enumerate(L) if is_decl(l) and ',' not in l and '[' not in l]
    if not ds:
        return None
    i = rng.choice(ds)
    m = DECL.match(L[i])
    name = re.sub(r'[*&\s]', '', m.group(3))
    pat = re.compile(r'\b%s\b' % re.escape(name))
    for j in range(i + 1, len(L)):
        if pat.search(L[j]):
            break
    else:
        return None
    ma = re.match(r'^(\t+)%s\s*=\s*([^=].*);\s*$' % re.escape(name), L[j])
    d = L[i]
    if ma and rng.random() < 0.7:
        L[j] = '%s%s %s = %s;' % (ma.group(1), m.group(2), m.group(3).strip(), ma.group(2))
        del L[i]
        return 'merge decl+init %s' % name
    if j == i + 1:
        return None
    del L[i]
    L.insert(j - 1, '\t' * indent(L[j - 1]) + d.strip())
    return 'decl to first use %s' % name


def m_int_type(L, rng, ctx):
    ds = [i for i, l in enumerate(L) if (DECL.match(l) and DECL.match(l).group(2) in INTS) or
          (DECL_INIT.match(l) and DECL_INIT.match(l).group(2) in INTS)]
    if not ds:
        return None
    i = rng.choice(ds)
    m = DECL.match(L[i]) or DECL_INIT.match(L[i])
    new = rng.choice([t for t in INTS if t != m.group(2)])
    L[i] = L[i].replace(m.group(2), new, 1)
    return 'type %s -> %s: %s' % (m.group(2), new, L[i].strip()[:30])


COMM = re.compile(r'(?<=[(=,!&|?:]\s)(%s) (\+|\*|\||&|\^|==|!=|<|>|<=|>=) (%s)(?=\s*[);,?:]|\s(?:&&|\|\|))' % (OPND, OPND))
FLIP = {'<': '>', '>': '<', '<=': '>=', '>=': '<='}


def m_commute(L, rng, ctx):
    c = [(i, m) for i, l in enumerate(L) for m in COMM.finditer(l) if not l.strip().startswith('//')]
    if not c:
        return None
    i, m = rng.choice(c)
    a, op, b = m.groups()
    L[i] = L[i][:m.start()] + '%s %s %s' % (b, FLIP.get(op, op), a) + L[i][m.end():]
    return 'commute %s %s %s' % (a, op, b)


ANYBIN = re.compile(r'(%s) (\+|\*) (%s)' % (OPND, OPND))


def m_commute_any(L, rng, ctx):
    c = [(i, m) for i, l in enumerate(L) for m in ANYBIN.finditer(l) if not l.strip().startswith('//')]
    if not c:
        return None
    i, m = rng.choice(c)
    a, op, b = m.groups()
    L[i] = L[i][:m.start()] + '%s %s %s' % (b, op, a) + L[i][m.end():]
    return 'commute(any) %s %s %s' % (a, op, b)


def m_incdec(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        for m in re.finditer(r'(?<![\w+\-])(%s)(\+\+|--)(?=\s*[;)])' % OPND, l):
            c.append((i, m, 'post'))
        for m in re.finditer(r'(?<![\w+\-])(\+\+|--)(%s)(?=\s*[;)])' % OPND, l):
            c.append((i, m, 'pre'))
    if not c:
        return None
    i, m, k = rng.choice(c)
    new = (m.group(2) + m.group(1))
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'incdec %s' % new


def m_compound(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t+)(%s) = \2 ([+\-*/|&]) (.+);$' % OPND, l)
        if m:
            c.append((i, '%s%s %s= %s;' % (m.group(1), m.group(2), m.group(3), m.group(4))))
        m = re.match(r'^(\t+)(%s) ([+\-*/|&])= (.+);$' % OPND, l)
        if m:
            e = m.group(4) if re.match(r'^%s$' % OPND, m.group(4)) else '(%s)' % m.group(4)
            c.append((i, '%s%s = %s %s %s;' % (m.group(1), m.group(2), m.group(2), m.group(3), e)))
            if m.group(3) in '+*|&':
                c.append((i, '%s%s = %s %s %s;' % (m.group(1), m.group(2), e, m.group(3), m.group(2))))
    if not c:
        return None
    i, new = rng.choice(c)
    L[i] = new
    return 'compound %r' % new.strip()[:40]


def m_if_invert(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        t = l.strip()
        m = re.match(r'^if\s*\((.*)\)$', t)
        if not m:
            continue
        s1 = stmt_span(L, i + 1)
        if not s1 or s1[1] >= len(L) or L[s1[1]].strip() != 'else' or indent(L[s1[1]]) != indent(l):
            continue
        s2 = stmt_span(L, s1[1] + 1)
        if s2:
            c.append((i, m.group(1), s1, s2))
    if not c:
        return None
    i, cond, s1, s2 = rng.choice(c)
    ind = '\t' * indent(L[i])
    if cond.startswith('!(') and cond.endswith(')') and cond.count('(') == 1:
        ncond = cond[2:-1]
    elif re.match(r'^!%s$' % OPND, cond):
        ncond = cond[1:]
    elif re.match(r'^%s$' % OPND, cond):
        ncond = '!' + cond
    else:
        ncond = '!(%s)' % cond
    new = [ind + 'if (%s)' % ncond] + L[s2[0]:s2[1]] + [ind + 'else'] + L[s1[0]:s1[1]]
    L[i:s2[1]] = new
    return 'invert if (%s)' % cond[:30]


CMPNEG = {'<': '>=', '>': '<=', '<=': '>', '>=': '<', '==': '!=', '!=': '=='}


def m_cmp_form(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        if not re.match(r'^\s*(if|while|else if)\b', l.strip()) and '?' not in l:
            continue
        for m in re.finditer(r'(?<=[(|&]\s|.\()(%s) (<=|>=|<|>|==|!=) (%s)(?=\)|\s(?:&&|\|\|))' % (OPND, OPND), l):
            c.append((i, m))
        for m in re.finditer(r'!\((%s) (<=|>=|<|>|==|!=) (%s)\)' % (OPND, OPND), l):
            c.append((i, m))
    if not c:
        return None
    i, m = rng.choice(c)
    a, op, b = m.groups()
    if m.group(0).startswith('!('):
        new = '%s %s %s' % (a, CMPNEG[op], b)
    else:
        new = '!(%s %s %s)' % (a, CMPNEG[op], b)
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'cmp form %s' % new


def m_truth_form(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        if not re.match(r'^(if|while|else if)\b', l.strip()):
            continue
        for m in re.finditer(r'(?<=[(|&]\s|.\()(!?)(%s)(?=\)|\s(?:&&|\|\|))' % OPND, l):
            if m.group(2) not in ('LTTRUE', 'LTFALSE', 'TRUE', 'FALSE') and not m.group(2)[0].isdigit():
                c.append((i, m, 'bare'))
        for m in re.finditer(r'(%s) (==|!=) (0|LTNULL|NULL|LTFALSE|FALSE)\b' % OPND, l):
            c.append((i, m, 'cmp'))
    if not c:
        return None
    i, m, k = rng.choice(c)
    if k == 'bare':
        z = rng.choice(['0', 'LTNULL'])
        new = '%s %s %s' % (m.group(2), '==' if m.group(1) else '!=', z)
    else:
        new = ('!' if m.group(2) == '==' else '') + m.group(1)
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'truth form %s' % new


def m_ternary(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t+)(.+?) = (.+?) \? (.+?) : (.+);$', l)
        if m and '?' not in m.group(4) + m.group(5):
            c.append(('t', i, m))
        m = re.match(r'^(\t+)if\s*\((.*)\)$', l)
        if m and i + 3 < len(L) and L[i + 2].strip() == 'else':
            a = re.match(r'^\t+(.+?) = (.+);$', L[i + 1])
            b = re.match(r'^\t+(.+?) = (.+);$', L[i + 3])
            if a and b and a.group(1) == b.group(1):
                c.append(('i', i, (m, a, b)))
    if not c:
        return None
    k, i, m = rng.choice(c)
    if k == 't':
        ind, lhs, cond, a, b = m.groups()
        if rng.random() < 0.3:
            L[i:i + 1] = [ind + '%s = %s;' % (lhs, b), ind + 'if (%s)' % cond, ind + '\t%s = %s;' % (lhs, a)]
        else:
            L[i:i + 1] = [ind + 'if (%s)' % cond, ind + '\t%s = %s;' % (lhs, a), ind + 'else', ind + '\t%s = %s;' % (lhs, b)]
        return 'ternary -> if: %s' % lhs[:30]
    mi, a, b = m
    L[i:i + 4] = ['%s%s = (%s) ? %s : %s;' % (mi.group(1), a.group(1), mi.group(2), a.group(2), b.group(2))]
    return 'if -> ternary: %s' % a.group(1)[:30]


def m_block_wrap(L, rng, ctx):
    rs = runs(L)
    if not rs:
        return None
    a, b = rng.choice(rs)
    i = rng.randrange(a, b)
    j = min(b, i + rng.randint(1, 4))
    ind = '\t' * indent(L[i])
    L[i:j] = [ind + '{'] + ['\t' + x for x in L[i:j]] + [ind + '}']
    return 'wrap block %d lines at %r' % (j - i, L[i + 1].strip()[:30])


def m_block_unwrap(L, rng, ctx):
    c = [i for i, l in enumerate(L) if l.strip() == '{' and i > 0 and
         (L[i - 1].strip().endswith((';', '}', '{')) or not L[i - 1].strip())]
    if not c:
        return None
    i = rng.choice(c)
    e = block_end(L, i)
    if e is None:
        return None
    L[i:e + 1] = [x[1:] if x.startswith('\t') else x for x in L[i + 1:e]]
    return 'unwrap block'


def m_param_copy(L, rng, ctx):
    ps = ctx['params']
    if not ps:
        return None
    typ, name = rng.choice(ps)
    new = fresh_name(ctx, L, rng.choice(['p' + name[0].upper() + name[1:] + '2', name + 'Local', 'tmp' + name[0].upper() + name[1:]]))
    pat = re.compile(r'\b%s\b' % re.escape(name))
    if not any(pat.search(l) for l in L):
        return None
    start = 0
    while start < len(L) and (is_decl(L[start]) or not L[start].strip() or L[start].strip().startswith('//')):
        start += 1
    if rng.random() < 0.4:      # copy later: from a random use on
        uses = [i for i in range(start, len(L)) if pat.search(L[i]) and indent(L[i]) == 1]
        if uses:
            start = rng.choice(uses)
    for i in range(start, len(L)):
        L[i] = pat.sub(new, L[i])
    L.insert(start, '\t%s %s = %s;' % (typ, new, name))
    return 'copy param %s' % name


def m_local_split(L, rng, ctx):
    ds = [i for i, l in enumerate(L) if is_decl(l) and ',' not in l and '[' not in l and indent(l) == 1]
    if not ds:
        return None
    i = rng.choice(ds)
    m = DECL.match(L[i])
    name = re.sub(r'[*&\s]', '', m.group(3))
    ptr = m.group(3).replace(name, '').strip()
    pat = re.compile(r'\b%s\b' % re.escape(name))
    uses = [j for j in range(i + 1, len(L)) if pat.search(L[j]) and indent(L[j]) == 1]
    if len(uses) < 2:
        return None
    j = rng.choice(uses[1:])
    new = name + '2'
    for k in range(j, len(L)):
        L[k] = pat.sub(new, L[k])
    L.insert(j, '\t%s %s%s = %s;' % (m.group(2), ptr, new, name))
    return 'split local %s at %r' % (name, L[j + 1].strip()[:30])


def m_loop_form(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        t = l.strip()
        if re.match(r'^for\s*\((.*);(.*);(.*)\)$', t) or re.match(r'^while\s*\((.*)\)$', t):
            if i + 1 < len(L) and L[i + 1].strip() == '{' and block_end(L, i + 1) is not None:
                c.append(i)
    if not c:
        return None
    i = rng.choice(c)
    e = block_end(L, i + 1)
    ind = '\t' * indent(L[i])
    t = L[i].strip()
    body = L[i + 2:e]
    has_continue = any(re.search(r'\bcontinue\b', x) for x in body)
    m = re.match(r'^for\s*\((.*);(.*);(.*)\)$', t)
    if m:
        init, cond, inc = (x.strip() for x in m.groups())
        if has_continue or not cond:
            return None
        k = rng.randrange(3)
        pre = [ind + init + ';'] if init else []
        tail = [ind + '\t' + inc + ';'] if inc else []
        if k == 0:
            new = pre + [ind + 'while (%s)' % cond, ind + '{'] + body + tail + [ind + '}']
        elif k == 1:
            new = pre + [ind + 'for (;;)', ind + '{', ind + '\tif (!(%s))' % cond, ind + '\t\tbreak;'] + body + tail + [ind + '}']
        else:
            new = pre + [ind + 'if (%s)' % cond, ind + '{', ind + '\tdo', ind + '\t{'] + ['\t' + x for x in body + tail] + \
                [ind + '\t} while (%s);' % cond, ind + '}']
        L[i:e + 1] = new
        return 'for -> form %d' % k
    m = re.match(r'^while\s*\((.*)\)$', t)
    cond = m.group(1)
    k = rng.randrange(3)
    if k == 0:
        new = [ind + 'for (;;)', ind + '{', ind + '\tif (!(%s))' % cond, ind + '\t\tbreak;'] + body + [ind + '}']
    elif k == 1:
        new = [ind + 'do', ind + '{', ind + '\tif (!(%s))' % cond, ind + '\t\tbreak;'] + body + [ind + '} while (1);']
    else:
        if has_continue:
            return None
        new = [ind + 'if (%s)' % cond, ind + '{', ind + '\tdo', ind + '\t{'] + ['\t' + x for x in body] + \
            [ind + '\t} while (%s);' % cond, ind + '}']
    L[i:e + 1] = new
    return 'while -> form %d' % k


def m_index_form(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        for m in re.finditer(r'&(%s)\[(\w+)\]' % r'[A-Za-z_]\w*(?:(?:->|\.)\w+)*', l):
            c.append((i, m, '(%s + %s)' % (m.group(1), m.group(2))))
        for m in re.finditer(r'\((%s) \+ (\w+)\)' % r'[A-Za-z_]\w*(?:(?:->|\.)\w+)*', l):
            c.append((i, m, '&%s[%s]' % (m.group(1), m.group(2))))
    if not c:
        return None
    i, m, new = rng.choice(c)
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'index form %s' % new


def m_chain_assign(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t+)(%s) = (%s) = ([^=;]+);$' % (OPND, OPND), l)
        if m:
            c.append((i, [m.group(1) + '%s = %s;' % (m.group(3), m.group(4)), m.group(1) + '%s = %s;' % (m.group(2), m.group(4))], 1))
        m = re.match(r'^(\t+)(%s) = ([^=;]+);$' % OPND, l)
        if m and i + 1 < len(L):
            m2 = re.match(r'^(\t+)(%s) = ([^=;]+);$' % OPND, L[i + 1])
            if m2 and m2.group(3) == m.group(3) and m2.group(1) == m.group(1):
                c.append((i, [m.group(1) + '%s = %s = %s;' % (m2.group(2), m.group(2), m.group(3))], 2))
    if not c:
        return None
    i, new, n = rng.choice(c)
    L[i:i + n] = new
    return 'chain assign %r' % new[0].strip()[:40]


def m_swap_stmts_any(L, rng, ctx):
    """Swap two adjacent whole statements (control statements with their bodies included)."""
    c = []
    for i in range(len(L)):
        s1 = stmt_span(L, i)
        if not s1 or (i > 0 and re.match(r'^(if|for|while|else|do)\b', L[i - 1].strip()) and not L[i - 1].strip().endswith(';')):
            continue
        j = s1[1]
        while j < len(L) and not L[j].strip():
            j += 1
        s2 = stmt_span(L, j)
        if s2 and j < len(L) and indent(L[j]) == indent(L[i]) and not L[j].strip().startswith('else') \
                and not is_decl(L[i]) and not is_decl(L[j]):
            c.append((s1, (j, s2[1])))
    if not c:
        return None
    s1, s2 = rng.choice(c)
    L[s1[0]:s2[1]] = L[s2[0]:s2[1]] + L[s1[1]:s2[0]] + L[s1[0]:s1[1]]
    return 'swap statements at %r' % L[s1[0]].strip()[:30]


def m_early_return(L, rng, ctx):
    """`if (c) { A } [rest]` <-> guard forms are too varied to do textually; this one only toggles
    `if (c) return X;` between one line and a braced body (affects nothing in VC6) - kept as a no-op slot."""
    return None


def m_assoc(L, rng, ctx):
    c = [(i, m) for i, l in enumerate(L) for m in re.finditer(r'(%s) ([+*]) (%s) \2 (%s)' % (OPND, OPND, OPND), l)]
    if not c:
        return None
    i, m = rng.choice(c)
    a, op, b, d = m.groups()
    new = rng.choice(['%s %s (%s %s %s)' % (a, op, b, op, d), '(%s %s %s) %s %s' % (a, op, d, op, b),
                      '%s %s %s %s %s' % (b, op, a, op, d), '%s %s %s %s %s' % (a, op, d, op, b)])
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'assoc %s' % new


def m_decl_scope(L, rng, ctx):
    """Move a top-level declaration into the innermost block that holds all its uses."""
    ds = [i for i, l in enumerate(L) if is_decl(l) and ',' not in l and indent(l) == 1]
    if not ds:
        return None
    i = rng.choice(ds)
    m = DECL.match(L[i])
    name = re.sub(r'[*&\s\[\]\w]*?(\w+)(\[\w*\])*$', r'\1', m.group(3).strip())
    pat = re.compile(r'\b%s\b' % re.escape(name))
    uses = [j for j in range(len(L)) if j != i and pat.search(L[j])]
    if not uses or min(indent(L[j]) for j in uses) < 2:
        return None
    first = uses[0]
    k = first
    while k > 0 and not (L[k].strip() == '{' and indent(L[k]) < indent(L[first]) and (block_end(L, k) or 0) >= uses[-1]):
        k -= 1
    if k <= 0:
        return None
    d = L[i].strip()
    L.insert(k + 1, '\t' * (indent(L[k]) + 1) + d)
    del L[i if i < k else i + 1]
    return 'decl into block: %s' % name


MEMB = r'[A-Za-z_]\w*(?:(?:->|\.)\w+(?:\(\))?|\[\w+\])*'
# project idioms that are the same operation written two ways (NOTES.md: accessors vs members, SDK macros vs operators)
IDIOMS = [
    (r'&(%s)->m_Message\b' % MEMB, r'\1->GetMessageImpl()'), (r'(%s)->GetMessageImpl\(\)' % MEMB, r'&\1->m_Message'),
    (r'VEC_INIT\((%s)\);' % MEMB, r'\1.Init();'), (r'(%s)\.Init\(\);' % MEMB, r'VEC_INIT(\1);'),
    (r'VEC_COPY\((%s), (%s)\);' % (MEMB, MEMB), r'\1 = \2;'),
    (r'^(\t+)(%s) = (%s);$' % (MEMB, MEMB), r'\1VEC_COPY(\2, \3);'),
    (r'(%s)\.Dot\((%s)\)' % (MEMB, MEMB), r'VEC_DOT(\1, \2)'), (r'VEC_DOT\((%s), (%s)\)' % (MEMB, MEMB), r'\1.Dot(\2)'),
    (r'(%s)\.Dot\((%s)\)' % (MEMB, MEMB), r'\2.Dot(\1)'),
    (r'VEC_SUB\((%s), (%s), (%s)\);' % (MEMB, MEMB, MEMB), r'\1 = \2 - \3;'),
    (r'VEC_ADD\((%s), (%s), (%s)\);' % (MEMB, MEMB, MEMB), r'\1 = \2 + \3;'),
    (r'^(\t+)(%s) = (%s) - (%s);$' % (MEMB, MEMB, MEMB), r'\1VEC_SUB(\2, \3, \4);'),
    (r'^(\t+)(%s) = (%s) \+ (%s);$' % (MEMB, MEMB, MEMB), r'\1VEC_ADD(\2, \3, \4);'),
    (r'->GetPos\(\)', r'->m_Pos'), (r'->m_Pos\b', r'->GetPos()'),
    (r'->GetDims\(\)', r'->m_Dims'), (r'->m_Dims\b', r'->GetDims()'),
    (r'\.GetSize\(\)', r'.m_nElements'), (r'\.m_nElements\b', r'.GetSize()'),
    (r'&(%s)\[0\]' % MEMB, r'\1.GetArray()'), (r'(%s)\.GetArray\(\)' % MEMB, r'&\1[0]'),
    (r'\(LTBOOL\)', ''), (r'\b0\.0f\b', '0'), (r'\bLTNULL\b', '0'),
    (r'(%s)\.MagSqr\(\)' % MEMB, r'VEC_MAGSQR(\1)'), (r'VEC_MAGSQR\((%s)\)' % MEMB, r'\1.MagSqr()'),
    (r'(%s)\.Mag\(\)' % MEMB, r'VEC_MAG(\1)'), (r'VEC_MAG\((%s)\)' % MEMB, r'\1.Mag()'),
    (r'(%s) \*= (%s);' % (MEMB, MEMB), r'VEC_MULSCALAR(\1, \1, \2);'),
    (r'LTMIN\(([^(),]+), ([^(),]+)\)', r'LTMIN(\2, \1)'), (r'LTMAX\(([^(),]+), ([^(),]+)\)', r'LTMAX(\2, \1)'),
]
IDIOMS = [(re.compile(a, re.M), b) for a, b in IDIOMS]


def m_idiom(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        if l.strip().startswith('//'):
            continue
        for k, (pat, rep) in enumerate(IDIOMS):
            for m in pat.finditer(l):
                c.append((i, k, m))
    if not c:
        return None
    i, k, m = rng.choice(c)
    pat, rep = IDIOMS[k]
    if rep.startswith(r'\1VEC_'):     # `a = b;` / `a = b - c;` -> VEC_*: only for vectors
        ty = gettype(ctx, m.group(2))
        if not ty or ty[1] != 'vec':
            return None
    new = m.expand(rep)
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'idiom %r -> %r' % (m.group(0)[:30], new.strip()[:30])


FLOATISH = re.compile(r'(?<![\w.>])((?:[A-Za-z_]\w*(?:->|\.))+(?:x|y|z|m_f\w+|m_Dist|m_Radius)|[A-Za-z_]\w*\[\w+\]\.(?:x|y|z))\b(?!\s*(?:=[^=]|\+=|-=|\*=|/=|\())')
VECISH = re.compile(r'(?<![\w.>&])((?:[A-Za-z_]\w*(?:->|\.))+(?:m_Normal|m_Pos|m_Dims|m_v[A-Z]\w*|m_Velocity|m_Scale)|[A-Za-z_]\w*->GetPos\(\))(?![\w.(]|\s*(?:=[^=]|\+=|-=|\*=|/=))')


def enclosing_block_start(L, i):
    """Index of the first line of the innermost block containing line i (0 for the function body)."""
    ind = indent(L[i])
    for k in range(i - 1, -1, -1):
        if L[k].strip() == '{' and indent(L[k]) < ind:
            return k + 1
    return 0


def m_named_temp(L, rng, ctx):
    """Name a float / vector operand as a local declared earlier in the same block (first-reference order,
    register choice) and use it from there on in that block."""
    c = []
    for i, l in enumerate(L):
        if l.strip().startswith('//') or is_decl(l):
            continue
        for m in FLOATISH.finditer(l):
            c.append((i, m.group(1), 'float'))
        for m in VECISH.finditer(l):
            c.append((i, m.group(1), 'vec'))
    if not c:
        return None
    i, expr, kind = rng.choice(c)
    b0 = enclosing_block_start(L, i)
    # candidate insertion points: statement starts in the same block, at the same indentation, before line i
    pts = [k for k in range(b0, i + 1) if indent(L[k]) == indent(L[i]) and L[k].strip() and
           not L[k].strip().startswith(('else', '{', '}', '//')) and
           not (k > 0 and re.match(r'^(if|for|while|else|do)\b', L[k - 1].strip()) and not L[k - 1].strip().endswith(';'))]
    if not pts:
        return None
    at = rng.choice(pts) if rng.random() < 0.5 else pts[-1]
    n = ctx.setdefault('ntemp', 0)
    ctx['ntemp'] = n + 1
    ind = '\t' * indent(L[i])
    if kind == 'float':
        name = 'fTmp%d' % n
        decl = '%sfloat %s = %s;' % (ind, name, expr)
        use = name
    else:
        form = rng.randrange(3)
        name = ('vTmp%d' if form == 0 else 'pVec%d' if form == 1 else 'vRef%d') % n
        decl = ind + ('LTVector %s = %s;' if form == 0 else 'LTVector *%s = &%s;' if form == 1 else 'LTVector &%s = %s;') % (name, expr)
        use = name if form != 1 else '(*%s)' % name
        if form == 1 and expr.endswith('()'):
            return None
    pat = re.compile(r'(?<![\w.>])' + re.escape(expr) + r'(?![\w(])')
    end = len(L)
    for k in range(i, len(L)):
        if indent(L[k]) < indent(L[i]) and L[k].strip().startswith('}'):
            end = k
            break
    stop = i + 1 if rng.random() < 0.3 else end
    for k in range(at, stop):
        if not re.search(re.escape(expr) + r'\s*(=[^=]|\+=|-=|\*=|/=|\+\+|--)', L[k]):
            L[k] = pat.sub(use, L[k])
    L.insert(at, decl)
    return 'named temp %s = %s' % (name, expr)


def m_dead_local(L, rng, ctx):
    """Add or remove an unused local (frame size / slot layout: NOTES.md wave 5, `float[4]`)."""
    dead = [i for i, l in enumerate(L) if re.match(r'^\t(float|uint32|LTVector) (unused\w*)(\[\d+\])?;$', l)]
    if dead and rng.random() < 0.5:
        del L[rng.choice(dead)]
        return 'remove dead local'
    n = ctx.setdefault('ndead', 0)
    ctx['ndead'] = n + 1
    d = rng.choice(['float unused%d;', 'uint32 unused%d;', 'float unused%d[2];', 'float unused%d[3];',
                    'float unused%d[4];', 'LTVector unused%d;']) % n
    ds = [i for i, l in enumerate(L) if is_decl(l) and indent(l) == 1]
    L.insert(rng.choice(ds + [0]) if ds else 0, '\t' + d)
    return 'dead local %s' % d


def m_decl_hoist(L, rng, ctx):
    """Move a declaration from an inner block to the top of the function (splitting off its initialiser)."""
    c = [i for i, l in enumerate(L) if indent(l) >= 2 and (is_decl(l) or (DECL_INIT.match(l) and
         DECL_INIT.match(l).group(2).split()[-1] not in NOT_TYPE and '&' not in DECL_INIT.match(l).group(3)))]
    if not c:
        return None
    i = rng.choice(c)
    m = DECL_INIT.match(L[i])
    if m and not is_decl(L[i]):
        ind, typ, ptr, name, expr = m.groups()
        L[i] = '%s%s = %s;' % (ind, name, expr)
        L.insert(0, '\t%s %s%s;' % (typ, ptr, name))
        return 'hoist decl %s (init stays)' % name
    d = L.pop(i)
    L.insert(0, '\t' + d.strip())
    return 'hoist decl %r' % d.strip()[:30]


def m_else_form(L, rng, ctx):
    """`if (c) { ...; return/break/continue; } else { B }` <-> the same without the else."""
    c = []
    for i, l in enumerate(L):
        if not re.match(r'^if\s*\(.*\)$', l.strip()):
            continue
        s1 = stmt_span(L, i + 1)
        if not s1:
            continue
        last = L[s1[1] - 2].strip() if L[s1[1] - 1].strip() == '}' else L[s1[1] - 1].strip()
        if not re.match(r'^(return\b.*|break|continue|goto \w+);$', last) and not last.startswith(('RETURN_ERROR', 'ERR(')):
            continue
        j = s1[1]
        if j < len(L) and L[j].strip() == 'else' and indent(L[j]) == indent(l):
            s2 = stmt_span(L, j + 1)
            if s2:
                c.append(('drop', i, j, s2))
        else:
            # the rest of the enclosing block becomes the else body
            e = j
            while e < len(L) and not (L[e].strip().startswith('}') and indent(L[e]) < indent(l)):
                e += 1
            if e > j and any(x.strip() for x in L[j:e]) and not any(is_decl(x) or DECL_INIT.match(x) for x in L[j:e] if indent(x) == indent(l)):
                c.append(('add', i, j, (j, e)))
    if not c:
        return None
    k, i, j, s2 = rng.choice(c)
    ind = '\t' * indent(L[i])
    if k == 'drop':
        body = L[s2[0]:s2[1]]
        if body and body[0].strip() == '{':
            body = [x[1:] if x.startswith('\t') else x for x in body[1:-1]]
        else:
            body = [x[1:] if x.startswith('\t') else x for x in body]
        L[j:s2[1]] = body
        return 'drop else after terminating if'
    body = [x for x in L[s2[0]:s2[1]]]
    while body and not body[0].strip():
        body.pop(0)
    while body and not body[-1].strip():
        body.pop()
    L[s2[0]:s2[1]] = [ind + 'else', ind + '{'] + ['\t' + x for x in body] + [ind + '}']
    return 'add else after terminating if'


def m_tail_dup(L, rng, ctx):
    """Copy the statement after an if/else into both branches, or pull a common last statement out."""
    c = []
    for i, l in enumerate(L):
        if not re.match(r'^if\s*\(.*\)$', l.strip()) or i + 1 >= len(L) or L[i + 1].strip() != '{':
            continue
        e1 = block_end(L, i + 1)
        if e1 is None or e1 + 2 >= len(L) or L[e1 + 1].strip() != 'else' or L[e1 + 2].strip() != '{':
            continue
        e2 = block_end(L, e1 + 2)
        if e2 is None:
            continue
        if e2 + 1 < len(L) and is_simple(L[e2 + 1]) and indent(L[e2 + 1]) == indent(l) and not is_decl(L[e2 + 1]):
            c.append(('dup', i, e1, e2))
        if is_simple(L[e1 - 1]) and L[e1 - 1].strip() == L[e2 - 1].strip() and not L[e1 - 1].strip().startswith(('return', 'break', 'continue')):
            c.append(('merge', i, e1, e2))
    if not c:
        return None
    k, i, e1, e2 = rng.choice(c)
    if k == 'dup':
        s = L.pop(e2 + 1)
        L.insert(e2, '\t' + s)
        L.insert(e1, '\t' + s)
        return 'tail duplicate %r' % s.strip()[:30]
    s = L[e1 - 1]
    del L[e2 - 1]
    del L[e1 - 1]
    L.insert(e2 - 1, s[1:])
    return 'tail merge %r' % s.strip()[:30]


def m_cast(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        if l.strip().startswith('//'):
            continue
        for m in re.finditer(r'\((float|int|uint32|uint8|uint16|char|LTBOOL|int32|short|long)\)(?=[\w(])', l):
            c.append((i, m, 'del'))
        for m in re.finditer(r'(?<== )(%s)(?=;| [+\-*/])' % MEMB, l):
            c.append((i, m, 'add'))
    if not c:
        return None
    i, m, k = rng.choice(c)
    if k == 'del':
        if rng.random() < 0.5:
            L[i] = L[i][:m.start()] + L[i][m.end():]
            return 'drop cast %s' % m.group(0)
        new = '(%s)' % rng.choice(['float', 'int', 'uint32', 'uint8', 'uint16', 'char', 'long'])
        L[i] = L[i][:m.start()] + new + L[i][m.end():]
        return 'cast %s -> %s' % (m.group(0), new)
    new = '(%s)%s' % (rng.choice(['float', 'int', 'uint32', 'uint8', 'uint16', 'char']), m.group(1))
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'add cast %s' % new[:30]


def m_logic_swap(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t*(?:else )?(?:if|while)\s*\()([^&|]+?) (&&|\|\|) ([^&|]+)(\))$', l)
        if m and m.group(2).count('(') == m.group(2).count(')') and m.group(4).count('(') == m.group(4).count(')'):
            c.append((i, m))
    if not c:
        return None
    i, m = rng.choice(c)
    if rng.random() < 0.5 and m.group(3) == '&&' and m.group(1).strip().startswith('if') and L[i + 1].strip() != '{':
        return None
    L[i] = m.group(1) + m.group(4) + ' ' + m.group(3) + ' ' + m.group(2) + m.group(5)
    return 'swap %s operands' % m.group(3)


def m_nest_and(L, rng, ctx):
    """`if (a && b) S` <-> `if (a) if (b) S` (only without an else)."""
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t*)if\s*\(([^&|]+?) && ([^&|]+)\)$', l)
        if m and m.group(2).count('(') == m.group(2).count(')'):
            s = stmt_span(L, i)
            s1 = stmt_span(L, i + 1)
            if s and s1 and s[1] == s1[1]:      # no else
                c.append((i, m, s1))
    if not c:
        return None
    i, m, s1 = rng.choice(c)
    ind = m.group(1)
    body = L[s1[0]:s1[1]]
    L[i:s1[1]] = [ind + 'if (%s)' % m.group(2), ind + '{', ind + '\tif (%s)' % m.group(3)] + ['\t' + x for x in body] + [ind + '}']
    return 'nest && into two ifs'


def m_return_form(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t+)return (.+?) \? (.+?) : (.+);$', l)
        if m:
            c.append(('t', i, m))
        m = re.match(r'^(\t+)if\s*\((.*)\)$', l)
        if m and i + 2 < len(L):
            a = re.match(r'^\t+return (.+);$', L[i + 1])
            b = re.match(r'^\t+return (.+);$', L[i + 2]) if indent(L[i + 2]) == indent(l) else None
            if a and b:
                c.append(('i', i, (m, a, b)))
    if not c:
        return None
    k, i, m = rng.choice(c)
    if k == 't':
        ind, cond, a, b = m.groups()
        L[i:i + 1] = [ind + 'if (%s)' % cond, ind + '\treturn %s;' % a, ind + 'return %s;' % b]
        return 'return ternary -> if'
    mi, a, b = m
    L[i:i + 3] = ['%sreturn (%s) ? %s : %s;' % (mi.group(1), mi.group(2), a.group(1), b.group(1))]
    return 'return if -> ternary'


# ---------------------------------------------------------------- wave 8 mutations (the user's hand-pass levers)
# Several need the type of an expression. VC6 has no typeof, so the types come from the compiler itself: a probe
# compile rewrites one occurrence per line to `(*(__tpT**)0 = (E), (E))` and reads the type out of the C2440/C2679
# error (Target.probe_types). Types are cached per expression text; a mutator that meets an unknown expression
# queues it (ctx['pending']) and gives up; the main loop probes the queue against the current body between batches.

KEYWORDS = NOT_TYPE | {'sizeof', 'this', 'switch', 'default', 'const', 'static', 'struct', 'class', 'unsigned', 'signed',
                       'int', 'float', 'char', 'short', 'long', 'double', 'void', 'operator', 'inline', 'virtual',
                       'typedef', 'enum', 'union', 'register', 'volatile', 'true', 'false'}
PATHRE = re.compile(r'(?<![\w.>:])([A-Za-z_]\w*)((?:(?:->|\.)[A-Za-z_]\w*|\[[^\[\]]*\])+)')
SEGRE = re.compile(r'(->|\.)([A-Za-z_]\w*)|\[[^\[\]]*\]')
ASSIGN_AFTER = r'\s*(?:=(?!=)|\+=|-=|\*=|/=|\|=|&=|\^=|<<=|>>=|\+\+|--)'
INTWORDS = {'char', 'short', 'int', 'long', 'unsigned', 'signed', 'bool', '__int64', 'uint32', 'int32', 'uint16',
            'int16', 'uint8', 'int8', 'LTBOOL', 'BOOL', 'DWORD', 'WORD', 'BYTE'}


def code_part(l):
    """The line without a trailing // comment (good enough: no string literal holds `//` in these bodies)."""
    k = l.find('//')
    return l if k < 0 else l[:k]


def norm_type(t):
    t = re.sub(r'\b(class|struct|union) ', '', t)
    enum = 'enum ' in t
    t = re.sub(r'\benum ', '', t).strip()
    t = t.replace('_CVector<float>', 'LTVector').replace('unsigned long', 'uint32')
    t = t.replace('unsigned short', 'uint16').replace('unsigned char', 'uint8')
    t = re.sub(r'>>', '> >', t)
    t = re.sub(r'\s+\*', '*', t).replace('*', ' *').replace(' * *', ' **').strip()
    return t, enum


def type_kind(t, enum=False):
    if t is None:
        return None
    if '[' in t or '(' in t or "'" in t or '`' in t or '__unnamed' in t or 'anonymous' in t:
        return 'array' if '[' in t else 'bad'
    b = re.sub(r'\bconst\b', '', t).strip()
    if b.endswith('*'):
        return 'ptr'
    if b.endswith('&'):
        return 'bad'
    if b in ('float', 'double'):
        return 'float'
    if enum or all(w in INTWORDS for w in b.split()):
        return 'int'
    if b == 'LTVector':
        return 'vec'
    return 'class'


def parse_probe_type_diagnostic(line):
    """Return (source line number, probed expression type) for a VC6 assignment probe diagnostic.

    C2440 is localized in some VC6 installs, so use its stable code and the quoted assignment
    operand/types rather than the English "cannot convert from" wording.  C2679 accepts both
    English right-hand-operand diagnostics and localized diagnostics with quoted operand types.
    """
    loc = re.search(r'\((\d+)\)\s*:\s*error\s+(C2440|C2679)\s*:', line)
    if not loc:
        return None
    line_no, code = int(loc.group(1)), loc.group(2)
    message = line[loc.end():]

    if code == 'C2440':
        if not re.match(r"\s*'='\s*:", message):
            return None
        quoted = re.findall(r"'([^']*)'", message)
        if len(quoted) < 3 or quoted[0] != '=':
            return None
        source_type = quoted[1].strip()
        has_probe_destination = any(
            re.search(r'__tpT\s*\*\s*$', destination)
            for destination in quoted[2:]
        )
        if not source_type or not has_probe_destination:
            return None
        return line_no, source_type

    english = re.match(r"\s*binary\s+'='\s*:", message)
    if english:
        right_type = re.search(
            r"right-hand operand of type\s+'([^']+)'", message
        )
        if right_type:
            return line_no, right_type.group(1).strip()

    # Some VC6 locales translate the C2679 prose but retain the quoted operator and type.
    quoted = re.findall(r"'([^']*)'", message)
    if len(quoted) >= 2 and quoted[0] == '=' and quoted[1].strip():
        return line_no, quoted[1].strip()
    return None


def gettype(ctx, expr):
    """(type text, kind) from the probe cache, or None (unknown types are queued for the next probe)."""
    tc = ctx.setdefault('types', {})
    if expr in tc:
        return tc[expr]
    ctx.setdefault('pending', set()).add(expr)
    return None


def path_occurrences(L, with_index=False):
    """Member paths in the body: list of (line, start, end, path text, root) for every prefix that ends in a
    member (not followed by a call). Index segments end the path unless with_index."""
    out = []
    for i, l in enumerate(L):
        c = code_part(l)
        if not c.strip() or c.strip().startswith('#'):
            continue
        for m in PATHRE.finditer(c):
            root = m.group(1)
            if root in KEYWORDS or root[0].isdigit():
                continue
            pos = m.start(2)
            for s in SEGRE.finditer(c, pos, m.end()):
                if s.start() != pos:
                    break
                pos = s.end()
                if not s.group(1):
                    if not with_index:
                        break
                    continue
                if c[pos:pos + 1] == '(' or c[pos:].lstrip().startswith('('):
                    break
                out.append((i, m.start(), pos, c[m.start():pos], root))
    return out


def stmt_starts(L):
    """Lines a new statement may be inserted before (at that line's indentation)."""
    out = []
    prev = None
    for k, l in enumerate(L):
        t = l.strip()
        if not t or t.startswith('//'):
            continue
        ok = not t.startswith(('else', '{', '}', '#', 'case ', 'default')) and not re.match(r'^\w+:$', t)
        if ok and prev is not None:
            p = code_part(L[prev]).strip()
            ok = p.endswith((';', '{', '}')) or re.match(r'^\w+:$', p) or p.startswith(('case ', 'default'))
        if ok:
            out.append(k)
        prev = k
    return out


def open_of(L, k):
    """Index of the `{` line of the innermost block containing line k (-1: the function body)."""
    ind = indent(L[k]) if L[k].strip() else 99
    for j in range(k - 1, -1, -1):
        if L[j].strip() == '{' and indent(L[j]) < ind:
            return j
    return -1


def block_range(L, o):
    """(first, end) lines of the block whose `{` is at o (end = index of its `}`; o=-1: the whole body)."""
    if o < 0:
        return 0, len(L)
    e = block_end(L, o)
    return o + 1, (e if e is not None else len(L))


def encloses(L, o, k):
    a, b = block_range(L, o)
    return a <= k < b


def stmt_start_of(L, i):
    """The statement start a statement containing line i can be preceded by (walks up continuation lines and
    brace-less control headers)."""
    ss = set(stmt_starts(L))
    k = i
    while k > 0 and k not in ss:
        k -= 1
    return k if k in ss else None


def writes(l, expr):
    c = code_part(l)
    return bool(re.search(r'(?<![\w.>])' + re.escape(expr) + ASSIGN_AFTER, c) or
                re.search(r'(?:\+\+|--)' + re.escape(expr) + r'\b', c))


def fresh_name(ctx, L, base):
    words = ctx.get('words', set()) | set(re.findall(r'[A-Za-z_]\w*', '\n'.join(L)))
    base = re.sub(r'\W', '', base) or 'tmp'
    if base[0].isdigit():
        base = 'v' + base
    n, k = base, 2
    while n in words or n in KEYWORDS:
        n = '%s%d' % (base, k)
        k += 1
    return n


def member_base(path):
    last = re.split(r'->|\.', path)[-1]
    b = re.sub(r'^m_', '', last)
    return b or 'x'


def local_name(prefix, path):
    """Hungarian-ish local name for a member: m_pSetup -> pSetup, m_fTime -> fTime, x -> fX."""
    b = member_base(path)
    if re.match(r'^(p|f|n|v|b|i|dw|h)[A-Z]', b):
        return b
    return prefix + cap(b) if prefix else b[0].lower() + b[1:]


def cap(s):
    return s[:1].upper() + s[1:]


def decl_line_of(L, name):
    """(line, type, declarator) of the declaration of a local, or None."""
    for i, l in enumerate(L):
        m = DECL.match(l)
        if m and m.group(2).split()[-1] not in NOT_TYPE and '(' not in l:
            for d in m.group(3).split(','):
                if re.sub(r'[*&\s]|\[\w*\]', '', d) == name:
                    return i, m.group(2), d.strip()
        m = DECL_INIT.match(l)
        if m and m.group(4) == name and m.group(2).split()[-1] not in NOT_TYPE:
            return i, m.group(2), m.group(3) + name
    return None


def insertion_points(L, f, after=-1):
    """Statement starts k <= f whose block encloses line f (a declaration there is in scope at f), after line
    `after`."""
    out = []
    for k in stmt_starts(L):
        if after < k <= f and encloses(L, open_of(L, k), f):
            out.append(k)
    return out


def m_refparam(L, rng, ctx):
    """A reference or pointer local for a member path (`CMoArray<X> &a = p->m_World.m_X;`, `T *pX = &p->m_X;`),
    declared at a chosen statement before its first use; later uses in that block rewritten to it."""
    occ = [o for o in path_occurrences(L) if not is_decl(L[o[0]])]
    if not occ:
        return None
    i, _, _, path, root = rng.choice(occ)
    ty = gettype(ctx, path)
    if not ty:
        return None
    t, kind = ty
    if kind in (None, 'bad', 'array'):
        return None
    if kind in ('float', 'int') and rng.random() < 0.6:     # the levers found so far were arrays, vectors, pointers
        return None
    # the first use of the path in the block chain; the root must not be (re)assigned between the declaration and it
    first = min(o[0] for o in occ if o[3] == path)
    last_w = max([k for k in range(first) if writes(L[k], root) or (DECL_INIT.match(L[k]) and DECL_INIT.match(L[k]).group(4) == root)
                  or (is_decl(L[k]) and re.search(r'\b%s\b' % re.escape(root), L[k]))] or [-1])
    pts = insertion_points(L, first, last_w)
    if not pts:
        return None
    k = pts[-1] if rng.random() < 0.4 else rng.choice(pts)
    a, b = block_range(L, open_of(L, k))
    form = 'ref' if kind == 'ptr' or rng.random() < 0.6 else 'ptr'
    base = member_base(path)
    name = fresh_name(ctx, L, local_name('', path) if form == 'ref' else 'p' + cap(base))
    sp = '' if t.endswith('*') else ' '
    decl = '\t' * indent(L[k]) + ('%s%s&%s = %s;' % (t, sp, name, path) if form == 'ref' else '%s%s*%s = &%s;' % (t, sp, name, path))
    pat = re.compile(r'(?<![\w.>])(&\s*)?' + re.escape(path) + r'(?![\w(]|\s*\()')
    stop = b
    if rng.random() < 0.2:
        uses = [j for j in range(k, b) if pat.search(code_part(L[j]))]
        if len(uses) > 1:
            stop = rng.choice(uses[1:])
    n = 0
    for j in range(k, stop):
        if writes(L[j], root) and j != k:
            break
        def rep(m):
            if form == 'ref':
                return (m.group(1) or '') + name
            if not m.group(1):
                return '(*%s)' % name       # P -> (*pX); (*pX).m -> pX->m below
            rest = L[j][m.end():].lstrip()
            if rest.startswith(('[', '.', '->')):
                return m.group(1) + '(*%s)' % name      # &P[i] / &P.m: the & belongs to the longer path
            return name     # &P -> pX
        new = pat.sub(rep, L[j])
        if form == 'ptr':
            new = new.replace('(*%s).' % name, '%s->' % name)
        if new != L[j]:
            n += 1
            L[j] = new
    if not n:
        return None
    L.insert(k, decl)
    return '%s %s for %s (%d lines)' % (form, name, path, n)


def first_operand(c):
    """(start, end) of the first operand to read early in a statement: the right-hand side of an assignment,
    the condition of an if/while, else the whole statement."""
    t = c.strip()
    m = re.match(r'^(?:else\s+)?(?:if|while)\s*\(', t)
    off = len(c) - len(c.lstrip())
    if m:
        return off + m.end(), len(c)
    m = re.search(r'(?<![=!<>+\-*/|&^])=(?!=)', c)
    if m:
        return m.end(), len(c)
    return 0, len(c)


def m_firstop(L, rng, ctx):
    """Read the first operand of a statement into a local before it (`float fX = pPos->x;`), uses rewritten."""
    c = []
    for i, l in enumerate(L):
        cc = code_part(l)
        if not cc.strip() or cc.strip().startswith(('for', '#', 'case', 'return;')):
            continue
        a, b = first_operand(cc)
        occ = [o for o in path_occurrences([cc], with_index=True) if a <= o[1] < b]
        occ = [o for o in occ if not re.match(ASSIGN_AFTER, cc[o[2]:]) and not cc[:o[1]].rstrip().endswith('&')]
        if occ:
            c.append((i, occ))
    if not c:
        return None
    i, occ = rng.choice(c)
    # the leftmost complete path is the "first operand"; sometimes any one
    leftmost = [o for o in occ if o[1] == min(x[1] for x in occ)]
    o = max(leftmost, key=lambda x: x[2]) if rng.random() < 0.7 else rng.choice(occ)
    _, s, e, path, root = o
    a_, b_ = first_operand(code_part(L[i]))
    if code_part(L[i])[a_:b_].strip().rstrip(';').strip() == path:
        return None     # `x = P;` is already a read into a local
    ty = gettype(ctx, path)
    if not ty:
        return None
    t, kind = ty
    if kind not in ('float', 'int', 'ptr', 'vec'):
        return None
    st = stmt_start_of(L, i)
    if st is None:
        return None
    if re.search(r'\[', path):
        idx = re.findall(r'\[([^\]]*)\]', path)
        lw = max([k for k in range(i) if any(re.search(r'\b%s\b' % re.escape(x.strip()), L[k]) and writes(L[k], x.strip()) for x in idx if x.strip().isidentifier())] or [-1])
    else:
        lw = -1
    lw = max([lw] + [k for k in range(i) if writes(L[k], root) or writes(L[k], path)])
    pts = insertion_points(L, st, lw)
    if not pts:
        return None
    k = st if (st in pts and rng.random() < 0.7) else rng.choice(pts)
    pre = {'float': 'f', 'int': 'n', 'ptr': 'p', 'vec': 'v'}[kind]
    name = fresh_name(ctx, L, local_name(pre, path))
    ind = '\t' * indent(L[k])
    pat = re.compile(r'(?<![\w.>])' + re.escape(path) + r'(?![\w\[]|\s*(?:->|\.)?\s*\w*\s*\()')
    # rewrite: this occurrence only, or every later read in the block until the path or its root is written
    L[i] = L[i][:s] + name + L[i][e:]
    n = 1
    if rng.random() < 0.5:
        _, b = block_range(L, open_of(L, k))
        for j in range(i + 1, b):
            if writes(L[j], path) or writes(L[j], root):
                break
            new = pat.sub(name, L[j])
            if new != L[j]:
                L[j] = new
                n += 1
    if rng.random() < 0.3:      # declared at the top of the function, assigned at the statement
        L.insert(k, ind + '%s = %s;' % (name, path))
        top = 0
        while top < len(L) and (is_decl(L[top]) or not L[top].strip() or L[top].strip().startswith('//')):
            top += 1
        L.insert(top, '\t%s%s%s;' % (t, '' if t.endswith('*') else ' ', name))
        return '%s = %s (declared at the top, %d uses)' % (name, path, n)
    L.insert(k, ind + '%s%s%s = %s;' % (t, '' if t.endswith('*') else ' ', name, path))
    return '%s %s = %s (%d uses)' % (t, name, path, n)


FORHDR = re.compile(r'^(\t*)for\s*\(\s*([A-Za-z_]\w*)\s*=\s*([^;]*);([^;]*);(.*)\)\s*$')


def m_loopctr(L, rng, ctx):
    """Give a loop its own counter when the function reuses one counter variable for several loops."""
    loops = {}
    for i, l in enumerate(L):
        m = FORHDR.match(l)
        if m:
            loops.setdefault(m.group(2), []).append(i)
    c = [(v, i) for v, ii in loops.items() if len(ii) >= 2 for i in ii]
    if not c:
        return None
    v, i = rng.choice(c)
    sp = stmt_span(L, i)
    if not sp:
        return None
    pat = re.compile(r'\b%s\b' % re.escape(v))
    # the counter's value after the loop must be dead: its next use after the loop is an assignment
    for j in range(sp[1], len(L)):
        if pat.search(code_part(L[j])):
            m = FORHDR.match(L[j])
            if not ((m and m.group(2) == v) or re.match(r'^\t+%s\s*=[^=]' % re.escape(v), L[j])):
                return None
            break
    d = decl_line_of(L, v)
    if not d:
        return None
    di, typ, declarator = d
    ptr = '*' * declarator.split(v)[0].count('*')
    name = None
    words = set(re.findall(r'[A-Za-z_]\w*', '\n'.join(L))) | ctx.get('words', set())
    for cand in ('j', 'k', 'n', v + '2', 'iLoop'):
        if cand not in words:
            name = cand
            break
    if not name:
        return None
    for j in range(sp[0], sp[1]):
        L[j] = pat.sub(name, L[j])
    form = rng.randrange(3)
    if form == 2 and not ptr:
        L[i] = re.sub(r'for\s*\(\s*', 'for (%s ' % typ, L[i], count=1)
        return 'loop counter %s for %s (declared in the for)' % (name, v)
    if form == 1 and is_decl(L[di]) and indent(L[di]) == 1:
        L[di] = L[di].rstrip()[:-1] + ', %s%s;' % (ptr, name)
        return 'loop counter %s for %s (in its declaration list)' % (name, v)
    L.insert(di + 1, '\t' * indent(L[di]) + '%s %s%s;' % (typ, ptr, name))
    return 'loop counter %s for %s' % (name, v)


def m_ptrstep(L, rng, ctx):
    """Pointer stepping <-> indexing in a loop: `p = arr; for (i=0; c; i++, p++) { ... }` <->
    `for (i=0; c; i++) { p = &arr[i]; ... }` (the older checklist's "pointer stepping"; sm_SendChangedLightAnims)."""
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t*)for\s*\(\s*(\w+)\s*=\s*0\s*;([^;]*);\s*(?:\2\+\+|\+\+\2)\s*,\s*(?:(\w+)\+\+|\+\+(\w+))\s*\)\s*$', l)
        if m and i + 1 < len(L) and L[i + 1].strip() == '{':
            p = m.group(4) or m.group(5)
            k = i - 1
            while k >= 0 and not L[k].strip():
                k -= 1
            mi = re.match(r'^\t*%s\s*=\s*(.+);\s*$' % re.escape(p), L[k]) if k >= 0 else None
            if mi and not re.search(r'\b%s\s*(?:=[^=]|\+\+|--|\+=|-=)' % re.escape(p), '\n'.join(L[i + 2:block_end(L, i + 1) or i + 2])):
                c.append(('index', i, (m, p, k, mi.group(1).strip())))
        m = re.match(r'^(\t*)for\s*\(\s*(\w+)\s*=\s*0\s*;([^;]*);\s*(?:\2\+\+|\+\+\2)\s*\)\s*$', l)
        if m and i + 2 < len(L) and L[i + 1].strip() == '{':
            mi = re.match(r'^\t*(\w+)\s*=\s*&(.+)\[%s\];\s*$' % re.escape(m.group(2)), L[i + 2])
            if mi:
                c.append(('step', i, (m, mi)))
    if not c:
        return None
    k, i, d = rng.choice(c)
    if k == 'index':
        m, p, pk, arr = d
        ind = m.group(1)
        if not re.match(r'^[\w.>\-]+$', arr):
            arr = '(%s)' % arr
        L[i] = '%sfor (%s=0;%s; %s++)' % (ind, m.group(2), m.group(3), m.group(2))
        L.insert(i + 2, ind + '\t%s = &%s[%s];' % (p, arr, m.group(2)))
        del L[pk]
        return 'index %s[%s] instead of stepping %s' % (arr[:30], m.group(2), p)
    m, mi = d
    ind = m.group(1)
    p, arr = mi.group(1), mi.group(2)
    del L[i + 2]
    L[i] = '%sfor (%s=0;%s; %s++, %s++)' % (ind, m.group(2), m.group(3), m.group(2), p)
    L.insert(i, ind + '%s = %s;' % (p, arr))
    return 'step %s through %s' % (p, arr[:30])


def negate(cond):
    cond = cond.strip()
    if cond.startswith('!('):
        d = 0
        for k in range(1, len(cond)):
            d += {'(': 1, ')': -1}.get(cond[k], 0)
            if d == 0:
                break
        if k == len(cond) - 1:      # `!( ... )` around the whole condition
            return cond[2:-1]
    if re.match(r'^!%s$' % OPND, cond):
        return cond[1:]
    if re.match(r'^%s$' % OPND, cond) or re.match(r'^%s\(\)$' % OPND, cond):
        return '!' + cond
    m = re.match(r'^(%s|[\w.>\-]+\(\)) (==|!=|<=|>=|<|>) (%s|[\w.>\-]+\(\))$' % (OPND, OPND), cond)
    if m:
        return '%s %s %s' % (m.group(1), CMPNEG[m.group(2)], m.group(3))
    return '!(%s)' % cond


def if_chain_span(L, i):
    """(i, end) of the whole if / else if / else chain starting with the `if` at line i."""
    t = L[i].strip()
    if not re.match(r'^if\s*\(', t):
        return None
    s = stmt_span(L, i + 1) if not t.endswith(';') else (i, i + 1)
    if not s:
        return None
    j = s[1]
    while j < len(L) and L[j].strip().startswith('else') and indent(L[j]) == indent(L[i]):
        t2 = L[j].strip()
        if t2 == 'else':
            s2 = stmt_span(L, j + 1)
            if not s2:
                return None
            return (i, s2[1])
        if re.match(r'^else\s+if\s*\(', t2):
            s2 = stmt_span(L, j + 1) if not t2.endswith(';') else (j, j + 1)
            if not s2:
                return None
            j = s2[1]
            continue
        if t2.endswith(';'):        # else stmt;
            return (i, j + 1)
        return None
    return (i, j)


def follow(L, o, depth=0):
    """What runs after the block opened at line o: ('ret', 'return X;'), ('end', None) for the end of the
    function, ('stmt', text) for anything else, or None (a loop/switch body, or not understood)."""
    if depth > 12:
        return None
    if o < 0:
        return ('end', None)
    e = block_end(L, o)
    if e is None:
        return None
    h = o - 1
    while h >= 0 and (not L[h].strip() or L[h].strip().startswith('//')):
        h -= 1
    ht = L[h].strip() if h >= 0 else ''
    if re.match(r'^(for|while|do|switch)\b', ht) or ht.endswith(':'):
        return None
    if re.match(r'^(if|else)\b', ht):
        k = h
        while k >= 0:
            if indent(L[k]) == indent(L[h]) and re.match(r'^if\s*\(', L[k].strip()):
                sp = if_chain_span(L, k)
                if sp and sp[0] <= h and sp[1] > e:
                    nxt = sp[1]
                    break
            if L[k].strip() and indent(L[k]) < indent(L[h]):
                return None
            k -= 1
        else:
            return None
    else:
        nxt = e + 1
    while nxt < len(L) and (not L[nxt].strip() or L[nxt].strip().startswith('//')):
        nxt += 1
    if nxt >= len(L):
        return ('end', None)
    t = L[nxt].strip()
    if t.startswith('}'):
        return follow(L, open_of(L, nxt), depth + 1) if open_of(L, nxt) >= -1 else None
    if t.startswith('return'):
        return ('ret', t)
    return ('stmt', t)


def decls_in(lines):
    out = set()
    for l in lines:
        m = DECL.match(l)
        if m and m.group(2).split()[-1] not in NOT_TYPE and '(' not in l:
            out |= {re.sub(r'[*&\s]|\[\w*\]', '', d) for d in m.group(3).split(',')}
        m = DECL_INIT.match(l)
        if m and m.group(2).split()[-1] not in NOT_TYPE:
            out.add(m.group(4))
    return out


def m_wrapret(L, rng, ctx):
    """`if (c) return R; rest...` <-> `if (!c) { rest... }` (+ `return R;` when needed) inside a block: the
    early-return form pulls callee-saved pushes into the prologue, the wrapped form keeps the lazy push."""
    void = ctx.get('void', False)
    c = []
    for i, l in enumerate(L):
        t = code_part(l).strip()
        m = re.match(r'^if\s*\((.*)\)\s*(return\b[^;]*;)?$', t)
        if not m:
            continue
        cond = m.group(1)
        if cond.count('(') != cond.count(')'):
            continue
        ind = indent(l)
        # guard: the if's body is just a return
        if m.group(2):
            ret, gend = m.group(2), i + 1
        elif i + 1 < len(L) and re.match(r'^return\b[^;]*;$', code_part(L[i + 1]).strip()) and indent(L[i + 1]) == ind + 1:
            ret, gend = code_part(L[i + 1]).strip(), i + 2
        elif i + 3 < len(L) and L[i + 1].strip() == '{' and re.match(r'^return\b[^;]*;$', code_part(L[i + 2]).strip()) \
                and L[i + 3].strip() == '}':
            ret, gend = code_part(L[i + 2]).strip(), i + 4
        else:
            ret = None
        if ret:
            if gend < len(L) and L[gend].strip().startswith('else'):
                continue
            o = open_of(L, i)
            a, b = block_range(L, o)
            rest = L[gend:b]
            if not any(x.strip() and not x.strip().startswith('//') for x in rest):
                continue
            f = follow(L, o)
            lastst = [x for x in rest if x.strip() and not x.strip().startswith('//')][-1].strip()
            ok_drop = f == ('ret', ret) or (f == ('end', None) and void and ret == 'return;')
            ok_keep = ok_drop or lastst.startswith(('return', 'RETURN_ERROR'))
            if ok_keep and not (ret != 'return;' and decls_in(rest) & set(re.findall(r'\w+', ret))):
                c.append(('wrap', i, (cond, ret, gend, b, ok_drop, ok_keep, ind)))
            continue
        # wrapped form: `if (c) { body }` (no else) as the last statement of its block, maybe + `return R;`
        if i + 1 < len(L) and L[i + 1].strip() == '{':
            e = block_end(L, i + 1)
            if e is None or (e + 1 < len(L) and L[e + 1].strip().startswith('else')):
                continue
            o = open_of(L, i)
            a, b = block_range(L, o)
            tail = [x for x in L[e + 1:b] if x.strip() and not x.strip().startswith('//')]
            body = L[i + 2:e]
            if decls_in(body) & decls_in(L[:i + 1] + L[e + 1:]):
                continue
            if len(tail) == 1 and re.match(r'^return\b[^;]*;$', code_part(tail[0]).strip()):
                c.append(('unwrap', i, (cond, code_part(tail[0]).strip(), e, b, True)))
            elif not tail:
                f = follow(L, o)
                if f and f[0] == 'ret':
                    c.append(('unwrap', i, (cond, f[1], e, b, False)))
                elif f == ('end', None) and void:
                    c.append(('unwrap', i, (cond, 'return;', e, b, False)))
    if not c:
        return None
    k, i, d = rng.choice(c)
    if k == 'wrap':
        cond, ret, gend, b, ok_drop, ok_keep, ind = d
        rest = L[gend:b]
        while rest and not rest[-1].strip():
            rest.pop()
        keep = (not ok_drop) or rng.random() < 0.3
        sp = '\t' * ind
        new = [sp + 'if (%s)' % negate(cond), sp + '{'] + [('\t' + x) if x.strip() else x for x in rest] + [sp + '}']
        if keep:
            new.append(sp + ret)
        L[i:gend + len(rest)] = new
        return 'wrap early return (if (%s) %s)%s' % (cond[:30], ret[:20], ' + return' if keep else '')
    cond, ret, e, b, has_ret = d
    ind = '\t' * indent(L[i])
    body = [x[1:] if x.startswith('\t') else x for x in L[i + 2:e]]
    new = [ind + 'if (%s)' % negate(cond), ind + '\t' + ret] + body
    if has_ret:
        r = e + 1
        while r < b and not (L[r].strip() and not L[r].strip().startswith('//')):
            r += 1
        L[i:r + 1] = new
    else:
        L[i:e + 1] = new
    return 'unwrap into early return (if (%s) %s)' % (negate(cond)[:30], ret[:20])


CASTF = re.compile(r'\(float\)\s*(%s|\([^()]*\))' % OPND)


def m_fdiv(L, rng, ctx):
    """`x / (float)g` <-> `x / (g * 1.0f)`, an int divisor made float, and int-to-float conversions through a
    float local."""
    c = []
    for i, l in enumerate(L):
        cc = code_part(l)
        if not cc.strip() or is_decl(l):
            continue
        for m in CASTF.finditer(cc):
            div = cc[:m.start()].rstrip().endswith('/')
            c.append((i, m, 'mul' if div or rng.random() < 0.3 else 'local'))
        for m in re.finditer(r'\((%s) \* 1\.0f\)' % OPND, cc):
            c.append((i, m, 'cast'))
        for m in re.finditer(r'(?<=/ )(%s)(?![\w.\[(]|->)' % OPND, cc):
            ty = gettype(ctx, m.group(1)) if not m.group(1)[0].isdigit() else None
            if ty and ty[1] == 'int':
                c.append((i, m, 'intdiv'))
    if not c:
        return None
    i, m, k = rng.choice(c)
    if k == 'mul':
        L[i] = L[i][:m.start()] + '(%s * 1.0f)' % m.group(1) + L[i][m.end():]
        return '(float)%s -> (%s * 1.0f)' % (m.group(1), m.group(1))
    if k == 'cast':
        L[i] = L[i][:m.start()] + '(float)%s' % m.group(1) + L[i][m.end():]
        return '(%s * 1.0f) -> (float)%s' % (m.group(1), m.group(1))
    if k == 'intdiv':
        new = rng.choice(['(float)%s', '(%s * 1.0f)']) % m.group(1)
        L[i] = L[i][:m.start()] + new + L[i][m.end():]
        return 'int divisor %s -> %s' % (m.group(1), new)
    st = stmt_start_of(L, i)
    if st is None:
        return None
    name = fresh_name(ctx, L, 'f' + cap(member_base(m.group(1).strip('()'))))
    init = rng.choice(['(float)%s', '%s'])
    L[i] = L[i][:m.start()] + name + L[i][m.end():]
    L.insert(st, '\t' * indent(L[st]) + 'float %s = %s;' % (name, init % m.group(1)))
    return 'float local %s for (float)%s' % (name, m.group(1))


CONSTV = r'(?:-?\d[\w.]*|[A-Z][A-Z0-9_]+|LTNULL|NULL|LTTRUE|LTFALSE|TRUE|FALSE)'
STORE = re.compile(r'^(\t+)(%s) = (%s);\s*$' % (r'[A-Za-z_]\w*(?:(?:->|\.)\w+|\[\w+\])+', CONSTV))
READ = re.compile(r'^(\t+)([A-Za-z_]\w*) = ([A-Za-z_]\w*(?:(?:->|\.)\w+|\[\w+\])+);\s*$')


def loops_enclosing(L, j, after):
    """(start, end) spans of the loops that start after line `after` and enclose line j."""
    out = []
    for k in range(after + 1, j):
        if re.match(r'^(for|while|do)\b', L[k].strip()) and not L[k].strip().endswith(';'):
            sp = stmt_span(L, k)
            if sp and sp[0] < j < sp[1]:
                out.append(sp)
    return out


def m_redstore(L, rng, ctx):
    """Re-insert a redundant store of the same constant to the same lvalue (or a re-read of a member into the
    same local) later in the flow, with no write to it in between; or drop such a repeat."""
    c = []
    ss = stmt_starts(L)
    for i, l in enumerate(L):
        m = STORE.match(l)
        r = READ.match(l) if not m else None
        if not (m or r):
            continue
        lv = m.group(2) if m else r.group(2)
        root = re.match(r'\w+', lv).group(0)
        watch = [lv, root] + ([r.group(3), re.match(r'\w+', r.group(3)).group(0)] if r else [])
        o = open_of(L, i)
        _, b = block_range(L, o)
        for j in ss:
            if not (i < j < b):
                continue
            span = list(range(i + 1, j))
            for sp in loops_enclosing(L, j, i):
                span += range(sp[0], sp[1])
            if any(writes(L[k], w) for k in span for w in watch):
                break
            if L[j].strip() == l.strip():
                c.append(('drop', i, j))
                continue
            if j != i + 1:
                c.append(('add', i, j))
    if not c:
        return None
    k, i, j = rng.choice(c)
    if k == 'drop':
        s = L.pop(j)
        return 'drop repeated %r' % s.strip()[:40]
    L.insert(j, '\t' * indent(L[j]) + L[i].strip())
    return 'repeat %r at %r' % (L[i].strip()[:40], L[j + 1].strip()[:30])


def top_split(expr, ops):
    """Positions of the top-level binary operators in `ops` (paren/bracket depth 0, not unary, not ->/++/+=)."""
    out, d, k = [], 0, 0
    while k < len(expr):
        ch = expr[k]
        if ch in '([':
            d += 1
        elif ch in ')]':
            d -= 1
        elif d == 0 and ch in ops:
            nx = expr[k + 1:k + 2]
            pv = expr[:k].rstrip()[-1:] if expr[:k].strip() else ''
            if nx in ('>', '=', ch) or expr[k - 1:k] == ch or not pv or not (pv.isalnum() or pv in '_)].'):
                k += 1
                continue
            out.append((k, ch))
        k += 1
    return out


VEC_IDIOMS = [
    (r'VEC_DISTSQR\((%s), (%s)\)' % (MEMB, MEMB), r'(\1 - \2).MagSqr()'),
    (r'\((%s) - (%s)\)\.MagSqr\(\)' % (MEMB, MEMB), r'VEC_DISTSQR(\1, \2)'),
    (r'(%s)\.DistSqr\((%s)\)' % (MEMB, MEMB), r'VEC_DISTSQR(\1, \2)'),
    (r'VEC_DISTSQR\((%s), (%s)\)' % (MEMB, MEMB), r'\1.DistSqr(\2)'),
    (r'VEC_DIST\((%s), (%s)\)' % (MEMB, MEMB), r'(\1 - \2).Mag()'),
    (r'VEC_MULSCALAR\((%s), (%s), ([^;]+)\);' % (MEMB, MEMB), r'\1 = \2 * \3;'),
    (r'^(\t+)(%s) = (%s) \* ([\w.]+);$' % (MEMB, MEMB), r'\1VEC_MULSCALAR(\2, \3, \4);'),
    (r'VEC_DIVSCALAR\((%s), (%s), ([^;]+)\);' % (MEMB, MEMB), r'\1 = \2 / \3;'),
    (r'VEC_ADDSCALED\((%s), (%s), (%s), ([^;]+)\);' % (MEMB, MEMB, MEMB), r'\1 = \2 + \3 * \4;'),
    (r'^(\t+)(%s) = (%s) \+ (%s) \* ([\w.]+);$' % (MEMB, MEMB, MEMB), r'\1VEC_ADDSCALED(\2, \3, \4, \5);'),
    (r'VEC_NEGATE\((%s), (%s)\);' % (MEMB, MEMB), r'\1 = -\2;'),
    (r'VEC_ADD\((%s), \1, (%s)\);' % (MEMB, MEMB), r'\1 += \2;'),
    (r'VEC_SUB\((%s), \1, (%s)\);' % (MEMB, MEMB), r'\1 -= \2;'),
    (r'^(\t+)(%s) \+= (%s);$' % (MEMB, MEMB), r'\1VEC_ADD(\2, \2, \3);'),
    (r'^(\t+)(%s) -= (%s);$' % (MEMB, MEMB), r'\1VEC_SUB(\2, \2, \3);'),
    (r'VEC_SUB\((%s), (%s), (%s)\);' % (MEMB, MEMB, MEMB), r'\1 = \2 - \3;'),
    (r'VEC_ADD\((%s), (%s), (%s)\);' % (MEMB, MEMB, MEMB), r'\1 = \2 + \3;'),
]
VEC_IDIOMS = [(re.compile(a, re.M), b) for a, b in VEC_IDIOMS]
ASSIGN = re.compile(r'^(\t+)(%s) = (.+);\s*$' % MEMB)


def m_vecnamed(L, rng, ctx):
    """VEC_* macro <-> LTVector operator form; a one-expression vector computation <-> a named temporary
    (`LTVector vEnd = a + b; pos = vEnd;`) or a split into `v = A; v += B;`."""
    c = []
    ss = set(stmt_starts(L))
    for i, l in enumerate(L):
        cc = code_part(l)
        if not cc.strip():
            continue
        multi = i in ss     # one statement may become two only where a statement may start (not an if's bare body)
        for k, (pat, rep) in enumerate(VEC_IDIOMS):
            for m in pat.finditer(cc):
                c.append(('idiom', i, (k, m)))
        m = re.match(r'^(\t+)VEC_(ADD|SUB)\((%s), (%s), (%s)\);\s*$' % (MEMB, MEMB, MEMB), cc)
        if m and multi:
            c.append(('mname', i, m))
        m = ASSIGN.match(cc)
        if m and not is_decl(l) and not m.group(3).startswith('='):
            lhs, rhs = m.group(2), m.group(3)
            ty = gettype(ctx, lhs)
            tops = top_split(rhs, '+-*')
            if ty and ty[1] == 'vec' and tops and multi:
                c.append(('name', i, m))
            if multi and tops and ty and ty[1] in ('vec', 'float') and not re.search(r'(?<![\w.>])%s(?![\w])' % re.escape(lhs), rhs):
                c.append(('split', i, (m, tops)))
        # reverse of 'name': `LTVector vN = E;` + `X = vN;` with vN used nowhere else
        m = re.match(r'^(\t+)LTVector (\w+) = (.+);$', cc)
        if m and i + 1 < len(L):
            m2 = re.match(r'^(\t+)(%s) = %s;$' % (MEMB, re.escape(m.group(2))), code_part(L[i + 1]))
            if m2 and sum(len(re.findall(r'\b%s\b' % re.escape(m.group(2)), x)) for x in L) == 2:
                c.append(('unname', i, (m, m2)))
        # reverse of 'split': `X = A;` + `X op= B;`
        m = ASSIGN.match(cc)
        if m and i + 1 < len(L):
            m2 = re.match(r'^(\t+)%s ([+\-*])= (.+);$' % re.escape(m.group(2)), code_part(L[i + 1]))
            if m2 and indent(L[i + 1]) == indent(l):
                c.append(('merge', i, (m, m2)))
    if not c:
        return None
    k, i, d = rng.choice(c)
    ind = '\t' * indent(L[i])
    if k == 'idiom':
        j, m = d
        pat, rep = VEC_IDIOMS[j]
        new = m.expand(rep)
        L[i] = L[i][:m.start()] + new + L[i][m.end():]
        return 'vec idiom %r -> %r' % (m.group(0).strip()[:30], new.strip()[:30])
    if k == 'name':
        m = d
        name = fresh_name(ctx, L, 'v' + cap(member_base(m.group(2))))
        L[i:i + 1] = [ind + 'LTVector %s = %s;' % (name, m.group(3)), ind + '%s = %s;' % (m.group(2), name)]
        return 'named vector %s = %s' % (name, m.group(3)[:30])
    if k == 'mname':
        m = d
        name = fresh_name(ctx, L, 'v' + cap(member_base(m.group(3))))
        expr = '%s %s %s' % (m.group(4), '+' if m.group(2) == 'ADD' else '-', m.group(5))
        L[i:i + 1] = [ind + 'LTVector %s = %s;' % (name, expr), ind + '%s = %s;' % (m.group(3), name)]
        return 'named vector %s = %s (was VEC_%s)' % (name, expr[:30], m.group(2))
    if k == 'unname':
        m, m2 = d
        L[i:i + 2] = [ind + '%s = %s;' % (m2.group(2), m.group(3))]
        return 'unname vector %s' % m.group(2)
    if k == 'split':
        m, tops = d
        lhs, rhs = m.group(2), m.group(3)
        adds = [t for t in tops if t[1] in '+-']
        if adds:
            pos, op = adds[-1] if rng.random() < 0.7 else rng.choice(adds)
            if pos != (adds[-1][0]) and op == '-':
                return None
        else:
            pos, op = tops[-1]
        a, b = rhs[:pos].strip(), rhs[pos + 1:].strip()
        if op == '+' and rng.random() < 0.4 and len(adds) == 1:
            a, b = b, a
        if op == '-' and pos != tops[-1][0] and any(t[1] in '+-' and t[0] > pos for t in tops):
            return None
        if op == '*' and any(t[1] in '+-' for t in top_split(a, '+-')):
            a = '(%s)' % a
        if re.match(r'^\((.*)\)$', b) and b.count('(') == 1:
            b = b[1:-1]
        L[i:i + 1] = [ind + '%s = %s;' % (lhs, a), ind + '%s %s= %s;' % (lhs, op, b)]
        return 'split %s = .. %s ..' % (lhs[:20], op)
    m, m2 = d
    a, op, b = m.group(3), m2.group(2), m2.group(3)
    if top_split(b, '+-') or (op == '-' and top_split(b, '+-*')):
        b = '(%s)' % b
    if op == '*' and top_split(a, '+-'):
        a = '(%s)' % a
    L[i:i + 2] = [ind + '%s = %s %s %s;' % (m.group(2), a, op, b)]
    return 'merge %s = .. %s= ..' % (m.group(2)[:20], op)


ZSTORE = re.compile(r'^(\t+)([A-Za-z_]\w*(?:(?:->|\.)\w+|\[\w+\])*) = (0|0\.0f|0\.0|0\.f|0\.0F);\s*(//.*)?$')


def ftype_probes(L, ctx):
    """The `ftype` probes for a body: [(label, member, new body lines)]. Integer-0 stores to an int member become
    `*(float*)&m = 0.0f;` and 0.0f stores to a float member `*(uint32*)&m = 0;`, one store at a time and all stores
    of one member name together. Never candidates: a better score only says a header member type may be wrong."""
    stores = []
    for i, l in enumerate(L):
        m = ZSTORE.match(l)
        if not m:
            continue
        ty = gettype(ctx, m.group(2))
        if not ty:
            continue
        if ty[1] == 'int' and m.group(3) == '0' and ('->' in m.group(2) or '.' in m.group(2)):
            stores.append((i, m, '%s*(float*)&%s = 0.0f;', 'int'))
        elif ty[1] == 'float':
            stores.append((i, m, '%s*(uint32*)&%s = 0;', 'float'))
    out = []
    groups = {}
    for i, m, fmt, kind in stores:
        mem = member_base(m.group(2)) if ('->' in m.group(2) or '.' in m.group(2)) else m.group(2)
        groups.setdefault((mem, kind), []).append((i, m, fmt))
        L2 = list(L)
        L2[i] = fmt % (m.group(1), m.group(2))
        out.append(('line %d: %s (%s)' % (i + 1, m.group(2), kind), (mem, kind), L2))
    for (mem, kind), ss in groups.items():
        if len(ss) > 1:
            L2 = list(L)
            for i, m, fmt in ss:
                L2[i] = fmt % (m.group(1), m.group(2))
            out.append(('all %d stores of %s (%s)' % (len(ss), mem, kind), (mem, kind), L2))
    return out


def header_ftype_sites(ctx, sig, incdir):
    """Integer-0 member stores in inline code (constructors, Init/Clear methods) of the classes this function uses
    (types of its locals/members/parameters, and its own class): [(header path, line index, member)]. The user's
    float-member finds (CUDPQuery::m_Unknown9C, MoveState::m_nRestart) were stores in header constructors."""
    classes = set()
    for v in ctx.get('types', {}).values():
        if v and v[1] in ('class', 'ptr'):
            n = re.sub(r'\bconst\b|[*&]', '', v[0]).strip().split('<')[0].strip()
            if re.match(r'^[A-Za-z_][\w:]*$', n):
                classes.add(n.split('::')[-1])
    for m in re.finditer(r'(\w+)::~?\w+\s*\(', sig):
        classes.add(m.group(1))
    for p in ctx.get('params', []):
        classes.add(re.sub(r'\bconst\b|[*&]', '', p[0]).strip())
    out = []
    for h in sorted(glob_headers(incdir)):
        text = open(h, encoding='latin1').read()
        lines = text.split('\n')
        for m in re.finditer(r'\b(class|struct)\s+(\w+)\b[^;{()]*\{', text):
            if m.group(2) not in classes:
                continue
            d, k = 0, m.end() - 1
            while k < len(text):
                d += {'{': 1, '}': -1}.get(text[k], 0)
                if d == 0:
                    break
                k += 1
            a, b = text.count('\n', 0, m.start()), text.count('\n', 0, k)
            for i in range(a, b + 1):
                mm = re.match(r'^(\s*)(m_\w+)\s*=\s*0\s*;', lines[i])
                if mm:
                    out.append((h, i, mm.group(2), m.group(2)))
    return out


def glob_headers(d):
    import glob
    return glob.glob(os.path.join(d, '*.h'))


def probe_exprs(L):
    """Expressions worth typing up front: member paths (with index segments), plain locals and globals used as
    operands (for the divisor / vector / zero-store tests)."""
    ex = {}
    for i, s, e, p, root in path_occurrences(L, with_index=True):
        if not is_decl(L[i]):
            ex.setdefault(p, i)
    names = decls_in(L)
    for i, l in enumerate(L):
        if is_decl(l) or DECL_INIT.match(l):
            continue
        for m in re.finditer(r'(?<![\w.>:])([A-Za-z_]\w*)\b(?!\s*(?:\(|::|->|\.))', code_part(l)):
            n = m.group(1)
            if (n in names or n.startswith('g_')) and n not in KEYWORDS:
                ex.setdefault(n, i)
    return ex


MUTATORS = {
    'refparam': (m_refparam, 3), 'firstop': (m_firstop, 3), 'loopctr': (m_loopctr, 2), 'wrapret': (m_wrapret, 3),
    'fdiv': (m_fdiv, 2), 'redstore': (m_redstore, 2), 'vecnamed': (m_vecnamed, 3), 'ptrstep': (m_ptrstep, 1),
    'idiom': (m_idiom, 4), 'temp': (m_named_temp, 4), 'dead': (m_dead_local, 1), 'hoist': (m_decl_hoist, 2),
    'elseform': (m_else_form, 3), 'taildup': (m_tail_dup, 2), 'cast': (m_cast, 1), 'logic': (m_logic_swap, 1),
    'nestand': (m_nest_and, 1), 'retform': (m_return_form, 1),
    'move': (m_move_stmt, 6), 'swapstmt': (m_swap_stmts_any, 3), 'swapdecl': (m_swap_decl, 5),
    'splitdecl': (m_split_multi_decl, 2), 'splitinit': (m_split_init, 3), 'decluse': (m_decl_to_use, 3),
    'declscope': (m_decl_scope, 2), 'inttype': (m_int_type, 2), 'commute': (m_commute, 4), 'commany': (m_commute_any, 2),
    'incdec': (m_incdec, 1), 'compound': (m_compound, 2), 'ifinv': (m_if_invert, 3), 'cmpform': (m_cmp_form, 2),
    'truth': (m_truth_form, 1), 'ternary': (m_ternary, 2), 'wrap': (m_block_wrap, 2), 'unwrap': (m_block_unwrap, 1),
    'paramcopy': (m_param_copy, 2), 'localsplit': (m_local_split, 2), 'loop': (m_loop_form, 2),
    'index': (m_index_form, 1), 'chain': (m_chain_assign, 2), 'assoc': (m_assoc, 1),
}


def mutate(body, rng, ctx, ops):
    L = body.split('\n')
    names = [n for n in ops for _ in range(MUTATORS[n][1])]
    done = []
    want = rng.choice((1, 1, 1, 1, 2, 2, 3))
    tries = 0
    while len(done) < want and tries < 40:
        tries += 1
        n = rng.choice(names)
        L0 = list(L)
        try:
            r = MUTATORS[n][0](L, rng, ctx)
        except Exception as e:      # a mutator tripped over unusual text: skip it (and undo a half-done edit)
            r = None
            L[:] = L0
            if ctx.get('debug'):
                import traceback
                traceback.print_exc()
        if r and '%s: %s' % (n, r) in ctx.get('bad', ()):      # this exact edit failed to compile alone before
            r = None
        if r:
            done.append('%s: %s' % (n, r))
        else:
            L[:] = L0
    return ('\n'.join(L), done) if done else (None, None)


def op_names(what):
    return [w.split(':', 1)[0] for w in what if ':' in w and w.split(':', 1)[0] in MUTATORS]


# ---------------------------------------------------------------- compile + score

class Target:
    def __init__(self, src, addr, outdir=None, include=None):
        import build
        self.include = [os.path.abspath(d) for d in (include or [])]
        self.build = build
        self.src = os.path.abspath(src)
        self.unit = build.Unit(self.src)
        self.va = resolve_function(self.unit, addr)
        self.annot = [a for a in self.unit.annots if a.va == self.va and a.kind != 'GLOBAL']
        if not self.annot:
            sys.exit('no FUNCTION/STUB annotation for %08x in %s' % (self.va, src))
        self.annot = self.annot[0]
        self.exe = build.Exe(build.EXE)
        ext = build.SymTab().funcs.get(self.va)
        if ext is None:
            sys.exit('%08x is not a function start in %s (the Ghidra extent is unknown; see config splits.csv)' % (
                self.va, os.path.basename(build.SYMBOLS_CSV)))
        self.exe_len = ext[0] - self.va
        self.target = self.exe.read(self.va, self.exe_len + 64)
        # private scratch directory of this module, never under src/: build/permute/<addr> (lithtech), build/d3dren/permute/<addr>
        self.dir = os.path.abspath(outdir) if outdir else os.path.join(build.BUILD, 'permute', '%08x' % self.va)
        os.makedirs(self.dir, exist_ok=True)
        self.symname = None
        self.last_error = None
        # known names -> va (build/namemap.json of the module): relocation targets are checked against them, so a candidate
        # that swaps two calls or two globals is not a match
        import json
        nm = os.path.join(build.BUILD, 'namemap.json')
        self.name2va = {}
        if os.path.exists(nm):
            for va, n in json.load(open(nm)).items():
                self.name2va.setdefault(n, set()).add(int(va, 16))
        import capstone
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.right = [(i.address, i.size, '%s %s' % (i.mnemonic, i.op_str))
                      for i in self.md.disasm(self.target[:self.exe_len], self.va)]
        while self.right and self.right[-1][2].split()[0] in ('int3', 'nop'):
            self.right.pop()

    def compile(self, text, slot, want_msgs=False, include=None):
        """Compile `text` as this unit in a private scratch directory (never src/); None when it does not compile or the
        compiler hangs (killed, with its children, after COMPILE_TIMEOUT seconds).  include: private include directories
        searched before include/; want_msgs: return the compiler output instead."""
        d = os.path.join(self.dir, 'w%s' % slot)
        os.makedirs(d, exist_ok=True)
        first = ['/I' + x for x in (include or []) + self.include]
        obj, out = toolenv.compile_tu(self.src, text, d, self.unit.flags, timeout=COMPILE_TIMEOUT, first=first)
        if want_msgs:
            return out
        self.last_error = None if obj else (toolenv.compile_errors(out) or out.splitlines()[-4:])
        return obj

    def probe_types(self, head, L, tail, exprs, jobs=4):
        """Types of expressions from compiler errors: {expr: (type, kind) or None}. exprs: {expr: line index}."""
        from concurrent.futures import ThreadPoolExecutor
        items = []
        for e, i in exprs.items():
            pat = re.compile(r'(?<![\w.>:])' + re.escape(e) + r'(?![\w(\[]|\s*\()')
            ln = [k for k in ([i] + list(range(len(L)))) if 0 <= k < len(L) and pat.search(code_part(L[k])) and not is_decl(L[k])]
            if ln:
                items.append((e, ln[0], pat))
        groups = []
        for e, i, pat in items:
            for g in groups:
                if i not in g and len(g) < 40:
                    g[i] = (e, pat)
                    break
            else:
                groups.append({i: (e, pat)})
        hl = head.count('\n')
        out = {e: None for e in exprs}

        def run(arg):
            k, g = arg
            L2 = list(L)
            for i, (e, pat) in g.items():
                c = code_part(L2[i])
                m = pat.search(c)
                L2[i] = L2[i][:m.start()] + '(*(__tpT**)0 = (%s), (%s))' % (e, e) + L2[i][m.end():]
            msgs = self.compile(head + '\tstruct __tpT;\n' + '\n'.join(L2) + tail, 'probe%d' % k, want_msgs=True)
            res = {}
            for line in msgs.splitlines():
                diagnostic = parse_probe_type_diagnostic(line)
                if diagnostic:
                    line_no, raw_type = diagnostic
                    i = line_no - hl - 2
                    if i in g and g[i][0] not in res:
                        t, enum = norm_type(raw_type)
                        res[g[i][0]] = (t, type_kind(t, enum))
            return res
        with ThreadPoolExecutor(max(1, min(jobs, len(groups)))) as ex:
            for res in ex.map(run, enumerate(groups)):
                out.update(res)
        return out

    def score(self, obj):
        """(scalar, ns, n, size delta, differing bytes, code hash) or None."""
        b = self.build
        try:
            o = b.CoffObj(obj)
        except Exception:
            return None
        if self.symname is None:
            self.unit.annots = [a for a in self.unit.annots]
            b.bind_symbols([self.unit], {self.unit.name: o})
            if not self.annot.symbol:
                sys.exit('cannot bind the function symbol: %s' % self.annot.error)
            self.symname = self.annot.symbol.name
        syms = [s for s in o.functions() if s.name == self.symname]
        if len(syms) != 1:
            return None
        sec, start, end = o.extent(syms[0])
        base = sec.data[start:end]
        relocs = {off: s.name for off, s, _, _ in o.relocs_in(sec, start, end)}
        mask = bytearray(len(base))
        for off in relocs:
            for k in range(4):
                if off + k < len(mask):
                    mask[off + k] = 1
        tgt = self.target
        nd = sum(1 for i in range(min(len(base), len(tgt))) if not mask[i] and base[i] != tgt[i])
        for off, s_, typ, addend in o.relocs_in(sec, start, end):
            known = self.name2va.get(s_.name)
            if not known or off + 4 > len(tgt) or typ not in (b.REL_DIR32, b.REL_REL32):
                continue
            f = int.from_bytes(tgt[off:off + 4], 'little', signed=(typ == b.REL_REL32))
            tva = (f - addend) if typ == b.REL_DIR32 else (self.va + off + 4 + f - addend)
            if (tva & 0xffffffff) not in known:
                nd += 4
        pad_ok = len(base) <= self.exe_len and all(x in (0xCC, 0x90) for x in tgt[len(base):self.exe_len])
        if nd == 0 and pad_ok:
            return (0.0, 0, 0, 0, 0, hashlib.md5(bytes(base)).hexdigest())
        left = [(i.address, i.size, '%s %s' % (i.mnemonic, i.op_str)) for i in self.md.disasm(base, 0)]
        while left and left[-1][2].split()[0] in ('int3', 'nop'):
            left.pop()
        n = b.aligned_score(left, self.right, relocs, False)[0]
        ns = b.aligned_score(left, self.right, relocs, True)[0]
        dsz = abs(len(left) - len(self.right))
        scalar = ns * 3.0 + n + min(nd, 400) / 400.0 + 0.5
        # masked bytes differ only through relocations here, which the final build.py check verifies
        h = bytearray(base)
        for i in range(len(h)):
            if mask[i]:
                h[i] = 0
        return (scalar, ns, n, dsz, nd, hashlib.md5(bytes(h)).hexdigest())


def _skip_lexeme(text, i):
    """If a comment, string or character literal starts at i, the index just past it, else i."""
    if text.startswith('//', i):
        j = text.find('\n', i)
        return len(text) if j < 0 else j
    if text.startswith('/*', i):
        j = text.find('*/', i + 2)
        return len(text) if j < 0 else j + 2
    c = text[i]
    if c in '"\'':
        j = i + 1
        while j < len(text) and text[j] != c:
            j += 2 if text[j] == '\\' else 1
        return j + 1
    return i


def split_source(text, va):
    """(head, body, tail, signature) of the definition annotated for `va`: head ends right after the line holding the opening
    brace, tail starts at the line break before the closing brace.  Works for member functions (Class::Method), constructors
    with initialiser lists and a brace on the signature's line."""
    m = re.search(r'// (STUB|FUNCTION): ' + modcfg.TAG + r' 0x0*%x\b[^\n]*\n' % va, text)
    if not m:
        sys.exit('annotation not found')
    i, depth = m.end(), 0
    while i < len(text):
        j = _skip_lexeme(text, i)
        if j != i:
            i = j
            continue
        c = text[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        elif c == ';' and depth == 0:
            sys.exit('%08x is annotated on a declaration, not a definition' % va)
        elif c == '{' and depth == 0:
            break
        i += 1
    else:
        sys.exit('no function body after the annotation of %08x' % va)
    brace, d, j = i, 0, i
    while j < len(text):
        k = _skip_lexeme(text, j)
        if k != j:
            j = k
            continue
        if text[j] == '{':
            d += 1
        elif text[j] == '}':
            d -= 1
            if d == 0:
                break
        j += 1
    else:
        sys.exit('unbalanced braces in the function at %08x' % va)
    a = brace + 1
    if text[a:a + 1] == '\n':
        a += 1
    b = j - 1 if text[j - 1:j] == '\n' and j - 1 >= a else j
    return text[:a], text[a:b], text[b:], text[m.end():brace]


def resolve_function(unit, key):
    """Address of the function named by `key`: a hex address, or a (qualified) name such as `ModelDraw::FUN_10002050`,
    `FUN_10002050` or `DrawModelShadows` that exactly one FUNCTION/STUB annotation of the unit carries."""
    k = str(key).strip()
    if re.fullmatch(r'(0[xX])?[0-9a-fA-F]{6,8}', k):
        return int(k, 16)
    cands = [a for a in unit.annots if a.kind != 'GLOBAL' and a.name and (a.name == k or a.name.endswith('::' + k))]
    vas = sorted({a.va for a in cands})
    if len(vas) != 1:
        sys.exit('%s: %d functions match %r %s' % (unit.rel, len(vas), k, ['%08x %s' % (a.va, a.name) for a in cands][:6]))
    return vas[0]


def locate(args):
    """(source file, function key) from `[src] <hex|name>`; without a source file the unit that annotates the function is found."""
    if len(args) >= 2:
        return args[0], args[1]
    import build
    key = args[0]
    hexa = re.fullmatch(r'(0[xX])?[0-9a-fA-F]{6,8}', key)
    hits = [u for u in build.find_units() if any(
        a.kind != 'GLOBAL' and ((hexa and a.va == int(key, 16)) or (not hexa and a.name and (a.name == key or a.name.endswith('::' + key))))
        for a in u.annots)]
    if len(hits) != 1:
        sys.exit('%r: %d units annotate it %s (name the source file first)' % (key, len(hits), [u.rel for u in hits][:5]))
    return hits[0].path, key


def parse_params(sig):
    sig = ' '.join(l.split('//')[0] for l in sig.split('\n'))
    if '(' not in sig:
        return []
    inner = sig[sig.index('(') + 1:sig.rindex(')')]
    out = []
    for p in inner.split(','):
        m = re.match(r'^\s*((?:const\s+)?[\w:]+(?:\s*[*&])*)\s*(\w+)\s*$', p.strip())
        if m and '&' not in m.group(1) and m.group(1) not in ('void',):
            out.append((m.group(1).strip(), m.group(2)))
    return out


def evaluate(job):
    slot, text = job[:2]
    obj = T.compile(text, slot, include=job[2] if len(job) > 2 else None)
    return T.score(obj) if obj else None


T = None


def load_text(a):
    return open(a.text or T.src, encoding='latin1', newline='').read()


def minimize(a):
    """Reduce <outdir>/match.cpp to the fewest changed hunks (against the current source, or --text) that still
    give a byte-identical function; writes min.cpp and prints the remaining diff."""
    global T
    import difflib
    T = Target(a.src, a.addr, a.outdir, a.include)
    orig = load_text(a)
    head, body, tail, _ = split_source(orig, T.va)
    mt = open(os.path.join(T.dir, 'match.cpp'), encoding='latin1', newline='').read()
    _, mbody, _, _ = split_source(mt, T.va)
    A, B = body.split('\n'), mbody.split('\n')
    r = evaluate((0, head + '\n'.join(B) + tail))
    if not r or r[0] != 0:
        sys.exit('match.cpp does not match on top of the current source (score %s)' % (r,))
    def reind(lines, j):
        # A's lines at B's indentation around j (indentation is not code; keeps min.cpp readable)
        ref = B[j] if j < len(B) and B[j].strip() else (B[j - 1] if j > 0 else '')
        d = indent(ref) - indent(lines[0]) if lines and ref.strip() and lines[0].strip() else 0
        return [('\t' * d + x) if d > 0 and x.strip() else (x[-d:] if d < 0 and x.startswith('\t' * -d) else x) for x in lines]

    def candidates():
        # whole hunks first, then single lines inside them; lines compared without indentation (a wrapped block
        # is then one inserted `{` / `}` pair, not a rewrite of the body)
        sa, sb = [x.strip() for x in A], [x.strip() for x in B]
        ops = [o for o in difflib.SequenceMatcher(None, sa, sb, autojunk=False).get_opcodes() if o[0] != 'equal']
        for tag, i1, i2, j1, j2 in ops:
            yield B[:j1] + reind(A[i1:i2], j1) + B[j2:]
        for tag, i1, i2, j1, j2 in ops:
            if tag == 'replace' and i2 - i1 == j2 - j1:
                for k in range(i2 - i1):
                    yield B[:j1 + k] + reind([A[i1 + k]], j1 + k) + B[j1 + k + 1:]
            if tag in ('insert', 'replace'):
                for k in range(j1, j2):
                    yield B[:k] + B[k + 1:]
            if tag == 'delete':
                for k in range(i1, i2):
                    yield B[:j1] + reind([A[k]], j1) + B[j1:]
        # moves the hunks can't express: an added copy local inlined back into its uses (`T x = y;` -> y), and
        # an added brace pair (with an added `else` before it) removed together
        added = {j for tag, i1, i2, j1, j2 in ops if tag in ('insert', 'replace') for j in range(j1, j2)}
        for j in sorted(added):
            m = DECL_INIT.match(B[j])
            if m and re.match(r'^[A-Za-z_][\w]*(?:(?:->|\.)\w+)*$', m.group(5).strip()) and '&' not in m.group(3):
                pat = re.compile(r'\b%s\b' % re.escape(m.group(4)))
                yield [pat.sub(m.group(5).strip(), x) for x in B[:j] + B[j + 1:]]
            if B[j].strip() == '{':
                e = block_end(B, j)
                if e is not None:
                    s = j - 1 if j > 0 and B[j - 1].strip() == 'else' and j - 1 in added else j
                    yield B[:s] + [x[1:] if x.startswith('\t') else x for x in B[j + 1:e]] + B[e + 1:]
    from concurrent.futures import ThreadPoolExecutor
    changed = True
    with ThreadPoolExecutor(a.jobs) as ex:
        while changed:
            changed = False
            cs = list(candidates())
            for k in range(0, len(cs), a.jobs):
                chunk = cs[k:k + a.jobs]
                res = list(ex.map(evaluate, [('m%d' % n, head + '\n'.join(C) + tail) for n, C in enumerate(chunk)]))
                hit = [C for C, r in zip(chunk, res) if r and r[0] == 0]
                if hit:
                    B, changed = hit[0], True
                    break
    # a mismatched indentation left by single-line reverts is cosmetic; the final diff is printed as is
    open(os.path.join(T.dir, 'min.cpp'), 'w', encoding='latin1', newline='').write(head + '\n'.join(B) + tail)
    for l in difflib.unified_diff(A, B, 'current', 'minimal match', n=1, lineterm=''):
        print(l)
    return 0


def sample(a, ctx, head, body, tail, s0, probe):
    import difflib
    from concurrent.futures import ThreadPoolExecutor
    rng = random.Random(a.seed)
    ops = [o for o in a.ops.split(',') if o in MUTATORS]
    for op in ops:
        cands, seen = [], set()
        for _ in range(a.sample * 20):
            if len(cands) >= a.sample:
                break
            L = body.split('\n')
            try:
                r = MUTATORS[op][0](L, rng, ctx)
            except Exception:
                if a.debug:
                    import traceback
                    traceback.print_exc()
                r = None
            if ctx.get('pending'):
                pend = ctx.pop('pending')
                probe(body, {e: i for e, i in probe_exprs(body.split('\n')).items() if e in pend})
                for e in pend:
                    ctx['types'].setdefault(e, None)
            nb = '\n'.join(L)
            if r and nb not in seen:
                seen.add(nb)
                cands.append((nb, r))
        if not cands:
            print('%-10s no applicable site' % op)
            continue
        with ThreadPoolExecutor(a.jobs) as ex:
            res = list(ex.map(evaluate, [('s%d' % k, head + nb + tail) for k, (nb, _) in enumerate(cands)]))
        nf = sum(1 for r in res if r is None)
        better = sum(1 for r in res if r and r[0] < s0[0])
        print('%-10s %3d candidates, %3d failed to compile (%.0f%%), %d better than the start' % (
            op, len(cands), nf, 100.0 * nf / len(cands), better), flush=True)
        if a.v:
            for (nb, what), r in zip(cands, res):
                print('  --- %s: %s' % ('FAIL' if r is None else 'ns=%d n=%d' % (r[1], r[2]), what))
                for l in list(difflib.unified_diff(body.split('\n'), nb.split('\n'), n=0, lineterm=''))[2:]:
                    if not l.startswith('@@'):
                        print('     ' + l)
    return 0


WAVE8 = ['refparam', 'firstop', 'loopctr', 'wrapret', 'fdiv', 'redstore', 'vecnamed', 'ptrstep', 'ftype']
OP_ALIASES = {'wave8': WAVE8, 'all': list(MUTATORS) + ['ftype'],
              # the wave-8 levers plus the wave-7 moves that found matches (NOTES.md "The permuter")
              'focus': WAVE8 + ['paramcopy', 'decluse', 'move', 'elseform', 'temp', 'idiom', 'swapstmt']}


HELP_OPS = """mutations (--ops, comma separated; default all; aliases: wave8 = the wave-8 ones, focus = wave8 plus
paramcopy,decluse,move,elseform,temp,idiom,swapstmt, all = everything):
  wave 7: move swapstmt swapdecl splitdecl splitinit decluse declscope hoist inttype commute commany incdec
          compound ifinv cmpform truth ternary wrap unwrap paramcopy localsplit loop index chain assoc idiom
          temp dead elseform taildup cast logic nestand retform
  wave 8 (the hand-pass levers, NOTES.md "The permuter"):
    refparam  reference/pointer local for a member path (`CMoArray<X> &a = p->m_W.m_X;`, `T *pX = &p->m_X;`),
              declared at a chosen statement, later uses in the block rewritten
    firstop   first operand of a statement read into a local first (`float fX = pPos->x;`), one or all reads
    loopctr   a loop gets its own counter when one counter serves several loops (new j: own line, in the
              declaration list, or in the for)
    wrapret   `if (c) return R; rest` <-> `if (!c) { rest }` [+ `return R;`] where the block's end leads to
              `return R` (the lazy-push lever)
    fdiv      `x / (float)g` <-> `x / (g * 1.0f)`, int divisors made float, (float)g through a float local
    redstore  repeat a store of the same constant (or a member re-read into the same local) later with no
              write in between, or drop such a repeat
    vecnamed  more VEC_* macro <-> operator pairs, `v = E;` <-> `LTVector vN = E; v = vN;`,
              `v = A + B;` <-> `v = A; v += B;`
    ptrstep   `p = arr; for (i=0; c; i++, p++)` <-> `for (i=0; c; i++) { p = &arr[i]; ...`
    ftype     (probe only, not a mutation) every integer-0 store to a member as `*(float*)&m = 0.0f` and every
              float zero store as `*(uint32*)&m = 0`, one store and one member at a time, on the start and the
              best body; an improvement means a header member type may be wrong: written to hints.txt, never
              to best.cpp. --ops without ftype turns it off.
Types for refparam/firstop/fdiv/vecnamed/ftype come from probe compiles (the C2440/C2679 error text).
Outputs besides best.cpp/match.cpp/log.txt: result.json (start/best ALIGNED, mutations in the best, compile
failures per mutation) and hints.txt (ftype hits)."""


def main(argv):
    global T
    global COMPILE_TIMEOUT
    import argparse, json
    from concurrent.futures import ThreadPoolExecutor
    ap = argparse.ArgumentParser(description=__doc__, epilog=HELP_OPS, formatter_class=argparse.RawDescriptionHelpFormatter,
                                 usage='permute.py [src file] <hex address | function name> [options]')
    ap.add_argument('target', nargs='+', help='[src file] <hex address | Class::Method | name>; without the source file '
                    'the unit that annotates the function is used (its directory and flags are used for compiling)')
    ap.add_argument('--timeout', type=float, default=COMPILE_TIMEOUT, help='seconds per candidate compile (a hung CL.EXE is killed)')
    ap.add_argument('--cut', action='store_true', help='compile only the unit up to the end of the function (faster for a big file; '
                                                       'falls back to the whole file when that does not compile)')
    ap.add_argument('--iters', type=int, default=3000)
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--temp', type=float, default=1.5)
    ap.add_argument('--ops', default=','.join(list(MUTATORS) + ['ftype']))
    ap.add_argument('--minutes', type=float, default=0)
    ap.add_argument('--resume', action='store_true', help='start from best.cpp of an earlier run')
    ap.add_argument('--text', help='take the source text from this file instead of <src> (a scratch copy); '
                    '<src> still gives the include directory, flags and unit')
    ap.add_argument('--outdir', help='output directory (default build/permute/<addr>)')
    ap.add_argument('--include', action='append', default=[], help='an include directory searched first '
                    '(a private copy of include/ for header experiments)')
    ap.add_argument('--minimize', action='store_true', help='minimise <outdir>/match.cpp into min.cpp')
    ap.add_argument('--debug', action='store_true', help='print mutator exceptions')
    ap.add_argument('--sample', type=int, default=0, metavar='N', help='no search: apply each --ops mutation N times '
                    'to the start body, compile each, print the compile-failure rate per mutation (-v: the diffs)')
    ap.add_argument('-v', action='store_true')
    ap.add_argument('--ftype-only', action='store_true', help='run only the ftype probes (hints.txt), no search')
    a = ap.parse_args(argv)
    COMPILE_TIMEOUT = a.timeout
    a.src, a.addr = locate(a.target)
    a.ops = ','.join(','.join(OP_ALIASES.get(o, [o])) for o in a.ops.split(','))
    if a.minimize:
        return minimize(a)
    T = Target(a.src, a.addr, a.outdir, a.include)
    rng = random.Random(a.seed)
    ops = [o for o in a.ops.split(',') if o in MUTATORS]
    orig = load_text(a)
    head, body, tail, sig = split_source(orig, T.va)
    if a.cut:
        full_tail = tail
        tail = '\n}\n'
        if evaluate((0, head + body + tail)) is None:
            print('--cut: the unit up to the end of the function does not compile (%s); using the whole file' % (T.last_error,))
            tail = full_tail
        else:
            orig = head + body + tail
    sigl = [x for x in sig.split('\n') if x.strip() and not x.strip().startswith('//')]
    ctx = {'params': parse_params(sig), 'debug': a.debug,
           'void': bool(sigl and re.match(r'^\s*(?:static\s+|inline\s+)*void\s+[\w:~]+\s*\(', sigl[0])),
           'words': set(re.findall(r'[A-Za-z_]\w*', sig))}      # (+ the body's words, per mutation)
    log = open(os.path.join(T.dir, 'log.txt'), 'a', encoding='utf-8')
    stats = {}

    def say(s):
        print(s, flush=True)
        log.write(s + '\n')
        log.flush()

    def probe(text_body, exprs):
        if exprs:
            ctx.setdefault('types', {}).update(T.probe_types(head, text_body.split('\n'), tail, exprs, a.jobs))

    s0 = evaluate((0, orig))
    if s0 is None:
        sys.exit('the unmodified source does not compile or the function was not found in the object: %s' % (T.last_error,))
    say('== %s %08x %s  start: ns=%d n=%d bytes=%d  (seed %s)' % (time.strftime('%H:%M:%S'), T.va, T.symname, s0[1], s0[2], s0[4], a.seed))
    rj = os.path.join(T.dir, 'result.json')
    prev = {}
    if os.path.exists(rj):
        try:
            prev = json.load(open(rj))
        except Exception:
            prev = {}

    def result(best, hist, matched, done=0, fails=0, secs=0):
        ops_best = {}
        for o in op_names(hist):
            ops_best[o] = ops_best.get(o, 0) + 1
        if '(resumed)' in hist:
            for o, n in prev.get('ops', {}).items():
                ops_best[o] = ops_best.get(o, 0) + n
        json.dump({'addr': '%08x' % T.va, 'name': T.symname, 'src': os.path.relpath(T.src, ROOT).replace('\\', '/'),
                   'start': [s0[1], s0[2]], 'best': [best[1], best[2]], 'matched': matched, 'ops': ops_best,
                   'candidates': done, 'failed': fails, 'seconds': round(secs), 'stats': stats,
                   'when': time.strftime('%Y-%m-%d %H:%M')}, open(rj, 'w'), indent=1)

    if s0[0] == 0:
        say('already matches')
        result(s0, [], True)
        return 0
    probe(body, probe_exprs(body.split('\n')))
    say('typed %d of %d expressions' % (sum(1 for v in ctx.get('types', {}).values() if v), len(ctx.get('types', {}))))
    if a.sample:
        return sample(a, ctx, head, body, tail, s0, probe)

    def ftype_sweep(b, base, label):
        if 'ftype' not in a.ops.split(','):
            return
        L = b.split('\n')
        pr = ftype_probes(L, ctx)
        if not pr:
            return
        with ThreadPoolExecutor(a.jobs) as ex:
            res = list(ex.map(evaluate, [('f%d' % k, head + '\n'.join(L2) + tail) for k, (_, _, L2) in enumerate(pr)]))
        nz = sum(1 for l in L if ZSTORE.match(l) and ZSTORE.match(l).group(3) == '0')
        hits = 0
        with open(os.path.join(T.dir, 'hints.txt'), 'a', encoding='utf-8') as h:
            for (lab, mem, _), r in zip(pr, res):
                if r and r[0] < base[0]:
                    hits += 1
                    h.write('%s %08x %s ftype (%s body): %s -> ALIGNED %d -> %d (ns %d -> %d)%s; %d integer-0 stores in '
                            'the body. Check the member type in the header/Jupiter (NOTES.md: a constant kept in a '
                            'register is a count of stores).\n' % (
                                time.strftime('%Y-%m-%d %H:%M'), T.va, T.symname, label, lab, base[2], r[2], base[1], r[1],
                                ' MATCH' if r[0] == 0 else '', nz))
            if hits > 1:
                h.write('    (%d probes improve: one integer-0 store too many may be all it shows; check which member '
                        'is the float)\n' % hits)
        say('ftype probes (%s body): %d, %d better%s' % (label, len(pr), hits, ' -> hints.txt' if hits else ''))

    def ftype_headers():
        """The same probe on the integer-0 stores in header inlines of the classes the function uses: each variant
        compiles against a private copy of the include directory with one store changed."""
        if 'ftype' not in a.ops.split(','):
            return
        base_inc = a.include[0] if a.include else os.path.join(ROOT, 'include')
        sites = header_ftype_sites(ctx, sig, base_inc)[:32]
        if not sites:
            return
        jobs_, dirs = [], []
        for k, (h, i, mem, cls) in enumerate(sites):
            d = os.path.join(T.dir, 'ftype_inc', str(k))
            if os.path.exists(d):
                shutil.rmtree(d)
            shutil.copytree(base_inc, d)
            hp = os.path.join(d, os.path.basename(h))
            hl = open(hp, encoding='latin1', newline='').read().split('\n')
            hl[i] = re.sub(r'(m_\w+)\s*=\s*0\s*;', r'*(float*)&\1 = 0.0f;', hl[i], count=1)
            open(hp, 'w', encoding='latin1', newline='').write('\n'.join(hl))
            jobs_.append(('h%d' % k, orig, [d]))
            dirs.append(d)
        with ThreadPoolExecutor(a.jobs) as ex:
            res = list(ex.map(evaluate, jobs_))
        hits = 0
        with open(os.path.join(T.dir, 'hints.txt'), 'a', encoding='utf-8') as hf:
            for (h, i, mem, cls), r in zip(sites, res):
                if r and r[0] < s0[0]:
                    hits += 1
                    hf.write('%s %08x %s ftype (header): %s::%s zeroed as an integer at %s:%d; stored as 0.0f -> ALIGNED %d '
                             '-> %d (ns %d -> %d)%s. The member is probably a float: check Jupiter and the exe\'s other '
                             'users, then every unit that includes the header.\n' % (
                                 time.strftime('%Y-%m-%d %H:%M'), T.va, T.symname, cls, mem, os.path.basename(h), i + 1,
                                 s0[2], r[2], s0[1], r[1], ' MATCH' if r[0] == 0 else ''))
            if hits > 1:
                hf.write('    (%d stores give an improvement: the probe only says one integer-0 store too many is counted; '
                         'which member is the float needs Jupiter/the exe\'s other users)\n' % hits)
        for d in dirs:
            shutil.rmtree(d, ignore_errors=True)
        say('ftype probes (header inlines of %s): %d, %d better%s' % (
            ', '.join(sorted({x[3] for x in sites})), len(sites), hits, ' -> hints.txt' if hits else ''))

    ftype_sweep(body, s0, 'start')
    ftype_headers()
    if a.ftype_only:
        return 1
    cur_body, cur, cur_hist = body, s0, []
    best_body, best, best_hist = body, s0, []
    bp = os.path.join(T.dir, 'best.cpp')
    if a.resume and os.path.exists(bp):
        _, rb, _, _ = split_source(open(bp, encoding='latin1', newline='').read(), T.va)
        r = evaluate((0, head + rb + tail))
        if r and r[0] == 0:
            open(os.path.join(T.dir, 'match.cpp'), 'w', encoding='latin1', newline='').write(head + rb + tail)
            say('*** MATCH: best.cpp already matches')
            result(r, ['(resumed)'], True)
            return 0
        if r and r[0] < s0[0]:
            cur_body, cur, cur_hist = rb, r, ['(resumed)']
            best_body, best, best_hist = rb, r, ['(resumed)']
            say('resumed from best.cpp: ns=%d n=%d bytes=%d' % (r[1], r[2], r[4]))
            probe(rb, {e: i for e, i in probe_exprs(rb.split('\n')).items() if e not in ctx.get('types', {})})
    seen = {hashlib.md5(body.encode('latin1')).hexdigest()}
    seen_code = {s0[5]: 1}
    stale, done, t0, fails = 0, 0, time.time(), 0
    with ThreadPoolExecutor(a.jobs) as ex:
        while done < a.iters and (not a.minutes or time.time() - t0 < a.minutes * 60):
            if ctx.get('pending'):      # type the expressions the mutators met, against the current body
                pend = ctx.pop('pending')
                cl = cur_body.split('\n')
                ex_ = {}
                for e in pend:
                    pat = re.compile(r'(?<![\w.>:])' + re.escape(e) + r'(?![\w(\[])')
                    ln = [k for k, l in enumerate(cl) if pat.search(code_part(l)) and not is_decl(l)]
                    if ln:
                        ex_[e] = ln[0]
                    else:
                        ctx.setdefault('types', {})[e] = None
                probe(cur_body, ex_)
            cands = []
            guard = 0
            while len(cands) < a.jobs and guard < a.jobs * 30:
                guard += 1
                nb, what = mutate(cur_body, rng, ctx, ops)
                if nb is None:
                    continue
                h = hashlib.md5(nb.encode('latin1')).hexdigest()
                if h in seen:
                    continue
                seen.add(h)
                cands.append((nb, what))
            if not cands:
                cur_body, cur, cur_hist = best_body, best, list(best_hist)
                stale += 1
                if stale > 50:
                    say('no new candidates: stopping')
                    break
                continue
            res = list(ex.map(evaluate, [(k, head + nb + tail) for k, (nb, _) in enumerate(cands)]))
            done += len(cands)
            fails += sum(1 for r in res if r is None)
            for r, (nb, what) in zip(res, cands):
                on = op_names(what)
                for o in set(on):
                    st = stats.setdefault(o, [0, 0, 0, 0])
                    st[0] += 1
                    st[1] += r is None
                    if len(on) == 1:        # failures of single-mutation candidates are this mutation's own
                        st[2] += 1
                        st[3] += r is None
                if r is None and len(what) == 1:
                    ctx.setdefault('bad', set()).add(what[0])
            scored = [(r, nb, what) for r, (nb, what) in zip(res, cands) if r is not None]
            if not scored:
                continue
            scored.sort(key=lambda x: x[0][0])
            r, nb, what = scored[0]
            if r[0] == 0:
                open(os.path.join(T.dir, 'match.cpp'), 'w', encoding='latin1', newline='').write(head + nb + tail)
                open(bp, 'w', encoding='latin1', newline='').write(head + nb + tail)
                say('*** MATCH after %d candidates (%.0fs): %s' % (done, time.time() - t0, ' ; '.join(cur_hist + what)))
                result(r, cur_hist + what, True, done, fails, time.time() - t0)
                return 0
            # anneal: take the best of the batch if it is better, equal (drift), or with a temperature-driven chance
            pick = None
            if r[0] < cur[0]:
                pick = scored[0]
            else:
                rng.shuffle(scored)
                for cand in scored:
                    d = cand[0][0] - cur[0]
                    novel = seen_code.get(cand[0][5], 0) == 0
                    if (d <= 0 and (novel or rng.random() < 0.3)) or (d > 0 and rng.random() < math.exp(-d / a.temp) * (0.5 if novel else 0.1)):
                        pick = cand
                        break
            for c in scored:
                seen_code[c[0][5]] = seen_code.get(c[0][5], 0) + 1
            if pick:
                r, nb, what = pick
                cur_body, cur, cur_hist = nb, r, cur_hist + what
                if r[0] < best[0]:
                    best_body, best, best_hist = nb, r, list(cur_hist)
                    stale = 0
                    open(bp, 'w', encoding='latin1', newline='').write(head + nb + tail)
                    say('%6d %5.0fs  best ns=%d n=%d bytes=%d  <- %s' % (done, time.time() - t0, r[1], r[2], r[4], ' ; '.join(what)))
                    result(best, best_hist, False, done, fails, time.time() - t0)
                    continue
            stale += 1
            if stale and stale % 60 == 0:       # wandered off without profit: restart from the best
                cur_body, cur, cur_hist = best_body, best, list(best_hist)
    say('== done: %d candidates (%d failed to compile), best ns=%d n=%d bytes=%d in %.0fs' % (
        done, fails, best[1], best[2], best[4], time.time() - t0))
    say('   compile failures per mutation (alone: failed/tried; in any candidate): ' + ', '.join(
        '%s %d/%d (%d/%d)' % (o, st[3], st[2], st[1], st[0]) for o, st in sorted(stats.items(), key=lambda x: -x[1][0])))
    if best_body != body:
        probe(best_body, {e: i for e, i in probe_exprs(best_body.split('\n')).items() if e not in ctx.get('types', {})})
        ftype_sweep(best_body, best, 'best')
    result(best, best_hist, False, done, fails, time.time() - t0)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
