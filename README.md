# ZMK Display 72x40

[![Test](https://github.com/amgskobo/zmk-display-72x40/actions/workflows/test.yml/badge.svg)](https://github.com/amgskobo/zmk-display-72x40/actions/workflows/test.yml)

[日本語](README_JA.md)

A status panel for small monochrome OLEDs (SSD1306, SSD1309, SH1106) drawn
without LVGL. It is meant for keyboards where the panel shares a bus with a
touch controller, or where flash, RAM and battery are tight.

- the highest active layer in large type, the host connection, the battery
  levels of both halves of a split, and Caps Lock and Studio's lock when those
  features are built in;
- no redraw timer: an event that changes something shown triggers one redraw,
  and only the columns that changed are sent. An unchanged panel costs nothing;
- layers raised by pointer or touch activity can be left off the panel, so
  using a trackpad causes no display traffic;
- on a bus shared with a touch controller, writes are placed right after the
  controller's reports, clear of its next read window;
- the panel is switched off 30 seconds after the last key, encoder or pointer
  (adjustable, or never) and before the keyboard powers off, `&soft_off`
  included.

```
 72x40 panel
 +------------------------+
 |       N U M            |   layer, double size
 |                        |
 | ᛒ1 ✓                   |   BLE profile 1, connected
 | L 85%            R 72% |   both halves' batteries
 | CAPS                   |   Caps Lock
 +------------------------+
```

## Install

### 1. Add the module

In the consuming config's `config/west.yml`:

```yaml
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-display-72x40
      remote: amgskobo
      revision: main
      path: zmk-display-72x40
```

### 2. Describe the panel

Any Zephyr display that stores 8-pixel pages (`SCREEN_INFO_MONO_VTILED`)
works. A 72x40 SSD1306 on I2C, for example:

```dts
&i2c1 {
    oled: ssd1306@3c {
        compatible = "solomon,ssd1306fb";
        reg = <0x3c>;
        width = <72>;
        height = <40>;
        segment-offset = <28>;
        page-offset = <0>;
        display-offset = <0>;
        multiplex-ratio = <39>;
        segment-remap;
        com-invdir;
        prechargep = <0x22>;
        use-internal-iref;
    };
};
```

The height must be a multiple of 8.

### 3. Declare the status panel

On a unibody keyboard or a split central (the half with the keymap):

```dts
/ {
    status_panel {
        compatible = "zmk,display-72x40";
        display = <&oled>;
        /* optional */
        hidden-layers = <6 7 8>;
        sync-input-device = <&trackpad>;
        contrast = <128>;
        rotation = <90>;
    };
};
```

The node enables `CONFIG_ZMK_DISPLAY_72X40`, which selects the display driver
subsystem. Leave `CONFIG_ZMK_DISPLAY` (ZMK's LVGL display) off: both would own
the same panel, so the module is not built while it is on.

| Property | Meaning |
| --- | --- |
| `display` | The panel. Its `width` and `height` size the canvas. |
| `hidden-layers` | Layer ids never named on the panel. A change to one of them does not wake the module. |
| `sync-input-device` | An input device on the same bus; see [Sharing a bus](#sharing-a-bus). |
| `contrast` | 0-255, set once at boot. |
| `reset-gpios` | The panel's active-low reset line, pulsed before the display driver initialises; see [Resetting the panel](#resetting-the-panel). |
| `rotation` | 0, 90, 180 or 270 degrees counter-clockwise, for a panel mounted on its side; see [A panel on its side](#a-panel-on-its-side). |

### Options

| Kconfig | Default | Meaning |
| --- | --- | --- |
| `CONFIG_ZMK_DISPLAY_72X40_CUSTOM_SETTINGS` | `n` | Publish the settings below to a Studio client (needs zmk-feature-custom-settings). |
| `CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS` | `30000` | The panel's timeout: dark this long after the last key, encoder or pointer report; `0` for none. |
| `CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH` | `n` | With zmk-led-patterns, briefly show the applied pattern ID/name, brightness and speed whenever one changes. Changes wake a dark panel; unchanged values do not trigger a splash. |
| `CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH_MS` | `1800` | Initial splash duration in milliseconds. With Studio integration it can be changed at run time; `0` disables the splash. |
| `CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS` | `20` | Gathers a burst of changes into one redraw. |
| `CONFIG_ZMK_DISPLAY_72X40_SYNC_WINDOW_MS` | `50` | How long the sync device counts as reporting after a report. |
| `CONFIG_ZMK_DISPLAY_72X40_RETRY_MS` | `1000` | Delay before a failed write is retried (three times). |
| `CONFIG_ZMK_DISPLAY_72X40_PEER_POWER` | `y` | Draw the bolt for a peripheral on external power (needs the split relay). |
| `CONFIG_ZMK_DISPLAY_72X40_POWER_REPORTER` | `y` | Built on a split peripheral: tells the central about its own power. |
| `CONFIG_ZMK_DISPLAY_72X40_POWER_POLL_MS` | `2000` | How often that peripheral looks at its VBUS line. |
| `CONFIG_ZMK_DISPLAY_72X40_RESET_INIT_PRIORITY` | `84` | POST_KERNEL priority of the reset pulse; before the display driver. |
| `CONFIG_ZMK_DISPLAY_72X40_RESET_PULSE_MS` | `10` | How long the reset line is held low. |
| `CONFIG_ZMK_DISPLAY_72X40_RESET_SETTLE_MS` | `50` | How long to wait after releasing it. |
| `CONFIG_ZMK_DISPLAY_72X40_INIT_PRIORITY` | `86` | POST_KERNEL priority of the boot-time panel clear; after the display driver. |
| `CONFIG_ZMK_DISPLAY_72X40_PM_INIT_PRIORITY` | `45` | POST_KERNEL priority of the power-off device for `&soft_off`; before the panel's bus. |
| `CONFIG_ZMK_DISPLAY_72X40_PM_LATE_INIT_PRIORITY` | `55` | The same for idle sleep, which suspends in reverse order; after the panel's bus. |

## What the panel shows

| Page | Content |
| --- | --- |
| 0-1 | The highest active layer that is not hidden, centred: at double size when the whole name fits that way (six characters on 72 columns, three on 40), at ordinary size on one line when it does not, and over two lines - split at the last hyphen where there is one - when it is too long even for that. An unnamed layer shows `LAYER <index>`. The block is always two pages tall, so the lines below never move. |
| 2 | `USB`, or a Bluetooth mark with the profile number and a check (connected), cross (bonded, not connected) or ring (not bonded, open for pairing). With no host selected, the preferred transport and a cross. On the rotated 40-column portrait panel, a compact 12×8 Studio padlock shares the connection line without moving other content. Its shackle opens to the right. Wider panels keep the inline icon and label. |
| 3 | `L 85%` and `R 72%` on a split central with `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING`, otherwise `BAT 85%`. A bolt after the percentage means that half is on external power; for the peripheral that needs [the power reporter](#a-peripheral-on-external-power). `R --` means no peripheral is connected: ZMK reports 0 both for a peripheral that has dropped and for one whose battery really is flat, so the panel asks the split transport which peripherals it is carrying, and a connected one shows its level, `0%` included. Too narrow for both halves side by side, they take a line each. |
| 4 | `CAPS` while the host's Caps Lock is on (`CONFIG_ZMK_HID_INDICATORS`). |

A panel with fewer pages shows the lines that fit. The font is a 5x7 ASCII
set drawn for this module, with six symbols of its own.

## How it stays cheap

The frame lives in RAM in the controller's own layout, one byte per column
per page (360 bytes for 72x40), next to a copy of what the panel shows.

1. A subscribed event sets a flag and schedules one pass on ZMK's
   low-priority work queue, `CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS` later. A
   layer event for a hidden layer returns before that.
2. The pass reads the state and draws the whole frame. That is a few thousand
   instructions, so no drawing state is kept between passes.
3. It compares the frame with the copy page by page and sends each changed
   run of columns as one write. Runs at most 12 columns apart are sent
   together, since addressing a second run costs about as much. Changing one
   battery digit sends at most five bytes of pixels.
4. While the panel is off, passes only draw; the frame goes out, before the
   panel switches back on, at the next key, encoder turn or pointer report.

Nothing redraws between events, and the panel's timeout runs only while it is
lit. On a 72x40 panel the module takes about 3.5 KB
of flash and 0.8 KB of RAM, and the SSD1306 driver another 1.4 KB of flash
(nRF52840 build). ZMK's own display adds LVGL, its fonts and object tree, and
a 10 ms tick while the keyboard is active; on the keyboard this module was
written for, switching to it made the image 115 KB smaller.

## Resetting the panel

A panel whose RES# is only driven by its own resistor and capacitor is reset
when power comes up and never again: a reset button press, a firmware flash
or a watchdog reset all leave the controller as it was, and one wedged by a
bus glitch stays wedged until the battery comes out. Wire RES# to a pin and
give it here, as `reset-gpios` on this node, and it is pulsed at every
firmware start.

It goes on this node rather than on the panel's own for a reason. Zephyr's
SSD1306 driver, given `reset-gpios`, releases the line and sends its first
command immediately; a panel that answers that with a NACK - the 72x40
72x40 modules do - fails the driver's init with `-EIO`, and the display stays
dark for good, because nothing retries it. This module instead pulses the
line before that driver runs (`CONFIG_ZMK_DISPLAY_72X40_RESET_INIT_PRIORITY`,
below `CONFIG_DISPLAY_INIT_PRIORITY`), holds it for
`CONFIG_ZMK_DISPLAY_72X40_RESET_PULSE_MS` and then waits
`CONFIG_ZMK_DISPLAY_72X40_RESET_SETTLE_MS`, so the driver meets a controller
that has finished starting up. Leave the panel's own node without a reset
pin.

## A panel on its side

These controllers store the picture in 8-pixel pages and cannot turn it, so
`rotation` turns it here. The status is laid out for the turned size and
projected column by column on the way out: a 72x40 panel at 90 or 270 is
drawn 40 wide and 72 tall, which puts the battery halves one under the other
and breaks a layer name too long for the line in two, at its last hyphen
where there is one. A turned panel must be a multiple of 8 columns wide.

90 turns the drawing counter-clockwise, so its top edge ends up along the
panel's left edge; if the panel then reads upside down, use 270. A turn costs
one pass over the frame (a bit at a time, about 3k pixels for 72x40) and a
second buffer the size of the frame; nothing changes for an unturned panel.

## A peripheral on external power

The central knows when it is plugged in, so its own line gets the bolt for
free. A split peripheral does not: it has no USB stack - it cannot expose USB
HID - so nothing in ZMK tells it, or the central, where its power comes from.

`CONFIG_ZMK_DISPLAY_72X40_POWER_REPORTER`, built on the peripheral instead of
the panel, reads the SoC's USB VBUS detection directly (one register bit on an
nRF with a USB peripheral, available whether or not the USB stack is built),
every `CONFIG_ZMK_DISPLAY_72X40_POWER_POLL_MS`. A change goes to the central
over the fork's split relay, and `CONFIG_ZMK_DISPLAY_72X40_PEER_POWER` draws it
as the bolt beside that half's battery level.

A link is reported up before the central has subscribed to the peripheral's
relay characteristic, and a notify sent before that is dropped without saying
so - the send queues the event and reports success either way - so the state
also goes out four times over the first few seconds of a link. The repeats
cost the central nothing: a redraw that changes no pixel sends no bytes.

Both symbols default on where they can be: the reporter on an nRF split
peripheral with `CONFIG_ZMK_SPLIT_RELAY_EVENT`, the central side wherever that
relay exists. On upstream ZMK, which has no relay events, neither is built and
the peripheral's line simply has no bolt.

## Sharing a bus

Some touch controllers, the IQS7211E among them, open a read window when they
have data (their ready line goes low) and close it at the next I2C STOP. A
panel write that lands between the window opening and the controller's read
delays that read, and its STOP may close the window early.

With `sync-input-device` pointing at such a controller, the module watches
its input reports. While the controller has reported within
`CONFIG_ZMK_DISPLAY_72X40_SYNC_WINDOW_MS`, every panel write waits for the next
report and goes out right after it, one write per report: the controller's
read has just finished and its next sample is a report period away. A redraw
then takes one report per changed run, a handful for a change to every line.
When the controller has been quiet for the window, writes go out at once.
Each write is at most one
page (72 bytes plus addressing), so a read that does coincide waits for about
2 ms at 400 kHz.

Use `hidden-layers` for layers the same device raises on its own; then
moving a finger changes nothing on the panel and nothing is sent at all.

## Power

- `CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS`, 30 seconds by default, switches
  the panel off (the controller's display-off command) that long after the
  last key press, encoder turn or pointer report - the inputs ZMK's idle
  timeout counts - and the next one switches it back on. What changes while
  it is dark is drawn but not lit, so a battery reading no one is looking at
  costs nothing; the panel comes back with that state already on it. Inputs
  only write a timestamp, and the timer runs only while the panel is lit.
- There is no separate switch for following ZMK's idle, because the timeout
  covers it: shorter than ZMK's idle timeout, it always darkens the panel
  first; longer, or `0` for none, it keeps the panel lit for as long as
  asked, until the keyboard powers off, which darkens it regardless.
- ZMK powers off along two paths, `&soft_off` and idle sleep after
  `CONFIG_ZMK_IDLE_SLEEP_TIMEOUT`, and neither raises an event of its own;
  the panel keeps its supply and its picture after the SoC powers off. The
  module therefore registers small power-management devices: ZMK suspends
  them before powering off, and the suspend stops the redraw work and switches
  the panel off. The two paths suspend devices in opposite orders -
  `&soft_off` in init order, idle sleep in reverse - and switching the panel
  off needs its bus, so there is one device either side of the bus:
  `CONFIG_ZMK_DISPLAY_72X40_PM_INIT_PRIORITY` before it and
  `CONFIG_ZMK_DISPLAY_72X40_PM_LATE_INIT_PRIORITY` after it. Whichever runs
  first switches the panel off; the other finds it dark and leaves it.
- At boot the panel is cleared as soon as its driver is up, so the
  controller's leftover memory is never shown.

## Settings at run time

Two settings can change while the keyboard runs, each starting at its build
time value:

| Setting | Setter | Default |
| --- | --- | --- |
| The panel's blank timeout | `zmk_display_72x40_set_blank_after_ms()` | `CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS` |
| Contrast, 0-255 | `zmk_display_72x40_set_contrast()` | the node's `contrast`, or the driver's |

The setters (`zmk-display-72x40/display_72x40.h`) are safe from any thread: they
record the value and schedule the module's own work, which stays the one thing
that writes to the panel, so a change is sent clear of the sync device's reads
like any other write. Each takes effect at once - a new timeout is measured
from the last key, and a timeout of zero lights a panel the old one had
darkened.

With `CONFIG_ZMK_DISPLAY_72X40_CUSTOM_SETTINGS=y` a Studio client edits them
through [zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings),
under the `amgskobo__display` heading: `display.blank_after_s` (seconds,
0-600, 0 for none), `display.contrast`, and, when LED splash is compiled in,
`display.led_splash_ms` (0-10000 ms, 0 disables it). The keys have a
fixed prefix rather than the node's name - there is only ever one panel - so
stored values survive the node being renamed. It needs the patched ZMK that
carries the custom Studio RPC; upstream ZMK builds none of it.

## Compatibility and tests

The module builds against upstream ZMK `main` and against the DYA Studio fork
(`cormoran/zmk` `main+dya`). It uses only Zephyr's display and input APIs and
ZMK's public event and state functions.

- `tests/run.sh` builds the drawing core on the host with strict warnings,
  optimised and under ASan/UBSan, checks the layout, the font and the run
  finder, and checks that the generated font header matches its source. Set
  `D72X40_TEST_DUMP=1` to print the frames the tests draw.
- `tests/run-integration-docker.sh upstream|dya` builds a firmware fixture
  with an SSD1306 on a XIAO BLE and runs a native_sim test that drives ZMK
  events against a recording display and checks what reaches the panel.

The font lives in `scripts/font5x7.txt`; `scripts/gen_font.py` regenerates
`include/zmk-display-72x40/display_72x40_font.h` from it.

## License

MIT. See [LICENSE](LICENSE).
