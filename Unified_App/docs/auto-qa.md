# `labrador --auto-qa`

The release binary tests itself and the board attached to it, in one run, and
writes a full QA report:

```sh
labrador --auto-qa <out_dir>
```

Options choose a quicker or more thorough run, a subset of the tests, and a
number of repeats (see "Choosing what runs"). It needs no QA build (no ImGui Test Engine) and no other program: hardware
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
  flushed, timestamped line for every phase and every test
  (`n/m id: status (t s)`, where m is the number of planned test executions
  over all iterations) throughout, and with a repeat a line `iteration k/n` at
  the start of each iteration. A test that reports its progress while it runs
  (the soaks: seconds streamed of the total, frames ok, bad and dropped; the
  firmware round trip: which step it is on) also writes that progress there
  whenever 20 s have passed without a line, so a soak never goes 30 s without
  one. Only progress the test itself reports is written, so a test that hangs
  still leaves the log silent, which the QA server takes for a hang after 10
  minutes. The first report.json, with the whole plan and no results, is
  written before the first test starts. A release without `--auto-qa` exits 2 with "unknown option" on
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

## Choosing what runs

```sh
labrador --auto-qa <out_dir> [--auto-qa-level quick|standard|thorough]
                             [--auto-qa-tests <item>[,<item>...]]
                             [--auto-qa-repeat <n>]
labrador --auto-qa-list      [the same three options]
```

With none of the options a run holds every test of the `standard` level once,
which is exactly the suite in the tables below, in that order and with those
ids. Only such a run, at `standard` or `thorough` with no selection and no
repeat, counts as a release test; anything narrower is for chasing a failure.
The options are valid only with `--auto-qa` or `--auto-qa-list`.

**Levels.** Every test has a minimum level, `quick` < `standard` < `thorough`,
and a run at a level holds the tests whose minimum is at or below it. A level
can also set a test's parameters: at `thorough` each transport soak needs five
clean 60 s windows instead of one, and its title says so. The assignment lives in
`QUICK_TESTS` and `THOROUGH_TESTS` in `src/autoqa/Plan.cpp`, so moving a test
between levels means moving its id from one list to another (an id there that
names no test stops the app).

- `quick` (29 tests, a few minutes): `hw.board.present`, both `hw.wiring`
  tests, both `hw.noise` tests, the eight `hw.siggen.chN.<wave>_831hz`,
  `hw.siggen.dc_levels`, `hw.gain.x0_5_831hz`, `hw.gain.x4_831hz`,
  `hw.gain.x64_831hz`, `hw.trigger.ch1_rising_831hz`,
  `hw.trigger.ch2_rising_square_831hz`, `hw.transport.<t>.measure` and
  `.measure_831hz` for bulk, iso1 and iso6, and four screenshots: desktop,
  compact and mobile in Classic Dark with the board, and desktop with USB off.
- `standard`: every other test, the firmware round trip included.
- `thorough`: no extra tests yet, only the five-window soaks.

The whole 831 Hz battery belongs to `standard` (a hard requirement of the
project owner): no `_831hz` test may be thorough-only, and the app refuses to
start if one is listed under `THOROUGH_TESTS`. `quick` draws its waveform tests
from the 831 Hz versions.

**Selection.** `--auto-qa-tests` takes up to 20 comma-separated items, each
matching `[a-z0-9_*][a-z0-9_.*-]{0,79}`:

- an item without `*` selects the test with that id and every test whose id
  starts with `<item>.`, so `hw.gain` selects the 16 gain tests, `hw.gain.x64`
  only `hw.gain.x64` (not `hw.gain.x64_831hz`), `gui` every screenshot and
  `firmware` the round trip;
- an item with `*` is a glob over the whole id, `*` matching any run of
  characters, dots included: `*_831hz` selects the 831 Hz battery (28 tests),
  `gui*.noboard` the five shots with USB off.

Only tests of the run's level can be selected. The tests a selected test
depends on are added automatically, in their normal place: each test declares
the conditions it needs and the ones it establishes, and every test that
establishes a needed condition joins the plan, transitively. Today `board`
comes from `hw.board.present`, `sg1_ch1` and `sg2_ch2` from `hw.wiring.sg1`
and `hw.wiring.sg2`, and `transport_<t>` from `hw.transport.<t>.measure`. So
`hw.transport.iso6.soak` runs after `hw.board.present`, both wiring tests and
`hw.transport.iso6.measure`, and any board-connected screenshot after
`hw.board.present`. Tests always run in the suite's order, whatever order the
items came in, and a phase with nothing planned is skipped.

