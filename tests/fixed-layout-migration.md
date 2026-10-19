# Fixed panel layout migration

The user selected support for one fixed 72x40 panel and removed configurable orientation.
The panel is 72x40; drawing is 40x72 and writes directly to its page layout.
This intentionally removes generic landscape/other-size/alternate-orientation
contracts. Those configurations are NOT APPLICABLE, never passing substitutes.
Original source and assertions at commit 9207005 were archived before deletion;
this review retains their path/hash index and raw before/failed/after logs.
No approved pixel frame or scheduling snapshot was updated.

| Former assertion family | Current disposition |
| --- | --- |
| test_font, bitmap_edges, direct_primitives | test_status_font_and_locks, test_direct_symbols_and_numbers and test_bitmap_edges independently check fixed controller pixels, all byte codes in the current font, clipped/erased strips, zero/partial/page crossings and extreme bounds |
| portrait layout, service locks and approved frames | test_approved_frames retains all 24 approved pixel frames unchanged; test_status_font_and_locks checks every clipped lock edge; test_state_and_bounds pins missing features and independent state |
| high Profile 255, peer charging and19 LED variants in separate native/reference fixtures | 67 original physical-frame checksums in fixed_frame_hashes.h, in the same optimized/sanitized/coverage core binary |
| font width/next-x saturation and invalid coordinates | Fixed status font width and internal advance retain bounded/empty/fallback checks. Unused generic text_n/tall/width APIs are removed; negative/extreme drawing coordinates remain checked |
| test_rotation, projection_coordinates and 64 old transpose bits | Unsupported transformation contract removed. Fixed bitmap and approved-frame tests retain exact physical pixel placement; no runtime projection or reference renderer remains |
| status/chosen rotation selector20-case fixture | Selector removed. test_fixed_panel_contract compiles actual production admission: correct panel accepted, wrong dimensions and any old property rejected |
| landscape layer/output/battery/indicator, other panels and wrapping | Unsupported layouts removed. Fixed group positions, whole-glyph truncation, battery limits/unknown/hidden/charging, exclusive HID checks and independent locks remain in approved frames/core/runtime fixtures |
| spans and redraw costs | test_spans keeps original ordered assertions unchanged; fixed redraw costs require zero unchanged bytes, bounded changes and unchanged columns outside the header |
| native layer change: only first two landscape pages | Fixed header columns 0..12 on any physical page, at most 65 bytes; all other existing native kernel/power/retry/shared-input assertions remain |
| runtime full/minimal and72 route/readiness/preference combinations | Retained, including transport/link state and assertions. Only canvas construction/projection is updated to fixed direct storage |

Coverage remains 100% for measured core lines/branches and all seven selected
runtime families. This percentage supplements the explicit cases; it does not
prove physical I2C deadlines, device CPU/current, USB/BLE/split or OLED behavior.

## Native symbol and lock simplification

Before retiring the unused private 5x7 ASCII/column path, source and assertions
were saved outside this owner under the review's retired-source directory with
SHA-256 identities. No approved frame, ordered span assertion or route matrix
was removed or updated. Historical unresolved simulator failures remain open.

| Removed private path/assertions | Retained current contract |
| --- | --- |
| Old 5x7 ASCII table, glyph lookup and text_row; printable/nonblank/fallback and all256 byte-code checks | Normal text uses the existing Panel Status 10 table; test_status_font_and_locks retains all256 code mappings, every printable nonblank except space, clipping, erase and fallback. Old 5x7 text is intentionally unsupported; no production caller remains. |
| Old dedicated control-code glyph identities | test_direct_symbols_and_numbers pins all seven original symbols against independent logical column data, every visible/clipped location and invalid codes; code zero remains blank. |
| Column mask/plot, invalid pages, draw/erase and offscreen bounds | test_bitmap_edges retains independent physical pixels and guard bytes for draw/erase, page crossings, clipped and extreme coordinates; direct symbol and lock sweeps retain each relevant bit/page edge. |
| Per-row lock bit reversal/geometry and private native_lock_row guard/zero-mask probes | test_status_font_and_locks retains the original independent closed/opened logical pixels across every clipped x and row, including empty/clipped masks; explicit INT_MIN/INT_MAX guards remain. Generic arbitrary nine-bit row API is retired. |
| Width/position saturation assertions inside removed text fixture | test_status_font_and_locks retains INT_MAX, exact INT_MAX-6 and representable INT_MIN+6 cases. |

Cache assertions and the explicit bypass negative control supplement these
preserved display contracts; they are not substitutes for physical acceptance.
