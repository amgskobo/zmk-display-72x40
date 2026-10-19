# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Invalid symbol sources must not silently produce firmware assets."""
import contextlib
import io
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("display_symbol_generator", ROOT / "scripts/gen_font.py")
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)

class SymbolSourceContract(unittest.TestCase):
    def test_required_symbols_and_stray_ascii(self):
        for code in range(1, 7):
            glyphs = {i: [0] * 5 for i in range(1, 7) if i != code}
            with self.assertRaisesRegex(SystemExit, "missing symbol"):
                GEN.render(glyphs, {})
        glyphs = {i: [0] * 5 for i in range(1, 7)}
        glyphs[32] = [0] * 5
        with self.assertRaisesRegex(SystemExit, "outside"):
            GEN.render(glyphs, {})
    def test_blank_marker_cannot_contain_pixels(self):
        glyphs = {i: [0] * 5 for i in range(7)}
        glyphs[0][0] = 1
        with self.assertRaisesRegex(SystemExit, "must be blank"):
            GEN.render(glyphs, {})
    def test_preview_rejects_text_and_preserves_the_blank_marker(self):
        with self.assertRaisesRegex(SystemExit, "not a status symbol"):
            GEN.preview({}, [ord('A')])
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            GEN.preview({}, [0])
        self.assertEqual(output.getvalue(), "0x00\n" + ".....\n" * 7)
    def test_malformed_incomplete_duplicate_and_unbound_rows(self):
        for text in (".....", "== 0x01 check\n....", "== 0x01 check\n.....", "== 0x01\n.....\n== 0x02"):
            with self.subTest(text=text), self.assertRaises(SystemExit):
                GEN.parse(text)
        glyph = "== 0x01 check\n" + ".....\n" * 7
        with self.assertRaisesRegex(SystemExit, "defined twice"):
            GEN.parse(glyph + glyph)

if __name__ == "__main__":
    unittest.main()