**Repeat.** `--auto-qa-repeat <n>` (1 to 200) runs the planned tests n times
in one process and one report. Each iteration is exactly what a single run
does and starts from the state the first one did: conditions unmet again, the
hardware tests' device session dropped (a transport test leaves one on its own
transport behind), USB switched back on if the screenshot tour had switched it
off, and the long first-shot settle again. Ids do not change; every result
carries its `iteration`. With n above 1 the files of iteration k go one level
down, `plots/iter<k>/`, `captures/iter<k>/` and `screenshots/iter<k>/`; with
n = 1 the paths are those of a plain run. The exit status is 0 only if every
test of every iteration passed.

**Usage errors** print one line on stderr and exit 2 before `<out_dir>`,
progress.log or a window exists: an option without `--auto-qa` or
`--auto-qa-list`, an option without its value or given twice, an unknown
level, a malformed selection, an item that matches no test of the level (the
message names the level, since the test may exist at a higher one) and a
repeat that is not a whole number from 1 to 200.

**`--auto-qa-list`** prints the plan the same options would run, as one JSON
object on stdout, and exits 0. It opens no window, touches no board or
settings and creates nothing:

```json
{"schema_version": 3, "level": "standard", "selection": ["hw.gain.x64"], "repeat": 1,
 "planned": [{"id": "hw.board.present", "group": "hw", "title": "...", "level": "quick",
              "iteration": 1, "prerequisite": true}, "..."]}
```

