# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_DISPLAY_042INCH)
  zephyr_include_directories(include)
  # On the app target, like ZMK's own display code: it reads the keymap,
  # endpoints and events, whose headers are only on that target's path.
  target_sources(app PRIVATE src/display_042inch.c)
  # On the app target too: it includes zmk/studio/custom.h, and through it the
  # nanopb-generated protocol header that only that target can see.
  if(CONFIG_ZMK_DISPLAY_042INCH_CUSTOM_SETTINGS)
    target_sources(app PRIVATE src/display_042inch_custom_settings.c)
  endif()
endif()

# The power reporter lives on the peripheral, where the panel does not, so it
# is built from its own symbol; the central side only listens.
if(CONFIG_ZMK_DISPLAY_042INCH_POWER_REPORTER OR CONFIG_ZMK_DISPLAY_042INCH_PEER_POWER)
  zephyr_include_directories(include)
  target_sources(app PRIVATE src/display_042inch_power.c)
endif()
