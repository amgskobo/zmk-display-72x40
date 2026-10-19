# ZMK Display 72x40

[![Test](https://github.com/amgskobo/zmk-display-72x40/actions/workflows/test.yml/badge.svg)](https://github.com/amgskobo/zmk-display-72x40/actions/workflows/test.yml)

[日本語](README_JA.md)

A status panel for a fixed 72x40 OLED, drawn without LVGL as a
40x72 portrait. Its controller page layout and mounting direction are fixed.
There is no configurable orientation or general-purpose panel layout.

- the highest active layer in large type, the host connection, the battery
  levels of both halves of a split, and Caps Lock and the service locks when those
  features are built in;
- no redraw timer: an event that changes something shown triggers one redraw,
  and only the columns that changed are sent. Unchanged frames cause no panel writes;
- layers raised by pointer or touch activity can be left off the panel, so
  using a trackpad causes no display traffic;
- on a bus shared with a touch controller, writes are scheduled after its
  reports to reduce contention; actual bus timing requires board measurements;
- the panel is switched off 30 seconds after the last key, encoder or pointer
  (adjustable, or never) and before the keyboard powers off, `&soft_off`
  included.


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

Use a 72x40 SSD1306 on I2C with 8-pixel pages
(`SCREEN_INFO_MONO_VTILED`):

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

The physical size must be exactly 72x40. Drawing coordinates are fixed at 40x72.

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
    };
};
```

The node enables `CONFIG_ZMK_DISPLAY_72X40`, which selects the display driver
subsystem. Leave `CONFIG_ZMK_DISPLAY` (ZMK's LVGL display) off: both would own
the same panel, so the module is not built while it is on.

| Property | Meaning |
| --- | --- |
| `display` | The fixed 72x40 panel, drawn in 40x72 portrait coordinates. |
| `hidden-layers` | Layer ids never named on the panel. A change to one of them does not wake the module. |
| `sync-input-device` | An input device on the same bus; see [Sharing a bus](#sharing-a-bus). |
| `contrast` | 0-255, set once at boot. |
| `reset-gpios` | The panel's active-low reset line, pulsed before the display driver initialises; see [Resetting the panel](#resetting-the-panel). |

### Options

| Kconfig | Default | Meaning |
| --- | --- | --- |
| `CONFIG_ZMK_DISPLAY_72X40_CUSTOM_SETTINGS` | `n` | Publish the settings below to a Studio client (needs zmk-feature-custom-settings). |
| `CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS` | `30000` | The panel's timeout: dark this long after the last key, encoder or pointer report; `0` for none. |
| `CONFIG_ZMK_DISPLAY_72X40_MESSENGER` | `y` when Messenger is enabled | Optional adapter for Messenger's admission-lock getter and observer. Set `n` to supply another lock provider. |
| `CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH` | `n` | Briefly show pattern ID/name, brightness and speed supplied through the display API. An accepted request wakes a dark panel. No LED module is required. |
| `CONFIG_ZMK_DISPLAY_72X40_LED_PATTERNS` | `y` when LED splash and zmk-led-patterns are enabled | Optional adapter from zmk-led-patterns events to the numeric display API. Set `n` to supply values from another producer. |
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

The fixed 40x72 drawing uses these groups:

| Rows | Content |
| --- | --- |
| 0-12 | Inverted header with the centred layer name at its native 10px font size. Longer names truncate at a whole glyph inside the header. |
| 16-24 | BLE symbol and active Profile on the left; USB symbol on the right. A check identifies the ready HID endpoint selected by ZMK, so at most one appears. A connected inactive transport has no mark; a disconnected bonded BLE Profile or unavailable USB HID has a cross. An unpaired BLE Profile has no mark. USB power is shown separately by the battery bolt. |
| 27-36 | Centred `CAPS` when enabled. This row is reserved so other groups never move. |
| 38-47, 49-58 | Left/right battery labels and right-aligned percentages, with a reserved charging bolt. An unavailable peer shows `--`; a connected empty battery shows `0%`. A unibody uses `B` and omits the peer row. |
| 60-70 | `S` and the Studio padlock on the left; `M` and the Messenger padlock on the right, when each source is available. The 9x11 locks have filled bodies and a shackle that opens diagonally. Both have an outer margin and never overlap their labels. |

With the Messenger adapter enabled, `M` follows the Messenger admission window.
Its expiry closes the displayed padlock while already admitted sessions may
continue. The adapter implements the public observer in Messenger's `service.h`
and forwards open, close and expiry to the display's status invalidation API.
It does not replace the advertising hook or add a polling timer. Service-state
changes do not wake a timed-out panel; normal input still does.

Text and the LED settings view use Panel Status 10, a printable-ASCII bitmap
subset derived from Mona10. The compact 5x7 data supplies dedicated status
symbols. Firmware reads packed pixels without a font parser or shaping engine.
The subset retains [OFL-1.1](licenses/mona-OFL.txt), separately from the module's
MIT code. Its source revision and bitmap provenance are in
[scripts/status10.txt](scripts/status10.txt).

## How it stays cheap

The frame lives in RAM in the controller's own layout, one byte per column
per page (360 bytes for 72x40), next to a copy of what the panel shows.

1. A subscribed event sets a flag and schedules one pass on ZMK's
   low-priority work queue, `CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS` later. A
   layer event for a hidden layer returns before that.
2. The pass reads the current state. An exact comparison with the last rendered
   status or LED tuple skips drawing when nothing changed. A changed status
   redraws only affected fixed groups; the LED view redraws the changed value
   range. A view switch or a complete status change draws a fresh full frame.
3. It compares the frame with the copy page by page and sends each changed
   run of columns as one write. Runs at most 12 columns apart are sent
   together, since addressing a second run costs about as much. Portrait digits
   span more than one controller page; the exact sent bytes depend on the
   changed pixels and their neighboring values.
4. While the panel is off, passes still acquire status and render changed
   states without bus writes. The frame goes out before the panel switches
   back on at the next key, encoder turn or pointer report.

Nothing redraws between events, and the panel's timeout runs only while it is
lit. Resource use depends on the selected features; use the current linked
firmware's memory-region report rather than a historical module size. The fixed panel
uses two 360-byte frame buffers, 720 bytes total, with no intermediate frame.
A separate exact-state cache is 44 bytes on the supported ARM/native ABI; it is
not another frame. It is owned by the display work handler and reset at init.
Partial rendering reuses that same cache and adds no persistent RAM. Peer
battery visibility also invalidates the local B/L label. A changed LED range
restores every value inside it, including an unchanged middle brightness line.
ZMK's own display adds
LVGL, its fonts and object tree, and a 10ms tick while the keyboard is active.

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

## Fixed mounting and migration

The drawing is permanently 40x72 and writes directly into the controller's
72x40 page memory. The existing screen appearance is preserved. The
rotation property, all orientation selectors, generic projection and landscape
layouts have been removed. Delete any old rotation property from the status
node; even an explicit zero is rejected at build time rather than ignored.
Other physical panel sizes also fail at build time.

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
do not add panel bytes or redraw an identical status; event handling and
status acquisition still occur.

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
Each write is at most one page (72 bytes plus addressing). Its estimated wire
time is about 2 ms at 400 kHz; that estimate excludes scheduler, driver and bus
overhead and does not bound physical RDY-to-read latency.

Use `hidden-layers` for layers the same device raises on its own; then
moving a finger changes nothing on the panel and nothing is sent at all.

## Power

- `CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS`, 30 seconds by default, switches
  the panel off (the controller's display-off command) that long after the
  last key press, encoder turn or pointer report - the inputs ZMK's idle
  timeout counts - and the next one switches it back on. What changes while
  it is dark is drawn but not lit, so a battery reading no one is looking at
  does not wake the panel; rendering still uses CPU. The panel comes back with
  that state already prepared. Inputs
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

## Optional sources and module boundaries

The core driver includes no Messenger or LED producer headers. Drawing,
blank/wake policy and all bus writes stay in the display; the short
[Messenger adapter](src/display_72x40_messenger.c) and
[LED event adapter](src/display_72x40_led_patterns.c) only translate their
producers' public APIs. The existing Custom Settings adapter remains separate.
The defaults preserve the bindings used by existing consumers.

The public [runtime header](include/zmk-display-72x40/display_72x40.h) offers:

| Input | Contract |
| --- | --- |
| `zmk_display_72x40_get_external_lock_status()` | Default weak provider returns unavailable. One replacement strong definition returns `{available, unlocked}` without blocking. Disable the Messenger adapter before supplying a replacement. The second lock retains its `M` label. |
| `zmk_display_72x40_status_changed()` | Marks status dirty and schedules the existing queue. It does not reset activity, wake a dark panel or access the bus in the caller. Publish the producer's current state before calling it. |
| `zmk_display_72x40_show_led_settings(pattern, brightness, speed)` | Available even without an LED producer. Ignored before initialization, with duration zero, or when LED splash support is off. Otherwise it preserves the existing overlay, wake and expiry policy. |

The lock is read when drawing; the display keeps no additional copy of the
producer's state. Dark panels still acquire/render a changed status into RAM,
so waking can use the current frame without querying the provider again.
The three numeric LED values are published/read as one atomic 32-bit tuple,
with no new mutex, heap allocation, worker or timer. Brightness and speed are
displayed as percentages (8-bit and 16-bit respectively); the API does not
clamp them. Pattern IDs 0–17 use `STEADY`, `BREATH`, `HEART`, `BLINK`, `FAST`,
`TRIPLE`, `SOS`, `CANDLE`, `SAW`, `BEACON`, `SLOW`, `STROBE`, `DBLBCN`, `LONG`,
`SPARK`, `COUNT`, `RIPPLE`, `FADE` in order; other IDs display `?` and their
numeric ID. These names are display presentation, independent of producer
headers. For another LED source, enable `LED_SPLASH`, disable `LED_PATTERNS`
and call the numeric API.
Every accepted numeric call requests the overlay and restarts its lifetime,
even for equal values; the producer decides when to report a change. The
zmk-led-patterns adapter preserves that producer's change-event policy.

## Compatibility and tests

The module builds against upstream ZMK `main` and against the DYA Studio fork
(`cormoran/zmk` `main+dya`). It uses only Zephyr's display and input APIs and
ZMK's public event and state functions.

- `tests/run.sh` builds the drawing core and LED splash timing checks on the host with strict warnings,
  optimised and under ASan/UBSan, checks the layout, the font and the run
  finder, and checks that the generated font header matches its source.
  It then lifts the driver, power, settings, default-provider and peer-adapter functions into the stubbed
  harnesses in `tests/runtime/`, which model the panel's memory: the driver
  built with every option on (fixed portrait, reset pin, contrast, sync
  device, split battery and power, LED splash) and with every option off,
  the peripheral power reporter, the settings bridge, and optional-source adapters. They cover boot
  clears with failing pages, diff writes, retries, the panel timeout, one bus
  operation per sync-device report, the splash and power-off. CI requires 100%
  line and branch coverage of the drawing core, the splash helper and each
  lifted function (`bash ./tests/run-docker.sh` runs the same in the ZMK
  image). Devicetree instantiation and event registration are left to the
  firmware fixtures below.
- `tests/run-integration-docker.sh upstream|dya` builds a firmware fixture
  with an SSD1306 on a XIAO BLE; the DYA variant also includes zmk-led-patterns
  and checks the LED splash Studio key. Both run a native_sim test that drives ZMK
  events against a recording display and checks what reaches the panel,
  including external lock/LED inputs with both producer modules absent.

The dedicated symbols live in `scripts/font5x7.txt`; `scripts/gen_font.py` regenerates
`include/zmk-display-72x40/display_72x40_font.h` from it.
The portrait subset lives in `scripts/status10.txt`;
`scripts/gen_status_font.py` regenerates `display_72x40_status_font.h`.
Both generators use only Python's standard library and accept `--check`.

Drawing uses one fixed controller-page format. The canvas contains only a
buffer pointer, with no dimensions, orientation state or storage hint. Text
uses preconverted row data; clipping and controller offsets are calculated
once per strip. The font has 950 bytes of pixels and 95 bytes of spacing data.
Filled locks use preconverted nine-bit rows, with clipping and page location
calculated once per icon. The unused old 5x7 ASCII table, column plotter and
generic text path have been removed; normal text retains its fallback glyph. The two frame buffers
remain 720 bytes; there is no projection buffer. See the
[assertion migration map](tests/fixed-layout-migration.md) for retired generic
cases and their fixed-display replacements. CPU timing and power on the real
board require separate measurements.

## License

Code and the original 5x7 font: MIT. See [LICENSE](LICENSE).
Panel Status 10 bitmap data: OFL-1.1, derived from Mona10;
see [licenses/mona-OFL.txt](licenses/mona-OFL.txt).


## Current test entry points (2026-10-05)

See [the module-owned test guide](tests/README.md). The default host command
executes every behavior scenario once under ASan/UBSan. Set `ZMK_TEST_MODE=full`
for all retained coverage/ABI variants. Legacy simulation has been removed
from the active suite; its source/snapshots remain externally archived.
