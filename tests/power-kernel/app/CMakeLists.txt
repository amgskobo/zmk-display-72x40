# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(display_power_kernel)
set(DISPLAY_POWER_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../../src/display_72x40_power.c" CACHE FILEPATH "Actual reporter source")
target_include_directories(app PRIVATE include)
target_compile_definitions(app PRIVATE DISPLAY_POWER_SOURCE="${DISPLAY_POWER_SOURCE}" CONFIG_ZMK_DISPLAY_72X40_POWER_REPORTER=1 CONFIG_ZMK_DISPLAY_72X40_POWER_POLL_MS=2000 CONFIG_ZMK_DISPLAY_72X40_LOG_LEVEL=3)
target_sources(app PRIVATE src/main.c)
