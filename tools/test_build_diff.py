"""Regression checks for diagnostic alignment and annotations; no compiler or shared build output needed."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import build
import gate


class MemoryImage:
    image_range = (0x10000000, 0x100a0000)

    def __init__(self, data, va=0x10001000):
        self.data, self.va, self.reads = data, va, []

    def read(self, va, n):
        self.reads.append((va, n))
        return self.data[va - self.va:va - self.va + n]


class BodyObject:
    def __init__(self, data, relocs=()):
        self.sec = SimpleNamespace(data=data)
        self.relocs = relocs

    def extent(self, symbol):
        return self.sec, 0, len(self.sec.data)

    def relocs_in(self, sec, start, end):
        return self.relocs


def import_symbol(name, secno=0, cls=2):
    return SimpleNamespace(name=name, secno=secno, cls=cls, is_section_symbol=False)


def annotation():
    return SimpleNamespace(va=0x10001000, symbol=SimpleNamespace(secno=1),
                           name='MisparsedNextDeclaration', mangled='?ActualFunction@@YAXXZ')


class BuildDiffTests(unittest.TestCase):
    @staticmethod
    def function_result(va, size, status='MATCH'):
        return SimpleNamespace(a=SimpleNamespace(kind='FUNCTION', va=va), size=size, status=status)

    def test_matching_icf_function_aliases_count_one_address_and_one_extent(self):
        rows = [self.function_result(0x10029d5a, 6), self.function_result(0x10029d5a, 6)]

        coverage = build.matched_function_coverage(rows)

        self.assertEqual(len(rows), 2)  # both FUNCTION annotations remain counted
        self.assertEqual(coverage, {0x10029d5a: 6})
        self.assertEqual(sum(coverage.values()), 6)

    def test_distinct_matching_function_addresses_sum_both_extents(self):
        rows = [self.function_result(0x1000, 6), self.function_result(0x2000, 6)]

        coverage = build.matched_function_coverage(rows)

        self.assertEqual(coverage, {0x1000: 6, 0x2000: 6})
        self.assertEqual(sum(coverage.values()), 12)

    def test_alias_coverage_uses_maximum_matching_extent_at_each_address(self):
        rows = [self.function_result(0x1000, 6), self.function_result(0x1000, 8)]

        coverage = build.matched_function_coverage(rows)

        self.assertEqual(coverage, {0x1000: 8})

    def test_legacy_gate_summary_parser_accepts_deduplicated_coverage_line(self):
        output = ('2/2 annotated functions match; 6 of 100 .text function bytes (6.000%)  [symbols: symbols.csv]\n'
                  '1 unique matched addresses (1 excess matching aliases)\n')

        parsed, compile_failed = gate.parse(output)

        self.assertFalse(compile_failed)
        self.assertEqual(parsed['matching'], 2)
        self.assertEqual(parsed['annotated'], 2)
        self.assertEqual(parsed['match_bytes'], 6)
        self.assertEqual(parsed['text_function_bytes'], 100)

    def test_failing_alias_stays_counted_while_matching_extent_is_deduplicated(self):
        rows = [self.function_result(0x10029d5a, 6), self.function_result(0x10029d5a, 6, 'CLASH')]
        unit = SimpleNamespace(name='test-no-range', annots=[
            SimpleNamespace(kind='FUNCTION', va=0x10029d5a),
            SimpleNamespace(kind='FUNCTION', va=0x10029d5a),
        ])
        symtab = SimpleNamespace(funcs={0x10029d5a: (0x10029d60, 'DllMain')})

        coverage = build.matched_function_coverage(rows)
        summary = build.unit_summaries([unit], rows, symtab)[0]

        self.assertEqual(sum(coverage.values()), 6)
        self.assertEqual(sum(r.a.kind == 'FUNCTION' for r in rows), 2)
        self.assertEqual(sum(r.a.kind == 'FUNCTION' and r.status == 'MATCH' for r in rows), 1)
        self.assertIn('1 MATCH', summary)
        self.assertIn('6 bytes matched', summary)
        self.assertIn('1 FUNCTION annotations not MATCH', summary)

    def test_named_pe_imports_are_parsed_once_and_ordinals_are_skipped(self):
        class FakePE:
            OPTIONAL_HEADER = SimpleNamespace(ImageBase=0x10000000, SizeOfImage=0x10000)
            DIRECTORY_ENTRY_IMPORT = [SimpleNamespace(imports=[
                SimpleNamespace(name=b'QueryPerformanceCounter', address=0x10002000, ordinal=None),
                SimpleNamespace(name=None, address=0x10002004, ordinal=7)])]

            def __init__(self):
                self.parses = []

            def parse_data_directories(self, directories):
                self.parses.append(directories)

        fake_pe = FakePE()
        pefile = SimpleNamespace(PE=lambda path, fast_load: fake_pe,
                                 DIRECTORY_ENTRY={'IMAGE_DIRECTORY_ENTRY_IMPORT': 1})
        with patch.dict(sys.modules, {'pefile': pefile}):
            exe = build.Exe('fake.exe')
        self.assertEqual(fake_pe.parses, [[1]])
        self.assertEqual(exe.import_slots, {'QueryPerformanceCounter': {0x10002000}})

    def test_import_spellings_bind_stdcall_cdecl_at_zero_and_decorated_miles_names(self):
        names = {
            '__imp__QueryPerformanceCounter@4': ('QueryPerformanceCounter', 0x10002000),
            '__imp__printf': ('printf', 0x10002004),
            '__imp__AIL_lock@0': ('_AIL_lock@0', 0x10002008),
        }
        obj = BodyObject(b'')
        obj.symbols = {i: import_symbol(symbol) for i, symbol in enumerate(names)}
        slots = {pe_name: {slot} for pe_name, slot in names.values()}
        bound, diagnostics = build.bind_import_symbols(SimpleNamespace(import_slots=slots), {'test': obj}, {})
        self.assertEqual(bound, {symbol: slot for symbol, (pe_name, slot) in names.items()})
        self.assertEqual(diagnostics, [])
        self.assertIn('QueryPerformanceCounter', build.import_name_candidates('__imp__QueryPerformanceCounter@4'))
        self.assertEqual(build.import_name_candidates('__imp__foo@4@8'), {'_foo@4@8', 'foo@4@8'})
        self.assertNotIn('foo', build.import_name_candidates('__imp___foo@4'))

    def test_ambiguous_duplicate_dll_names_and_normalized_collisions_stay_unbound(self):
        duplicate_pe = SimpleNamespace(DIRECTORY_ENTRY_IMPORT=[
            SimpleNamespace(dll=b'first.dll', imports=[SimpleNamespace(name=b'foo', address=0x10002000)]),
            SimpleNamespace(dll=b'second.dll', imports=[SimpleNamespace(name=b'foo', address=0x10002004)])])
        duplicate_slots = build.named_import_slots(duplicate_pe)
        obj = BodyObject(b'')
        obj.symbols = {0: import_symbol('__imp_foo')}
        bound, diagnostics = build.bind_import_symbols(SimpleNamespace(import_slots=duplicate_slots), {'dup': obj}, {})
        self.assertEqual(bound, {})
        self.assertIn('ambiguous PE import __imp_foo', diagnostics[0][1])

        collision = BodyObject(b'')
        collision.symbols = {0: import_symbol('__imp__foo@4')}
        slots = {'foo': {0x10002000}, '_foo@4': {0x10002004}}
        bound, diagnostics = build.bind_import_symbols(SimpleNamespace(import_slots=slots), {'collision': collision}, {})
        self.assertEqual(bound, {})
        self.assertIn('ambiguous PE import __imp__foo@4', diagnostics[0][1])

    def test_only_undefined_external_import_pointers_are_bound_and_name_conflicts_use_pe_slot(self):
        obj = BodyObject(b'')
        obj.symbols = {
            0: import_symbol('__imp_foo'),
            1: import_symbol('__imp_foo', secno=1),
            2: import_symbol('__imp_foo', cls=3),
            3: import_symbol('_foo@4'),
        }
        name2va = {'__imp_foo': 0x10002004}
        bound, diagnostics = build.bind_import_symbols(SimpleNamespace(import_slots={'foo': {0x10002000}}),
                                                        {'unit': obj}, name2va)
        self.assertEqual(bound, {'__imp_foo': 0x10002000})
        self.assertEqual(name2va['__imp_foo'], 0x10002004)
        self.assertEqual(len(diagnostics), 1)
        self.assertIn('namemap 10002004, PE 10002000', diagnostics[0][1])

    def test_run_check_validates_import_relocations_against_pe_slot(self):
        import_name = '__imp__foo@4'
        iat = 0x10002000
        imp = import_symbol(import_name)
        function = SimpleNamespace(name='_TestFunction', is_section_symbol=False, secno=1, cls=2)
        data = b'\xa1\0\0\0\0\xc3'
        obj = BodyObject(data, [(1, imp, build.REL_DIR32, 0)])
        obj.symbols = {0: function, 1: imp}
        obj.functions = lambda: [function]

        cases = ((iat, 'MATCH', None, None), (iat, 'MATCH', iat + 4, None),
                 (iat + 4, 'RELOC', iat + 4, None), (iat, 'MATCH', None, 'KnownImportAlias'))
        for actual, expected, old_import_va, alias in cases:
            with self.subTest(actual=actual, old_import_va=old_import_va, alias=alias), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                src, inc, output = root / 'src', root / 'include', root / 'build'
                src.mkdir()
                inc.mkdir()
                output.mkdir()
                source = src / 'test.cpp'
                source.write_text('// FUNCTION: %s 0x10001000 _TestFunction\nvoid TestFunction() {}\n' % build.modcfg.TAG)
                old_names = {'_TestFunction': 0x10001000}
                if old_import_va is not None:
                    old_names[import_name] = old_import_va
                if alias:
                    old_names[alias] = iat
                old_slots = {}
                if old_import_va is not None:
                    old_slots[old_import_va] = import_name
                if alias:
                    old_slots[iat] = alias
                output_log = io.StringIO()
                with patch.multiple(build, ROOT=str(root), SRC=str(src), INC=str(inc), BUILD=str(output)), \
                        patch.object(build, 'CoffObj', return_value=obj), \
                        patch.object(build, 'build_namemap', return_value=(
                            old_slots, old_names, {}, [])), \
                        patch.object(build, 'load_icf', return_value={}), \
                        contextlib.redirect_stdout(output_log):
                    unit = build.Unit(str(source))
                    Path(unit.base_obj).parent.mkdir(parents=True)
                    Path(unit.base_obj).touch()
                    image = MemoryImage(b'\xa1' + actual.to_bytes(4, 'little') + b'\xc3')
                    image.import_slots = {'foo': {iat}}
                    symtab = SimpleNamespace(funcs={0x10001000: (0x10001006, 'TestFunction')}, names={})
                    results, namemap = build.run_check([unit], image, symtab)
                self.assertEqual(len(results), 1)
                result = results[0]
                self.assertEqual(result.status, expected)
                self.assertEqual(result.unverified, [])
                self.assertEqual(result.bad_relocs, [] if expected == 'MATCH' else
                                 [(1, import_name, iat, actual)])
                self.assertEqual(build.ALL_NAMES[import_name], iat)
                self.assertEqual(namemap[iat], alias or import_name)
                self.assertFalse(any(va != iat and name == import_name for va, name in namemap.items()))
                if old_import_va is None:
                    self.assertNotIn('PE import slot conflict', output_log.getvalue())
                else:
                    self.assertIn('PE import slot conflict', output_log.getvalue())

    def test_function_pointer_declarators_keep_the_variable_name(self):
        for declaration, name in [
                ('extern void (__fastcall *g_pfnCalcFogAlpha)(int);', 'g_pfnCalcFogAlpha'),
                ('extern void (*DAT_1006cd70)();', 'DAT_1006cd70'),
                ('int (__cdecl * const callbacks[4])(int) = {0};', 'callbacks'),
                ('void (Widget::*callback)(int);', 'callback'),
                ('__declspec(dllimport) void (__stdcall *callback)(int);', 'callback')]:
            with self.subTest(declaration=declaration):
                self.assertEqual(build._decl_name([declaration], 0, True), name)
        for declaration in ('void Real(void (*callback)(int), int);',
                            'void Real(int); // void (*callback)(int);',
                            'void *value = (void (*callback)(int))0;'):
            self.assertIsNone(build._function_pointer_name(declaration))

    def test_callback_annotations_verify_correct_and_reject_wrong_relocation_addresses(self):
        callback_va = 0x1005872c
        callback_name = '?g_pfnCalcFogAlpha@@3P6IXPAV?$_CVector@M@@PAK@ZA'
        callback = SimpleNamespace(name=callback_name, is_section_symbol=False, secno=0, cls=2)
        function = SimpleNamespace(name='_TestFunction', is_section_symbol=False, secno=1, cls=2)
        obj = BodyObject(bytes.fromhex('a100000000c3'), [(1, callback, build.REL_DIR32, 0)])
        obj.symbols = {0: callback, 1: function}
        obj.functions = lambda: [function]
        tag = build.modcfg.TAG
        global_decl = '// GLOBAL: %s 0x1005872c\nextern void (__fastcall *g_pfnCalcFogAlpha)(int);\n' % tag
        function_decl = '// FUNCTION: %s 0x10001000\nvoid TestFunction() {}\n' % tag
        for location in ('source', 'header'):
            for actual_va in (callback_va, 0x10058c40):
                with self.subTest(location=location, actual_va=actual_va), tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    src, inc, output = root / 'src', root / 'include', root / 'build'
                    src.mkdir()
                    inc.mkdir()
                    output.mkdir()
                    source = src / 'test.cpp'
                    source.write_text((global_decl if location == 'source' else '') + function_decl)
                    (inc / 'test.h').write_text(global_decl if location == 'header' else '')
                    with patch.multiple(build, ROOT=str(root), SRC=str(src), INC=str(inc), BUILD=str(output)), \
                            patch.object(build, 'CoffObj', return_value=obj), \
                            patch.object(build, 'load_icf', return_value={}), \
                            contextlib.redirect_stdout(io.StringIO()):
                        unit = build.Unit(str(source))
                        Path(unit.base_obj).parent.mkdir(parents=True)
                        Path(unit.base_obj).touch()
                        symtab = SimpleNamespace(funcs={0x10001000: (0x10001006, 'TestFunction')},
                                                 names={actual_va: 'DAT_%08x' % actual_va})
                        image = MemoryImage(b'\xa1' + actual_va.to_bytes(4, 'little') + b'\xc3')
                        results, names = build.run_check([unit], image, symtab)
                        self.assertEqual(build.ALL_NAMES[callback_name], callback_va)
                        self.assertEqual(names[callback_va], callback_name)
                        if location == 'source':
                            self.assertEqual(unit.annots[0].name, 'g_pfnCalcFogAlpha')
                            self.assertEqual(unit.annots[0].symbol, callback_name)
                        else:
                            self.assertEqual(build.header_globals()[0][2], 'g_pfnCalcFogAlpha')
                        self.assertEqual(len(results), 1)
                        result = results[0]
                        self.assertFalse(result.unverified)
                        self.assertEqual(result.status, 'MATCH' if actual_va == callback_va else 'RELOC')
                        self.assertEqual(result.bad_relocs, [] if actual_va == callback_va else
                                         [(1, callback_name, callback_va, actual_va)])

    def test_lint_skips_callback_variables_but_keeps_real_prototype_warnings(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src, inc = root / 'src', root / 'include'
            src.mkdir()
            inc.mkdir()
            (inc / 'test.h').write_text('extern void (__fastcall *First)(int);\n'
                                      'void Real(void (*callback)(int), int);\n')
            (src / 'test.cpp').write_text('extern void (*Second)();\n'
                                        'void Real(void (*callback)(int), float);\n')
            out = io.StringIO()
            with patch.multiple(build, ROOT=str(root), SRC=str(src), INC=str(inc)), \
                    contextlib.redirect_stdout(out):
                build.lint()
            self.assertNotIn('WARNING PROTOTYPE void ', out.getvalue())
            self.assertIn('WARNING PROTOTYPE Real ', out.getvalue())

    def test_image_ranges_normalize_both_modules_without_hiding_constants(self):
        for bounds, address in [((0x10000000, 0x100a0000), 0x10054890),
                                ((0x400000, 0x800000), 0x654321)]:
            with self.subTest(bounds=bounds):
                left = [(0, 5, 'mov eax, 0')]
                right = [(bounds[0], 5, 'mov eax, 0x%x' % address)]
                self.assertEqual(build.aligned_score(left, right, {1: 'global'}, False, bounds)[0], 0)
                for constant in (bounds[0] - 1, bounds[1], 0x3f800000):
                    text = 'cmp eax, 0x%x' % constant
                    self.assertEqual(build._norm_insn(text, False, False, bounds), text)

    def test_default_range_uses_active_module_image(self):
        with patch.object(build, '_ACTIVE_IMAGE_RANGE', None), \
                patch.object(build, 'Exe', return_value=SimpleNamespace(image_range=MemoryImage.image_range)) as loader:
            self.assertEqual(build._norm_insn('mov eax, 0x10054890', False, False), 'mov eax, A')
            self.assertEqual(build._norm_insn('mov edx, 0x10054894', False, False), 'mov edx, A')
            loader.assert_called_once_with(build.EXE)

    def test_relocation_keeps_other_constants_scale_and_stack_offsets(self):
        bounds = MemoryImage.image_range
        for source, target in [
                ('test byte ptr [0], 1', 'test byte ptr [0x1006908c], 1'),
                ('mov dword ptr [4], 0xbf800000', 'mov dword ptr [0x10054894], 0xbf800000'),
                ('mov eax, dword ptr [ecx*4 + 4]', 'mov eax, dword ptr [ecx*4 + 0x10054894]'),
                ('mov dword ptr [ebp - 4], 0', 'mov dword ptr [ebp - 4], 0x10054890')]:
            with self.subTest(source=source):
                self.assertEqual(build._norm_insn(source, True, False, bounds),
                                 build._norm_insn(target, False, False, bounds))
        self.assertNotEqual(build._norm_insn('test byte ptr [0], 1', True, False, bounds),
                            build._norm_insn('test byte ptr [0x1006908c], 2', False, False, bounds))
        self.assertNotEqual(build._norm_insn('mov dword ptr [4], 0xbf800000', True, False, bounds),
                            build._norm_insn('mov dword ptr [0x10054894], 0x3f800000', False, False, bounds))
        self.assertEqual(build._norm_insn('mov dword ptr [ebp - 4], 0', True, True, bounds),
                         build._norm_insn('mov dword ptr [ebp - 8], 0x10054890', False, True, bounds))

    def test_encoded_fields_mask_all_relocations_and_preserve_member_displacements(self):
        cases = [
            # Both the memory address and immediate are relocated.
            ('c7050000000000000000', 'c70590480510c0610110', [2, 6]),
            # Only the immediate is relocated; +0x70 belongs to the object's layout.
            ('c7407000000000', 'c7407090480510', [3]),
            # Capstone omits the encoded zero displacement from [eax].
            ('8b8000000000', '8b8090480510', [2]),
            # It also omits zero displacement after an index scale.
            ('8b048500000000', '8b048590480510', [3]),
            # A negative object-file addend becomes a positive linked address.
            ('8b80fcffffff', '8b808c480510', [2]),
            # A relocated memory operand does not mask the nonrelocated immediate 1.
            ('c7050000000001000000', 'c7059048051001000000', [2]),
        ]
        for source, target, offsets in cases:
            with self.subTest(source=source, offsets=offsets):
                data, linked = bytes.fromhex(source), bytes.fromhex(target)
                symbol = SimpleNamespace(name='global')
                relocs = [(off, symbol, build.REL_DIR32, 0) for off in offsets]
                r = build.Result(annotation())
                r.size, r.target_size = len(data), len(linked)
                self.assertEqual(build.aligned_counts(r, BodyObject(data, relocs), MemoryImage(linked)),
                                 (0, 0, 1, 1))
        for target in ('c7407490480510', 'c7407094480510'):
            data, linked = bytes.fromhex('c7407000000000'), bytes.fromhex(target)
            symbol = SimpleNamespace(name='global')
            r = build.Result(annotation())
            r.size = r.target_size = len(data)
            counts = build.aligned_counts(r, BodyObject(data, [(3, symbol, build.REL_DIR32, 0)]), MemoryImage(linked))
            # +0x74 is a real layout difference; either valid relocated address may be ignored.
            self.assertEqual(counts[0], 1 if target.startswith('c74074') else 0)
        r = build.Result(annotation())
        r.size = r.target_size = 10
        counts = build.aligned_counts(r, BodyObject(bytes.fromhex('c7050000000001000000'),
                                     [(2, symbol, build.REL_DIR32, 0)]),
                                     MemoryImage(bytes.fromhex('c7059048051002000000')))
        self.assertEqual(counts[0], 1)

    def test_plain_tuple_list_callers_remain_compatible_with_multiple_relocations(self):
        left = [(0, 10, 'mov dword ptr [0], 0')]
        right = [(0x10001000, 10, 'mov dword ptr [0x10054890], 0x100161c0')]
        self.assertEqual(build.aligned_score(left, right, {2: 'global', 6: 'function'}, False,
                                            MemoryImage.image_range)[0], 0)

    def test_diff_uses_target_extent_for_shorter_and_longer_bodies(self):
        for base, target_size in [(b'\xc3', 3), (b'\xc3\x90\x90', 1)]:
            with self.subTest(base_size=len(base), target_size=target_size):
                image = MemoryImage(b'\xc3\x90\x90\xcc')
                obj = BodyObject(base)
                r = build.Result(annotation())
                r.size, r.target_size = len(base), target_size
                self.assertEqual(build.aligned_counts(r, obj, image)[2:], (len(base), target_size))
                out = io.StringIO()
                with contextlib.redirect_stdout(out):
                    build.print_diff(r, obj, image)
                self.assertIn('(%d vs %d instructions)' % (len(base), target_size), out.getvalue())
                self.assertIn('ALIGNED ?ActualFunction@@YAXXZ:', out.getvalue())
                self.assertEqual(image.reads, [(r.a.va, target_size)] * 2)

    def test_unknown_extent_keeps_compiled_length_fallback(self):
        r = build.Result(annotation())
        r.size = 3
        image = MemoryImage(b'\xc3\x90\x90\xcc')
        self.assertEqual(build.aligned_counts(r, BodyObject(b'\xc3\x90\x90'), image)[2:], (3, 3))

    def test_checker_status_and_relocation_validation_are_unchanged(self):
        a = annotation()
        for data, target, target_size, expected in [
                (b'\xc3', b'\xc3', 1, 'MATCH'),
                (b'\xc3', b'\xc2', 1, 'DIFF'),
                (b'\xc3', b'\xc3\x55', 2, 'SIZE')]:
            with self.subTest(expected=expected):
                r = build.check_function(a, BodyObject(data), MemoryImage(target),
                                         SimpleNamespace(funcs={a.va: (a.va + target_size, 'ActualFunction')}),
                                         {}, [])
                self.assertEqual((r.status, r.size, r.target_size), (expected, len(data), target_size))
        reloc_symbol = SimpleNamespace(name='global', is_section_symbol=False, secno=2, cls=2)
        obj = BodyObject(b'\xb8\x00\x00\x00\x00\xc3', [(1, reloc_symbol, build.REL_DIR32, 0)])
        target = b'\xb8\x90\x48\x05\x10\xc3'
        r = build.check_function(a, obj, MemoryImage(target),
                                 SimpleNamespace(funcs={a.va: (a.va + 6, 'ActualFunction')}),
                                 {'global': 0x10054894}, [])
        self.assertEqual((r.status, r.diffs), ('RELOC', 0))
        self.assertEqual(r.bad_relocs, [(1, 'global', 0x10054894, 0x10054890)])


if __name__ == '__main__':
    unittest.main()
