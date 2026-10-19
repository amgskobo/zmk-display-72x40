# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

"""The actual driver selector follows its status node, independently of chosen."""

import pathlib
import re
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parent.parent


class NativeStorageSelector(unittest.TestCase):
    def test_rotation_uses_status_node(self):
        source = (ROOT / "src" / "display_72x40.c").read_text(encoding="utf-8")
        blocks = re.findall(
            r"(?m)^#if[^\n]*(?:\\\n[^\n]*)*\n#define D72X40_NATIVE_STORAGE 1\n#endif",
            source,
        )
        self.assertEqual(len(blocks), 1, "exactly one production selector is required")
        pieces = ["#define DT_CHOSEN(name) chosen_node\n"]
        expected = []
        # These are independent metadata inputs, not generated rendering output.
        for chosen in (None, 0, 90, 180, 270):
            for instance, enabled in ((0, 0), (90, 1), (180, 0), (270, 1)):
                name = f"chosen_{chosen}_instance_{instance}"
                chosen_value = "fallback" if chosen is None else str(chosen)
                pieces.extend((
                    "#undef DT_PROP_OR\n#undef DT_INST_PROP_OR\n",
                    "#undef D72X40_NATIVE_STORAGE\n",
                    f"#define DT_PROP_OR(node, prop, fallback) {chosen_value}\n",
                    f"#define DT_INST_PROP_OR(instance, prop, fallback) {instance}\n",
                    blocks[0] + "\n",
                    "#ifndef D72X40_NATIVE_STORAGE\n#define D72X40_NATIVE_STORAGE 0\n#endif\n",
                    f"{name} D72X40_NATIVE_STORAGE\n",
                ))
                expected.append(f"{name} {enabled}")
        result = subprocess.run(
            ["cc", "-E", "-P", "-x", "c", "-"],
            input="".join(pieces), text=True, capture_output=True, check=True,
        )
        self.assertEqual(result.stdout.splitlines(), expected)


if __name__ == "__main__":
    unittest.main()
