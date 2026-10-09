"""byte_gate.asm_functions: inline assembly is attributed to the function body that holds it."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import byte_gate  # noqa: E402
import modcfg  # noqa: E402

SRC = '''
inline int MulHigh(int a, int b)
{
    int r;
    __asm
    {
        mov eax, a
        imul b
        mov r, edx
    }
    return r;
}

// FUNCTION: %(tag)s 0x00401000
int UsesHelper(int x)
{
    const char *s = "{ not a brace";
    return MulHigh(x, 3);
}

// FUNCTION: %(tag)s 0x00401020
void OwnAsm()
{
    _asm int 3
}

// FUNCTION: %(tag)s 0x00401030
int Plain(int x)
{
    // __asm in a comment does not count
    return x + 1;
}
''' % {'tag': modcfg.TAG}


class Unit:
    def __init__(self, name, path):
        self.name, self.path = name, path


class AsmTest(unittest.TestCase):
    def test_attribution(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'u.cpp')
            with open(p, 'w') as f:
                f.write(SRC)
            verb, uses = byte_gate.asm_functions([Unit('u', p)], ['u'])
        self.assertEqual(verb, {'u': {0x401020}})
        self.assertEqual(uses, {'u': {0x401000: ['MulHigh']}})


if __name__ == '__main__':
    unittest.main()
