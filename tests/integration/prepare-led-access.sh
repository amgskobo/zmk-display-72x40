#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
# Public manifest URLs need no key. A caller may explicitly supply private access.
set -euo pipefail

mkdir -p ci-results
if [ -z "${LED_READ_KEY:-}" ]; then
    echo "No optional LED read key supplied; using the manifest HTTPS URL." \
        | tee ci-results/dependency-access.txt
    exit 0
fi

: "${RUNNER_TEMP:?RUNNER_TEMP is required for an explicitly supplied LED key}"
: "${GITHUB_ENV:?GITHUB_ENV is required for an explicitly supplied LED key}"
umask 077
printf '%s\n' "$LED_READ_KEY" >"$RUNNER_TEMP/display-led-read-key"
printf 'ZMK_TEST_LED_DEPLOY_KEY=%s\n' "$RUNNER_TEMP/display-led-read-key" >>"$GITHUB_ENV"
echo "Using an explicitly supplied read-only key for the LED dependency." \
    | tee ci-results/dependency-access.txt
