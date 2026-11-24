# `labrador --auto-qa`

The release binary tests itself and the board attached to it, in one run, and
writes a full QA report:

```sh
labrador --auto-qa <out_dir>
```

It needs no QA build (no ImGui Test Engine) and no other program: hardware
loopback tests through librador, a screenshot tour of the app's own UI and,
last, a firmware round trip through the DFU bootloader all run in-process. The
exit status is 0 only if every test passed (skipped tests do not count
against it). The code is in `src/autoqa/`, with measurement code shared with
`labrador-cli` in `measure/`.

Installed binaries: `/Applications/Labrador.app/Contents/MacOS/labrador`,
`C:\Program Files\EspoTek Labrador\labrador.exe`, `/usr/bin/labrador` (`.deb`),
or the AppImage itself.

## What the caller provides

- **The board**, with signal generator 1 wired to scope input CH1 and SG2 to
  CH2. The `hw.wiring` tests verify this. With no board the hardware tests
  fail with "no usable board" and everything else still runs, so the report is
  always complete.
- **Exclusive use of the board** for the whole run: nothing else (the app, the
  CLI, another VM) may hold it. The run takes no lock itself.
- **A desktop session**, since the app opens its window (software GL is fine).
  Windows up to 1280 wide and 768 tall must fit the display unshrunk, or the
  screenshots' size checks fail.

## What it guarantees

- `<out_dir>/progress.log` is created before the window opens, and gets a
  flushed, timestamped line for every phase and every test (`n/m id: status`)
  throughout. A release without `--auto-qa` exits 2 with "unknown option" on
  stderr instead (every unknown argument is an error), and an older release
  that ignored arguments would simply open its window and never create
  progress.log.
- The user's settings are never touched: the run keeps its settings in
  `<out_dir>/isolated-settings/settings.ini`, deleted at the end, and writes no
  `imgui.ini`. The calibration wizard never runs and the board's stored
  calibration is never written.
- The board ends on the bundled firmware: the firmware round trip always
  finishes by checking that and flashing the bundled image back if needed.
- Nothing is timed in frames: every wait is wall-clock, so slow software GL
  only makes the run longer.

## Phases and tests

**Phase 1 of 3, on a worker thread, while the window shows a status screen.** The
app's own USB session is set aside; each test sets the PSU, both generators
and the scope itself and measures, so none depends on what an earlier one left
behind. Each saves the samples it judged (CSV, first 5000 rows) and a plot of
the captured waveform with the best fit of the ideal shape (red) and the
expected waveform (green dashed) over it.

