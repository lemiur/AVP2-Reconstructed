"""flagscan: variant flag editing and the --detail comparison; inline_rules: probe source helpers."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import flagscan  # noqa: E402
import inline_rules  # noqa: E402

STL = ['/O1', '/Ob2', '/D__STL_NO_BAD_ALLOC', '/IE:/AVP2Source/build/proj/LT2/lithshared/stl']


class VariantFlags(unittest.TestCase):
    def test_native_keeps_own(self):
        self.assertEqual(flagscan.variant_flags('native', STL), STL)

    def test_replacement(self):
        self.assertEqual(flagscan.variant_flags('/O2 /Ob1', STL), ['/O2', '/Ob1'])

    def test_drop_and_append(self):
        self.assertEqual(flagscan.variant_flags('native -/Ob2 +/Ob1', STL), ['/O1'] + STL[2:] + ['/Ob1'])

    def test_substitute_keeps_position(self):
        self.assertEqual(flagscan.variant_flags('native ~/O1=/O2', STL), ['/O2'] + STL[1:])
        self.assertEqual(flagscan.variant_flags('native ~/Ox=/O2', STL), STL)

    def test_own_list_untouched(self):
        own = list(STL)
        flagscan.variant_flags('native -/Ob2', own)
        self.assertEqual(own, STL)

    def test_bad_token(self):
        with self.assertRaises(ValueError):
            flagscan.variant_flags('native /Ob1', STL)


class Compare(unittest.TestCase):
    def test_broken_gained_sums(self):
        ref = {(1, 'u', 1): ('FUNCTION', 'MATCH', 0, 0, 'f'), (2, 'u', 2): ('STUB', 'DIFF', 10, 2, 's'),
               (3, 'u', 3): ('FUNCTION', 'DIFF', 4, 0, 'g')}
        m = {(1, 'u', 1): ('FUNCTION', 'SIZE', 7, 1, 'f'), (2, 'u', 2): ('STUB', 'MATCH', 0, 0, 's'),
             (3, 'u', 3): ('FUNCTION', 'DIFF', 4, 0, 'g'), (4, 'u', 4): ('STUB', 'SIZE', None, None, 't')}
        broken, gained, aln, calls = flagscan.compare(ref, m)
        self.assertEqual(broken, [(1, 'u', 1)])          # a FUNCTION that matched before; (3) never matched
        self.assertEqual(gained, [(2, 'u', 2)])
        self.assertEqual((aln, calls), (0, 0))            # unmeasurable STUB scores count as 0


class Probes(unittest.TestCase):
    def test_ballast_is_k_stores(self):
        src = inline_rules.ballast('b', 3, 'static')
        self.assertTrue(src.startswith('static void b()'))
        self.assertEqual(src.count('g_bal['), 3)

    def test_vector_probe_has_template_ctor(self):
        self.assertIn('_V(T mx, T my, T mz)', inline_rules.VEC)


if __name__ == '__main__':
    unittest.main()
