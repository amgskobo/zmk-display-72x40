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
| [test_fixed_panel_contract.py](test_fixed_panel_contract.py) | Compiles the actual production admission assertions: fixed 72x40 accepted, wrong dimensions and a stale orientation property rejected |
| [test_runtime_coverage.py](test_runtime_coverage.py) | Public full-mode runner rejects empty/unrelated gcov; each gated range needs executable lines; measured misses remain failures |
| [test_integration_evidence.py](test_integration_evidence.py) | Actual shell entry freshness, output mapping, failure-code propagation and retained source/graph/logs under controlled tool failures |

The root `test_*.py` units run in both quick and full through `tests/run.sh`.
The fixed-panel admission fixture compiles the actual production assertions
with controlled devicetree metadata. The coverage controls use a recording compiler and synthetic gcov,
and the integration lifecycle controls use recording Docker/west commands.
They establish validator and process contracts; they execute no firmware.

The settings fixture runs actual extracted callback/read/apply functions with
controlled registry reads and pthread-backed kernel mutex stand-ins. It pauses
an old callback after reading 30 seconds, admits a newer 10-second registry
write, and requires the final driver value to remain 10 seconds. The fixture
uses condition rendezvous and a bounded stall watchdog. It checks callback
serialization rather than persistent storage or real Zephyr scheduling.

`test_bitmap_edges`, `test_direct_symbols_and_numbers` and
`test_status_font_and_locks` check negative/extreme coordinates, independent
logical symbol pixels, all 256 text codes, clip/erase edges and saturated
unrepresentable text widths. Removed generic width/cell APIs are listed in
the migration map.

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
## Fixed panel layout and assertion migration

The owner supports one 72x40 controller frame and a fixed 40x72 drawing layout.
The core fixture retains the 24 independently approved frames pixel by pixel.
It additionally compares 67 physical-frame checksums captured before the
refactor: each approved state with ordinary/high Profile and peer charging,
plus all 19 LED IDs at full-width numeric limits. The approved pixel data is
unchanged. A deliberate pixel flip must change the full-frame checksum.

Independent pixel assertions exercise all 256 byte codes, clipped bitmap strips,
zero/partial/multiple-page heights, draw/erase masks, every relevant lock edge,
invalid/extreme coordinates, empty text, whole-glyph truncation, independent
state and missing features. Ordered span joins, full unknown-page refresh,
unchanged zero-write and header-only differential writes remain covered.
The real-kernel layer-change case now requires every write to stay within
controller columns 0..12, across at most five pages; it no longer assumes the
removed landscape layout's first two controller pages.

The removed generic font/layout/projection and orientation specialization tests
are archived and mapped in [fixed-layout-migration.md](fixed-layout-migration.md).
There is no alternate storage format, compiler hint, orientation parameter or
production reference renderer. One core binary per compiler variant exercises the fixed
implementation. The fixed panel admission fixture compiles actual production
assertions; it is a metadata/compile check, not physical acceptance.

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
The independent approved frames pin the full fixed layout, and the runtime
fixture retains the complete selected-route/readiness matrix. The former
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
CI uses the manifest HTTPS URL when no optional
`LED_DEPENDENCY_READ_KEY` is supplied. Public LED repositories require no secret,
and an empty or absent key no longer stops CI before dependency retrieval.
If an explicit key is supplied, CI stores it under `RUNNER_TEMP` and removes it
on success or failure. A private repository still needs real read access;
optional support grants none. Fork PRs receive no repository secret and can
retrieve only dependencies available without that secret.

`tests/integration/prepare-led-access.sh` is the actual CI admission entry.
`test_led_dependency_access.py` executes its absent/empty-key path, private
file permissions, no-secret-log and missing-runner-path boundaries. The public
DYA wrapper fixture verifies the actual Docker argv contains no credential
mount or URL rewrite. These recording-tool checks prove admission, not a real
GitHub fetch or firmware compatibility.

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

## Rejected fixed-panel configurations

For DTS/interface changes, run `bash tests/integration/check-fixed-panel.sh upstream|dya`
with the same `ZMK_TEST_OFFLINE_WORKSPACE` and a new `ZMK_TEST_OUTPUT`.
This owner entry builds the actual native DT/kernel path twice and requires
rejection of an old orientation property and a wrong physical width.
Each negative build must emit its exact panel-contract diagnostic; unrelated
compiler failures do not count as a passing rejection. These are additional
configuration guards, separate from the five valid integration configurations.

## Exact-state rendering efficiency

