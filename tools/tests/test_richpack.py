"""richpack: merging keeps every section, symbol and relocation reference; pack() produces the Rich histogram."""
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import coffedit  # noqa: E402
import richpack  # noqa: E402
from coffedit import Coff, Sec  # noqa: E402


def obj(name, comp=None, assoc=False):
    c = Coff()
    c.sections.append(Sec('.text', b'\xe8\0\0\0\0\xc3', [[1, 2, 0x14]], 0x60501020 if assoc else 0x60500020))
    c.add_symbol('.text', 0, 1, 0, coffedit.CLS_STATIC)
    c.syms[-1].aux = coffedit.section_aux(6, 1)
    c.syms.append(None)
    c.add_symbol(name, 0, 1, 0x20, coffedit.CLS_EXTERNAL)
    c.add_symbol('_callee', 0, 0, 0x20, coffedit.CLS_EXTERNAL)
    if assoc:
        c.sections.append(Sec('.xdata$x', b'\0' * 4, [], 0x40301040))
        c.add_symbol('.xdata$x', 0, 2, 0, coffedit.CLS_STATIC)
        c.syms[-1].aux = coffedit.section_aux(4, 0, selection=5, number=1)
        c.syms.append(None)
    if comp is not None:
        c.add_symbol('@comp.id', comp, -1, 0, coffedit.CLS_STATIC)
    return c


class MergeTest(unittest.TestCase):
    def test_merge_rebases_sections_symbols_and_relocations(self):
        a, b = obj('_a', 0x000a1f6f, assoc=True), obj('_b')
        m = richpack.merge([a, b])
        self.assertEqual(len(m.sections), 3)
        off = len(a.syms)
        self.assertEqual(m.sections[2].relocs, [[1, 2 + off, 0x14]])
        self.assertEqual(m.syms[off + 2].name, '_b')
        self.assertEqual(m.syms[off + 2].sec, 3)
        # associative COMDAT's Number keeps pointing at its own object's .text
        xdata = [s for s in m.syms if s is not None and s.name == '.xdata$x'][0]
        self.assertEqual(struct.unpack_from('<HB', xdata.aux, 12), (1, 5))
        self.assertEqual([s.name for s in m.syms if s is not None and s.name.startswith('@comp')], ['@comp.ix'])

    def test_pack_reproduces_histogram_and_order(self):
        entries = [(0x000c1c7b, 2), (0x000a1f6f, 3), (0x000606c7, 1)]
        with tempfile.TemporaryDirectory() as d:
            paths = []
            for i in range(4):
                p = os.path.join(d, 'in%d.obj' % i)
                obj('_f%d' % i).save(p)
                paths.append(p)
            out = richpack.pack(paths, entries, os.path.join(d, 'rich'))
            self.assertEqual(len(out), 6)
            ids = []
            for p in out:
                c = Coff.load(p)
                ids.append([s.value for s in c.syms if s is not None and s.name == '@comp.id'])
            self.assertTrue(all(len(x) == 1 for x in ids))
            seq = [x[0] for x in ids]
            last = sorted(set(seq), key=lambda v: max(i for i, x in enumerate(seq) if x == v))
            self.assertEqual(last, [v for v, _ in entries])
            self.assertEqual({v: seq.count(v) for v in seq}, dict(entries))
            # every input section survives, in order
            names = [s.name for p in out for s in Coff.load(p).syms if s is not None and s.name.startswith('_f')]
            self.assertEqual(names, ['_f0', '_f1', '_f2', '_f3'])

    def test_set_timestamp_touches_only_the_field(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'x.exe')
            data = bytearray(0x200)
            data[0:2] = b'MZ'
            struct.pack_into('<I', data, 0x3c, 0x80)
            data[0x80:0x84] = b'PE\0\0'
            struct.pack_into('<I', data, 0x88, 0x11111111)
            open(p, 'wb').write(data)
            self.assertEqual(richpack.set_timestamp(p, 0x3cbcdc64), 0x11111111)
            new = open(p, 'rb').read()
            self.assertEqual(struct.unpack_from('<I', new, 0x88)[0], 0x3cbcdc64)
            self.assertEqual(new[:0x88] + new[0x8c:], bytes(data[:0x88] + data[0x8c:]))


if __name__ == '__main__':
    unittest.main()
