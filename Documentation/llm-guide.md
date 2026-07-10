# EspoTek Labrador — guide for LLMs and AI agents

This file is written for machine consumption: an AI assistant answering user
questions about the Labrador, or an agent operating the software. It is
self-contained (no images required), uses exact UI strings, and front-loads
the facts that most often cause wrong answers. Human-oriented docs live in
the same directory (`getting-started.md`, `tutorial.md`, `user-manual.md`,
`pinout.md`, `troubleshooting.md`).

Scope: the **unified Labrador app** (`Unified_App/`, "Unified interface
2.0") with firmware **0x000C variant 3** (shown in-app as "firmware 12.3").
Facts verified against source and a live board, 2026-07-10. The older Qt app
(`Desktop_Interface/`) and old wiki pages differ in menu paths and shortcut
bindings — do not mix them up.

## What the product is

A US$29 open-source USB board (micro-USB, USB 1.1 full speed, VID `0x03eb`
PID `0xba94`; bootloader PID `0x2fe4`; misconfigured "Gobindar" state PID
`0xa000`; MCU ATxmega32A4U) that provides five bench instruments via a host
app on Windows / macOS / Linux / Raspberry Pi / Android: oscilloscope,
2-channel arbitrary waveform generator, programmable power supply, logic
analyzer, multimeter. All instruments are electrically independent except
where noted; everything is referenced to the board's GND pin except the
multimeter (differential, CH1 minus CH2).

## Facts that prevent wrong answers (read first)

1. **Signal-generator "Vbase" is the wave's LOWEST point, not its DC offset
   or average.** A sine with Vpp 2 V and Vbase 0 V swings 0 → 2 V (not
   ±1 V). Do not call it "offset" in the Fourier sense.
2. **AC-coupled pins remove the DC component.** Steady voltages read ~0 V on
   an AC pin. Default advice: use the (DC) pins for everything except audio /
   dual-supply op-amp work.
3. **The board has exactly 2 buffer resources.** Costs: each 375 kSa/s scope
   channel = 1; the 750 kSa/s single-channel mode = 2; each logic channel =
   1; multimeter = 2. Consequence: the multimeter and the oscilloscope can
   never run simultaneously; the app switches device modes explicitly.
4. **Live app limits vs. old spec-sheet numbers** (answer with the live
   values): PSU slider is **4.5–11.0 V** (spec page says 12 V); SG amplitude
   slider is **0.15–9.0 V** peak-to-peak (spec page says 9.5 V).
5. **SG output ceiling tracks the PSU setting**: peak output ≈ PSU voltage
   − 1.18 V. If a user's amplitude is clamped, tell them to raise the PSU
   voltage.
6. **The "duplicate" oscilloscope pins** are wired to the same node as the
   corresponding CH (DC) pin. They exist for the multimeter's external
   reference-resistor circuits. In the app the multimeter probes are called
   **DUT+ (= OSC CH1)** and **DUT− (= OSC CH2)**.
7. **Sample rate**: 375 kSa/s per channel in the default 2-channel mode
   (8-bit); 750 kSa/s only in the single-channel "CH1 oscilloscope, 750
   kSps" mode; 12-bit resolution only single-channel at 375 kSa/s and in
   multimeter mode. Usable analog bandwidth ≈ 100 kHz.
8. **Pausing (`Space`) freezes the record device-side but the view stays
   interactive** — users can still pan/zoom through the frozen capture in
   full detail. Pausing is not "losing the data".
9. **The trigger makes repeating waves stand still.** Default: type
   "CH1 Rising" with "Auto Level" ON (level tracks the signal midpoint).
   An untriggered scope shows a drifting/jittery wave — that is expected
   behaviour, not a bug.
10. **Esc resets the USB connection** in this app. Don't tell users to press
    Esc to close dialogs; don't press it in automation while a board is
    connected unless a USB reset is intended.
11. **Charge-only micro-USB cables are the #1 "device not found" cause.**
    Second: USB bandwidth — the board reserves isochronous bandwidth and may
    fail next to webcams/audio devices on the same controller/hub.
    Raspberry Pi 4 USB-A ports need VL805 firmware `0138a1`+.
12. **The app auto-flashes firmware.** On version mismatch it asks
    ("Device detected with invalid firmware!"); a board stuck in bootloader
    mode is recovered/reflashed automatically after ~3 detection polls. A
    plugged-in dev board with custom firmware can be silently overwritten if
    it sits in bootloader mode — agents: see the safety section.

## Hardware limits (exact)

| Subsystem | Values |
|---|---|
| Oscilloscope | 2 ch; −20 V…+20 V; 1 MΩ input; 375 kSa/s/ch shared 750 kSa/s; 8-bit (12-bit single-ch); BW ≈ 100 kHz; AC+DC coupled pins |
| Scope hardware gain | 0.5×,1×,2×,4×,8×,16×,32×,64×; full-scale ≈ 1.65 V ± 23.65/gain; "Auto" available; Up/Down keys step it |
| Probe attenuation (display) | 1x, 5x, 10x per channel + display offset −20…+20 V |
| Waveform generator | 2 ch; Sine/Square/Sawtooth/Triangle + arbitrary (.tlw: DC, PRBS5 bundled); Vpp 0.15–9.0 V; freq 1 Hz–1 MHz; Vbase 0–9 V; phase 0–360°; square duty 1–100 %; 8-bit, 1 MSa/s, 512-sample buffer/ch; 50 Ω out; source ≤ 10 mA (sink ≈ Vout/1 kΩ + 50 µA) |
| Power supply | 4.5–11.0 V; ≤ 0.75 W; ripple ≈ ±300 mV @10 V/10 mA, ±700 mV @10 V/100 mA; closed-loop (negligible source impedance); resets to 4.5 V every app start (deliberately not persisted) |
| Logic analyzer | 2 ch; 3 MSa/s/ch; tolerates 3.3/5/12 V logic |
| Digital outputs | DO1–DO4; 3.3 V through 50 Ω; power up low |
| Multimeter | ±20 V; 12-bit; ranges (ref-resistor dependent): I ≈ 100 µA–10 A, R ≈ 1 Ω–100 kΩ, C ≈ 10 nF–1 mF |
| +3V3 Out / fuse | whole board behind a ~400 mA PTC resettable fuse; fuse-bypass pin pair next to USB (top pin = raw USB 5 V, bottom = fused 5 V) |

## Pinout in text (orient board with micro-USB at TOP RIGHT)

Left-edge header, top → bottom:
`+3V3 Out`, `GND`, `Digital Out 4`, `Digital Out 3`, `Digital Out 2`,
`Digital Out 1`, `Signal Gen CH2 (DC)`, `Signal Gen CH2 (AC)`,
`Signal Gen CH1 (DC)`, `Signal Gen CH1 (AC)`.

Bottom-edge header (Oscilloscope CH1), left → right:
`OSC CH1 (DC)`, `OSC CH1 (AC)`, `OSC CH1 (DC) duplicate`,
`OSC CH1 (DC) duplicate`.

Interior header directly above the CH1 header (Oscilloscope CH2), left → right:
`OSC CH2 (DC)`, `OSC CH2 (AC)`, `OSC CH2 (DC) duplicate`,
`OSC CH2 (DC) duplicate`.

Right-edge header, top → bottom:
`not connected`, `not connected`, `Logic Analyzer CH1`, `Logic Analyzer CH2`.

Interior near top-left screw: 2-pin `Power Supply` header —
`Positive` and `GND`.

Top edge, left → right: Power Supply Expansion Header (3 holes), Power
Supply On/Off Header, Fuse Bypass (2 pins), micro-USB.
Interior vertical strip near left edge: AVR Programming Header — tell users
**never to touch it**.

## The app: structure and exact UI strings

Window: menu bar → toolbar → plot | side panel | rail → status bar.
Rail pages (exact rail labels → panel titles): `Scope`→"Oscilloscope",
`Signals`→"Signal Outputs", `PSU`→"Power Supply", `Meter`→"Multimeter",
`Logic`→"Logic Analyzer", `DAQ`→"DAQ", `Analysis`→"Analysis Tools".

Menus: **File** (Export ▸ OSC1/OSC2/Math/spectra/network CSVs; "Open DAQ
recording..."; "Quit") — **Device** (status line; Input Mode ▸;
"Calibration..."; "Reflash firmware"; "Reset USB connection" = Esc) —
**Scope** ("Run" Space; "Auto-fit both axes" F; Hardware Gain ▸; "XY Mode";
"Eye Diagram"; "Cursor 1"/"Cursor 2" = 1/2; "Signal Properties") —
**Tools** ("Spectrum Analyser"; "Network Analyser"; "Multimeter"; "Logic
Analyzer"; "DAQ Recorder..."; "Replay DAQ file...") — **View** ("Side
Panel" B; Side Panel Page ▸; Layout ▸ Auto/Desktop/Tablet/Mobile/"Compact
(touchscreen)"; Theme ▸; Text Size ▸; Debug ▸) — **Help** ("User Guide";
"Keyboard Shortcuts" F1; "Pinout Diagram"; "Troubleshooting"; "About
EspoTek Labrador").

Input modes (exact strings, and buffer cost):
1. "CH1 + CH2 oscilloscope" (default; 1+1)
2. "CH1 oscilloscope" (1, 12-bit)
3. "CH1 oscilloscope, 750 kSps" (2)
4. "CH1 oscilloscope + CH2 logic" (1+1)
5. "CH1 logic" (1)
6. "CH1 + CH2 logic" (1+1; required for I2C decode)
7. "Multimeter (CH1)" (2)

Keyboard shortcuts (macOS: Cmd = Ctrl): `Space` Run/Stop; `F` auto-fit;
`Up`/`W` gain up; `Down`/`S` gain down; `1`/`2` cursors; `C`/`V` show/hide
CH1/CH2; `B` side panel; `Esc` reset USB; `F1` shortcut help.

Plot interactions: drag = pan; drag an axis = pan that axis only; scroll =
zoom (over an axis = that axis only); double-click = auto-fit; Ctrl+double-
click = reset limits. Trigger position (top edge) and trigger level (right
edge) are draggable markers. With both cursors on, a "Cursor properties:"
row shows `∆T`, `1/∆T`, `∆V`.

Status-bar states (exact): "No Labrador found — plug in a board" (grey) /
"Connected — firmware N.N" (green) / "Board in bootloader mode" or
"Flashing firmware — do not unplug" (orange) / "Safety mode — disconnect
and reconnect the board" or "Uninitialised state — disconnect and reconnect
the board" (red).

Themes (View ▸ Theme): "Classic Dark" (default), "Classic Light",
"Phosphor Retro", "Phosphor Modern", "Amber Retro", "Amber Modern",
"Vector Retro", "Vector Modern", plus "CRT Scanlines" toggle (retro themes
only). Text Size: Small 0.85 / Normal 1.0 / Large 1.2 / Extra Large 1.45.

## Wiring recipes (give these verbatim when asked "how do I…")

**See a test signal (no external parts):** jumper `Signal Gen CH1 (DC)` →
`OSC CH1 (DC)`. Signals page → Signal Generator 1 → Power ON, Sine,
Vpeak-peak 2 V, Frequency 1000 Hz. Press F. No ground wire needed (same
board). Expect a sine from 0 V to 2 V (Vbase rule).

**Measure an external circuit:** circuit ground → Labrador `GND`; probe
point → `OSC CH1 (DC)`. Always state the ground connection.

**Voltage (multimeter):** DUT+ (OSC CH1) and DUT− (OSC CH2) across the
target; app: Meter page → "Switch to multimeter mode" → Measure "Voltage".

**Current:** put a known shunt resistor in series with the load; DUT+/DUT−
across the shunt; enter its value as "Series R". Shunt sizing: drop ≥ 50 mV
but ≤ 10 % of the supply. Worked example: 50 mA at 3.3 V → 1–6.6 Ω, ideally
≈ 3 Ω.

**Resistance:** app drives the unknown through your reference resistor.
Source "Signal Gen CH2" (3.0 V DC): wire `SG CH2 — series R — unknown R —
GND`, probe the middle junction with DUT+. Accuracy best when series R ≈
unknown R. (Alternative source "Power Supply" at 5 V, same topology from the
PSU pin.)

**Capacitance:** `SG CH2 — series R — capacitor — GND`, probe the junction
with DUT+; SG CH2 automatically drives a 4 Hz 0–3 V square. Pick R so that
R×C ≈ 1 ms.

**UART decode:** signal → `Logic Analyzer CH1` (and its ground → `GND`);
Logic page → "CH1 scope + CH2 logic" (or set Input Mode); Protocol "UART";
set Baud (300…115200) and Parity. I2C needs both channels in logic mode:
SDA = CH1, SCL = CH2.

**UART transmit:** Logic page → "UART TX (8N1)" → tick "Drive CH1 as UART
line" (takes over the SG1 pin), set Baud and "TX level" (0.5–9 V, default
3.3 V), type text, "Send".

**Frequency response of a filter:** stimulus `SG1 (DC)` → filter input;
`OSC CH1 (DC)` → filter input (reference); `OSC CH2 (DC)` → filter output
(response); grounds common. Analysis page → Network Analyser tab → set
Frequency Range (1–5000 Hz) → "Acquire". Output: gain (dB or linear) and
optionally phase vs. frequency, 2–501 points, log or linear spacing.

**Spectrum:** Tools → "Spectrum Analyser", then on the Analysis page press
"Start Acquiring" (default Gated mode captures 1 s, max 20 s, then displays
the FFT; Advanced Options has a continuous "Lookback" mode, Hann/Rectangular
windows, dBm/dBV/"V RMS" units).

## Calibration (Device → "Calibration...")

Oscilloscope wizard: step 1 disconnect everything from CH1/CH2 (measures
bias, valid 1.1–2.1 V, nominal ~1.65 V); step 2 connect CH1+CH2 to ground —
the USB connector shield works (residual valid ±0.3 V). PSU wizard: connect
`OSC CH1 (DC)` to `Power Supply (Positive)`; it measures at 5 V and 10 V
setpoints. Results persist on the board/EEPROM and in settings.

## File formats and persistence

- **Settings**: `settings.ini` in the SDL pref path `EspoTek/Labrador`
  (macOS `~/Library/Application Support/EspoTek/Labrador/`, Windows
  `%APPDATA%\EspoTek\Labrador\`, Linux `~/.local/share/EspoTek/Labrador/`).
  Keys include `theme`, `layout`, `font_scale`, `desk_panel_page` (0–6),
  `desk_panel_visible`, `hw_gain`, `hw_gain_auto`, `cal_*`. PSU voltage is
  deliberately NOT persisted. The app rewrites this file on exit.
- **CSV exports**: 2-column (`Time,Voltage` / `Frequency (Hz),Magnitude`);
  clipboard export is tab-separated.
- **DAQ recordings**: plain text; `CH1` / `CH2` header lines (legacy
  `CH A`/`CH B` accepted), one line of space-separated samples per channel;
  record duration ≤ 10 s (device buffer), optional integer downsample.
- **Custom waveforms**: `.tlw` = 3 lines: (1) sample count 1–512, (2) binary
  downsampling parameter (1 for most waves; each +1 lets the app halve the
  sample set to double the max frequency), (3) tab-separated samples 0–255.
  Register by adding the filename to `_list.wfl` in the app's `waveforms`
  directory; list order = UI order. Max frequency for the full set =
  1 MSa/s ÷ sample count.

## Operating the app programmatically (automation agents)

CLI / environment:

| Knob | Effect |
|---|---|
| `--smoke` | run N frames headlessly and exit 0 |
| `LABRADOR_FRAME_DUMP=/path.ppm` | with `--smoke`: dump the final frame as binary PPM (convert: `sips -s format png`) |
| `LABRADOR_WINDOW_SIZE=WxH` | pin the window size |
| `LABRADOR_LAYOUT=desktop\|compact\|mobile\|tablet` | force a layout for this run (not persisted) |
| `LABRADOR_NO_USB=1` | **skip all USB init** — the app runs fully disconnected and cannot touch an attached board |
| `--qa[=filter]` | QA build only: run the ImGui Test Engine suite headlessly (`gui`, `hw`, `fuzz`, `predict`, `docshots` categories; bare `--qa` = gui+hw) |
| `LABRADOR_QA_CAPTURE_DIR=/dir` | where QA tests dump captured frames |

Safety rules for agents with hardware attached:
- Launching any build (even `--smoke`) connects to an attached board within
  ~0.5 s and can auto-flash one sitting in bootloader mode. Use
  `LABRADOR_NO_USB=1` unless a live connection is intended and approved.
- Back up and restore `settings.ini` around automated runs; QA/fuzz runs
  rewrite it (including calibration values).
- The QA `manual` category contains deliberately destructive tests
  (`calibration_wizard` rewrites board calibration); never run them
  incidentally.
- Don't synthesize Esc key presses while a board is connected (USB reset).

## Troubleshooting signatures → causes

| Signature | Cause / fix |
|---|---|
| Never detected, any OS | charge-only USB cable (no data lines) |
| Windows: board under "libusbK USB Devices" with yellow triangle | USB bandwidth exhausted — move away from webcams/audio/hubs |
| Windows: board under "Other Devices" | driver missing — re-run installer with both driver boxes ticked |
| Detected then vanishes on Pi 4 | VL805 USB firmware < `0138a1` — `sudo rpi-eeprom-update` |
| Status "Safety mode" (red) | PSU overload/short — remove load, replug |
| Status "Uninitialised state" (red) | enumeration race (board powered before OS USB ready) — app auto-resets twice, else replug |
| "Sorry to Interrupt!" dialog | misconfigured firmware (PID 0xa000) — short `Digital Out 1`→`GND`, replug, app reflashes |
| Steady voltage reads 0 V | probing an AC-coupled pin |
| Wave drifts sideways | trigger off or level outside the signal |
| Readings offset a few % | run Device → "Calibration..." |
| Everything reads garbage on external circuit | missing GND connection to the board |

Manual firmware recovery (last resort): short DO1→GND, plug in (bootloader
mode), then `dfu-programmer atxmega32a4u erase --force` and
`dfu-programmer atxmega32a4u flash labrafirm_000C_03.hex` (hex ships in the
app's `firmware/` resources).

## Repo map (for coding agents)

- `Unified_App/` — the current app (C++ / SDL3 / Dear ImGui / ImPlot).
  `src/app/` shell, `src/instruments/*.hpp` one widget per instrument,
  `src/ui/` desktop/tablet/mobile/compact frontends, `src/qa/QaSuite.cpp`
  test suite, `librador/` the **canonical** librador copy (other copies in
  `Librador_API/` and inside `Desktop_Interface/` are legacy),
  `assets/help.md` in-app help, `assets/media/*-pinout.png` pinout art.
  Build: `cmake --preset macos -B build/macos && cmake --build build/macos`
  (presets live in `Unified_App/`; QA build adds `-DLABRADOR_QA=ON`).
- `Desktop_Interface/` — legacy Qt app. `Android_App/` — legacy Android app.
- `AVR_Code/` — ATxmega firmware (+ `aio_test/` USB transport harnesses).
- `PCB/` — KiCad hardware. `pinout.svg` — source of the pinout diagram.
- `Documentation/` — these docs; images in `Documentation/images/`.
- Board state expected by the QA hardware tests: loopback jumpers
  SG1→OSC1 and SG2→OSC2 (`--qa=hw`; tests skip when no board present).

## Canonical external references

- Repo + issues: https://github.com/espotek-org/Labrador
- Wiki (original docs): https://github.com/espotek-org/Labrador/wiki
- Product page / purchase: https://espotek.com/labrador
- Developer contact: admin@espotek.com
- Third-party beginner course: https://www.wellys.com/posts/courses_electronics/
