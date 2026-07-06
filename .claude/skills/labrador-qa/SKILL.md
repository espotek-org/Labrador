---
name: labrador-qa
description: Auto-QA the Unified_App Labrador GUI — build the QA build, run the ImGui Test Engine suite headlessly, screenshot-review every theme/page/text-size for cut-off text and layout breakage, and run hardware loopback tests against a real board (SG1→OSC1, SG2→OSC2). Two modes — report-only, or find-and-fix with re-verification. Use when asked to QA, regression-test, verify, or fix the Unified_App UI.
---

# Labrador Unified_App auto-QA

Work from `Unified_App/`. Three complementary passes; run all of them for a
full QA sweep, or pick the one that matches the request.

## Modes

**Report** (asked to "QA", "audit", "check"): run the passes, report defects
with evidence, change nothing.

**Fix** (asked to "fix", "QA and fix", or defects are found during other
work): for each defect, diagnose to root cause, fix, then **re-verify** —
rebuild BOTH the plain and QA builds, re-run the failing suite/filter until
green, and re-capture the screenshot that showed the problem. Rules:

- Fix the app, not the oracle: only change a test when the test itself is
  provably wrong (wrong path, bad timing) — say so explicitly when you do.
  A hardware test reading noise floor means the harness isn't wired; report,
  don't loosen thresholds.
- Fix root causes, not symptoms (a truncated label means a hardcoded width —
  make it CalcTextSize-driven rather than shortening the text; a modal during
  a fast run may be frame-rate-dependent logic — make it time-based).
- After all fixes: run the FULL `--qa` suite + smoke test all three layouts
  (`LABRADOR_LAYOUT=desktop|compact|mobile ./labrador --smoke`) to catch
  regressions, and spot-check 2–3 screenshots from the visual matrix.
- Keep app fixes and test-suite fixes in separate commits when committing.

## 0. Build the QA build

```sh
cmake --preset macos -DLABRADOR_QA=ON -B build/macos-qa   # or linux, etc.
cmake --build build/macos-qa
```

Gotchas:
- Run cmake from `Unified_App/` (presets live there), never from a build dir.
- Grep build output case-insensitively for errors ("CMake Error"), and beware
  `SDL_error.c` matching "error". `ninja: build stopped` = failed.
- Also build the plain preset afterwards — QA-only symbols
  (`ImGuiItemStatusFlags_Checked` etc.) must stay behind
  `#ifdef IMGUI_ENABLE_TEST_ENGINE` / `LABRADOR_QA`.

## 1. Headless UI test suite (interaction QA)

```sh
./build/macos-qa/labrador --qa          # stable regression suite (gui+hw)
./build/macos-qa/labrador --qa=gui      # UI-only
./build/macos-qa/labrador --qa=hw       # hardware loopback (needs the harness)
./build/macos-qa/labrador --qa=dbg      # dump live window names (path debugging)
./build/macos-qa/labrador --qa=fuzz     # interaction fuzzer (crash hunt; safe
                                        # unattended, board attached or not)
```

