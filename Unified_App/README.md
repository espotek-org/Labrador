# Labrador Unified App

Cross-platform GUI for the [EspoTek Labrador](https://espotek.com) board:
oscilloscope, signal generators, power supply, logic analyzer, multimeter and
data logger in one app. Dear ImGui + ImPlot + SDL3, with the merged `librador`
driving the hardware. One codebase, three form factors:

- **Desktop** (macOS / Linux / Windows) — menu-bar layout with a single side panel
- **Mobile** (Android) — Brent's touch tile layout, ships as an APK
- **Compact** — 800×480-class LCDs (Raspberry Pi 3 and newer)

## Building

All desktop builds use CMake presets (see `CMakePresets.json`); the top-level
`Makefile` is a thin convenience wrapper. Dependencies (SDL3, ImGui, ImPlot,
librador, fonts) are vendored under `deps/` — the only system requirements are
a C++17 toolchain, CMake ≥ 3.22, Ninja, pkg-config and libusb-1.0.

### macOS

```sh
brew install cmake ninja pkg-config libusb
make macos            # = cmake --preset macos && cmake --build --preset macos
./build/macos/labrador
```

### Linux

```sh
sudo apt install build-essential cmake ninja-build pkg-config libusb-1.0-0-dev \
    libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev \
    libegl1-mesa-dev libdecor-0-dev libpipewire-0.3-dev libdbus-1-dev
make linux
./build/linux/labrador
```

(The X11/Wayland dev packages are for the vendored SDL3 build.)

### Raspberry Pi (Pi 3 and newer)

```sh
make pi               # GLES 2.0 renderer (LABRADOR_GLES2=ON)
./build/pi/labrador
```

### Windows

From a shell with a C++ toolchain (MSVC “x64 Native Tools” or MSYS2 MinGW64)
plus CMake, Ninja and libusb (e.g. `pacman -S mingw-w64-x86_64-{toolchain,cmake,ninja,libusb}`):

```sh
make windows
build\windows\labrador.exe
```

### Android

Requires the Android SDK + NDK (the Gradle project drives the same CMake tree):

```sh
make android          # = cd android && ./gradlew assembleDebug
```

APK lands in `android/app/build/outputs/apk/debug/`.

## Running

- `./labrador` — the layout auto-selects by platform/resolution; override with
  the View ▸ Layout menu or `LABRADOR_LAYOUT=desktop|mobile|compact`.
- `./labrador --smoke` — render 60 frames and exit (CI smoke test). Add
  `LABRADOR_FRAME_DUMP=<path.ppm>` to dump the final frame for visual review.
- Settings persist to `settings.ini` in the per-user pref dir
  (macOS: `~/Library/Application Support/EspoTek/Labrador/`).

## UI tests (QA builds)

```sh
cmake --preset macos -DLABRADOR_QA=ON -B build/macos-qa
cmake --build build/macos-qa
./build/macos-qa/labrador --qa          # headless suite; non-zero exit on failure
./build/macos-qa/labrador --qa=hw      # hardware loopback: SG1→OSC1, SG2→OSC2
```

QA builds embed the [Dear ImGui Test Engine](https://github.com/ocornut/imgui_test_engine)
(interactive test windows appear when run without `--qa`). The suite lives in
`src/qa/QaSuite.cpp`.

### Automated QA with Claude

The repo ships a Claude Code skill (`.claude/skills/labrador-qa/SKILL.md`)
that automates the whole QA workflow. From a Claude Code session in this
repo, either invoke it directly or just ask:

```
/labrador-qa                      # or:
"QA the unified app"              # report-only: run everything, report defects
"QA the unified app and fix what you find"   # find-and-fix + re-verification
```

It runs three passes:

1. **Headless UI tests** — builds the QA build and runs the `--qa` suite.
2. **Screenshot matrix** — renders every theme / side-panel page / text size
   via `--smoke` + `LABRADOR_FRAME_DUMP` and visually reviews the frames for
   cut-off text, overlap, contrast and layout breakage.
3. **Hardware loopback** — with a board plugged in and **SG1 wired to OSC1,
   SG2 wired to OSC2**, verifies generated-vs-captured amplitude and
   frequency end-to-end (skipped when no board is present).

In report mode nothing is modified; in fix mode defects are fixed at root
cause and re-verified (both build configs, failing tests re-run, screenshots
re-captured) before a full regression pass. See the skill file for the test
engine's path/label conventions when adding new tests.

## Source layout

```
src/app/           App shell: SDL/GL lifecycle, librador session, settings, fonts
src/ui/            One Frontend per form factor (desktop, android, lowres)
src/instruments/   Instrument widgets shared by the desktop/compact layouts
src/qa/            --qa test suite (QA builds only)
src/platform/      File dialogs, paths, Android glue
librador/          Merged librador (USB protocol, decoding, DAQ)
deps/              Vendored SDL3/ImGui/ImPlot forks, test engine, stb, exprtk
assets/            Fonts (with licenses), pinout images, help text
firmware/          Board firmware images used by the reflash/recovery flows
```
