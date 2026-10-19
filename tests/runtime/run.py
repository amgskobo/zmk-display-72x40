# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

"""Exercise all production-driver scenarios in quick or full host mode.

Quick runs every fixture under ASan/UBSan. Full additionally executes the
optimized and measured coverage variants and retains the coverage gate below.

Each fixture names a source file, the functions whose lines and branches it
gates, and any further functions it only needs in order to compile. They are
lifted out of the source, under #line markers that keep their real file and
line numbers, into tests/runtime/<fixture>_harness.c in place of its
/* DRIVER_FUNCTIONS */ marker. The harness supplies stand-ins for Zephyr and
ZMK and drives the functions. Every line and branch inside a gated function
must run, measured from gcov's per-line report rather than its per-file total,
so a support function is compiled in without being held to it.

A name may end in ":2" to lift the second of two definitions -- the other
side of an #if -- and a name starting with "#define " lifts that macro.
"""

import pathlib
import re
import subprocess
import tempfile
import sys

import os

mode = os.environ.get("ZMK_TEST_MODE", "quick")
if mode not in ("quick", "full"):
    raise SystemExit("ZMK_TEST_MODE must be quick or full")
variants = (('optimized', ['-O2']), ('sanitized', ['-O1', '-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']), ('coverage', ['-O0', '--coverage']))
if mode == "quick":
    variants = tuple(v for v in variants if v[0] == "sanitized")

root = pathlib.Path(__file__).resolve().parents[2]
source_dir = root / "src"
links = ()
# Sources with a coverage gate of their own.
elsewhere = ()
# What every build of display_72x40.c compiles.
common = (
    "d72x40_schedule",
    "d72x40_schedule_now",
    "zmk_display_72x40_set_led_splash_ms",
    "d72x40_note_activity",
    "d72x40_quiet_handler",
    "d72x40_arm_quiet",
    "d72x40_activity_input_cb",
    "zmk_display_72x40_set_blank_after_ms",
    "zmk_display_72x40_set_contrast",
    "d72x40_wait_peer",
    "d72x40_collect_layer",
    "d72x40_collect_output",
    "d72x40_collect",
    "d72x40_frame_differs",
    "d72x40_flush",
    "d72x40_retry",
    "d72x40_send_contrast",
    "d72x40_work_handler",
    "zmk_display_72x40_refresh",
    "zmk_display_72x40_status_changed",
    "zmk_display_72x40_show_led_settings",
    "d72x40_listener",
    "d72x40_panel_init",
    "d72x40_pm_action",
    "d72x40_pm_device_init",
)
# The functions with lines of their own when the options are off: the #else
# sides. Everything else they compile is a subset of the full build.
optional = (
    "zmk_display_72x40_set_led_splash_ms",
    "zmk_display_72x40_status_changed",
    "zmk_display_72x40_show_led_settings",
    "d72x40_collect",
)


def without(names, *excluded):
    return tuple(name for name in names if name not in excluded)


fixtures = (
    # Every option on: turned, reset pin, contrast, sync device, split battery
    # and power, BLE, USB, battery, HID indicators, Studio locking, LED splash.
    ("full", "display_72x40.c", (
        "d72x40_reset_panel",
        "d72x40_led_splash_handler",
        "d72x40_peer_input_cb",
        "d72x40_peer_busy",
        "d72x40_peer_connected",
        "__wrap_zmk_endpoint_set_preferred_transport",
        "__wrap_zmk_endpoint_toggle_preferred_transport",
    ) + common, ()),
    # Every option off: no hidden layers, no sync device, no contrast.
    ("minimal", "display_72x40.c", ("d72x40_peer_busy:2",) + optional,
     without(common, *optional)),
    ("power", "display_72x40_power.c", (
        "d72x40_power_report",
        "d72x40_power_work_handler",
        "d72x40_power_sync_handler",
        "d72x40_power_listener",
        "d72x40_power_init",
    ), ()),
    ("settings", "display_72x40_custom_settings.c", (
        "d72x40_namespace_handler",
        "d72x40_read_int32",
        "d72x40_apply_settings",
        "d72x40_settings_event_cb",
    ), ()),
    ("status", "display_72x40_status.c", (
        "zmk_display_72x40_get_external_lock_status",
    ), ()),
    ("messenger", "display_72x40_messenger.c", (
        "zmk_display_72x40_get_external_lock_status",
        "zmk_ble_messenger_lock_state_changed",
    ), ()),
    ("led_patterns", "display_72x40_led_patterns.c", (
        "d72x40_led_patterns_listener",
    ), ()),
)