| Test | Checks |
| --- | --- |
| `hw.board.present` | a board is attached and runs the firmware this release bundles; records the default USB transport and whether calibration is stored on it |
| `hw.wiring.sg1`, `hw.wiring.sg2` | SG1 alone makes CH1 swing (at least 1 V of a 2 Vpp sine) while CH2 stays at the noise floor, and the reverse; a channel at the noise floor fails as "loopback not wired", a signal on the wrong input as swapped wires, and either way the tests needing that loopback fail with that reason |
| `hw.noise.ch1`, `ch2` | AC RMS with both generators parked (about 0.06 V), and the parked level |
| `hw.siggen.chN.{sine,square,triangle,sawtooth}_2khz` | one generator at a time, 2 kHz, 0.5-2.5 V at gain 4: librador's realised frequency and DAC levels against the request, the measured frequency against the realised one, the low and high levels of a least-squares fit of the ideal shape, the fit's residual, that the expected shape fits better than the other three, and the square's duty cycle |
| `hw.siggen.chN.sine_6vpp_x3` | a 0.5-6.5 V sine through the generators' x3 output stage, which needs the PSU raised to 8 V, so this also shows the PSU setting takes effect |
| `hw.siggen.dc_levels` | DC at 0.25, 0.5, 1, 2, 3, 4 and 6 V on both generators, read on both inputs, plus each channel's gain (slope) and offset, plotted against the limits |
| `hw.gain.x0_5` ... `x64` | for each of the eight scope gains, a 2 kHz sine filling most of that gain's input range on both inputs: unclipped, levels, frequency and shape |
| `hw.sweep.f10hz` ... `f50000hz` | a 2 Vpp sine at 10, 50, 100, 354.1, 1000, 2000, 5000, 10000, 20000 and 50000 Hz on each generator in turn: frequency, levels and (up to 20 kHz) shape |
| `hw.mode.scope1` | CH1-only mode streams 375 kS/s and no CH2 |
| `hw.mode.scope1_750k` | CH1-only 750 kS/s mode with a 10 kHz sine |
| `hw.isolation.sgA_to_chB.{sine,square}` | a 6 Vpp sine or square on one generator, the other parked: the idle input's AC RMS as a fraction of the driven one's |
| `hw.trigger.ch1_rising`, `ch1_falling`, `ch2_rising_square`, `ch2_falling_triangle` | librador's triggered capture: sample count, pre-trigger count, the samples either side of the level, and that the fitted generator waveform crosses the level in the right direction within 3 samples of t = 0 |
| `hw.trigger.placement` | all-post-trigger, mostly pre-trigger and 1.5 s windows: sample counts and the samples at the trigger |
| `hw.trigger.timeout_unreachable` | a level above the signal times out after the 2 s timeout |
| `hw.trigger.no_false_trigger_on_dc` | a DC level sitting on the trigger level, with hysteresis, never triggers on noise |
| `hw.transport.{bulk,iso1,iso6}.measure` | the session really uses the transport asked for (librador allows only bulk on macOS, so the iso tests skip there) and measures a 2 kHz sine on CH1 and a 4 kHz triangle on CH2 at once (smooth waves, because a square's edges couple into the other input as spikes that can fake a crossing) |
| `hw.transport.{bulk,iso1,iso6}.soak` | a 60 s stream with a 354.1 Hz sine on SG1 and a 1234.5 Hz triangle on SG2: no frames with a bad checksum, none dropped, at least 99 % of the expected frames, the delivered sample rate within 0.5 %, no pause over 0.25 s, and no period on either input more than 5 % off the median. These frequencies' periods are not a whole fraction of the 375-sample USB frame, so a lost or repeated frame shifts the waveform and is caught even if no counter notices. |
| `firmware.round_trip` | flashes the bundled `labrafirm_0007_02.hex` (librador reports 1, "runs other firmware", and the board 0x0007 variant 2), then the release's bundled image back (0x000c variant 3 here), then checks the board streams. Both directions jump to the DFU bootloader and re-enumerate, which is what USB passthrough and Windows driver binding tend to break. It skips when the board stores calibration, because the DFU erase would wipe it. |

**Phase 2 of 3, in the frame loop.** The app gets its USB session back and, with
`LABRADOR_SG1=sine:891:2:0.5` and `LABRADOR_SG2=square:557:1.5:0.3` set for
the run, every layout starts with both generators on, so the scope shows live
traces through the loopback. Neither period is a multiple of the 1 ms USB
frame, so a dropped frame is visible in the screenshots as a kink in the sine
or uneven square pulses; a 1 kHz sine or 500 Hz square repeats exactly every
frame or two and looks perfect even when every other frame is lost. For each shot the QA sets the layout, theme, text
size, side-panel page and window size through the app's own setters, waits at
least 1.5 s and 10 frames (6 s for the first, while the board connects and
starts streaming) until the window size is steady, then captures the frame as
a PNG. The last five shots switch USB off.

36 shots: every theme on the desktop Scope page; every side-panel page in
Classic Dark and Phosphor Retro; text sizes 0.85, 1.2 and 1.45 on the Scope
and Signals pages; compact at 800x480 in two themes; tablet at 1024x768 and
1280x720; mobile at 480x720; and five of those with USB off. Desktop runs at
1280x720.

**Phase 3 of 3, back on the worker thread: the firmware round trip.** It
runs last because it is the step most likely to break the USB link (both
directions re-enumerate the board through the DFU bootloader; QEMU's USB
passthrough on macOS hosts has aborted there), so a failure costs only its own
result, not the screenshot tour.

`report.json` is rewritten after every test with `"complete": false` until the
run finishes (schema 2; schema 1 wrote it once, at the end), so a crash, a killed process or a dead VM still leaves
everything recorded up to that point.

Each shot checks the app's status line (layout, theme, text size, board
`streaming` or `usb-disabled`, no safety-mode, uninitialised-board or flashing
state), that the window is the size asked for and the frame that size times a
pixel scale between 1 and 3, and that the frame is not blank (at least 64
distinct colours, no colour over 95 % of it, some luminance spread). Whether
it looks right is for the reviewer: every screenshot carries an `expect`
caption describing a correct render, including the signs of a lossy USB link.

## Tolerances

All limits come from measuring the reference board, which is uncalibrated (it
stores no calibration and the QA never writes one). `src/autoqa/Tolerances.h`
holds them with their derivations.

