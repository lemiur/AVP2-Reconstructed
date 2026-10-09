r"""Measure the compiler's inline-decision rules with toy probes (the compiler as the oracle; no unit is touched).

  python tools/inline_rules.py [--module d3dren] [--flags "/O2 /Ob2"] [--only NAME,...]
  python tools/inline_rules.py --compare [--flags "/O2 /Ob2"]     both modules' compilers side by side

Each rule is a small self-contained translation unit compiled with the module's wrapper (d3dren: VC6 RTM C2 12.00.8168;
lithtech: the Processor Pack C2 13.00.9044 through scripts\vc6cl.bat, or VC6CL in a worktree; D3DRENCL overrides the
renderer's) into build/<module>/scratch/inline_rules/;
the /FAs listing says which calls stayed out of line.  `b(K)` is an inline function of K `g[i] = i;` stores (6u each),
`ext()` a non-inline external call.  Printed values (and what tools/inline_budget.py assumes):

  tail       R11: a site in tail position (only the function exit follows) is inlined whatever its cost.  Each case is
             `b(400)` (2412u, over any small budget): alone, after ext(), last in an if arm, in loops, nested in a free
             wrapper w() (tail only when w is tail too), before `return 0`, `int r = bi(); return r;`, `b(); pend();`
             (an empty inline after it); then the arguments: only a call without stack arguments escapes (`b(3)` and
             `gc.b(3)` do not; `gc.b()` and __fastcall `b(3, 4)` do).  (inline_budget.py: R11, is_tail, stack_args)
  tailtree   R11: the expansion of a tail site that did not fit is unlimited (its own sites inline at any cost: w() of 300
             stores with an m(400) site); a tail site that fits is charged and its sites get the usual shares; a tail site
             over its limit charges nothing (the else arm still has the full budget)
  kcap       largest K whose single non-tail site `b(K); ext();` is inlined: the budget B of a small function in
             stores (B ~ 6 x kcap + 12 u; inline_budget R3: 1000u floor)
  cumul      for K = 10/20/40: how many of 10 non-tail `b(K)` sites inline (cumulative charging, R7)
  weights    how many statements of one kind an inline helper may hold and still inline at one non-tail site: global
             stores, ext() calls, empty blocks (/O2: the budget in each weight; /O1 judges each site alone with other weights)
  extcap     R12: largest size (u = 12 + body) at which /Ob2 auto-inlines a NON-inline function at one non-tail site:
             extern, static called once, static called twice, out-of-class member, and `inline` (the budget)
  autopend   R12: does a non-inline helper count as a pending site (R8)?  The share probe with six calls of a small
             extern helper, a 40-store extern one (over the cap) and a 40-store static one called many times
  ctor       the 3-float constructor's cost from a top-level probe (B - largest ballast before it), and what the
             pre-wave-9 cost probe saw: the call as R's last statement with R last in W (tail: read as free)
  order      plain helper defined before vs after its caller: inlined in both? (definition order)
  share      three `v = a - b;` operator sites on a vector class whose 3-float ctor is an inline call, P free accessor
             calls after each: which ctor calls stay out of line (R8: nested share = remaining / (1 + pending);
             `first out, later in` = shares growing as pending shrinks)
"""
import os
import re
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import toolenv  # noqa: E402  (module selection, MSYS argument repair, compiler environment)

HEAD = 'int g_bal[2000]; int g_s[200]; void ext();\ninline void tiny() { g_s[1] = 2; }\n'


def ballast(name, k, kw='inline'):
    return '%s void %s()\n{\n%s}\n' % (kw, name, ''.join('\tg_bal[%d] = %d;\n' % (i, i) for i in range(k)))


VEC = '''template<class T> class _V {
public:
	_V() {}
	_V(T mx, T my, T mz) {x=mx; y=my; z=mz;}
	_V<T> operator - (const _V<T> v) const { return _V<T>(x - v.x, y - v.y, z - v.z); }
	T x, y, z;
};
typedef _V<float> V;
V gA, gB, gC, gD, gE;
'''


