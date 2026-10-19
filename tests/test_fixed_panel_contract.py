# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Compile the actual production panel admission checks with controlled DT inputs."""
import pathlib
import re
import subprocess
import unittest
ROOT = pathlib.Path(__file__).resolve().parent.parent
class FixedPanelContract(unittest.TestCase):
    def test_fixed_panel_and_removed_property(self):
        source = (ROOT / "src/display_72x40.c").read_text(encoding="utf-8")
        block = re.search(r'BUILD_ASSERT\(D72X40_WIDTH ==[\s\S]*?fixed portrait layout"\);', source)
        self.assertIsNotNone(block, "production fixed-panel admission must be present")
        for width, height, old_property, accepted in ((72,40,0,True), (40,72,0,False), (128,64,0,False), (72,40,1,False)):
            with self.subTest(width=width, height=height, removed_property=old_property):
                unit = (f'#include <zmk-display-72x40/display_72x40_core.h>\n'
                        f'#define D72X40_WIDTH {width}\n#define D72X40_HEIGHT {height}\n'
                        f'#define DT_NODE_HAS_PROP(node, prop) {old_property}\n'
                        '#define BUILD_ASSERT(expr, msg) _Static_assert(expr, msg)\n' + block.group(0))
                result = subprocess.run(["cc", "-std=c11", "-Werror", "-fsyntax-only", "-I", str(ROOT / "include"), "-x", "c", "-"], input=unit, text=True, capture_output=True)
                self.assertEqual(result.returncode == 0, accepted, result.stderr)
if __name__ == "__main__":
    unittest.main()
