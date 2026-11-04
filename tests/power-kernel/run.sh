#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
workspace="$(cd "${1:?resolved workspace}" && pwd)"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
output="${ZMK_TEST_OUTPUT:?fresh output required}"
if [ -e "$output" ] || [ -L "$output" ]; then echo "output already exists: $output" >&2; exit 2; fi
mkdir -p "$output"
args=()
if [ -n "${DISPLAY_POWER_SOURCE:-}" ]; then args+=("-DDISPLAY_POWER_SOURCE=$DISPLAY_POWER_SOURCE"); fi
cd "$workspace"
export ZEPHYR_BASE="$workspace/zephyr"
west build -p=always -o=-j2 -s "$repo/tests/power-kernel/app" -d "$output/build" -b native_sim -- "${args[@]}" >"$output/build.log" 2>&1
failures=0
for run in 1 2 3; do
 status=0
 timeout 12 "$output/build/zephyr/zephyr.exe" >"$output/run-$run.log" 2>&1 || status=$?
 if [ "$status" -ne 0 ] || ! grep -Fq 'PASS: reconnect retains four retries across an old running callback' "$output/run-$run.log"; then failures=$((failures + 1)); fi
 cat "$output/run-$run.log"
done
test "$failures" -eq 0
