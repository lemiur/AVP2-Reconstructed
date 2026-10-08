import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import permute


class ProbeTypeDiagnosticTests(unittest.TestCase):
    def test_localized_c2440_uses_quoted_types_and_probe_destination(self):
        line = (
            r"C:\scratch\probe.cpp(267) : error C2440: '=' : 'int' ?? "
            r"'struct FUN_10033140::__tpT *' ??????????????(????? ; ???????)"
        )
        self.assertEqual(permute.parse_probe_type_diagnostic(line), (267, "int"))

    def test_english_c2440_keeps_source_type(self):
        line = (
            r"C:\scratch\probe.cpp(87) : error C2440: '=' : cannot convert from "
            r"'struct LTVector' to 'struct FUN_10033140::__tpT *'"
        )
        self.assertEqual(
            permute.parse_probe_type_diagnostic(line), (87, "struct LTVector")
        )

    def test_english_c2679_keeps_right_operand_type(self):
        line = (
            r"C:\scratch\probe.cpp(42) : error C2679: binary '=' : no operator "
            r"defined which takes a right-hand operand of type 'float' "
            r"(or there is no acceptable conversion)"
        )
        self.assertEqual(permute.parse_probe_type_diagnostic(line), (42, "float"))

    def test_localized_c2679_keeps_quoted_vector_type(self):
        line = (
            r"C:\scratch\probe.cpp(1355) : error C2679: ????? '=' : ? "
            r"'class _CVector<float>' ??????????????????????????(??????????)"
        )
        parsed = permute.parse_probe_type_diagnostic(line)
        self.assertEqual(parsed, (1355, "class _CVector<float>"))
        normalized, enum = permute.norm_type(parsed[1])
        self.assertEqual((normalized, permute.type_kind(normalized, enum)), ("LTVector", "vec"))

    def test_c2440_without_probe_destination_is_rejected(self):
        line = r"probe.cpp(12) : error C2440: '=' : 'int' cannot convert to 'Other *'"
        self.assertIsNone(permute.parse_probe_type_diagnostic(line))

    def test_unrelated_operator_diagnostics_are_rejected(self):
        c2440 = r"probe.cpp(12) : error C2440: '+' : 'int' to 'struct __tpT *'"
        c2679 = (
            r"probe.cpp(12) : error C2679: binary '+' : no operator defined which "
            r"takes a right-hand operand of type 'float' (or there is no conversion)"
        )
        localized_c2679 = r"probe.cpp(12) : error C2679: ????? '+' : ? 'float' ?????"
        self.assertIsNone(permute.parse_probe_type_diagnostic(c2440))
        self.assertIsNone(permute.parse_probe_type_diagnostic(c2679))
        self.assertIsNone(permute.parse_probe_type_diagnostic(localized_c2679))

    def test_non_error_and_unrelated_error_codes_are_rejected(self):
        self.assertIsNone(permute.parse_probe_type_diagnostic("probe.cpp(12) : warning C2440"))
        self.assertIsNone(
            permute.parse_probe_type_diagnostic(
                r"probe.cpp(12) : error C2065: '=' : 'int' to 'struct __tpT *'"
            )
        )


if __name__ == "__main__":
    unittest.main()
