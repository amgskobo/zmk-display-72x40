#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Build and run the drawing-core tests on the host, optimised and under the
# address and undefined-behaviour sanitizers, and check that the generated
# font header matches its source.

set -euo pipefail

repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-display-lite-test.XXXXXX")"

cleanup() {
    rm -rf "$build_dir"
}
trap cleanup EXIT HUP INT TERM

python3 "$repo_root/scripts/gen_font.py" --check

warnings=(-std=c11 -Wall -Wextra -Werror -pedantic -Wconversion -Wsign-conversion
    -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wundef)

printf '#include <zmk-display-lite/display_lite_core.h>\n' >"$build_dir/header.c"
cc "${warnings[@]}" -fsyntax-only -I"$repo_root/include" "$build_dir/header.c"

variants=(
    "optimised:-O2"
    "sanitized:-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
)

export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=print_stacktrace=1

for variant in "${variants[@]}"; do
    label="${variant%%:*}"
    read -r -a flags <<<"${variant#*:}"
    cc "${warnings[@]}" "${flags[@]}" -I"$repo_root/include" \
        "$repo_root/tests/test_display_lite_core.c" \
        -o "$build_dir/test-$label"
    "$build_dir/test-$label"
done
