# Module-owned tests

Run from this checkout. Docker + Bash is the host entry point; no keyboard
checkout, shared test dispatcher or simulator repository is required.

```bash
bash tests/run-docker.sh
ZMK_TEST_MODE=full bash tests/run-docker.sh
```

On PowerShell, set `$env:ZMK_TEST_MODE="full"` for the second command
and invoke Git Bash explicitly. Remove the variable for the default quick run.

Quick executes **all host behavioral fixtures and scenarios** once, using
AddressSanitizer/UndefinedBehaviorSanitizer on C fixtures. Full repeats those same assertions
under the retained optimized and measured coverage variants. CI uses quick for
push/PR and full for scheduled/manual runs.
Unknown mode values fail; there is no silent skipped success.

## Retained behavioral contracts

| Fixture | Assertions |
| --- | --- |
| [test_display_72x40_core.c](test_display_72x40_core.c) | Rendering primitives, frame contents, clipped/fallback/control glyphs, extreme coordinates, width 0 for nonpositive cells and saturated unrepresentable text widths |
| [test_display_72x40_splash.c](test_display_72x40_splash.c) | Splash state and timing |
| [runtime/full_harness.c](runtime/full_harness.c) | Status binding, selected HID transport/profile behavior, quiet timeout/activity interleavings and LED splash lifecycle |
| [runtime/power_harness.c](runtime/power_harness.c) | Blank/wake policy |
| [runtime/settings_harness.c](runtime/settings_harness.c) | Initial values, clamps, invalid/read failures, unrelated-event isolation and newer callback precedence across an older paused registry read |
| [runtime/status_harness.c](runtime/status_harness.c) | Default external lock provider is unavailable |
| [runtime/messenger_harness.c](runtime/messenger_harness.c) | Optional adapter reads the public getter and forwards invalidation without bus/activity effects |
| [runtime/led_patterns_harness.c](runtime/led_patterns_harness.c) | Optional adapter forwards the exact numeric tuple and ignores unrelated events |
| [test_native_storage_selector.py](test_native_storage_selector.py) | Actual production preprocessor selector under 20 independent status/chosen rotation combinations |
| [test_runtime_coverage.py](test_runtime_coverage.py) | Public full-mode runner rejects empty/unrelated gcov; each gated range needs executable lines; measured misses remain failures |
| [test_integration_evidence.py](test_integration_evidence.py) | Actual shell entry freshness, output mapping, failure-code propagation and retained source/graph/logs under controlled tool failures |

The root `test_*.py` units run in both quick and full through `tests/run.sh`.
The selector uses the actual production conditional with controlled devicetree
metadata. The coverage controls use a recording compiler and synthetic gcov,
and the integration lifecycle controls use recording Docker/west commands.
They establish validator and process contracts; they execute no firmware.

The settings fixture runs actual extracted callback/read/apply functions with
controlled registry reads and pthread-backed kernel mutex stand-ins. It pauses
an old callback after reading 30 seconds, admits a newer 10-second registry
write, and requires the final driver value to remain 10 seconds. The fixture
uses condition rendezvous and a bounded stall watchdog. It checks callback
serialization rather than persistent storage or real Zephyr scheduling.

`test_small_canvas_and_offscreen_guards` checks negative glyph rows and extreme
coordinates without corrupting the canvas. `test_text_width` returns zero for
nonpositive cells and saturates only unrepresentable positive widths; text
position helpers retain exact representable next-x values.

The full runtime fixture includes `test_quiet_race`, `test_quiet_setter_ordering`,
`test_splash_wrap` and `test_splash_republish`. The setter-ordering case admits a
new positive timeout after an older zero-timeout write is paused; the older
retirement must not remove the newer finite timer. The real-kernel fixture includes
`test_quiet_decision_race`, `test_quiet_reenable_after_running_disable` and the
LED-enabled `test_splash_republish_during_expiry`. They pin concurrent timeout
disable/activity, later positive-timeout re-enable, 32-bit uptime wrap and a new
LED tuple published while an old expiry callback is paused. Test rendezvous
are guarded by the existing test configuration. Native tests use Zephyr
workqueues with a recording display; the host fixture uses controlled work
stand-ins.