The `fuzz` category walks every menu/widget/page (desktop, mobile AND compact
layouts), mashes shortcuts, abuses plots/zooms, drags values to extremes,
cycles device modes, resizes the window to degenerate sizes, runs a seeded
monkey, and spams Esc-resets timed to land inside connection setup
(`esc_spam` — the repro for reset-during-reconnect races). Three more live in
`fuzzx` and must be run individually and deliberately, by name:
`--qa=minimize_restore` (wedges the headless harness — the main loop stops
while minimized), `--qa=quit_while_connected` (exits the app mid-run),
`--qa=calibration_wizard` (rewrites the attached board's calibration).
Crash-hunting rule: a run that dies without printing `QA: n/m tests passed`
is a crash even if the exit code looks tame — check
`~/Library/Logs/DiagnosticReports/labrador-*.ips` and rerun under
`lldb -o run -- ./labrador --qa=<test>`. Fuzz runs rewrite `settings.ini`
(calibration included) — restore your backup afterwards.

### Prediction QA (state → predict → interact → verify → report)

A model-in-the-loop visual QA pass that catches *behavioural* and *interaction*
regressions a static screenshot sweep misses. Each scenario in the `predict`
group (`RegisterPredictScenarios` in `QaSuite.cpp`) reaches a state, captures a
frame, performs ONE interaction, and captures another. The enabling primitive
is `QaRequestFrameDump(path)` (declared in `QaSuite.h`): a running test asks
the render loop to dump the next frame's framebuffer, so before/after live in
one session.

```sh
LABRADOR_QA_CAPTURE_DIR=/tmp/labqa_predict ./build/macos-qa/labrador --qa=predict
uv run tools/qa_predict_report.py \
    --captures /tmp/labqa_predict --findings tools/qa_predict_findings.json \
    --out /tmp/qa_report.html      # self-contained, images embedded
```

The loop: run the scenarios → for each, look at the `before` frame, **predict**
what the interaction does, look at the `after` frame, record prediction/actual/
verdict in a findings JSON (schema in `tools/qa_predict_findings.json`) →
`qa_predict_report.py` bakes frames + findings into a browsable HTML where a
human ticks Bug / Not a bug / Unsure per scenario and exports a reconciled JSON
→ fix from the export. Publish the HTML with the Artifact tool for review.
Every scenario must record `consistent` (did the after frame match the
prediction?) — it is the report's gate: a real run has thousands of scenarios,
so **only divergences surface to the human**; matches are counted in the
summary but hidden (no card, no embedded frames — `--show-matched` overrides
for debugging small runs). Don't notify the human when things work. Add
scenarios by registering more `predict` tests (drive with the test engine,
`QaCapture(ctx, "<id>", "before"/"after")` around the interaction). `predict`
is opt-in (not run by bare `--qa`), like `fuzz`. The `autofit_zoom_persists`
scenario doubles as the behavioural regression guard for the Auto Fit one-shot
fix (a stuck auto-fit makes the `after` frame identical to `before`).

Exit code 0 = all queued tests passed. The registered tests live in
`src/qa/QaSuite.cpp`. When writing new tests:
- The engine's `**/` wildcard matches by *label*; ID-only widgets (`##foo`)
  need exact paths: `ctx->WindowInfo("//Main Window/##sidepanel/Scope")` then
  `ctx->GetID("##scope_panel_run", w.Window->ID)`. Child window names are
  mangled (`##sidepanel_4E2DAD4E`) — never hardcode them.
- Menu popups are `//##Menu_00` (root) / `...###Menu_01` (submenus); combo
  popups are `//##Combo_00`. `//$FOCUSED` does NOT resolve menu popups.
- Custom toggles report state via `ImGuiItemStatusFlags_Checked`
  (`QaMarkItemChecked` in UIComponents.hpp) — read with `ItemInfo().StatusFlags`.
- The app is one fullscreen window: `MouseMoveToVoid()` asserts; use a
  park-on-plot move instead (see `ParkMouse`).
- Don't press Escape with a board connected (the app's Esc shortcut resets USB).
- Yield 2+ frames after a click before asserting state; after theme switches
  (font swaps!) park the mouse and yield ~8.
- Prologue every test with `PopupCloseAll()` + `SetRef("Main Window")`.

If a run fails oddly with a board attached, suspect a modal (firmware/safety/
uninitialised warnings): the failure signature is "Hovered id was 0x00000000
in ''". Investigate whether the modal is a QA artefact or a real app bug —
both frame-rate-dependent debounce and connect-transient false positives have
been real bugs found this way.

## 2. Screenshot matrix (visual QA — cut-off text, overlap, contrast)

The app dumps its final frame with `--smoke`:

```sh
LABRADOR_LAYOUT=desktop LABRADOR_FRAME_DUMP=/tmp/shot.ppm ./labrador --smoke
sips -s format png /tmp/shot.ppm --out /tmp/shot.png   # macOS; ImageMagick elsewhere
```

Drive the state matrix through `settings.ini`
(macOS: `~/Library/Application Support/EspoTek/Labrador/settings.ini` —
**back it up and restore it around every run**; the app rewrites it on exit):
- `theme=` classic-dark | classic-light | phosphor-retro | phosphor-modern |
  amber-retro | amber-modern | vector-retro | vector-modern
- `desk_panel_page=` 0..6 (Scope, Signals, PSU, Meter, Logic, DAQ, Analysis)
- `font_scale=` 0.85 | 1.0 | 1.2 | 1.45
- `desk_panel_width=`, `desk_panel_visible=`, `desk_scanlines=`