class Probe:
    def __init__(self, flags):
        self.flags = flags
        self.dir = toolenv.scratch_dir('inline_rules', toolenv.NAME)
        self.n = 0

    def calls(self, src, fn='F'):
        """Out-of-line call targets (in order) of function `fn` (extern "C++" void fn()) of a toy unit."""
        self.n += 1
        cpp = os.path.join(self.dir, 'p.cpp')
        asm = os.path.join(self.dir, 'p.asm')
        obj = os.path.join(self.dir, 'p.obj')
        for p in (asm, obj):
            if os.path.exists(p):
                os.remove(p)
        with open(cpp, 'w', newline='') as f:
            f.write(src)
        rc, out, _ = toolenv.run_cl(self.flags, '/FAs', '/Fa' + asm, '/Fo' + obj, cpp, cwd=self.dir, tmp=self.dir)
        if rc != 0 or not os.path.exists(asm):
            raise SystemExit('probe does not compile (%s):\n%s' % (' '.join(self.flags), out[-2000:]))
        text = open(asm, encoding='latin1').read()
        m = re.search(r'\?%s@@\S+\s+PROC NEAR(.*?)\?%s@@\S+\s+ENDP' % (fn, fn), text, re.S)
        if not m:
            raise SystemExit('no function %s in the probe listing' % fn)
        return [re.sub(r'^\?(\w+)@.*', r'\1', t) for t in re.findall(r'^\s+call\s+(\S+)', m.group(1), re.M)]

    def inlined(self, defs, body, callee):
        return callee not in self.calls(HEAD + defs + 'void F() { %s }\n' % body)

    def bsearch(self, lo, hi, ok):
        """Largest x in [lo, hi) with ok(x) (ok monotone, ok(lo) assumed); hi if all pass."""
        if ok(hi):
            return hi
        while hi - lo > 1:
            m = (lo + hi) // 2
            if ok(m):
                lo = m
            else:
                hi = m
        return lo