The portrait cases cover fixed group anchors, all four independent Studio/
Messenger padlocks, simultaneous BLE/USB, pairing/down states, long names,
aligned 0/100/unknown/hidden battery values and charging bolts. The runtime
fixture checks all 72 route/preference/BLE/USB readiness combinations, allowing
at most one check and following the actual route during fallback. It also verifies
that generic status invalidation schedules a redraw without resetting activity.
The optional Messenger adapter separately tests its observer-to-invalidation
binding. Missing lock sources and LED producer headers are supported; the
numeric LED input retains readiness, disabled, unchanged, wake and expiry
cases, including a full-width tuple/frame assertion. Minimal builds keep
the public LED API as a no-op.
The portrait now uses the approved native-size font, inverted header, fixed
connection/Caps/battery groups and filled locks with diagonal opening shackles.
`approved_portrait_frames.h` pins 24 complete frames from an independent canvas
reference for all lock combinations, three routes, and battery/charging/Caps
states. The old row assertions changed intentionally for this visual design;
no timing trace or transport expectation was accepted to hide a failure.
Generic projection is checked at every pixel of 100 deterministic random
frames in both quarter turns and on unaligned shapes. The unused full-frame
tile transpose and its 64 single-bit private-algorithm assertions are removed;
independent bitmap pixels and projection frames retain the rendering contract. Font clipping, invalid character fallback, whole-glyph
truncation, page crossings, erase mode and omitted features stay covered.
Direct quarter-turn storage also checks both directions against independent
pixel coordinates for all 24 approved frames, all printable glyphs and fallback,
clipped/erased masks, packed bitmaps crossing controller and logical pages,
invalid inputs, short headers, both locks and 19 LED patterns on three shapes.
The runtime LED limit assertion compares physical frames through the reference
projection instead of assuming that the drawing buffer has logical storage.
The core also compares all six control-code symbols at clipped x/row edges in
both quarter turns. The generated font no longer has alternate row/column
formats. A private `D72X40_NATIVE_STORAGE` hint remains for the driver compiler;
measurements showed that removing it retains unused paths. Its dedicated
`test_display_72x40_native.c` fixture and generic `native_reference.c` are
therefore retained under optimized and sanitizer builds. They still compare
all 24 states, high Profile/charging variants and 19 LED IDs through an
independent pixel-coordinate projection. The driver enables this hint only
for actual 90/270-degree storage; the generic owner supports all orientations.

| Removed or simplified assertion | Retained contract/check |
| --- | --- |
| Duplicate row/column font formats | One row format: 950-byte pixels plus 95-byte spacing metadata, total static assertion |
| Private general 16-bit row writer | Fixed nine-pixel lock masks retain independent bounds, zero-row and invalid inputs; bitmap writers retain erase/clipping and buffer bounds |
| 64 single-bit full-frame transpose cases | Algorithm removed; generic projection retains 100 independent dense frames at both turns; bitmap pixels are independently specified |
| Flat storage rejection in former font mode | Font mode removed; generic flat storage is checked by goldens and bitmap coordinates, while the driver's direct-storage hint requires turned storage |
| High Profile 255 and peer charging | Retained in the dedicated storage fixture and migrated to generic approved-state checks |

`test_bitmap_edges` checks flat, 90 and 270 storage with eight-pixel row strips,
negative/edge coordinates and eleven-row bitmaps. Nine-pixel locks retain their
golden-frame and clipped-edge assertions. Original sources are preserved in
the integration task's before-change archive. Warning, sanitizer, optimized
and documented 100% coverage gates retain the selected driver/power/settings/API
and failure/recovery scenarios.

The per-row `d72x40_native_row8` helper is now folded into the bitmap writer.
Its existing physical x=-1..72/y=-9..40 draw/erase assertions retain every
independently specified output byte. The fixture maps those physical inputs
to one-row logical bitmap coordinates and calls the retained entry point.
The full bitmap clipping, multi-page, partial bottom edge and both-direction
golden checks remain; no expected frame or failure assertion is weakened.

The previous portrait row assertions were updated for the intentional layout;
landscape, clipping, font, rotation, diff-write, power and settings assertions
remain in the suite. The optional observer is compiled with both modules in
the external keyboard and a focused upstream combined fixture.

