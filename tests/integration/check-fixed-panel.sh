#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Real DT/kernel compilation must reject unsupported panel contracts.
set -euo pipefail
variant="${1:-upstream}"
tests_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$tests_dir/../.." && pwd)"
source "$tests_dir/offline.sh"
prepare_offline_workspace
export ZEPHYR_BASE="$work_dir/zephyr"
west zephyr-export
for contract in stale-rotation wrong-panel; do
    if west build -s "$work_dir/zmk/app" -d "$build_root/$contract" -b native_sim//zmk_test_mock -- \
        -DCONFIG_ASSERT=y -DZMK_CONFIG="$tests_dir/runtime" \
        -DZMK_EXTRA_MODULES="$repo_root;$tests_dir/module" \
        -DEXTRA_DTC_OVERLAY_FILE="$tests_dir/$contract.overlay" >"$build_root/$contract.log" 2>&1; then
        echo "FAIL: unsupported $contract was accepted"
        exit 1
    else
        status=$?
        printf '%s\n' "$status" >"$build_root/$contract.exit"
    fi
    case "$contract" in
        stale-rotation) diagnostic='rotation was removed|rotation.*not declared' ;;
        wrong-panel) diagnostic='requires the fixed 72x40 panel' ;;
    esac
    if ! grep -Eq "$diagnostic" "$build_root/$contract.log"; then
        cat "$build_root/$contract.log"
        echo "FAIL: $contract failed for an unrelated reason"
        exit 1
    fi
    echo "PASS: actual $variant compilation rejects $contract"
done
