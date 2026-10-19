#!/usr/bin/env python3
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Compare actual renderer revisions on the host; retain raw ordered measurements."""
import argparse
import hashlib
import json
import pathlib
import statistics
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-include", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--iterations", type=int, default=500000)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.iterations <= 0 or args.repeats < 3:
        parser.error("positive iterations and at least three repeats required")
    args.output.mkdir(parents=True, exist_ok=False)
    records = []
    compiles = []
    identities = {}
    for label, include in (("before", args.baseline_include), ("after", ROOT / "include")):
        identities[label] = {str(p.relative_to(include)): hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in sorted((include / "zmk-display-72x40").glob("*.h"))}
        for flags in ("-Os", "-O2"):
            binary = args.output / (label + flags)
            command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", flags,
                       "-I" + str(include), str(ROOT / "tests/benchmark_render.c"), "-o", str(binary)]
            result = subprocess.run(command, capture_output=True, text=True)
            compiles.append({"command": command, "exit": result.returncode,
                             "stdout": result.stdout, "stderr": result.stderr})
            (args.output / "compiles.json").write_text(json.dumps(compiles, indent=2) + "\n")
            result.check_returncode()
    # Alternate revision order in each repeat; reuse the same compiled binaries.
    for flags in ("-Os", "-O2"):
        for mode in range(11):
            for repeat in range(args.repeats):
                for label in (("before", "after") if repeat % 2 == 0 else ("after", "before")):
                    command = [str(args.output / (label + flags)), str(mode), str(args.iterations)]
                    result = subprocess.run(command, capture_output=True, text=True)
                    record = {"revision": label, "flags": flags, "repeat": repeat,
                              "command": command, "exit": result.returncode,
                              "stdout": result.stdout, "stderr": result.stderr}
                    records.append(record)
                    (args.output / "raw.json").write_text(json.dumps(records, indent=2) + "\n")
                    result.check_returncode()
                    record["measurement"] = json.loads(result.stdout)
    comparisons = []
    for flags in ("-Os", "-O2"):
        for mode in range(11):
            pair = {label: [r["measurement"] for r in records if r["revision"] == label and
                           r["flags"] == flags and r["measurement"]["mode"] == mode]
                    for label in ("before", "after")}
            signatures = {(m["checksum"], m["frame_hash"]) for group in pair.values() for m in group}
            if len(signatures) != 1:
                raise RuntimeError(f"observable frame mismatch: {flags} mode {mode}")
            before = statistics.median(m["cpu_seconds"] for m in pair["before"])
            after = statistics.median(m["cpu_seconds"] for m in pair["after"])
            comparisons.append({"flags": flags, "mode": mode, "before_seconds": before,
                                "after_seconds": after, "cpu_reduction_percent": 100 * (1 - after / before),
                                "before_draws": pair["before"][0]["draws"], "after_draws": pair["after"][0]["draws"]})
    summary = {"scope": "Host process CPU only; no MCU, power or physical-bus claim",
               "identities": identities, "comparisons": comparisons}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary))

if __name__ == "__main__":
    main()
