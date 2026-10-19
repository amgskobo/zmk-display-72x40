#!/usr/bin/env python3
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Require actual before/after renderer headers to distinguish partial redraws."""
import argparse
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-include", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    commands = []
    identities = {}
    def run(command):
        result = subprocess.run(command, capture_output=True, text=True)
        commands.append({"command": [str(x) for x in command], "exit": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
        (args.output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        return result
    for label, include in (("before", args.baseline_include), ("after", ROOT / "include")):
        identities[label] = {str(p.relative_to(include)): hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in sorted((include / "zmk-display-72x40").glob("*.h"))}
        binary = args.output / label
        run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(include),
             str(ROOT / "tests/test_partial_render.c"), "-o", str(binary)]).check_returncode()
        for scenario in ("status", "led"):
            result = run([binary, scenario])
            if label == "after":
                result.check_returncode()
            elif result.returncode == 0 or "unchanged groups must not be redrawn" not in result.stderr:
                raise RuntimeError("baseline must reject the untouched-group sentinel assertion")
    (args.output / "summary.json").write_text(json.dumps({"status": "PASS", "identities": identities,
        "before": "Both unchanged-header assertions reject actual original full-redraw behavior",
        "after": "Both partial-update assertions and complete restored frames pass"}, indent=2) + "\n")
    print("PASS: original status/LED redraws rejected; current partial status/LED frames pass")

if __name__ == "__main__":
    main()
