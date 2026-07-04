# Unified_App — Architecture & Plan

One ImGui/librador-based Labrador app for Windows, macOS, Linux, Android, and Raspberry Pi,
replacing the three existing GUIs over time. Decisions below were settled with Chris on 2026-07-03.

## Lineage & what gets harvested

| Source | What we take | What we don't |
|---|---|---|
| `Desktop_Interface/` (Qt) | Feature spec (parity checklist below), hardware protocol knowledge (`xmega.h`, device modes, gain masks), decoder logic reference, `.tlw` waveform files | Qt/QCustomPlot code itself |
| `~/git/Labrador_ImGui` (Monash) | Instrument widgets: PlotWidget, OscData, OSCControl, SGControl/SignalType, PSUControl, AnalysisToolsWidget (FFT), NetworkAnalyser (Bode), math channels (exprtk), export, ControlWidget base + help system | `src/Layouts.cpp` — auto-generated test code, **do not port** (per Chris). Its SDL2 AppBase is rewritten for SDL3. |
| `Android_App/` (Brent) | Dep forks (SDL3+pinch, imgui 1.92.7+custom_imgui, implot), tile layout system (`settings_panel.cpp`, `ui_tile`), extended librador (DAQ, trigger, virtual transforms, UART/I2C decoders, libdfuprog flashing), MainActivity USB-FD flow, Gradle/CI structure | Hardcoded Pixel-6a DPI constants, fragile `../../../../../` CMake paths |

## Locked decisions

- **Deps = Brent's forks**, local-cloned into `deps/` at the Android app's pinned commits
  (imgui `60357e3b`, implot `9c62060`, SDL `b0d284439`). Convert to git submodules when first committed.
  His SDL fork adds `SDL_PinchFingerEvent`; his imgui adds touch widgets (`custom_imgui.h`) and pinch plumbing in `ImGuiIO`.
- **Renderer matrix**: desktop = GL 3.2 core (mac) / GL 3.0 (win/linux); Android = GLES 3.0; **Pi = GLES 2.0** (floor is Pi 3/VC4; costs Pi 4+ nothing measurable for an ImGui workload).
- **Three layouts** in `src/app/layouts/`: `desktop` (replicate Monash's shipped arrangement — "very good", iterated with several dozen students), `mobile` (Brent's tile system — "good", tested on Android), `compact` (new, for 800x480 Pi LCDs). Auto-selected by platform + resolution, manual override in settings. The Qt app's layout is explicitly NOT a UX reference ("not great" per Chris) — it is a feature checklist only. Monash's `Layouts.cpp` multi-layout dispatch is auto-generated test code, also not a reference; what IS the reference is the desktop arrangement Monash actually shipped and tested.
- **Firmware**: expect version `0x0007`, **variant 2** on all platforms — one constant pair in one header, ready to flip to the upcoming v3 dual-interface variant. Flash in-process via repo-root `libdfuprog` everywhere (Windows may need a dfu-programmer.exe fallback — verify).
- **Android ships as an update to the existing Play listing**: `applicationId org.qtproject.example.Labrador` must never change (namespace stays `com.EspoTek.Labrador`).
- **Full parity** with the Qt app on desktop; mobile/Pi get what fits. Flagged-dubious features listed at the bottom for Chris to veto.
- **FFT library**: FFTW is a build burden on Android/Windows; plan is kissfft (BSD) or a small in-tree radix-2 FFT — spectrum display doesn't need FFTW-grade performance. (Deviation from both older apps; flag if objectionable.)

## Directory layout

```
Unified_App/
  Makefile              # thin wrapper: make macos|linux|pi|windows|android
  CMakeLists.txt        # the real build (desktop + android via gradle externalNativeBuild)
  CMakePresets.json
  deps/                 # SDL, imgui, implot (Brent's forks), exprtk.hpp, stb_image.h
  librador/             # THE canonical merged librador (base = Android's, de-JNI'd)
  src/
    main.cpp
    app/                # AppBase (SDL3/GL init, fonts, DPI), App orchestrator
      layouts/          # desktop.cpp, mobile.cpp, compact.cpp
    core/               # acquisition, DSP, FFT, decoders glue, calibration, .tlw loader, settings
    widgets/            # instrument widgets (scope, SG, PSU, multimeter, LA, DAQ, FFT, Bode, ...)
    platform/           # paths, file dialogs, USB open-vs-FD-injection, android JNI glue
  android/              # Gradle project (from Android_App), CMake points at ../CMakeLists.txt
  firmware/             # labrafirm_0007_02.hex (+ dfu via libdfuprog at repo root)
  packaging/            # mac bundle, AppImage/deb + udev rule, WiX, Play notes
  docs/PLAN.md          # this file
```

