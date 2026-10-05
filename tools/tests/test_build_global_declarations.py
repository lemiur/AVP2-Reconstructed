import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build


class GlobalDeclarationTests(unittest.TestCase):
    def test_function_pointer_globals_keep_the_variable_name(self):
        declarations = [
            ('extern void (__fastcall *g_pfnCalcFogAlpha)(int);', 'g_pfnCalcFogAlpha'),
            ('extern void (*DAT_1006cd70)();', 'DAT_1006cd70'),
            ('int (__cdecl * const callbacks[4])(int) = {0};', 'callbacks'),
            ('void (Widget::*callback)(int);', 'callback'),
            ('void (Widget::*callbacks[3])(int);', 'callbacks'),
            ('__declspec(dllimport) void (__stdcall *callback)(int);', 'callback'),
        ]
        for declaration, name in declarations:
            with self.subTest(declaration=declaration):
                self.assertEqual(build._function_pointer_name(declaration), name)
                self.assertEqual(build._decl_name([declaration], 0, True), name)

    def test_ordinary_and_constructor_style_globals_keep_existing_names(self):
        declarations = [
            ('extern uint32 g_FrameCount;', 'g_FrameCount'),
            ('LTLink g_Link(LTLink_Init);', 'g_Link'),
        ]
        for declaration, name in declarations:
            with self.subTest(declaration=declaration):
                self.assertIsNone(build._function_pointer_name(declaration))
                self.assertEqual(build._decl_name([declaration], 0, True), name)

    def test_callback_parameters_are_not_global_function_pointer_declarators(self):
        declarations = [
            'void Register(void (*callback)(int), int flags);',
            'void Real(int); // void (*callback)(int);',
            'void *value = (void (*callback)(int))0;',
        ]
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                self.assertIsNone(build._function_pointer_name(declaration))


if __name__ == '__main__':
    unittest.main()