def definition(source, name):
    """Return (first line, last line, text) of name's definition."""
    if name.startswith("#define "):
        start = source.index(name)
        end = source.index("\n\n", start) + 1
        first = source.count("\n", 0, start) + 1
        return first, first + source.count("\n", start, end - 1), source[start:end]
    name, _, nth = name.partition(":")
    skip = int(nth or 1) - 1
    for match in re.finditer(r"\b" + name + r"\s*\(", source):
        closing = source.find(")", match.end())
        if not source[closing + 1:].lstrip().startswith("{"):
            continue  # a call or a prototype
        start = source.rfind("\n", 0, match.start()) + 1
        if start == match.start():
            start = source.rfind("\n", 0, start - 1) + 1  # return type on its own line
        head = source[start:match.start()]
        if not head or head[0].isspace() or head.startswith("#"):
            continue  # a call inside a body or a macro
        if skip:
            skip -= 1
            continue
        end = body_end(source, source.index("{", closing))
        first = source.count("\n", 0, start) + 1
        return first, first + source.count("\n", start, end - 1), source[start:end]
    raise RuntimeError(f"missing definition: {name}")


def body_end(source, opening):
    """Index just past the brace that closes the one at opening.

    Braces inside comments and string or character literals do not count.
    """
    depth = 0
    i = opening
    while True:
        if source.startswith("/*", i):
            i = source.index("*/", i) + 2
            continue
        if source.startswith("//", i):
            i = source.index("\n", i)
            continue
        char = source[i]
        if char in "\"'":
            i += 1
            while source[i] != char:
                i += 2 if source[i] == "\\" else 1
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1


def misses(gcov_text, ranges):
    """Count lines and branches inside ranges; list what never ran."""
    lines = executed = branches = taken = 0
    observed = [0 for _range in ranges]
    missed = []
    current = None
    for row in gcov_text.splitlines():
        if row.startswith("branch"):
            if current is None:
                continue
            branches += 1
            if re.search(r"taken [1-9]", row):
                taken += 1
            else:
                missed.append(f"  {current[1]}: {current[2].strip()}\n    {row}")
            continue
        found = re.match(r"\s*([^:]+):\s*(\d+):(.*)", row)
        if not found:
            continue
        count, number, text = found.group(1).strip(), int(found.group(2)), found.group(3)
        inside = any(first <= number <= last for first, last in ranges)
        current = (count, number, text) if inside else None
        if not inside or count == "-":
            continue
        lines += 1
        for index, (first, last) in enumerate(ranges):
            if first <= number <= last:
                observed[index] += 1
        if count.startswith(("#####", "=====")):
            missed.append(f"  {number}: {text.strip()}  (never ran)")
        else:
            executed += 1
    if not ranges or not lines:
        raise RuntimeError("coverage report has no executable lines in gated functions")
    if any(count == 0 for count in observed):
        raise RuntimeError("coverage report has no executable lines for a gated function range")
    return lines, executed, branches, taken, missed


