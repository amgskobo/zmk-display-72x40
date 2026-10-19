# Fixed AKKB layout migration

The user selected AKKB-only support and removed configurable orientation.
The panel is 72x40; drawing is 40x72 and writes directly to its page layout.
This intentionally removes generic landscape/other-size/alternate-orientation
contracts. Those configurations are NOT APPLICABLE, never passing substitutes.
Original source and assertions at commit 9207005 were archived before deletion;
this review retains their path/hash index and raw before/failed/after logs.
No approved pixel frame or scheduling snapshot was updated.

| Former assertion family | Current disposition |
| --- | --- |
| test_font, bitmap_edges, direct_primitives | test_text_and_font and test_bitmap_edges independently check fixed controller pixels for all byte codes, clipped/erased strips, zero/partial/page crossings and extreme bounds |
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