Minimum sweep: every theme on page 0; every page in one retro + one classic
theme; font_scale 1.45 on pages 0 and 1; LABRADOR_LAYOUT=compact and =mobile
once each. Read each PNG and look for:
- truncated/cut-off text (labels ending in fragments, clipped combo previews)
- overlapping widgets, text on same-colour backgrounds, unreadable contrast
- layout overflow (widgets escaping their segment frames), misaligned rows
- missing chrome (bezel, segment frames in CRT themes; borders in classic)

Widths should be `CalcTextSize`-driven — a truncation usually means a
hardcoded pixel width; fix the width computation, not the label.

## 3. Hardware loopback (real-board QA)

Harness: Labrador board plugged in via USB, **SG1 wired to OSC1, SG2 wired to
OSC2**. Then:

```sh
./build/macos-qa/labrador --qa=hw
```

Each test drives a 1 kHz, 2 Vpp sine on the generator and asserts the captured
waveform's amplitude (1.2–3.0 Vpp window, generous for uncalibrated boards)
and frequency (800–1200 Hz). A Vpp at noise level (< 0.1 V) with a connected
board means the loopback wires aren't attached — report that, don't "fix" the
test. Tests skip with a warning when no board is present, so `--qa` (all) is
safe everywhere. The tests wait up to 5 s for enumeration.

Crash triage: any run that dies instead of printing `QA: n/m tests passed`
is a crash — rerun the failing filter with `lldb -o run -- ./labrador
--qa=<filter>` and capture the backtrace.

## 4. Transport frame-integrity tools (USB/firmware QA)

Three standalone tools validate the AIO firmware transports (0x000C+)
end-to-end. Use them whenever the scope shows corruption, after firmware
changes, or when a transport regression is suspected.

```sh
# Full librador-path check (the numbers that matter for the apps):
cmake --build build/macos --target librador_bulk_test
./build/macos/librador_bulk_test
# Expected on firmware 0x000C: 100.00% pass on every phase, dropped=0,
# unvalidated=0 (modes 0 and 2, quiet AND with the 354.1 Hz fgen sine).

# Raw-USB harness (per-transport, mode as 3rd arg; needs SG1->OSC1 loop):
cd AVR_Code/aio_test && make
./aio_transport_test bulk|iso1|iso6|bulkdiag|bulkdiaggen <secs> [mode]

# Live peek at a RUNNING app's firmware state - no interface claim, so it
# never disturbs streaming.  transport: 1=iso6 2=iso1 3=bulk:
./aio_transport_test/../aio_peek   # i.e. AVR_Code/aio_test/aio_peek
```

Interpretation gotchas (each of these was a real multi-day trap):
- **Never validate transports on a quiet/DC input.** A constant input
  XOR-masks device-side buffer collisions from the checksums (identical
  bytes overwrite identical bytes) — bulk once measured 99% on DC while
  torn ~40% under a real signal. Always drive the fgen with a sine whose
  period is incommensurate with the 1 ms USB frame (354.1 Hz is the
  convention; also the frequency from the original field report).
- `librador_bulk_test` (async 16-URB queue) is the acceptance number.
  The sync-loop harness stalls the wire between reads and shows ~97-99%
  on a healthy bulk transport — that gap is the harness, not the device.
- iso6 typically shows ~99.7% plus a few % "unvalidated" (meta packets
  lost; frames themselves fine) — known, tracked separately.
- `bulkdiaggen` decodes the DMA write-pointer snapshot the firmware puts
  in bulk header pad bytes 8-16 and maps stale byte ranges in failing
  frames — the tool that pinpointed the half-parity and DMA-phase bugs.
- A board that reports the wrong firmware (aio_peek/harness print it)
  may have been downgraded by a stale app build with auto-flash — check
  before diagnosing "corruption" (expected: 0x000C variant 03).

## Reporting

Summarise as: suites run + pass/fail counts, screenshots reviewed (matrix
covered), defects found (with screenshot/log evidence and file:line where
diagnosed), and anything skipped (e.g. hw without a harness). Distinguish
app bugs from test-suite bugs explicitly. In fix mode, additionally list
each defect as fixed (with the verifying re-run/screenshot) or deferred
(with why), and note any new tests added to lock the fix in.
