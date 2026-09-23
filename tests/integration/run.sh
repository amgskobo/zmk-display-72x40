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

tests_dir=/src/tests/integration

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

mkdir -p "$work_dir/config"
cp "$tests_dir/config/$variant/west.yml" "$work_dir/config/west.yml"
cd "$work_dir"

if [ ! -d .west ]; then
    west init -l config
fi
west update --narrow --fetch-opt=--depth=1
west zephyr-export

export ZEPHYR_BASE="$work_dir/zephyr"

mkdir -p "$work_dir/build"

cmake_args=()
extra_modules="/src;$tests_dir/firmware"
peripheral_conf="$tests_dir/peripheral.conf"
if [ "$variant" = dya ]; then
    cmake_args+=("-DEXTRA_CONF_FILE=$tests_dir/studio.conf;$tests_dir/relay.conf;$tests_dir/custom-settings.conf"
                 "-DEXTRA_DTC_OVERLAY_FILE=$tests_dir/led-splash.overlay")
    extra_modules="$extra_modules;$work_dir/zmk-led-patterns"
    peripheral_conf="$peripheral_conf;$tests_dir/relay.conf"
fi

firmware_dir="$work_dir/build/firmware"
rm -rf "$firmware_dir"

if ! west build -s "$work_dir/zmk/app" -d "$firmware_dir" -b xiao_ble/nrf52840/zmk -- \
    -DZMK_EXTRA_MODULES="$extra_modules" \
    -DSHIELD=display_72x40_test \
    "${cmake_args[@]}" >"$firmware_dir.log" 2>&1; then
    echo "FAILED: $variant firmware fixture did not build"
    tail -n 80 "$firmware_dir.log"
    exit 1
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
grep -Fxq display_72x40_pm "$firmware_dir/zephyr/strings.txt"
grep -Fxq display_72x40_pm_late "$firmware_dir/zephyr/strings.txt"
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

peripheral_dir="$work_dir/build/peripheral"
rm -rf "$peripheral_dir"

if ! west build -s "$work_dir/zmk/app" -d "$peripheral_dir" -b xiao_ble/nrf52840/zmk -- \
    -DZMK_EXTRA_MODULES="/src;$tests_dir/firmware" \
    -DSHIELD=display_72x40_test \
    -DEXTRA_CONF_FILE="$peripheral_conf" >"$peripheral_dir.log" 2>&1; then
    echo "FAILED: $variant peripheral fixture did not build"
    tail -n 80 "$peripheral_dir.log"
    exit 1
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

runtime_dir="$work_dir/build/runtime"
rm -rf "$runtime_dir"

if ! west build -s "$work_dir/zmk/app" -d "$runtime_dir" -b native_sim//zmk_test_mock -- \
    -DCONFIG_ASSERT=y -DZMK_CONFIG="$tests_dir/runtime" \
    -DZMK_EXTRA_MODULES="/src;$tests_dir/module" >"$runtime_dir.log" 2>&1; then
    echo "FAILED: $variant runtime build"
    tail -n 80 "$runtime_dir.log"
    exit 1
fi
if grep -n 'display_72x40[a-z_]*\.c:[0-9]*:[0-9]*: warning' "$runtime_dir.log"; then
    echo "FAILED: $variant runtime build warned in the module"
    exit 1
fi

runtime_log="$work_dir/build/runtime.run.log"
if ! timeout 60 "$runtime_dir/zephyr/zmk.exe" >"$runtime_log" 2>&1; then
    echo "FAILED: $variant runtime execution"
    tail -n 80 "$runtime_log"
    exit 1
fi
if ! grep -Fq "display-72x40 runtime tests: PASS" "$runtime_log"; then
    echo "FAILED: $variant runtime tests did not report success"
    tail -n 80 "$runtime_log"
    exit 1
fi
grep -F 'PASS: ' "$runtime_log" | sed -e 's/.*PASS: /PASS: /'
echo "PASS: $variant runtime execution"

# The panel's own blank timeout needs a build of its own: it darkens the panel
# part way through every test above.
blank_dir="$work_dir/build/runtime-blank"
rm -rf "$blank_dir"

if ! west build -s "$work_dir/zmk/app" -d "$blank_dir" -b native_sim//zmk_test_mock -- \
    -DCONFIG_ASSERT=y -DZMK_CONFIG="$tests_dir/runtime" \
    -DEXTRA_CONF_FILE="$tests_dir/blank.conf" \
    -DZMK_EXTRA_MODULES="/src;$tests_dir/module" >"$blank_dir.log" 2>&1; then
    echo "FAILED: $variant blank-timeout build"
    tail -n 80 "$blank_dir.log"
    exit 1
fi
if grep -n 'display_72x40[a-z_]*\.c:[0-9]*:[0-9]*: warning' "$blank_dir.log"; then
    echo "FAILED: $variant blank-timeout build warned in the module"
    exit 1
fi
grep -q '^CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS=400$' "$blank_dir/zephyr/.config"

blank_log="$work_dir/build/runtime-blank.run.log"
if ! timeout 60 "$blank_dir/zephyr/zmk.exe" >"$blank_log" 2>&1; then
    echo "FAILED: $variant blank-timeout execution"
    tail -n 80 "$blank_log"
    exit 1
fi
if ! grep -Fq "display-72x40 runtime tests: PASS" "$blank_log"; then
    echo "FAILED: $variant blank-timeout tests did not report success"
    tail -n 80 "$blank_log"
    exit 1
fi
grep -F 'PASS: ' "$blank_log" | sed -e 's/.*PASS: /PASS: /'

echo "display-72x40 $variant integration: PASS"