## librador merge (task 2)

Base = `Android_App/app/src/main/cpp/deps/librador` (richest). Surgery:
1. All `<jni.h>`, `<android/log.h>`, `SDL_GetAndroidActivity()`, asset-manager, and Java-callback code behind `#ifdef __ANDROID__`, isolated into a platform-glue seam (`src/platform/`), not inside library code where avoidable.
2. Desktop device-open path (`libusb_open_device_with_vid_pid`, hotplug/reconnect polling) restored from the Monash copy alongside the Android FD-injection path (`libusb_wrap_sys_device`).
3. Back-check the Monash/standalone copies for anything the Android copy dropped.
4. Long-term: this copy becomes canonical; `Librador_API` and Android_App copies deprecate.

## Milestones (session task list mirrors these)

1. ✅ Scaffold (this doc, deps clones, dirs)
2. Unified librador (merge + de-JNI)
3. macOS shell: SDL3 + imgui 1.92 + implot compiling, window + demo + device version query
4. Port Monash instrument widgets (adapt imgui 1.90→1.92, implot 0.17→fork, librador API)
5. Qt-parity features (checklist below)
6. Layouts: desktop, mobile (Brent's tiles), Pi compact
7. Cross-platform builds: Makefile targets, Linux/Pi/Windows, Android Gradle
8. Firmware flashing unification
9. Packaging + CI

## Feature parity checklist (vs Qt Desktop_Interface)

Legend: [M] exists in Monash port-source, [B] exists in Brent's Android app, [ ] new work.

### Oscilloscope
- [M] 2ch plot, pan/zoom, trigger (rising/falling, level, channel), cursors + delta readout
- [M] Auto-fit, Run/Stop, math channels (exprtk), signal properties (Vmin/Vmax/ΔV/ΔT/period)
- [B] Virtual transforms: per-ch gain view, offset, AC coupling, pause
- [ ] HW gain 0.5–64 + auto-gain; x1/x10 attenuation; per-ch offset spinners
- [ ] Double-rate 750 kSps single-channel mode; XY mode; low-pass filter toggle
- [ ] Per-channel Max/Min/Mean/RMS stats; selectable FPS; scope calibration (3-stage, persisted)

### Signal generator (2ch)
- [M] Sine/Square/Sawtooth/Triangle, freq/Vpp/offset/phase/duty, preview, clipping warning
- [ ] `.tlw` arbitrary waveform file loader (adds DC, PRBS5; user-extensible) — port file format from Qt app
- [ ] UART TX encoding on CH1 (type bytes → waveform); offset-type option (from GND / mean point)

### Power supply
- [M/B] Voltage slider 4.5–12 V
- [ ] PSU calibration; auto-lock; CH2-sharing arbitration with SG (rSource 253/254/255)

### Multimeter
- [B] Device mode 7 plumbing exists in librador
- [ ] Full UI: V / I / R / C modes, autorange + forced units, series-resistance selector, pause

### Logic analyzer / bus sniffer
- [B] UART (300–115200 baud, parity) + I2C decoders and decode console
- [ ] Desktop-grade UI: 2 digital channel plot lanes, per-ch baud/parity config, hex/ASCII toggle, LA offsets
- [ ] 4x digital outputs
- [ ] Scope/LA/multimeter bandwidth arbitration UI (Qt `bufferControl` equivalent)

### DAQ
- [B] Record CH A/B → CSV with downsampling, duration, chronological order (recently fixed)
- [ ] Averaging + max-file-size settings; **DAQ file replay mode** with start/end trim
- [M] Snapshot/export: CSV + clipboard; [ ] export plot image

### Analysis
- [M] FFT spectrum: Rectangular/Hann windows, dBm/dBV/Vrms, gated/lookback → [ ] add Hamming/Blackman/Flat-top windows, log-X
- [M] Bode/network analyser: sweep, magnitude+phase, log/linear

### System / UX
- [M] In-app help + pinouts; device status; safety-mode warnings
- [B] Firmware flash flow (bootloader jump → libdfuprog → relaunch); bootloader-mode detection
- [ ] Dark mode (+ light); settings persistence (INI via imgui or own); keyboard shortcuts; connection auto-recovery; manual firmware recovery

### Flagged — port last, Chris may veto (from "full parity but flag" decision)
- Eye diagram view
- Gobindar recovery flow (misflashed-board rescue, PID 0xA000)
- AVR debug console / debug buttons / `unified_debug_structure`
- Baudot decode path in the UART decoder
- "Force Square" (already deprecated in Qt app)
- Qt connection-type menu exposing variant 1 ("Lo bw") — proposal: drop variant 1 entirely, variant 2 everywhere (v3 soon)

## Port status (2026-07-03)

Milestones 1–4 DONE: full Monash instrument set ported to `src/instruments/`,
building clean on macOS and passing `--smoke`.

Milestone 7 (cross-platform build) DONE with caveats (2026-07-04):
- macOS: builds + smoke-tested. Linux/Pi/Windows: presets + Makefile targets
  written but UNVERIFIED (need those machines or CI post-submodule-conversion).
- Android: `android/` Gradle project builds `app-debug.apk` from the same
  CMakeLists (libSDL3.so shared + liblabrador.so + prebuilt libusb1.0.so per
  ABI, firmware + fonts + pinouts + help in assets; applicationId preserved).
  Asset loading is unified through `loadAsset()` (SDL_LoadFile — reads the APK
  on Android, `assets/` next to the binary on desktop); fonts/textures/help all
  load from memory.
  ON-DEVICE VERIFIED (2026-07-04, Pixel 5, API 34): installs and runs — our
  liblabrador.so loads, main() runs, GL context + Adreno shader compiler active,
  window sized 1080x2138, stable, no ANR (visual confirmation pending Chris's
  eyes — adb screencap can't capture the GL surface). Fixes that on-device
  testing required:
   - main.cpp: guard `#include <SDL3/SDL_main.h>` to non-Android + mark main
     visibility("default") — Android's SDLActivity dlsym's the exported "main";
     the header had renamed it to SDL_main.
   - AppBase font load: ImGui 1.92 asserts if a merge font sets GlyphOffset with
     size 0 — gave each merge font an explicit reference size (Brent's values).
     (Only bit Android because desktop builds Release with asserts off.)
   - AppBase: Android DPI scaling (Pixel-6a reference, Brent's approach) +
     content-rect insets from MainActivity status/nav bar heights (return 0 on
     API<35, correct — system already insets the surface there).
   - main.cpp: SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER for numeric keyboard.
   - build.gradle: versionCode 12 (past the installed dev build's 11); debug
     applicationIdSuffix ".unified" so it installs side-by-side with any
     release/Play build (release keeps the exact Play id).
   - new src/platform/android_ui.{h,cpp}: JNI helpers for dpi + bar heights.
  STILL untested on device: USB attach->connect->stream (needs USB-OTG + board),
  firmware flash dialogs, DAQ file writing via SAF, touch interactions.
- Android glue avoids SDL internal symbols (public SDL_OpenIO wrapper +
  JNI call to SDLActivity.openFileDescriptor) since libSDL3.so hides internals.

Key adaptations made during the port:
- `librador_get_analog_data` changed semantics in the Android era (3rd param:
  sample rate → sample count). Added `librador_get_analog_data_by_rate` with the
  exact Monash decimation math; all rate-passing call sites switched to it.
- `librador_send_*_wave` gained back the Monash `phase_rad = 0.0` 5th parameter
  (generator evaluates at `x - phase_rad`).
- ImPlot fork removed `SetNextLineStyle` + `ImPlotStyle` item styling; converted
  to trailing `ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, …)`
  on all plot calls (2.5 = Monash's old global default, kept for visual parity).
- ImGui 1.92: `AllowItemOverlap`→`AllowOverlap`, `ScrollX`→`Scroll.x`,
  `ImTextureID` casts, non-const `ImFont*`, `misc/cpp/imgui_stdlib.cpp` added to
  the imgui target.
- App.h includes `imgui_internal.h`/`implot_internal.h` before instrument
  headers (Monash App.hpp pattern — UIComponents/SignalType rely on it).
- nfd → async `platform/file_dialog.h` (SDL3 dialogs, results pumped on main thread).
- FFTW → `src/instruments/fftw3.h` shim over pocketfft.
- Known upstream Monash bug kept as-is (fix during parity work): AnalysisTools
  "OSC1 Spectrum" export reads the OSC2 trace (copy-paste bug).

## Mobile UI rebuilt on Brent's widgets (2026-07-04, VERIFIED on Pixel 5)

Chris rejected the first mobile layout (Monash desktop widgets reflowed onto a
phone — clipped + cluttered). Decision: per-form-factor UI classes, and port
Brent's ACTUAL Android widgets as the mobile UI. Done:
- `src/ui/Frontend.h`: abstract Frontend (startUp/update/shutDown(App&)). Shared
  App/AppBase keeps SDL lifecycle, librador connection/polling, firmware/
  bootloader/gobindar flows, settings, DebugConsole; it selects + drives a
  Frontend by form factor.
- `src/ui/mobile/`: Brent's inputs_ui/sig_gen_ui/psu_ui/trigger_ui/
  virtual_transform_ui/logic_decode_ui/daq_ui/plot_ui + settings_panel + ui_tile
  ported verbatim (globals -> MobileFrontend members; layout free-funcs ->
  members). All 13 librador calls matched the merged API unchanged. DAQ file
  output uses getPrefPath() on desktop; Android SAF path #ifdef'd. Landscape
  tile width in font units (AppBase owns DPI). Retired the mobile_density hack.
- App routes resolvedLayout()==Mobile to MobileFrontend (lazy); desktop/compact
  still use the in-App layout methods until they're extracted too.
- VERIFIED on Pixel 5 (API 34): renders Brent's clean tile UI (plot + Inputs/
  Logic Dec/DAQ tiles), matches his app. (Idle screencap catches a black SDL
  frame — the app only swaps on input; a tap forces a real frame.)

FULL SPLIT DONE (2026-07-04): App is now a thin session controller
(connection/polling, firmware/bootloader/gobindar flows, settings file, debug
console, frontend selection). Three Frontend classes:
  - DesktopFrontend (src/ui/desktop/) — Monash widgets, full layout
  - LowResFrontend (src/ui/lowres/) — compact 800x480 tab layout
  - AndroidFrontend (src/ui/android/) — Brent's touch widgets
  DesktopFrontend + LowResFrontend share InstrumentFrontend (src/ui/
  InstrumentFrontend) which owns the widget set, wiring, menu bar,
  UpdateHardwareState, auto-gain, shortcuts, help, settings for widget keys.
  App exposes a small public accessor interface (isConnected/isFlashing/
  layoutMode/darkTheme/showDebugConsole/requestFirmwareReflash/...) the
  frontends call. onDeviceConnected + loadSettings/saveSettings are Frontend
  virtuals. Old src/app/layouts/{desktop,compact,mobile}.cpp deleted. All three
  smoke-pass; desktop layout bodies moved verbatim (pure structural refactor).
  Class names per Chris: Desktop/Android/LowRes; base InstrumentFrontend.

REMAINING: USB-on-phone (OTG) + firmware/DAQ on-device tests; in-tree git commit.

## Milestone 5/6 status (2026-07-04)

DONE: Layout system (desktop = Monash, mobile = Brent's tiles ported —
selector popup, collapse button, masonry columns; compact = 800x480 tabs;
auto-selection + View menu + LABRADOR_LAYOUT env override). Five new
instrument widgets integrated in all three layouts and building green
(desktop/compact/mobile smoke + Android APK):
- InputsControl (device-mode arbiter, modes 0-7)
- MultimeterControl (V/I/R/C with Qt isodriver math incl. capacitance
  hysteresis scan; R-drive via SG CH2 3V DC; PSU-source R shows a hint —
  full PSU/SG2 arbitration deferred, see below)
- LogicDecodeControl (UART 300-115200 + parity, I2C when both channels
  logic, decode console with overlap-matched scrollback; hex is widget-side —
  librador exposes no decode-time hex API)
- DAQControl (Brent's duration/downsample mapping, async save dialog)
- DigitalOutControl (4ch, diff-based sends, reconnect resend)
Widget registry: `widgets[13]` in App.h; mobile visibility defaults show
Inputs, others opt-in via tile selector.

ALSO DONE (2026-07-04 later): HW gain 0.5-64x + conservative auto-gain
(Qt isodriver heuristic, 500ms cooldown, reconnect reapply); settings
persistence (settings.ini in pref dir, atomic writes, save-on-change; PSU
voltage deliberately NOT persisted) + dark/light theme via SetGlobalStyle(bool);
.tlw arbitrary waveforms (DC, PRBS5 + user-extensible via _list.wfl in
assets/waveforms) + UART TX 8N1 on SG1 (burst state machine — one-shot needs a
future librador 0xb2 API, see SGControl comment); Device > Reflash firmware
menu item.

HARDWARE VERIFIED (2026-07-04, real board on Chris's Mac): desktop connect
path end-to-end — enumerate, claim, firmware 0007/02 verified, iso streaming
thread live. FIRST FLASH ATTEMPT CRASHED and taught us two things, both fixed:
(1) the Qt-era "wedged board" sentinels 176/179 are actually
LIBUSB_ERROR_NO_DEVICE/IO error codes cast through uint8_t — during a flash
the main thread saw the board vanish, ran its own teardown_connection
concurrently with the flash worker's, and the double std::thread::join
SIGABRTed. teardown_connection is now mutex-guarded + idempotent, and the main
thread makes no USB control transfers while m_flashing (UpdateHardwareState +
Device menu gated). (2) Added AUTOMATIC BOOTLOADER RECOVERY (per Chris): a
board seen in bootloader mode for ~1.5s with no flash in progress gets a
launch-first-reflash-if-needed rescue, once per episode
(librador_bootloader_recover / App::startBootloaderRecovery). VERIFIED on
hardware: the crash-stranded board (PID 0x2fe4) was auto-recovered back to
application firmware (0xba94) on next app launch. Still to verify on hardware:
a full erase+flash+launch cycle (Reflash menu item, post-fix), signal
quality/triggering, multimeter accuracy, decode correctness.

ALSO DONE (2026-07-04, second wave — all green, on desktop + Android APK):
XY mode, x1/x5/x10 attenuation + per-channel display offset (Qt divide
semantics), scope + PSU calibration (3-stage wizard, corrections inside
o1buffer conversion, persisted via settings), Baudot/ITA2 decode (Qt's was a
stub returning 'a' — ours is the first real one), debug console (librador log
sink from all threads + AVR debug readout, Debug menu), eye diagram (trigger-
aligned overlays, median-UI symbol estimate, Qt display3 axes), Gobindar
recovery (PID 0xa000 detection -> Qt-worded dialog + diagram.png -> wait for
user short-DO1+replug -> flash), view-driven auto-gain REPLACING the stepped
version (per Chris: recompute from visible plot y-range each frame, snap with
Qt's 0.98 headroom, USB command only on change; eye view publishes VisibleY,
XY does not — OSC2-only axis).

STILL OPEN for full Qt parity: DAQ file replay, keyboard shortcuts.
PSU/SG2 arbitration RESOLVED BY DESIGN (Chris, 2026-07-04): the unified app
never drives the PSU rail from the signal gens (Qt's shared-rail behavior was
not ported); SG peak is hard-CLAMPED to PSU - 1.18 V
(amplitude reduced first, then offset; re-clamps every frame so lowering the
PSU pulls active signals down too) AND the tooltip warning explains the limit
(Chris: "clamp it and warn").
DECISION (Chris, 2026-07-04): the previously flagged-dubious items are now
WANTED — eye diagram, Gobindar recovery (PID 0xa000 rescue), debug console,
Baudot decode all go in. Force Square is EXCLUDED permanently
(Chris, 2026-07-04) — deprecated in Qt, not wanted in Unified_App.

## Milestone 5 design — parity widgets (agreed structure)

Device-mode arbitration: a new **InputsControl** widget (design base = Brent's
`inputs_ui`) owns the device mode. Per-channel function select (CH1/CH2 =
Scope | Logic | Off) plus special modes (750 kSps single-channel, Multimeter).
Mode map (usbcallhandler): 0=CH1 scope, 1=CH1 scope+CH2 logic, 2=dual scope,
3=CH1 logic, 4=dual logic, 6=750k, 7=multimeter. On change it calls
librador_set_device_mode (which already resets buffers + resends SG settings).
Other widgets read the current mode from it:
- **MultimeterControl** (new): active in mode 7. V/I/R/C with the conversion
  math ported from Desktop_Interface/isodriver.cpp (autorange, series-R
  selector). v1 = V (DC + RMS) from the mode-7 buffer, then I/R/C.
- **LogicDecodeControl** (new, base = Brent's `logic_decode_ui`): per-channel
  baud (300–115200) + parity + hex toggle, UART/I2C select, decode console from
  librador_get_uart_string / librador_get_i2c_string. NOTE decoding is driven
  by librador_get_digital_data() polling (getMany_singleBit → UartDecode), so
  the widget polls that each frame while a logic mode is active.
- **DAQControl** (new, base = Brent's `daq_ui`): channel select, units
  (Volts/ADC/Bits), downsample interval, duration → librador_daq +
  librador_poll_daq_status; desktop picks the output file via
  ShowSaveFileDialog. DAQ *file replay* (Qt feature) is a later, separate step.
- **DigitalOutControl** (new, trivial): 4 checkboxes → librador_set_digital_out.
Integration: `widgets[8]` array and `MobileLayoutState::widget_visible[8]`
become dynamic (std::vector) when these land; desktop right column gains the
new widgets below Analysis Tools; compact layout gains tabs; mobile gains tiles.
Remaining Qt-parity items after that: .tlw arbitrary waveforms + UART TX
encode, HW gain/attenuation/AC-coupling controls, XY mode, calibration,
dark/light theme, settings persistence, DAQ replay, keyboard shortcuts.

## Committing / repo integration (for when this lands in git)

Nothing is committed yet. `deps/SDL`, `deps/imgui`, `deps/implot` are local
clones of the Android_App submodule checkouts (same pinned commits). To land:
1. Convert those three to submodules using the same URLs/branches as
   `.gitmodules` has for Android_App (brentfpage forks).
2. **`deps/imgui` carries one local uncommitted patch**: `imgui.h` guards the
   `#include <android/log.h>` / `LOGW` block behind `#ifdef __ANDROID__`
   (required for desktop builds). Commit + push that to the imgui fork branch
   first — or re-home the three forks under the EspoTek org if push access to
   brentfpage's repos is an issue.
3. `deps/pocketfft_hdronly.h`, `deps/exprtk.hpp`, `deps/stb_image.h`,
   `deps/libusb-android/` are plain vendored files — commit directly.
4. `libdfuprog/CMakeLists.txt` was modified (desktop support, backward
   compatible with the Android_App build — Android path unchanged).
5. Once deps are submodules, CI can build Unified_App on push (macos + linux +
   android jobs); until then a checkout has no deps and CI would fail.

## Porting gotchas log

- imgui 1.90.6 → 1.92.7: new font system (`FontScaleDpi`, no `Fonts->Build()` prebake), `BeginChild` flags API, `custom_imgui.h` widgets require Brent's fork (not upstream).
- SDL2 → SDL3: renamed API surface (`SDL_CreateWindow` signature, event types, `SDL_EVENT_*`), gamepad/touch event reshuffle, `SDL_main` header semantics.
- Brent's implot fork reports `IMPLOT_VERSION "1.0"`; Monash code targets 0.17 — check `SetupAxes`/tick API drift when porting PlotWidget.
- Monash `getResourcePath()` assumes .app bundle or CWD; replace with SDL3 `SDL_GetBasePath()`/`SDL_GetPrefPath()`.
- Monash loads README.md at runtime as hard dependency for help text — make help content compiled-in or optional.
- Qt app compares firmware against compile-time `DEFINED_EXPECTED_VARIANT` while a runtime `expected_variant` global drives endpoint config — do not replicate; one runtime constant.
- `-fsigned-char` required globally (Android issue #231; Qt app also forces it on Pi) — apply on all ARM targets.
