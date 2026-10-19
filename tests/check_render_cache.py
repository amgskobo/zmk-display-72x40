#!/usr/bin/env python3
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Require the real driver fixture to reject the old redraw-on-every-notification behavior."""
import argparse
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    unit = args.output / "full.c"
    commands = []
    def run(command):
        result = subprocess.run(command, capture_output=True, text=True)
        commands.append({"command": [str(a) for a in command], "exit": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
        (args.output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        return result
    run([sys.executable, ROOT / "tests/runtime/run.py", "--emit-full", unit]).check_returncode()
    outcomes = {}
    for label, defines in (("normal", []), ("bypass", ["-DD72X40_TEST_BYPASS_CACHE=1"])):
        binary = args.output / label
        command = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", *defines,
                   "-I" + str(ROOT / "include"), "-I" + str(ROOT / "src"),
                   "-I" + str(ROOT / "tests/runtime"), str(unit), "-o", str(binary)]
        run(command).check_returncode()
        result = run([binary])
        if label == "normal":
            result.check_returncode()
        elif result.returncode == 0 or "render_calls == draws && panel_writes == writes" not in result.stderr:
            raise RuntimeError("redraw bypass did not fail the exact efficiency assertion")
        outcomes[label] = {"exit": result.returncode, "expected": label == "normal" or result.returncode != 0}
    (args.output / "summary.json").write_text(json.dumps(outcomes, indent=2) + "\n")
    print("render cache: normal PASS; bypass rejected at the unchanged-notification assertion")

if __name__ == "__main__":
    main()