The core keeps normal text in Panel Status 10 and status symbols in one native
row table. The old 5x7 ASCII/column path is retired, with its original source
and unique assertions mapped in [fixed-layout-migration.md](fixed-layout-migration.md).
Lock clipping/page calculations occur once per icon rather than for each row.

The exact-state cache is 44 bytes on the supported ARM/native ABI, beside the
unchanged two 360-byte frame buffers. It belongs to one canvas, must be reset
when that canvas resets, and is accessed only by the display work handler.
In the normal status view, every invalidation still acquires current data,
including external providers while dark. The LED view retains its existing
numeric-tuple acquisition. Identical state skips drawing. Changed normal fields
redraw their affected groups; changed LED values clear/render their bounded
range. View switches and cache reset draw a fresh complete frame. Unknown panel pages, explicit refresh, retries,
contrast and blank/wake continue through the existing bus/power policy.

The core fixture checks every status field, all LED tuple boundary combinations,
independent caches, both view switches, reset, direct symbol pixels and every
16-bit numeric value at capacities1..8. The runtime fixture observes actual
draw calls and requires 100 identical invalidations to add neither draws nor
panel writes. All prior selected-route/readiness assertions remain.

Run the focused negative control from this owner:

```bash
python3 tests/check_render_cache.py --output /absolute/new/cache-control
```

It compiles the same public emitted full fixture normally and with a test-only
cache bypass. Normal must pass; bypass must abort specifically at the
unchanged-notification draw/write assertion. The production algorithm is not
rewritten or replaced. Raw commands, compiler output and actual exits remain.

For a repeatable host process CPU comparison against preserved original
headers, run:

```bash
python3 tests/benchmark_render.py --baseline-include /absolute/original/include \
  --output /absolute/new/benchmark --iterations 2000000 --repeats 3
```

Both revisions use the same owner benchmark, inputs, compiler and -Os/-O2 flags.
The same compiled binary is reused for three or more repeats, with alternating
revision order. Eleven scenarios retain the original mixed/repeated status and
LED changes, and add individual layer/battery/lock/profile and LED
pattern/brightness/speed changes. Observable checksums and final frame hashes must
match before reporting medians. This measures host process CPU only; the
synthetic repeat ratio is explicit and establishes no real notification rate,
MCU speed, I2C timing or electrical/power result.

## Fixed-group partial rendering

The normal-view cache compares all fields feeding six groups: header,
connections, Caps, local battery, peer battery and locks. Peer battery visibility
also changes the local B/L label. Full status changes and retained nonvisual
field changes use the complete path. No status field or selected-route assertion
was removed. LED updates clear one bounded value range; a pattern+speed update
also redraws the unchanged brightness line inside that range. The constant LED
header remains untouched. Views, expiry, reset and explicit panel refresh retain
their existing policy. The cache and mutable RAM are unchanged.

The core fixture compares576 approved-state transitions,64 dirty-group
combinations in both directions and64 LED boundary-tuple transitions with full
rendering. All prior24 independent pixel goldens and67 frame checksums remain.
The field-to-group cases include connection availability, label coupling and
both directions of disappearing groups. No old assertion was retired here.

[The partial-update fixture](test_partial_render.c) runs in every quick/full C
variant. A fixture-only sentinel in the unrelated inverted header must survive a
battery or LED brightness update. Restoring the sentinel must then produce the
exact complete frame. There is no firmware probe or alternate renderer.

To prove the assertions reject the actual previous full-redraw implementation:

```bash
python3 tests/check_partial_render.py --baseline-include /absolute/original/include \
  --output /absolute/new/partial-control
```

The same fixture compiles against current and preserved original headers without
rewriting either source. Both original status/LED cases must fail at the exact
untouched-group sentinel assertion; both current cases must pass. Raw exits,
compiler output and header identities remain in the fresh output directory.

Benchmark modes are0=repeated mixed status,1=changing mixed status,
2=repeated LED tuple,3=changing LED tuple,4=layer,5=local battery,6=Messenger lock,
7=Profile,8=LED pattern,9=LED brightness,10=LED speed. Mode0/2 changes once
per100 notifications; all other modes change every notification. This explicit
workload does not establish actual device event rates or power savings.

## Sync timestamp boundary

The peer-report timestamp has a separate seen bit in the existing atomic flags;
zero is valid at boot and after32-bit uptime wrap. The host full fixture
checks absent/non-sync input, a completed report at0, the exact window edge,
UINT32_MAX-to0 and a new report at0. An unknown frame remains queued without
a bus write while that report is recent. These controlled-time cases use the
actual driver functions; the native matrices separately retain real-kernel
one-operation-per-report assertions. Physical bus timing remains a separate gate.
