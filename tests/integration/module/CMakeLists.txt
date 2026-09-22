# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_DISPLAY_LITE_TEST)
    target_sources(app PRIVATE src/recording_display.c src/runtime_test.c)
endif()