Each fixture keeps its existing normal, boundary/invalid, independent-state,
reset/cancellation and failure/recovery scenarios where applicable. Checks
assert ordered outputs and explicit time inputs. Coverage is a full-mode gate,
not a reason to replace assertions or a hardware completeness claim. Every gated
function range must contain measured executable lines; an empty report, a report
for another source, or a missing range fails closed. Runtime extraction selects
function bodies and supplies controlled declarations; the separately compiled
stock/DYA firmware and native fixtures check real feature/registration/kernel
boundaries. A support function may be compiled without a coverage gate.

Focused source emission is available for reviewing or compiling the same owner
fixtures without a private source adapter:

```bash
python3 tests/runtime/run.py --emit-settings /absolute/new/settings.c
python3 tests/runtime/run.py --emit-full /absolute/new/full.c
```

These entries require a fresh source path and emit source only. Successful
emission is not a passing runtime test. The default entry still runs every
fixture and scenario.

## Separate integration and physical contracts

Keep the existing owner `run-integration-docker.sh` commands and accepted
upstream/DYA variants for changed APIs, DTS/Kconfig/registration or actual
kernel boundaries. Native fixtures and negative guards are retained. Do not
run all firmware variants for a pure arithmetic edit. Host extracted functions
and controlled work queues do not prove real scheduling or transport delivery.

Each upstream/DYA integration command builds five configurations: the XIAO
central firmware, XIAO peripheral, native runtime, native blank-timeout and
[native external inputs](integration/external-ports.conf). The last enables
the LED view with neither Messenger nor zmk-led-patterns, supplies a replacement
lock provider, and checks status acquisition while dark without a wake or bus
write, later wake, exact LED frame, expiry and disabled input. Each native
executable is run three times without rebuilding between repeats.
All earlier runtime/blank assertions remain. A wake may use the frame already rendered
while dark; the provider is not required to be called again on wake.

The inner `tests/integration/run.sh upstream|dya` also accepts explicit cached
workspace/build inputs for offline callers:

| Variable | Contract |
| --- | --- |
| `ZMK_TEST_WORKSPACE` | Existing workspace parent, with a variant subdirectory. Normal mode initializes/updates the selected owner manifest here. |
| `ZMK_TEST_REUSE_WORKSPACE=1` | Requires that explicit workspace and `.west`; checks that its manifest exactly matches the selected fixture, then skips fetching. The caller must resolve and record actual checkout revisions and instructions. |
| `ZMK_TEST_BUILD_ROOT` | Optional build-output directory; default is the selected workspace's `build`. |
| `ZMK_TEST_REUSE_BUILDS=1` | Retains build products for incremental rebuilds. All five configure/build, config/registration/warning guards and native executions/assertions still run. Default `0` clears fixture build directories; values other than `0`/`1` fail. |

The Docker wrapper mounts this module as `/src`. The inner command derives
owner paths from its script location. External callers must provide the selected
owner checkout and owner-required SDK/host libraries;
reuse does not establish freshness or hardware fidelity by itself. Keep raw
results and binary/input identities when reusing builds.

## Empty BLE profile marks

An unpaired active profile keeps its Bluetooth symbol and profile number, with
no connection mark. A bonded disconnected profile retains a cross; a connected
profile retains a check when selected for HID. The portrait BLE and USB checks
identify the selected HID route exclusively. A connected inactive link has no
mark; this also uses the internal OPEN value without changing BLE pairing state.
The core tests compare every pixel in the empty mark region after a connected
frame, and the landscape test compares the complete output page. The former
circle expectations changed intentionally for this owner-requested presentation.

## Current test ownership and compatibility

Run the relevant public interface/build checks on both unchanged stock ZMK
and DYA when this owner supports both. See the selected core in the manifest.

Canonical entries (run from the repository root):

```bash
bash tests/run-docker.sh
```

[maintenance.json](maintenance.json) records supported cores, current commands
and explicit gaps. It is a reference map, not a passing test or a claim that
registration/coverage totals establish completeness. Preserve ordered outputs,
raw timestamps and the first failure. Read actual dependencies before execution.

## Explicit offline integration

