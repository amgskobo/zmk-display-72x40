#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Build and run the drawing-core tests on the host, optimised and under the
# address and undefined-behaviour sanitizers, and check that the generated
# font header matches its source.

set -euo pipefail

mode="${ZMK_TEST_MODE:-quick}"
case "$mode" in
    quick|full) ;;
    *) echo "ZMK_TEST_MODE must be quick or full" >&2; exit 2 ;;
esac
export ZMK_TEST_MODE="$mode"

repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
python3 -m unittest discover -s "$repo_root/tests/integration" -p test_registration.py -v
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-display-72x40-test.XXXXXX")"

cleanup() {
    rm -rf "$build_dir"
}
trap cleanup EXIT HUP INT TERM

python3 "$repo_root/scripts/gen_font.py" --check
python3 "$repo_root/scripts/gen_status_font.py" --check

warnings=(-std=c11 -Wall -Wextra -Werror -pedantic -Wconversion -Wsign-conversion
    -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wundef)

printf '#include <zmk-display-72x40/display_72x40_core.h>\n' >"$build_dir/header.c"
cc "${warnings[@]}" -fsyntax-only -I"$repo_root/include" "$build_dir/header.c"
printf '#include <zmk-display-72x40/display_72x40_splash.h>\n' >"$build_dir/splash-header.c"
cc "${warnings[@]}" -fsyntax-only -I"$repo_root/include" "$build_dir/splash-header.c"

variants=(
    "optimised:-O2"
    "sanitized:-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
)
if [ "$mode" = quick ]; then
    variants=("sanitized:-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all")
fi

export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=print_stacktrace=1

for variant in "${variants[@]}"; do
    label="${variant%%:*}"
    read -r -a flags <<<"${variant#*:}"
    cc "${warnings[@]}" "${flags[@]}" -I"$repo_root/include" \
        "$repo_root/tests/test_display_72x40_core.c" \
        -o "$build_dir/test-$label"
    "$build_dir/test-$label"
    cc "${warnings[@]}" "${flags[@]}" -I"$repo_root/include" \
        "$repo_root/tests/test_display_72x40_splash.c" \
        -o "$build_dir/splash-$label"
    "$build_dir/splash-$label"
    cc "${warnings[@]}" "${flags[@]}" -I"$repo_root/include" \
        "$repo_root/tests/test_partial_render.c" -o "$build_dir/partial-$label"
    "$build_dir/partial-$label" status
    "$build_dir/partial-$label" led
done

if [ "$mode" = full ]; then
cc "${warnings[@]}" -O0 --coverage -I"$repo_root/include" \
    "$repo_root/tests/test_display_72x40_core.c" -o "$build_dir/core-coverage"
"$build_dir/core-coverage"
core_coverage=$(cd "$build_dir" && gcov -b -c \
    -o "$build_dir/core-coverage-test_display_72x40_core.gcno" \
    "$repo_root/tests/test_display_72x40_core.c")
printf '%s\n' "$core_coverage"
core_helper=$(printf '%s\n' "$core_coverage" | grep -F -A4 '/display_72x40_core.h')
printf '%s\n' "$core_helper" | grep -Fq 'Lines executed:100.00%'
printf '%s\n' "$core_helper" | grep -Fq 'Taken at least once:100.00%'

cc "${warnings[@]}" -O0 --coverage -I"$repo_root/include" \
    "$repo_root/tests/test_display_72x40_splash.c" -o "$build_dir/splash-coverage"
"$build_dir/splash-coverage"
coverage=$(cd "$build_dir" && gcov -b -c -o "$build_dir/splash-coverage-test_display_72x40_splash.gcno" \
    "$repo_root/tests/test_display_72x40_splash.c")
printf '%s\n' "$coverage"
helper=$(printf '%s\n' "$coverage" | grep -F -A4 '/display_72x40_splash.h')
printf '%s\n' "$helper" | grep -Fq 'Lines executed:100.00%'
printf '%s\n' "$helper" | grep -Fq 'Taken at least once:100.00%'

fi

python3 "$repo_root/tests/runtime/run.py"
python3 -m unittest discover -v -s "$repo_root/tests" -p 'test_*.py'
