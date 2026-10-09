"""inline_budget: tail positions from /Od listings, the auto-inline candidates, and the model's R11/R12 replay (no compiler)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import inline_budget as ib  # noqa: E402


def listing(*procs, public=()):
    """A minimal /FAs listing: procs are (name, comdat, [instruction lines])."""
    out = ['PUBLIC\t%s' % p for p in public]
    for name, comdat, code in procs:
        out.append('%s PROC NEAR\t\t\t\t\t; %s%s' % (name, name, ', COMDAT' if comdat else ''))
        out.append('; 12   : {')
        out += ['\t' + c if not c.endswith(':') else c for c in code]
        out.append('%s ENDP' % name)
    return '\n'.join(out + ['END'])


PRO = ['push\tebp', 'mov\tebp, esp']
EPI = ['mov\tesp, ebp', 'pop\tebp', 'ret\t0']
B = '?b@@YAXXZ'
BI = '?bi@@YAHXZ'
F = '?F@@YAXXZ'
FI = '?F@@YAHXZ'


def tails(code, fn=F, extra=()):
    lst = ib.parse_listing(listing((fn, False, PRO + code + EPI), (B, True, PRO + EPI), *extra))
    return lst[fn]['tails']


class TailPosition(unittest.TestCase):
    def test_last_statement(self):
        self.assertEqual(tails(['call\t' + B]), [True])

    def test_call_after(self):
        self.assertEqual(tails(['call\t' + B, 'call\t?ext@@YAXXZ']), [False, True])

    def test_store_after(self):
        self.assertEqual(tails(['call\t' + B, 'mov\tDWORD PTR ?g@@3HA, 1']), [False])

    def test_dead_local_store_after(self):
        self.assertEqual(tails(['call\t' + B, 'mov\tDWORD PTR _l$[ebp], 1']), [True])

    def test_if_arm_jumps_to_exit(self):
        code = ['cmp\tDWORD PTR ?g@@3HA, 0', 'je\tSHORT $L1', 'call\t' + B, 'jmp\tSHORT $L2', '$L1:',
                'call\t?ext@@YAXXZ', '$L2:']
        self.assertEqual(tails(code), [True, True])

    def test_loop_is_not_tail(self):
        code = ['$L1:', 'cmp\tDWORD PTR ?g@@3HA, 0', 'je\tSHORT $L2', 'call\t' + B, 'jmp\tSHORT $L1', '$L2:']
        self.assertEqual(tails(code), [False])

    def test_return_own_result_through_local(self):
        code = ['call\t' + BI, 'mov\tDWORD PTR _r$[ebp], eax', 'mov\teax, DWORD PTR _r$[ebp]']
        self.assertEqual(tails(code, FI, ((BI, True, PRO + EPI),)), [True])

    def test_return_constant_after_void_call(self):
        self.assertEqual(tails(['call\t' + B, 'xor\teax, eax'], FI), [False])

    def test_return_other_local(self):
        code = ['call\t' + B, 'mov\teax, DWORD PTR _l$[ebp]']
        self.assertEqual(tails(code, FI), [False])

    def test_ternary_temporary(self):
        code = ['cmp\tDWORD PTR ?g@@3HA, 0', 'je\tSHORT $L1', 'call\t' + BI, 'mov\tDWORD PTR -4+[ebp], eax',
                'jmp\tSHORT $L2', '$L1:', 'mov\tDWORD PTR -4+[ebp], 0', '$L2:', 'mov\teax, DWORD PTR -4+[ebp]']
        self.assertEqual(tails(code, FI, ((BI, True, PRO + EPI),)), [True])

    def test_empty_inline_after(self):
        pend = '?pend@@YAXXZ'
        self.assertEqual(tails(['call\t' + B, 'call\t' + pend], extra=((pend, True, PRO + EPI),)), [True, True])

    def test_storing_inline_after(self):
        tiny = '?tiny@@YAXXZ'
        self.assertEqual(tails(['call\t' + B, 'call\t' + tiny],
                               extra=((tiny, True, PRO + ['mov\tDWORD PTR ?g@@3HA, 2'] + EPI),)), [False, True])

    def test_stack_args(self):
        self.assertFalse(ib.stack_args('?h@@YAXXZ'))            # void h(void)
        self.assertTrue(ib.stack_args('?h@@YAXH@Z'))            # void h(int)
        self.assertFalse(ib.stack_args('?m@C@@QAEXXZ'))         # C::m(void): this in ecx
        self.assertTrue(ib.stack_args('?m@C@@QAEXH@Z'))         # C::m(int)
        self.assertFalse(ib.stack_args('?h@@YIXHH@Z'))          # __fastcall h(int, int)
        self.assertTrue(ib.stack_args('?h@@YIXHHH@Z'))          # __fastcall h(int, int, int)
        self.assertTrue(ib.stack_args('?h@@YIXM@Z'))            # __fastcall h(float)
        self.assertFalse(ib.stack_args('?f@@YIXPAUX@@PAI@Z'))   # __fastcall f(X *, unsigned *)

    def test_tail_site_needs_no_stack_args(self):
        hi = '?h@@YAXH@Z'
        lst = ib.parse_listing(listing((F, False, PRO + ['push\t3', 'call\t' + hi, 'add\tesp, 4'] + EPI),
                                       (hi, True, PRO + ['mov\tDWORD PTR ?g@@3HA, 1'] + EPI)))
        self.assertEqual(lst[F]['tails'], [True])
        self.assertFalse(ib.build_tree(lst, F)[0].tail)

    def test_struct_return_never_tail(self):
        self.assertFalse(ib.is_tail(['call\t' + B, 'ret\t0'], 0, None, 'struct'))


class Candidates(unittest.TestCase):
    def setUp(self):
        self.lst = ib.parse_listing(listing(
            (F, False, PRO + ['call\t?h@@YAXXZ', 'call\t?s@@YAXXZ', 'call\t?t@@YAXXZ', 'call\t?t@@YAXXZ', 'call\t' + B] + EPI),
            ('?h@@YAXXZ', False, PRO + EPI), ('?s@@YAXXZ', False, PRO + EPI), ('?t@@YAXXZ', False, PRO + EPI),
            (B, True, PRO + ['mov\tDWORD PTR ?g@@3HA, 1'] + EPI), public=(F, '?h@@YAXXZ')))

    def test_kinds(self):
        k = lambda c: ib.site_kind(self.lst, c)
        self.assertEqual(k(B), 'inline')
        self.assertEqual(k('?h@@YAXXZ'), 'auto')      # extern
        self.assertEqual(k('?s@@YAXXZ'), 'once')      # static, one reference
        self.assertEqual(k('?t@@YAXXZ'), 'auto')      # static, two references
        self.assertIsNone(k('?ext@@YAXXZ'))           # not defined in the unit
        self.assertIsNone(ib.site_kind(self.lst, '?h@@YAXXZ', auto=False))

    def test_tree(self):
        sites = ib.build_tree(self.lst, F)
        self.assertEqual([s.kind for s in sites], ['auto', 'once', 'auto', 'auto', 'inline'])
        self.assertEqual([s.tail for s in sites], [False, False, False, False, True])

    def test_static_inline(self):
        text = '\n'.join(['int g;', 'static inline void s(int a,', '  int b)', '{', '\tg = a;', '}', 'static void t()', '{',
                          '\tg = 1;', '}'])
        lst = ib.parse_listing(listing(('?s@@YAXHH@Z', False, PRO + EPI), ('?t@@YAXXZ', False, PRO + EPI),
                                       ('?u@@YAXXZ', False, PRO + EPI)))
        lst['?s@@YAXHH@Z']['line'], lst['?t@@YAXXZ']['line'], lst['?u@@YAXXZ']['line'] = 4, 8, 2
        ib.mark_static_inline(lst, text)
        self.assertTrue(lst['?s@@YAXHH@Z'].get('static_inline'))
        self.assertFalse(lst['?t@@YAXXZ'].get('static_inline'))
        self.assertTrue(lst['?u@@YAXXZ'].get('static_inline'))       # not defined in this text: a header's
        self.assertEqual(ib.site_kind(lst, '?s@@YAXHH@Z'), 'inline')

    def test_flags(self):
        self.assertEqual(ib.ob_level(['/O2']), 1)          # VC6: /O2 implies /Ob1
        self.assertEqual(ib.ob_level(['/O2', '/Ob2']), 2)
        self.assertEqual(ib.ob_level(['/O2', '/Ob1']), 1)
        self.assertEqual(ib.ob_level(['/O1', '/Ob2']), 2)
        self.assertEqual(ib.ob_level(['/O1']), 1)
        self.assertTrue(ib.size_opt(['/O1', '/Ob2']))
        self.assertFalse(ib.size_opt(['/O2', '/Ob2']))


def site(name, kind='inline', tail=False, children=()):
    s = ib.Site(name, 1, kind, tail)
    s.children = list(children)
    return s


def fix_depth(sites, d=1):
    for s in sites:
        s.depth = d
        fix_depth(s.children, d + 1)
    return sites


class Model(unittest.TestCase):
    def test_tail_escapes_budget_and_charges_nothing(self):
        costs = {'big': {'cost': 2412}, 'c': {'cost': 612}}
        sites = fix_depth([site('big', tail=True), site('c')])
        ib.simulate(sites, 1000, costs)
        self.assertEqual([s.decision for s in sites], ['tail', 'inline'])

    def test_tail_that_fits_is_charged_and_shares(self):
        costs = {'w': {'cost': 620}, 'm': {'cost': 2412}}
        sites = fix_depth([site('w', tail=True, children=[site('m')])])
        ib.simulate(sites, 1000, costs)
        self.assertEqual((sites[0].decision, sites[0].children[0].decision), ('inline', 'refused'))

    def test_tail_subtree_unlimited(self):
        costs = {'w': {'cost': 1812}, 'm': {'cost': 2412}}
        sites = fix_depth([site('w', tail=True, children=[site('m')])])
        ib.simulate(sites, 1000, costs)
        self.assertEqual((sites[0].decision, sites[0].children[0].decision), ('tail', 'inline'))
        self.assertEqual(ib.refused_multiset(sites), {})

    def test_auto_cap(self):
        costs = {'h': {'cost': 175}, 'k': {'cost': 174}, 'o': {'cost': 300}}
        sites = fix_depth([site('h', 'auto'), site('k', 'auto'), site('o', 'once')])
        ib.simulate(sites, 1000, costs)
        self.assertEqual([s.decision for s in sites], ['cap', 'inline', 'inline'])
        self.assertEqual(ib.refused_multiset(sites), {'h': 1})

    def test_auto_over_cap_in_tail(self):
        costs = {'h': {'cost': None, 'over_cap': True}}
        sites = fix_depth([site('h', 'auto', tail=True)])
        ib.simulate(sites, 1000, costs)
        self.assertEqual(sites[0].decision, 'tail')

    def test_over_cap_not_pending(self):
        costs = {'op': {'cost': 68}, 'ctor': {'cost': 47}, 'h': {'cost': 252}, 'k': {'cost': 18}}
        sites = fix_depth([site('op', children=[site('ctor')]), site('h', 'auto'), site('h', 'auto')])
        ib.simulate(sites, 1000, costs)
        self.assertEqual(sites[0].pending, 0)
        sites = fix_depth([site('op', children=[site('ctor')]), site('k', 'auto'), site('k', 'auto')])
        ib.simulate(sites, 1000, costs)
        self.assertEqual(sites[0].pending, 2)


if __name__ == '__main__':
    unittest.main()