def ungated_functions():
    """Functions defined in the sources that no fixture gates.

    A file named in `elsewhere` has a gate of its own and is skipped, so a
    function added to any other source without a test fails this run.
    """
    gated = {}
    for _fixture, file_name, names, _support in fixtures:
        gated.setdefault(file_name, set()).update(name.split(":")[0] for name in names)
    definition_pattern = re.compile(
        r"^(?:static\s+|__weak\s+)?(?:inline\s+)?(?:const\s+)?[a-z_0-9]+(?:\s+[a-z_0-9]+)?"
        r"[\s*]+([a-z_0-9]+)\([^;]*?\)\s*\{", re.M)
    missing = []
    for path in sorted(source_dir.glob("*.c")):
        if path.name in elsewhere:
            continue
        defined = set(definition_pattern.findall(path.read_text(encoding="utf-8")))
        missing += [f"{path.name}: {name}" for name in sorted(defined - gated.get(path.name, set()))]
    return missing


def render_fixture(spec):
    fixture, file_name, gated, support = spec
    source_path = source_dir / file_name
    source = source_path.read_text(encoding="utf-8")
    functions = []
    ranges = []
    for name in (*support, *gated):
        first, last, text = definition(source, name)
        if name in gated:
            ranges.append((first, last))
        elif not name.startswith("#"):
            # A harness need not call every function it only compiles.
            text = "__attribute__((unused)) " + text
        functions.append(f'#line {first} "{source_path}"\n{text}')

    harness_path = root / "tests/runtime" / f"{fixture}_harness.c"
    harness = harness_path.read_text(encoding="utf-8")
    marker = "/* DRIVER_FUNCTIONS */"
    resume = harness.count("\n", 0, harness.index(marker)) + 2
    rendered = harness.replace(marker, "\n".join(functions) +
                               f'\n#line {resume} "{harness_path}"')
    return rendered, ranges


def main():
    if len(sys.argv) > 1:
        if len(sys.argv) != 3 or sys.argv[1] not in ("--emit-settings", "--emit-full"):
            raise SystemExit("usage: run.py [--emit-settings|--emit-full FRESH_SOURCE_OUTPUT]")
        output = pathlib.Path(sys.argv[2])
        if output.exists() or output.is_symlink():
            raise SystemExit("fresh source output required")
        rendered, _ranges = render_fixture(next(spec for spec in fixtures if spec[0] == sys.argv[1][7:]))
        with output.open("x", encoding="utf-8") as handle:
            handle.write(rendered)
        return
    missing_functions = ungated_functions()
    if missing_functions:
        raise RuntimeError("functions no fixture gates:\n  " + "\n  ".join(missing_functions))

    for spec in fixtures:
        fixture, file_name, gated, support = spec
        rendered, ranges = render_fixture(spec)
        with tempfile.TemporaryDirectory(prefix=f"{fixture}-runtime-") as folder:
            work = pathlib.Path(folder)
            unit = work / "test.c"
            unit.write_text(rendered, encoding="utf-8")
            for variant, flags in variants:
                binary = work / variant
                subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                                *(["-pthread"] if fixture == "settings" else []),
                                "-I" + str(root / "include"), "-I" + str(source_dir),
                                "-I" + str(root / "tests/runtime"),
                                str(unit), *map(str, links), "-o", str(binary)],
                               check=True)
                subprocess.run([str(binary)], check=True)
            if mode == "quick":
                print(f"{fixture}: PASS (all scenarios, ASan/UBSan)")
                continue
            subprocess.run(["gcov", "-b", "-c", "-o", str(work / "coverage-test.gcno"), str(unit)],
                           cwd=work, check=True, capture_output=True, text=True)
            report = (work / f"{file_name}.gcov").read_text(encoding="utf-8")
            lines, executed, branches, taken, missed = misses(report, ranges)
            count = sum(not name.startswith("#") for name in gated)
            print(f"{fixture}: {count} functions of {file_name}: "
                  f"lines {executed}/{lines}, branches {taken}/{branches}")
            if missed:
                print("\n".join(missed))
                raise RuntimeError(f"{fixture} fell below 100% coverage")


if __name__ == "__main__":
    main()