The existing `tests/integration/run.sh upstream|dya` entry accepts
`ZMK_TEST_OFFLINE_WORKSPACE=/absolute/resolved/workspace` and
`ZMK_TEST_OUTPUT=/absolute/new/output`. This path performs no initialization or
network update, requires the pinned unchanged stock/DYA core and Zephyr, records
their identities, and rejects an existing evidence path before building. Resolve
and read all actual dependency instructions before selecting it. All existing
firmware/native/configuration/registration assertions remain enabled. The
ordinary wrapper retains its existing dependency-setup behavior; offline callers
supply the selected owner and SDK/native libraries (the canonical container
mounts the owner as `/src`).
For DYA, `ZMK_TEST_LED_PATTERNS_MODULE` may explicitly select the resolved LED
owner override in place of the workspace's cached LED module. Its instructions
and source identity must be recorded like every other selected dependency.

## Retained CI and online integration evidence

The ordinary online wrapper may retain evidence at an explicit new host path:

```bash
ZMK_TEST_OUTPUT=/absolute/new/evidence bash tests/run-integration-docker.sh upstream
ZMK_TEST_OUTPUT=/absolute/another/new/evidence bash tests/run-integration-docker.sh dya
```

It mounts that path at `/results`, keeps its exact Docker command, and passes
`/results/build` as the fresh inner output. The inner online entry retains owner
source hashes before dependency setup, its ordered shell command trace, a
successful `west manifest --freeze --active-only` graph, compiler logs, native
logs and build artifacts. Temporary dependency workspaces still clean up. A
failed graph freeze stops before building and retains its stderr/partial output;
a failed build or native execution keeps its actual exit status. Existing or
symlinked output paths are rejected before Docker/west is invoked.

Without this opt-in, ordinary online setup and temporary workspace cleanup keep
their existing behavior. Offline callers retain their separate fresh-output and
no-update contract; the online graph-freeze step is not imposed on cached
override workspaces. `ZMK_TEST_OUTPUT` takes precedence over `ZMK_TEST_BUILD_ROOT`
when online retention is selected.

The DYA fixture selects the LED repository named in its manifest. When that
repository is private, callers need read access. The optional
`ZMK_TEST_LED_DEPLOY_KEY=/absolute/read-only-key` wrapper input accepts a
dedicated repository read-only deploy key file for DYA. It mounts the key and
the checked-in GitHub host key read-only outside source/evidence, uses strict
host verification, and configures only the LED repository URL for SSH. Key
contents are never copied into output. The pinned public host key is documented
by [GitHub](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/githubs-ssh-key-fingerprints).
CI uses the repository secret `LED_DEPENDENCY_READ_KEY`; it stores the temporary
file under `RUNNER_TEMP` and removes it on success or failure. Fork PRs receive
no repository secret and therefore fail the DYA dependency-access gate explicitly.
Configuring support does not grant access: the LED deploy key and Display secret
must be provisioned separately.

CI is configured to keep host console output and each upstream/DYA integration
output under a separate artifact using `always()` and `pipefail`. Scheduled
online compatibility follows the selected moving core manifests; it does not
claim the same revisions as a locally audited offline cache. Hosted CI execution
and artifact upload require their own observed run.

Passing host/native/compile checks supports software review. Physical panel
output, I2C/shared-bus deadlines, USB/BLE/split delivery, reset persistence and
power/current acceptance still require matching production-board evidence.

## Linked PM registration guard

`integration/registration.py` requires exact defined ELF device, initialization
and PM objects for both early/late power hooks. The previous `strings` line
check rejected a valid stock ELF whose late device name adjoined printable
bytes (`88display_72x40_pm_late`). The saved pre-repair stock command fails
at that check while its exact ELF registrations are present. Negative controls
remove each object independently and reject suffixed names, unresolved references
and plain strings. The host quick/full entry runs these validator cases; all
firmware configuration/settings and native power-order assertions remain.

Run the registration validator regressions directly with:

```bash
python3 -m unittest discover -v -s tests/integration -p test_registration.py
```

The firmware entry records `zephyr/pm-symbols.txt` from the configured compiler's
`nm --defined-only` command, then invokes
`python3 tests/integration/registration.py BUILD/zephyr/pm-symbols.txt`.