| Quantity | Measured on the reference board | Limit |
| --- | --- | --- |
| Generator frequency vs librador's realised frequency | 0-300 ppm on SG1, up to 420 ppm on SG2; the DAC timer and ADC share one clock | 0.1 % |
| Realised frequency vs the request | timer rounding, at most 0.5/48 by construction | 1.05 % |
| Realised DAC levels vs the request | 8-bit truncation | 1.5 DAC steps (12.5 mV, or 37.6 mV through the x3 stage) |
| Levels (DC readings, fitted low/high) vs realised levels | offsets -0.03 to -0.14 V near 0 V, CH1 gain error about +3 %, up to 1.6 extra ADC steps at gains 0.5 and 1; worst 0.22 V at 0.5 V and 0.38 V at 6.5 V | 0.25 V + 3 % of the level + 2 ADC steps (ADC step = 0.185 V / gain) |
| Channel gain (slope of DC readings vs set level) | 0.968-1.03 | 0.94-1.06 |
| Shape: RMS of samples minus the fitted ideal shape (frequency free within 0.1 %), over the fitted swing | worst on clean captures: sine 2.9 % (20 kHz; 1.4 % at 2 kHz), triangle 1.3 %, sawtooth 4.5 %, square 6.7 % (single-sample spikes at the edges) | sine 6 %, triangle 3 %, sawtooth 9 %, square 14 %, and the expected shape must fit best of the four |
| Square duty cycle | 0.493-0.498 | 0.48-0.52 |
| Noise, generators parked, gain 4 | 11.8-15.2 mV RMS | 30 mV |
| Crosstalk into the idle input, 6 Vpp at 2 kHz | sine 1.1-1.5 %, square 1.3-2.7 % of the driven RMS | sine 3 %, square 5 % |
| Stream | a healthy link loses nothing | 0 bad, 0 dropped, 0 period anomalies, rate within 0.5 %, pause at most 0.25 s |

Captures broken by lost USB frames give shape residuals of 55-180 % and
frequencies off by a factor of two, so a lossy link fails the affected tests
on its own, and each such test also notes how many frames were lost in its
window. Three measured effects shaped the tests rather than the limits: with
both generators running, one generator's DAC steps couple into the other input
above about 10 kHz, so the per-waveform and sweep tests drive one generator at
a time; above 20 kHz the scope sees fewer than 19 samples per period and a sine
fits as well as a triangle, so the 50 kHz point checks frequency and levels
only; and the fit lets frequency float within the frequency tolerance, because
a generator's few hundred ppm of offset, accumulated over a thousand periods,
would otherwise read as a wrong shape.

## Output

```
<out_dir>/progress.log     timestamped progress, flushed line by line
<out_dir>/report.json      the report (below)
<out_dir>/captures/        CSV windows and stream-check results, per test
<out_dir>/plots/           waveform and level plots, per test (PNG)
<out_dir>/screenshots/     the screenshot tour (PNG)
```

### report.json (schema_version 2)

```json
{"schema_version": 2,
 "suite": {"name": "labrador --auto-qa", "commit": "<sha the build was configured from, or null>"},
 "app": {"version": "2.0.123", "labrador": "<argv[0]>", "labrador_cli": null, "firmware": "labrafirm_000C_03.hex"},
 "platform": {"os": "macOS", "os_version": "14.7.6", "arch": "x86_64", "hostname": "..."},
 "started_at": "2026-11-23T08:29:10Z", "finished_at": "2026-11-23T08:45:51Z",
 "complete": true,
 "result": "pass|fail",
 "counts": {"pass": 0, "fail": 0, "skip": 0, "error": 0},
 "tests": [{"id": "hw.siggen.ch1.sine_2khz", "group": "hw", "title": "...", "status": "pass|fail|skip|error",
            "duration_s": 1.2, "message": "why it failed or skipped, plus notes such as lost USB frames",
            "measurements": [{"name": "CH1 frequency", "unit": "Hz", "value": 1999.9, "expected": 2000, "min": 1998, "max": 2002}],
            "artifacts": [{"path": "plots/hw_siggen_ch1_sine_2khz_CH1.png", "kind": "png", "caption": "..."}]}],
 "screenshots": [{"id": "gui.desktop_1280x720.classic-dark.scope.text100.board",
                  "path": "screenshots/gui.desktop_1280x720.classic-dark.scope.text100.board.png",
                  "caption": "...", "expect": "what a correct render shows"}]}
```

Paths are relative to `<out_dir>`. Groups are `hw`, `firmware` and `gui`.
`app.version` is `LABRADOR_VERSION` from the build (CI passes `2.0.<run>`;
a local build reports the project version), and `suite.commit` the commit
CMake read when it configured the build. A test fails when a measurement is
outside its `min` and `max` (or a text measurement differs from `expected`),
is an error when it threw (the message says what), and is skipped or failed
without running when a precondition is unmet: tests needing the board or a
loopback fail, and the soaks of transports the platform lacks skip. A
measurement without limits is recorded for the reviewer only.
