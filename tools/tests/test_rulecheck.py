"""rulecheck.parse_unit and the [match] findings on a small unit."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import modcfg  # noqa: E402
import rulecheck as RC  # noqa: E402

T = modcfg.TAG
SRC = '''
struct LocalView { int a; };            /* R1: a type defined in the unit */
extern int g_Count;                     // R2: a global declared in the unit

// FUNCTION: %(t)s 0x00401000
int UsesBoth(LocalView *p)
{
	return p->a + g_Count;
}

// FUNCTION: %(t)s 0x00401020
int Offsets(void *p)
{
	return *(int *)((char *)p + 0x58);  // R4
}

// RULE-EXCEPTION: R4, the exe reads a field the struct cannot express
// FUNCTION: %(t)s 0x00401040
int Excepted(void *p)
{
	return *(int *)((char *)p + 8);
}

class Thing
{
public:
	// FUNCTION: %(t)s 0x00401060 ?Get@Thing@@QAEHXZ
	int Get() { volatile int x = m_n; return x; }   // R17
	int m_n;
};

// FUNCTION: %(t)s 0x00401080 _$E1
// FUNCTION: %(t)s 0x00401090 _$E2
static Thing g_Thing;

// FUNCTION: %(t)s 0x004010a0 ?Elsewhere@@YAXXZ
''' % {'t': T}


class RuleTest(unittest.TestCase):
    def test_parse(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'u.cpp')
            open(p, 'w').write(SRC)
            bodies, types, externs, elsewhere = RC.parse_unit(p)
        by = {b.va: b for b in bodies}
        self.assertEqual(set(by), {0x401000, 0x401020, 0x401040, 0x401060, 0x401080, 0x401090})
        self.assertEqual([e[1] for e in elsewhere], [0x4010a0])
        self.assertIn('LocalView', types)
        self.assertIn('g_Count', externs)
        self.assertEqual(by[0x401060].name, 'Get')
        self.assertEqual(by[0x401040].exc, {'R4'})
        self.assertTrue(RC.OFFSET_CAST.search(by[0x401020].code))
        self.assertIn('volatile', by[0x401060].code)

    def test_this_casts(self):
        self.assertTrue(RC.THIS_CAST.search('(CNetHandler*)this'))
        self.assertFalse(RC.THIS_CAST.search('(void*)this'))
        self.assertFalse(RC.THIS_CAST.search('reinterpret_cast<IUnknown*>(this)'))


if __name__ == '__main__':
    unittest.main()