def body_u(W, arr='g_bal'):
    """Statements worth exactly W u (6u global stores, a 13u `if(0)` store, 2u empty blocks), as inline_budget.fine_body."""
    for a in (0, 1):
        for b in range(6):
            rem = W - 13 * a - 2 * b
            if rem >= 0 and rem % 6 == 0:
                return (''.join('\t%s[%d]=%d;\n' % (arr, i % 2000, i + 1) for i in range(rem // 6)) +
                        ('\tif(0) %s[1999]=1;\n' % arr) * a + '\t{ }\n' * b)
    return body_u(W - 1, arr)


def rule_tail(p):
    b4 = ballast('b', 400)
    st = ''.join('\tg_bal[%d] = %d;\n' % (i, i) for i in range(400))
    bi = 'inline int bi()\n{\n%s\treturn g_bal[3];\n}\n' % st
    bp = 'inline void b(int a)\n{\n%s}\n' % st
    bx = 'void b(int a)\n{\n%s}\n' % st
    bm = 'struct C { int a; void b() {\n%s} void b(int q) {\n%s} }; C gc;\n' % (st, st)
    bf = 'inline void __fastcall b(int a, int c)\n{\n%s}\ninline void __fastcall b(int a, int c, int d)\n{\n%s}\n' % (st, st)
    cases = [('alone', b4, 'void F() { b(); }'), ('then ext()', b4, 'void F() { b(); ext(); }'),
             ('after ext()', b4, 'void F() { ext(); b(); }'),
             ('if arm', b4, 'void F() { if(g_s[0]) b(); else ext(); }'),
             ('x();return;', b4, 'void F() { if(g_s[0]) { b(); return; } ext(); }'),
             ('while', b4, 'void F() { while(g_s[0]--) b(); }'),
             ('nested, w tail', b4 + 'inline void w() { g_s[2] = 1; b(); }\n', 'void F() { ext(); w(); }'),
             ('nested, w not tail', b4 + 'inline void w() { b(); }\n', 'void F() { w(); ext(); }'),
             ('return 0 after', b4, 'int F() { b(); return 0; }'),
             ('r = bi(); return r', bi, 'int F() { int r = bi(); return r; }'),
             ('g = bi()', bi, 'void F() { g_s[3] = bi(); }'),
             ('empty inline after', b4 + 'inline void pend() {}\n', 'void F() { b(); pend(); }'),
             ('tiny() after', b4, 'void F() { b(); tiny(); }'),
             ('b(int) with a stack arg', bp, 'void F() { ext(); b(3); }'),
             ('extern b(int)', bx, 'void F() { ext(); b(3); }'),
             ('member gc.b()', bm, 'void F() { ext(); gc.b(); }'),
             ('member gc.b(int)', bm, 'void F() { ext(); gc.b(3); }'),
             ('fastcall b(int, int)', bf, 'void F() { ext(); b(3, 4); }'),
             ('fastcall b(int, int, int)', bf, 'void F() { ext(); b(3, 4, 5); }')]
    out = []
    for label, defs, fn in cases:
        c = p.calls(HEAD + defs + fn + '\n')
        out.append('%s %s' % (label, 'call' if ('b' in c or 'bi' in c) else 'in'))
    return ', '.join(out)


def rule_tailtree(p):
    st = lambda n: ''.join('\tg_bal[%d]=%d;\n' % (i % 2000, i) for i in range(n))
    big = 'inline void m() {\n%s}\ninline void w() {\n%s\tm(); ext();\n}\nvoid F() { ext(); w(); }\n' % (st(400), st(300))
    fit = 'inline void m() {\n%s}\ninline void w() {\n%s\tm(); ext();\n}\nvoid F() { ext(); w(); }\n' % (st(400), st(100))
    arm = ballast('b', 400) + ballast('c', 100) + 'void F() { if(g_s[0]) b(); else { c(); ext(); } }\n'
    arm2 = ballast('b', 100) + ballast('c', 100) + 'void F() { if(g_s[0]) b(); else { c(); ext(); } }\n'
    r = [('w(300 stores) over its limit: its m(400)', 'm' not in p.calls(HEAD + big)),
         ('w(100 stores) fits: its m(400)', 'm' not in p.calls(HEAD + fit)),
         ('tail b(400) then else-arm c(100)', 'c' not in p.calls(HEAD + arm)),
         ('tail b(100) (fits, charged) then else-arm c(100)', 'c' not in p.calls(HEAD + arm2))]
    return ', '.join('%s %s' % (l, 'in' if v else 'call') for l, v in r)


def rule_kcap(p):
    k = p.bsearch(1, 600, lambda k: p.inlined(ballast('b', k), 'b(); ext();', 'b'))
    return 'K = %d stores (B ~ %du)' % (k, 6 * k + 12)


def rule_cumul(p):
    out = []
    for k in (10, 20, 40):
        c = p.calls(HEAD + ballast('b', k) + 'void F() { %s }\n' % ('b(); ext(); ' * 10))
        out.append('K=%d: %d/10' % (k, 10 - c.count('b')))
    return ', '.join(out)


def rule_weights(p):
    out = []
    for label, stmt in (('stores', lambda i: '\tg_bal[%d] = %d;\n' % (i, i)), ('ext() calls', lambda i: '\text();\n'),
                        ('empty blocks', lambda i: '\t{ }\n')):
        k = p.bsearch(0, 600, lambda k: 'b' not in p.calls(HEAD + 'inline void b()\n{\n%s}\nvoid F() { b(); ext(); }\n' % (
            ''.join(stmt(i) for i in range(k)))))
        out.append('%s %d' % (label, k))
    return ', '.join(out)


def rule_extcap(p):
    def cap(kw, body, post='', fn='h', pre=''):
        def ok(W):
            src = HEAD + pre + '%s void h()\n{\n%s}\n' % (kw, body_u(W)) + 'void F() { %s }\n' % body + post
            return fn not in p.calls(src)
        return p.bsearch(0, 1200, ok)
    out = [('extern', 12 + cap('', 'h(); ext();')),
           ('static (one call)', 12 + cap('static', 'h(); ext();')),
           ('static (two calls)', 12 + cap('static', 'h(); ext(); h(); ext();')),
           ('inline', 12 + cap('inline', 'h(); ext();'))]

    def mok(W):
        src = HEAD + 'struct C { void m(); int a; }; C gc;\nvoid C::m()\n{\n%s}\nvoid F() { gc.m(); ext(); }\n' % body_u(W)
        return 'm' not in p.calls(src)
    out.append(('member', 17 + p.bsearch(0, 1200, mok)))
    rem = 12 + cap('', 'b(); ext(); h(); ext();', pre=ballast('b', 140))
    return 'u: ' + ', '.join('%s %d' % kv for kv in out) + '; extern after b(140 stores) (~148u left): %d' % rem


def rule_autopend(p):
    big = ''.join('\tg_s[%d]=1;\n' % i for i in range(40))
    rows = []
    for label, d in (('extern 1 store', 'void tx() { g_s[1] = 2; }\n'), ('extern 40 stores', 'void tx() {\n%s}\n' % big),
                     ('static 40 stores', 'static void tx() {\n%s}\n' % big)):
        t = 'tx(); ' * 6
        c = p.calls(HEAD + VEC + d + 'void F() { gC = gA - gB; ext(); %s gD = gA - gC; ext(); %s gE = gB - gC; ext(); %s ext(); }\n'
                    % (t, t, t))
        seq, site = [], 0
        for x in c:
            if x == 'ext':
                site += 1
            elif x.startswith('??0?$_V'):
                seq.append(site + 1)
        rows.append('%s: ctor out at site %s (%s)' % (label, ','.join(map(str, seq)) or '-',
                                                     'pending' if seq else 'not pending'))
    return '; '.join(rows)


def rule_ctor(p):
    esc = 'void *gp; float gf[3];\n' + ballast('m', 20)
    loc = '{ V o(gf[0], gf[1], gf[2]); gp = &o; }'
    init = 'm();'
    top = p.bsearch(0, 160, lambda k: not any(x.startswith('??0?$_V') for x in p.calls(   # b(k) must fit (charged)
        HEAD + VEC + esc + ballast('b', k) + 'void F() { b(); ext(); %s ext(); }\n' % loc)))
    lift = ''.join('\tg_bal[%d]=%d;\n' % (1000 + i, i) for i in range(500))

    def inl(expr, tail, W):
        """The pre-wave-9 cost probe (R last in F: tail) or the current one (R first): is the callee inlined after W u of
        ballast in R?  F's budget is 6032u; the old search ran up to 5990u."""
        r = 'inline void R()\n{\n%s\t%s\n}\n' % (body_u(W), expr)
        w = ('void F()\n{\n%s\tR();\n}\n' % lift) if tail else ('void F()\n{\n\tR();\n%s}\n' % lift)
        c = p.calls(HEAD + VEC + esc + r + w)
        return not any(x.startswith('??0?$_V') or x == 'm' or x == 'R' for x in c)
    word = lambda b: 'inlined' if b else 'refused'
    return ('3-float ctor at top level: inlined up to b(%d stores) -> cost %d-%du.  Wrapper probe at ballast 5990u '
            '(no room left): ctor as an escaping local: R last in F %s (R over its limit, tail: unlimited subtree), R first %s; '
            'a void m() of 20 stores (132u) as the last statement of R at ballast 5900u (~116u left): R last in F %s (the call is tail too), R first %s' % (
                top, 1000 - 12 - 6 * (top + 1) + 1, 1000 - 12 - 6 * top, word(inl(loc, True, 5990)), word(inl(loc, False, 5990)),
                word(inl(init, True, 5900)), word(inl(init, False, 5900))))


def rule_order(p):
    h = 'void h()\n{\n\tg_s[3] = 1;\n\tg_s[4] = 2;\n\tg_s[5] = 3;\n}\n'
    before = 'h' not in p.calls(HEAD + h + 'void F() { h(); ext(); }\n')
    after = 'h' not in p.calls(HEAD + 'void h();\nvoid F() { h(); ext(); }\n' + h)
    return 'plain helper defined before the caller: %s; after: %s' % (
        'inlined' if before else 'call', 'inlined' if after else 'call')


def rule_share(p):
    rows = []
    for pend, k in ((0, 0), (6, 0), (2, 120), (12, 0)):
        t = 'tiny(); ' * pend
        c = p.calls(HEAD + VEC + ballast('b', k) +
                    'void F() { b(); gC = gA - gB; ext(); %s gD = gA - gC; ext(); %s gE = gB - gC; ext(); %s ext(); }\n' % (t, t, t))
        seq, site = [], 0
        for x in c:
            if x == 'ext':
                site += 1
            elif x.startswith('??0?$_V'):
                seq.append(site + 1)
        ops = sum(x.startswith('??G?$_V') for x in c)
        rows.append('P=%d K=%d: ctor out at site %s%s' % (pend, k, ','.join(map(str, seq)) or '-', ' (op- out %d)' % ops if ops else ''))
    return '; '.join(rows)


RULES = [('tail', rule_tail), ('tailtree', rule_tailtree), ('kcap', rule_kcap), ('cumul', rule_cumul), ('weights', rule_weights),
         ('extcap', rule_extcap), ('autopend', rule_autopend), ('ctor', rule_ctor), ('order', rule_order),
         ('share', rule_share)]


def main():
    args = sys.argv[1:]
    flags = ['/O2', '/Ob2']
    if '--flags' in args:
        i = args.index('--flags')
        flags = args[i + 1].split()
        del args[i:i + 2]
    only = None
    if '--only' in args:
        i = args.index('--only')
        only = args[i + 1].split(',')
        del args[i:i + 2]
    if '--compare' in args:
        rows = {}
        for mod in ('d3dren', 'lithtech'):
            env = dict(os.environ, DECOMP_MODULE=mod, MSYS2_ARG_CONV_EXCL='*')
            # modcfg: VC6CL selects the engine wrapper only (a worktree sets it), D3DRENCL the renderer's
            r = subprocess.run([sys.executable, os.path.abspath(__file__), '--module', mod, '--flags', ' '.join(flags)]
                               + (['--only', ','.join(only)] if only else []), capture_output=True, text=True, env=env)
            print('%-8s %s' % (mod, r.stdout.splitlines()[0] if r.stdout else ''))
            if r.returncode:
                print(r.stdout + r.stderr)
                return 1
            rows[mod] = dict(l.split(': ', 1) for l in r.stdout.splitlines() if ': ' in l and not l.startswith('compiler'))
        for name, _ in RULES:
            if name in rows['d3dren']:
                a, b = rows['d3dren'][name], rows['lithtech'].get(name)
                print('%-7s %s %s' % (name, 'SAME' if a == b else 'DIFF', a))
                if a != b:
                    print('%-7s      lithtech: %s' % ('', b))
        return 0
    p = Probe(flags)
    print('compiler: %s (module %s), flags %s' % (toolenv.modcfg.CL, toolenv.NAME, ' '.join(flags)))
    for name, fn in RULES:
        if only and name not in only:
            continue
        print('%s: %s' % (name, fn(p)))
        sys.stdout.flush()
    return 0


if __name__ == '__main__':
    sys.exit(main())
