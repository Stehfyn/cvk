"""Regression checks for the C constant parser used by Python generation."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from generate_python import constant


class PythonConstantTests(unittest.TestCase):
    def test_suffixes_preserve_hex_digits_and_integer_width(self):
        for expression, expected in (
            ("0xFFFFFFFF", 0xFFFFFFFF),
            ("0xFFFFFFFFULL", 0xFFFFFFFF),
            ("0xFF", 255),
            ("0xFULL", 15),
            ("(~0U)", 2**32 - 1),
            ("(~0ULL)", 2**64 - 1),
            ("1000.0F", 1000.0),
            ("VK_MAKE_API_VERSION(0, 1, 4, 0)", (1 << 22) | (4 << 12)),
            ("((VkDeviceSize)0)", 0),
            ('"VK_KHR_surface"', "VK_KHR_surface"),
        ):
            with self.subTest(expression=expression):
                self.assertEqual(constant(expression, {}), expected)

    def test_unknown_expressions_are_rejected(self):
        for expression in ("sizeof(int)", "value.member", "f(1)", "lambda: 1"):
            with (
                self.subTest(expression=expression),
                self.assertRaises((ValueError, KeyError)),
            ):
                constant(expression, {})


if __name__ == "__main__":
    unittest.main()
