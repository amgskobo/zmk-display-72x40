#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Build the XIAO BLE firmware fixture (an SSD1306 on I2C with an input device
# beside it) and run the native_sim self-tests against a recording display,
# on upstream ZMK or on the DYA fork.

set -euo pipefail

variant="${1:-upstream}"
case "$variant" in
upstream | dya) ;;
*)
    echo "unknown integration variant '$variant': expected upstream or dya" >&2
    exit 2
    ;;
esac

tests_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$tests_dir/../.." && pwd)"

# Retention is opt-in. Temporary dependency workspaces still clean up on exit.
if [ -z "${ZMK_TEST_OFFLINE_WORKSPACE:-}" ] && [ -n "${ZMK_TEST_OUTPUT:-}" ]; then
    case "$ZMK_TEST_OUTPUT" in
        /*) ;;
        *) echo "ZMK_TEST_OUTPUT must be absolute" >&2; exit 2 ;;
    esac
    if [ -e "$ZMK_TEST_OUTPUT" ] || [ -L "$ZMK_TEST_OUTPUT" ]; then
        echo "evidence output already exists: $ZMK_TEST_OUTPUT" >&2
        exit 2
    fi
    mkdir -p -- "$(dirname -- "$ZMK_TEST_OUTPUT")"
    mkdir -- "$ZMK_TEST_OUTPUT"
    build_root="$ZMK_TEST_OUTPUT"
    exec 9>"$build_root/commands.log"
    BASH_XTRACEFD=9
    set -x
    python3 "$tests_dir/evidence.py" "$repo_root" >"$build_root/source-identity.json"
fi

case "${ZMK_TEST_REUSE_BUILDS:-0}" in
0 | 1) ;;
*) echo "ZMK_TEST_REUSE_BUILDS must be 0 or 1" >&2; exit 2 ;;
esac

prepare_build() {
    if [ "${ZMK_TEST_REUSE_BUILDS:-0}" = 0 ]; then
        rm -rf "$1"
    fi
}

if [ -n "${ZMK_TEST_OFFLINE_WORKSPACE:-}" ]; then
    source "$tests_dir/offline.sh"
    prepare_offline_workspace
else
if [ -n "${ZMK_TEST_WORKSPACE:-}" ]; then
    work_dir="$ZMK_TEST_WORKSPACE/$variant"
    mkdir -p "$work_dir"
else
    work_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-display-72x40-integration.XXXXXX")"
    cleanup() {
        rm -rf "$work_dir"
    }
    trap cleanup EXIT HUP INT TERM
fi

case "${ZMK_TEST_REUSE_WORKSPACE:-0}" in
1)
    test -n "${ZMK_TEST_WORKSPACE:-}" && test -d "$work_dir/.west" || {
        echo "reuse requires an explicit existing ZMK_TEST_WORKSPACE" >&2
        exit 2
    }
    cmp "$tests_dir/config/$variant/west.yml" "$work_dir/config/west.yml"
    cd "$work_dir"
    ;;
0)
    mkdir -p "$work_dir/config"
    cp "$tests_dir/config/$variant/west.yml" "$work_dir/config/west.yml"
    cd "$work_dir"
    if [ ! -d .west ]; then
        west init -l config
    fi
    west update --narrow --fetch-opt=--depth=1
    ;;
*)
    echo "ZMK_TEST_REUSE_WORKSPACE must be 0 or 1" >&2
    exit 2
    ;;
esac
fi
if [ -z "${ZMK_TEST_OFFLINE_WORKSPACE:-}" ] && [ -n "${ZMK_TEST_OUTPUT:-}" ]; then
    # Failed or partial output is retained, but never named a successful graph.
    west manifest --freeze --active-only >"$build_root/west-manifest.tmp" \
        2>"$build_root/graph-freeze.stderr"
    mv "$build_root/west-manifest.tmp" "$build_root/west-manifest.yml"
fi
west zephyr-export

export ZEPHYR_BASE="$work_dir/zephyr"

build_root="${build_root:-${ZMK_TEST_BUILD_ROOT:-$work_dir/build}}"
mkdir -p "$build_root"

cmake_args=()
extra_modules="$repo_root;$tests_dir/firmware"
peripheral_conf="$tests_dir/peripheral.conf"
if [ "$variant" = dya ]; then
    cmake_args+=("-DEXTRA_CONF_FILE=$tests_dir/studio.conf;$tests_dir/relay.conf;$tests_dir/custom-settings.conf"
                 "-DEXTRA_DTC_OVERLAY_FILE=$tests_dir/led-splash.overlay")
    extra_modules="$extra_modules;${ZMK_TEST_LED_PATTERNS_MODULE:-$work_dir/zmk-led-patterns}"
    peripheral_conf="$peripheral_conf;$tests_dir/relay.conf"
fi

firmware_dir="$build_root/firmware"
prepare_build "$firmware_dir"

if west build -s "$work_dir/zmk/app" -d "$firmware_dir" -b xiao_ble/nrf52840/zmk -- \
    -DZMK_EXTRA_MODULES="$extra_modules" \
    -DSHIELD=display_72x40_test \
    "${cmake_args[@]}" >"$firmware_dir.log" 2>&1; then
    :
else
    build_exit=$?
    echo "FAILED: $variant firmware fixture did not build"
    tail -n 80 "$firmware_dir.log"
    exit "$build_exit"
fi

firmware_config="$firmware_dir/zephyr/.config"
test -f "$firmware_dir/zephyr/zmk.uf2"
for symbol in CONFIG_ZMK_DISPLAY_72X40 CONFIG_SSD1306 CONFIG_ZMK_LOW_PRIORITY_WORK_QUEUE \
    CONFIG_PM_DEVICE CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING \
    CONFIG_ZMK_HID_INDICATORS; do
    if ! grep -q "^$symbol=y" "$firmware_config"; then
        echo "FAILED: $variant firmware fixture lacks $symbol"
        exit 1
    fi
done
if [ "$variant" = dya ]; then
    for symbol in CONFIG_ZMK_LED_PATTERNS CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH; do
        grep -q "^$symbol=y" "$firmware_config" || {
            echo "FAILED: $variant firmware fixture lacks $symbol"
            exit 1
        }
    done
fi
for symbol in CONFIG_ZMK_DISPLAY CONFIG_LVGL; do
    if grep -q "^$symbol=y" "$firmware_config"; then
        echo "FAILED: $variant firmware fixture pulled in $symbol"
        exit 1
    fi
done
if [ "$variant" = dya ]; then
    grep -q '^CONFIG_ZMK_STUDIO_LOCKING=y' "$firmware_config"
    # The peripheral's bolt needs the fork's relay; upstream leaves it out.
    grep -q '^CONFIG_ZMK_DISPLAY_72X40_PEER_POWER=y' "$firmware_config"
    grep -q '^CONFIG_ZMK_DISPLAY_72X40_CUSTOM_SETTINGS=y' "$firmware_config"
elif grep -q '^CONFIG_ZMK_DISPLAY_72X40_PEER_POWER=y' "$firmware_config"; then
    echo "FAILED: the peer power indicator was built without the relay"
    exit 1
fi
strings "$firmware_dir/zephyr/zmk.elf" >"$firmware_dir/zephyr/strings.txt"
# A device name can adjoin printable ELF bytes; line-based strings output
# cannot establish its registration. Require exact linked registration objects.
nm_tool="$(sed -n 's/^CMAKE_NM:FILEPATH=//p' "$firmware_dir/CMakeCache.txt")"
"$nm_tool" --defined-only "$firmware_dir/zephyr/zmk.elf" >"$firmware_dir/zephyr/pm-symbols.txt"
python3 "$tests_dir/registration.py" "$firmware_dir/zephyr/pm-symbols.txt"
if [ "$variant" = dya ]; then
    # The settings a Studio client lists, under the module's own heading.
    # The keys are strings of their own; the subsystem id is stored where
    # `strings` joins it to the bytes in front, so it is matched anywhere.
    for text in display.blank_after_s display.contrast display.led_splash_ms; do
        if ! grep -Fxq "$text" "$firmware_dir/zephyr/strings.txt"; then
            echo "FAILED: $variant firmware fixture lacks the setting $text"
            exit 1
        fi
    done
    if ! grep -Fq amgskobo__display "$firmware_dir/zephyr/strings.txt"; then
        echo "FAILED: $variant firmware fixture lacks the amgskobo__display subsystem"
        exit 1
    fi
elif grep -q '^CONFIG_ZMK_DISPLAY_72X40_CUSTOM_SETTINGS=y' "$firmware_config"; then
    echo "FAILED: custom settings built without the Studio RPC"
    exit 1
fi
if grep -n 'display_72x40[a-z_]*\.c:[0-9]*:[0-9]*: warning' "$firmware_dir.log"; then
    echo "FAILED: $variant firmware fixture warned in the module"
    exit 1
fi
echo "PASS: $variant firmware fixture"

peripheral_dir="$build_root/peripheral"
prepare_build "$peripheral_dir"

if west build -s "$work_dir/zmk/app" -d "$peripheral_dir" -b xiao_ble/nrf52840/zmk -- \
    -DZMK_EXTRA_MODULES="$repo_root;$tests_dir/firmware" \
    -DSHIELD=display_72x40_test \
    -DEXTRA_CONF_FILE="$peripheral_conf" >"$peripheral_dir.log" 2>&1; then
    :
else
    build_exit=$?
    echo "FAILED: $variant peripheral fixture did not build"
    tail -n 80 "$peripheral_dir.log"
    exit "$build_exit"
fi

peripheral_config="$peripheral_dir/zephyr/.config"
test -f "$peripheral_dir/zephyr/zmk.uf2"
# The panel belongs to the central; only the power reporter is built here,
# and only where the fork's relay carries what it reports.
if [ "$variant" = dya ]; then
    grep -q '^CONFIG_ZMK_DISPLAY_72X40_POWER_REPORTER=y' "$peripheral_config"
fi
if grep -q '^CONFIG_ZMK_DISPLAY_72X40=y' "$peripheral_config"; then
    echo "FAILED: the panel was built on the peripheral"
    exit 1
fi
if grep -n 'display_72x40[a-z_]*\.c:[0-9]*:[0-9]*: warning' "$peripheral_dir.log"; then
    echo "FAILED: $variant peripheral fixture warned in the module"
    exit 1
fi
echo "PASS: $variant peripheral fixture"

runtime_dir="$build_root/runtime"
prepare_build "$runtime_dir"

if west build -s "$work_dir/zmk/app" -d "$runtime_dir" -b native_sim//zmk_test_mock -- \
    -DCONFIG_ASSERT=y -DZMK_CONFIG="$tests_dir/runtime" \
    -DZMK_EXTRA_MODULES="$repo_root;$tests_dir/module" >"$runtime_dir.log" 2>&1; then
    :
else
    build_exit=$?
    echo "FAILED: $variant runtime build"
    tail -n 80 "$runtime_dir.log"
    exit "$build_exit"
fi
if grep -n 'display_72x40[a-z_]*\.c:[0-9]*:[0-9]*: warning' "$runtime_dir.log"; then
    echo "FAILED: $variant runtime build warned in the module"
    exit 1
fi

for repeat in 1 2 3; do
    runtime_log="$build_root/runtime.run.$repeat.log"
    if timeout 60 "${runtime_dir}/zephyr/zmk.exe" >"$runtime_log" 2>&1; then
        :
    else
        runtime_exit=$?
        echo "FAILED: $variant runtime execution repeat $repeat"
        tail -n 80 "$runtime_log"
        exit "$runtime_exit"
    fi
    if ! grep -Fq "display-72x40 runtime tests: PASS" "$runtime_log"; then
        echo "FAILED: $variant runtime repeat $repeat did not report success"
        tail -n 80 "$runtime_log"
        exit 1
    fi
    grep -F 'PASS: ' "$runtime_log" | sed -e 's/.*PASS: /PASS: /'
    echo "PASS: $variant runtime execution repeat $repeat"
done

# The panel's own blank timeout needs a build of its own: it darkens the panel
# part way through every test above.
blank_dir="$build_root/runtime-blank"
prepare_build "$blank_dir"

if west build -s "$work_dir/zmk/app" -d "$blank_dir" -b native_sim//zmk_test_mock -- \
    -DCONFIG_ASSERT=y -DZMK_CONFIG="$tests_dir/runtime" \
    -DEXTRA_CONF_FILE="$tests_dir/blank.conf" \
    -DZMK_EXTRA_MODULES="$repo_root;$tests_dir/module" >"$blank_dir.log" 2>&1; then
    :
else
    build_exit=$?
    echo "FAILED: $variant blank-timeout build"
    tail -n 80 "$blank_dir.log"
    exit "$build_exit"
fi
if grep -n 'display_72x40[a-z_]*\.c:[0-9]*:[0-9]*: warning' "$blank_dir.log"; then
    echo "FAILED: $variant blank-timeout build warned in the module"
    exit 1
fi
grep -q '^CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS=400$' "$blank_dir/zephyr/.config"

for repeat in 1 2 3; do
    runtime_log="$build_root/runtime-blank.run.$repeat.log"
    if timeout 60 "${blank_dir}/zephyr/zmk.exe" >"$runtime_log" 2>&1; then
        :
    else
        runtime_exit=$?
        echo "FAILED: $variant blank-timeout execution repeat $repeat"
        tail -n 80 "$runtime_log"
        exit "$runtime_exit"
    fi
    if ! grep -Fq "display-72x40 runtime tests: PASS" "$runtime_log"; then
        echo "FAILED: $variant blank-timeout repeat $repeat did not report success"
        tail -n 80 "$runtime_log"
        exit 1
    fi
    grep -F 'PASS: ' "$runtime_log" | sed -e 's/.*PASS: /PASS: /'
    echo "PASS: $variant blank-timeout execution repeat $repeat"
done

# Exercise public inputs with both peer modules absent.
ports_dir="$build_root/runtime-external-ports"
prepare_build "$ports_dir"
if west build -s "$work_dir/zmk/app" -d "$ports_dir" -b native_sim//zmk_test_mock -- \
    -DCONFIG_ASSERT=y -DZMK_CONFIG="$tests_dir/runtime" \
    -DEXTRA_CONF_FILE="$tests_dir/external-ports.conf" \
    -DZMK_EXTRA_MODULES="$repo_root;$tests_dir/module" >"$ports_dir.log" 2>&1; then
    :
else
    build_exit=$?
    echo "FAILED: $variant external-ports build"
    tail -n 80 "$ports_dir.log"
    exit "$build_exit"
fi
if grep -n 'display_72x40[a-z_]*\.c:[0-9]*:[0-9]*: warning' "$ports_dir.log"; then
    echo "FAILED: $variant external-ports build warned in the module"
    exit 1
fi
grep -q '^CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH=y$' "$ports_dir/zephyr/.config"
for symbol in CONFIG_ZMK_BLE_MESSENGER CONFIG_ZMK_LED_PATTERNS \
    CONFIG_ZMK_DISPLAY_72X40_MESSENGER CONFIG_ZMK_DISPLAY_72X40_LED_PATTERNS; do
    if grep -q "^$symbol=y" "$ports_dir/zephyr/.config"; then
        echo "FAILED: $variant external ports unexpectedly requires $symbol"
        exit 1
    fi
done
for repeat in 1 2 3; do
    runtime_log="$build_root/runtime-external-ports.run.$repeat.log"
    if timeout 60 "${ports_dir}/zephyr/zmk.exe" >"$runtime_log" 2>&1; then
        :
    else
        runtime_exit=$?
        echo "FAILED: $variant external-ports execution repeat $repeat"
        tail -n 80 "$runtime_log"
        exit "$runtime_exit"
    fi
    if ! grep -Fq "PASS: external lock and LED inputs work without Messenger or an LED producer" "$runtime_log"; then
        echo "FAILED: $variant external-ports repeat $repeat did not report success"
        tail -n 80 "$runtime_log"
        exit 1
    fi
    if ! grep -Fq "display-72x40 runtime tests: PASS" "$runtime_log"; then
        echo "FAILED: $variant external-ports repeat $repeat did not report success"
        tail -n 80 "$runtime_log"
        exit 1
    fi
    grep -F 'PASS: ' "$runtime_log" | sed -e 's/.*PASS: /PASS: /'
    echo "PASS: $variant external-ports execution repeat $repeat"
done

echo "display-72x40 $variant integration: PASS"
