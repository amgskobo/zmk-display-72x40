# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_DISPLAY_72X40)
  zephyr_include_directories(include)
  # On the app target, like ZMK's own display code: it reads the keymap,
  # endpoints and events, whose headers are only on that target's path.
  target_sources(app PRIVATE src/display_72x40.c src/display_72x40_status.c)
  if(CONFIG_ZMK_DISPLAY_72X40_MESSENGER)
    target_sources(app PRIVATE src/display_72x40_messenger.c)
  endif()
  if(CONFIG_ZMK_DISPLAY_72X40_LED_PATTERNS)
    target_sources(app PRIVATE src/display_72x40_led_patterns.c)
  endif()
  if(CONFIG_ZMK_USB AND CONFIG_ZMK_BLE)
    zephyr_link_libraries(-Wl,--wrap=zmk_endpoint_set_preferred_transport)
    zephyr_link_libraries(-Wl,--wrap=zmk_endpoint_toggle_preferred_transport)
  endif()
  # On the app target too: it includes zmk/studio/custom.h, and through it the
  # nanopb-generated protocol header that only that target can see.
  if(CONFIG_ZMK_DISPLAY_72X40_CUSTOM_SETTINGS)
    target_sources(app PRIVATE src/display_72x40_custom_settings.c)
  endif()
endif()

# The power reporter lives on the peripheral, where the panel does not, so it
# is built from its own symbol; the central side only listens.
if(CONFIG_ZMK_DISPLAY_72X40_POWER_REPORTER OR CONFIG_ZMK_DISPLAY_72X40_PEER_POWER)
  zephyr_include_directories(include)
  target_sources(app PRIVATE src/display_72x40_power.c)
endif()
