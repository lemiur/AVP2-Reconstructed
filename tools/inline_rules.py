r"""Measure the compiler's inline-decision rules with toy probes (the compiler as the oracle; no unit is touched).

  python tools/inline_rules.py [--module d3dren] [--flags "/O2 /Ob2"] [--only NAME,...]
  python tools/inline_rules.py --compare [--flags "/O2 /Ob2"]     both modules' compilers side by side

Each rule is a small self-contained translation unit compiled with the module's wrapper (d3dren: VC6 RTM C2 12.00.8168;
lithtech: the Processor Pack C2 13.00.9044 through scripts\vc6cl.bat, or VC6CL in a worktree; D3DRENCL overrides the
renderer's) into build/<module>/scratch/inline_rules/;
the /FAs listing says which calls stayed out of line.  `b(K)` is an inline function of K `g[i] = i;` stores (6u each),
`ext()` a non-inline external call.  Printed values (and what tools/inline_budget.py assumes):

  tail       does a site in tail position (nothing but the function exit after it) escape the budget?  `b(400);` alone
             vs `b(400); ext();`.  (inline_budget.py: not modelled)
  kcap       largest K whose single non-tail site `b(K); ext();` is inlined: the budget B of a small function in
             stores (B ~ 6 x kcap + 12 u; inline_budget R3: 1000u floor)
  cumul      for K = 10/20/40: how many of 10 non-tail `b(K)` sites inline (cumulative charging, R7)
  extcap     largest number of stores in a NON-inline helper that /Ob2 still auto-inlines: plain extern / static /
             `inline` (inline_budget: no separate cap for auto-inline candidates)
  order      plain extern helper defined before vs after its caller: inlined in both? (definition order)
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


def rule_tail(p):
    alone = p.inlined(ballast('b', 400), 'b();', 'b')
    nontail = p.inlined(ballast('b', 400), 'b(); ext();', 'b')
    return 'b(400) as the last statement: %s; followed by ext(): %s' % (
        'inlined' if alone else 'call', 'inlined' if nontail else 'call')


def rule_kcap(p):
    k = p.bsearch(1, 600, lambda k: p.inlined(ballast('b', k), 'b(); ext();', 'b'))
    return 'K = %d stores (B ~ %du)' % (k, 6 * k + 12)


def rule_cumul(p):
    out = []
    for k in (10, 20, 40):
        c = p.calls(HEAD + ballast('b', k) + 'void F() { %s }\n' % ('b(); ext(); ' * 10))
        out.append('K=%d: %d/10' % (k, 10 - c.count('b')))
    return ', '.join(out)


def rule_extcap(p):
    out = []
    for kw in ('', 'static', 'inline'):
        k = p.bsearch(0, 300, lambda k: p.inlined(ballast('h', k, kw), 'h(); ext();', 'h'))
        out.append('%s %d' % (kw or 'extern', k))
    return 'stores: ' + ', '.join(out)


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


RULES = [('tail', rule_tail), ('kcap', rule_kcap), ('cumul', rule_cumul), ('extcap', rule_extcap), ('order', rule_order),
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