`planned` is the same list report.json carries (below).

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
| `hw.noise.ch1`, `ch2` | the noise floor with both generators parked (about 0.06 V): the median AC RMS of 5 ms slices of a 0.5 s window, which bursts picked up from outside the board do not move, with the whole window's RMS and the noisiest slice recorded beside it; and the parked level |
| `hw.siggen.chN.{sine,square,triangle,sawtooth}_2237hz`, `..._831hz` | one generator at a time, 2237 Hz and again at exactly 831 Hz, 0.5-2.5 V at gain 4 (neither period fits the 375-sample USB frame evenly, so a lost, repeated or shifted frame shows up in the fit, where at 2 kHz it would read as a clean capture): librador's realised frequency and DAC levels against the request, the measured frequency against the realised one, the low and high levels of a least-squares fit of the ideal shape, the fit's residual, that the expected shape fits better than the other three, and the square's duty cycle |
| `hw.siggen.chN.sine_6vpp_x3`, `sine_6vpp_x3_831hz` | a 0.5-6.5 V sine, at 2237 Hz and at 831 Hz, through the generators' x3 output stage, which needs the PSU raised to 8 V, so this also shows the PSU setting takes effect |
| `hw.siggen.dc_levels` | DC at 0.25, 0.5, 1, 2, 3, 4 and 6 V on both generators, read on both inputs, plus each channel's gain (slope) and offset, plotted against the limits |
| `hw.gain.x0_5` ... `x64`, `hw.gain.x0_5_831hz` ... `x64_831hz` | for each of the eight scope gains, a 2237 Hz sine (831 Hz for the `_831hz` tests) filling most of that gain's input range on both inputs: unclipped, levels, frequency and shape |
| `hw.sweep.f10hz` ... `f49487hz` | a 2 Vpp sine at 10, 50, 113, 354.1, 1250, 1789, 5477, 10477, 19433 and 49487 Hz on each generator in turn: frequency, levels and (up to 20 kHz) shape. From 113 Hz up the points are chosen by the frequency the generator really makes, so that a lost USB frame shows (see "Frequencies that show a lost frame") |
| `hw.mode.scope1`, `hw.mode.scope1_831hz` | CH1-only mode streams 375 kS/s and no CH2, with a 2237 Hz sine and with an 831 Hz one |
| `hw.mode.scope1_750k`, `hw.mode.scope1_750k_831hz` | CH1-only 750 kS/s mode with a 10477 Hz sine and with an 831 Hz one |
| `hw.isolation.sgA_to_chB.{sine,square}` | a 6 Vpp 2237 Hz sine or square on one generator, the other parked: the idle input's AC RMS as a fraction of the driven one's |
| `hw.trigger.ch1_rising`, `ch1_falling`, `ch2_rising_square`, `ch2_falling_triangle`, each also as `..._831hz` | librador's triggered capture of a 2237 Hz wave (831 Hz for the `_831hz` tests): sample count, pre-trigger count, the samples either side of the level, and that the fitted generator waveform crosses the level in the right direction within 3 samples of t = 0 |
| `hw.trigger.placement`, `hw.trigger.placement_831hz` | on a 2237 Hz sine (831 Hz for the `_831hz` test), all-post-trigger, mostly pre-trigger and 1.5 s windows: sample counts and the samples at the trigger |
| `hw.trigger.timeout_unreachable` | a level above the signal times out after the 2 s timeout |
| `hw.trigger.no_false_trigger_on_dc` | a DC level sitting on the trigger level, with hysteresis, never triggers on noise |
| `hw.transport.{bulk,iso1,iso6}.measure` | the session really uses the transport asked for (librador allows only bulk on macOS, so the iso tests skip there) and measures a 2237 Hz sine on CH1 and a 4474 Hz triangle on CH2 at once (smooth waves, because a square's edges couple into the other input as spikes that can fake a crossing); its result gates the soak and the `_831hz` measure |
| `hw.transport.{bulk,iso1,iso6}.measure_831hz` | once the transport's measure has passed, an 831 Hz sine on CH1 and an 831 Hz triangle on CH2, told apart by their shapes |
| `hw.transport.{bulk,iso1,iso6}.restarts_831hz` | the transport opened eight times in a row, each start measuring an 831 Hz sine on CH1 and an 831 Hz triangle on CH2: the transport in use, no lost or corrupt frames in the judged window, and each input's frequency and shape. iso6 once started with its endpoints out of step on five starts in eight, which opening a transport once or twice can miss |
| `hw.transport.{bulk,iso1,iso6}.soak` | a stream with a 354.1 Hz sine on SG1 and a 1234.5 Hz triangle on SG2, judged in 60 s windows while it keeps running. A clean window has no frames with a bad checksum, none dropped, at least 99 % of the expected frames, the delivered sample rate within 0.5 %, no pause over 0.25 s, and no period off the median on both inputs at once. The soak needs one clean window (five at the `thorough` level). On real hardware the first window that is not clean fails it. In a virtual machine up to seven windows may be interrupted before the clean ones, each recorded in the result, because a hypervisor now and then leaves a passed-through board unserviced for a fraction of a second; a window that lost over 5 % of its frames is a broken link and fails the soak at once (see "Virtual machines"). These frequencies' periods are not a whole fraction of the 375-sample USB frame, so a lost or repeated frame shifts the waveform and is caught even if no counter notices. |
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
run finishes (since schema 2; schema 1 wrote it once, at the end), so a crash, a killed process or a dead VM still leaves
everything recorded up to that point. With a repeat, each iteration runs the
three phases in turn before the next iteration starts.

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
| Noise floor, generators parked, gain 4 (median AC RMS of 5 ms slices) | whole-window RMS: CH1 12.6-18.1 mV over 36 runs; CH2 19.8-36.9 mV over 115, lifted by bursts from outside the board (a Wi-Fi beacon every 102.4 ms on the QA tester) | 30 mV |
| Crosstalk into the idle input, 6 Vpp at 2 kHz | sine 1.1-1.5 %, square 1.3-2.7 % of the driven RMS | sine 3 %, square 5 % |
| Stream | a healthy link loses nothing | 0 bad, 0 dropped, 0 period anomalies, rate within 0.5 %, pause at most 0.25 s |

Captures broken by lost USB frames give shape residuals of 55-180 % and
frequencies off by a factor of two, so a lossy link fails the affected tests
on its own, and each such test also notes how many frames were lost in its
window. A capture that had lost or corrupt frames is taken again, up to four
times with waits of 0.2, 0.4 and 0.8 s between them, and the last one is
judged: these tests judge the analog path and the trigger, and the
`hw.transport` tests judge the stream. Three measured effects shaped the tests
rather than the limits: with
both generators running, one generator's DAC steps couple into the other input
above about 10 kHz, so the per-waveform and sweep tests drive one generator at
a time; above 20 kHz the scope sees fewer than 19 samples per period and a sine
fits as well as a triangle, so the 49487 Hz point checks frequency and levels
only; and the fit lets frequency float within the frequency tolerance, because
a generator's few hundred ppm of offset, accumulated over a thousand periods,
would otherwise read as a wrong shape.

### Frequencies that show a lost frame

The scope streams in 1 ms USB frames, and librador fills a lost frame with a
repeat of the one before, so everything after it is out of place by one frame.
How visible that is depends on the waveform: the shift is the fractional part
of (frequency / 1000) periods, and near 0 or 1 the waveform lands back on
itself. What counts is the frequency the generator really makes. Its timer and
the scope's ADC share one clock, so a request is rounded to whole timer ticks:
2 kHz becomes 1994.68 Hz, a period of exactly 188 samples and a shift of 0.995,
and even 997 Hz becomes 997.34 Hz, exactly 376 samples. 2237 Hz becomes
2239.64 Hz (a shift of 0.24) and 831 Hz becomes 829.65 Hz (0.83).

Every waveform test records this shift for its generator from the frequency
librador reports, and fails if it is under 0.1 or over 0.9, so a frequency that
would hide lost frames cannot come back unnoticed. The sweep's 10 and 50 Hz
points are recorded without a limit: below 100 Hz the shift is under a tenth of
a period whatever the frequency.

### Virtual machines

`hw.board.present` records whether the machine is real hardware or a virtual
machine (the hypervisor-present bit of CPUID on x86, `kern.hv_vmm_present` on
macOS; ARM Linux has neither and counts as real hardware), and report.json
carries it as `platform.virtual_machine` and `platform.hypervisor`. Only the
soaks treat the two differently. Real hardware polls the board on the USB
controller's own schedule, so a clean stream there has no excuse. A hypervisor
services a passed-through board in software, and when the host is busy it can
leave the board unserviced for 0.1 to 0.9 s, which costs an isochronous stream
its frames whatever the app does. A soak in a virtual machine may therefore
have up to seven interrupted windows before its clean ones. A stream that keeps
losing frames never has a clean window, and fails as it does on real hardware.

## Output

```
<out_dir>/progress.log     timestamped progress, flushed line by line
<out_dir>/report.json      the report (below)
<out_dir>/captures/        CSV windows and stream-check results, per test
<out_dir>/plots/           waveform and level plots, per test (PNG)
<out_dir>/screenshots/     the screenshot tour (PNG)
```

With `--auto-qa-repeat` above 1, the last three each hold an `iter<k>/`
directory per iteration instead.

### report.json (schema_version 3)

```json
{"schema_version": 3,
 "level": "standard",
 "selection": null,
 "repeat": 1,
 "planned": [{"id": "hw.board.present", "group": "hw", "title": "...", "level": "quick",
              "iteration": 1, "prerequisite": false}],
 "suite": {"name": "labrador --auto-qa", "commit": "<sha the build was configured from, or null>"},
 "app": {"version": "2.0.123", "labrador": "<argv[0]>", "labrador_cli": null, "firmware": "labrafirm_000D_03.hex"},
 "platform": {"os": "macOS", "os_version": "14.7.6", "arch": "x86_64", "hostname": "...",
              "virtual_machine": false, "hypervisor": null},
 "started_at": "2026-11-23T08:29:10Z", "finished_at": "2026-11-23T08:45:51Z",
 "complete": true,
 "result": "pass|fail",
 "counts": {"pass": 0, "fail": 0, "skip": 0, "error": 0},
 "tests": [{"id": "hw.siggen.ch1.sine_2237hz", "iteration": 1, "group": "hw", "title": "...", "status": "pass|fail|skip|error",
            "duration_s": 1.2, "message": "why it failed or skipped, plus notes such as lost USB frames",
            "measurements": [{"name": "CH1 frequency", "unit": "Hz", "value": 2236.9, "expected": 2237, "min": 2234.8, "max": 2239.2}],
            "artifacts": [{"path": "plots/hw_siggen_ch1_sine_2237hz_CH1.png", "kind": "png", "caption": "..."}]}],
 "screenshots": [{"id": "gui.desktop_1280x720.classic-dark.scope.text100.board",
                  "path": "screenshots/gui.desktop_1280x720.classic-dark.scope.text100.board.png",
                  "caption": "...", "expect": "what a correct render shows"}]}
```

Schema 3 adds the plan to schema 2. `level`, `selection` (null for every test
of the level, else the items as given, in order) and `repeat` are the run's
options. `planned` holds every test execution the run set out to do, in
execution order, and is complete in the first report.json (written before the
first test, with `"tests": []`); `prerequisite` is true for an entry that is
there only because a selected test needs it, and `level` is the test's minimum
level. Each entry of `tests` matches one `planned` entry by `id` and
`iteration`; the only results outside `planned` are `hw.suite` and
`firmware.suite`, recorded when a phase's worker thread stopped early, so a
run's progress is `len(tests)` of `len(planned)`. `counts` covers every result
of every iteration, and `screenshots` lists every shot of every iteration
(with a repeat, their paths tell the iterations apart).

Paths are relative to `<out_dir>`. Groups are `hw`, `firmware` and `gui`.
`app.version` is `LABRADOR_VERSION` from the build (CI passes `2.0.<run>`;
a local build reports the project version), and `suite.commit` the commit
CMake read when it configured the build. A test fails when a measurement is
outside its `min` and `max` (or a text measurement differs from `expected`),
is an error when it threw (the message says what), and is skipped or failed
without running when a precondition is unmet: tests needing the board or a
loopback fail, and the soaks of transports the platform lacks skip. A
measurement without limits is recorded for the reviewer only.
