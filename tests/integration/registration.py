#!/usr/bin/env python3
"""Check linked early/late PM registrations using exact defined ELF symbols."""
import argparse
from pathlib import Path
import re

REQUIRED = frozenset(
    prefix + device
    for device in ("display_72x40_pm", "display_72x40_pm_late")
    for prefix in ("__device_", "__init___device_", "__pm_device_")
)

def validate(text):
    # GNU nm --defined-only: address, symbol type, exact symbol name.
    defined = set(re.findall(r"^[0-9a-fA-F]+\s+[A-TV-Za-z]\s+(\S+)\s*$", text, re.MULTILINE))
    missing = REQUIRED - defined
    if missing:
        raise ValueError("Missing linked PM registration symbols: " + ", ".join(sorted(missing)))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("symbols", type=Path)
    args = parser.parse_args()
    validate(args.symbols.read_text(encoding="utf-8"))
    print("PASS: exact early/late display device, init and PM registrations")
