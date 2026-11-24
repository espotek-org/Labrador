# labrador-cli

Headless command-line companion to the Labrador app. It drives the board
through the same `librador` the GUI uses and ships with the same firmware
image, so it can run where no window is wanted: shell scripts, cron jobs, a
headless Raspberry Pi, or a quick capture from a terminal.

```
labrador-cli <command> [options]
labrador-cli help [command]

Commands:
  info             Show the board's firmware, USB transport and stored calibration
  siggen           Set the signal generators' waveform, frequency and levels
  measure          Drive the signal generators and measure what the scope sees
  capture          Save full-rate scope samples around a trigger edge to a CSV file
  stream-test      Stream the scope for a while and check no USB frames are lost or corrupt
  update-firmware  Flash the board with the firmware this build expects
```

Only one program can hold the board: close the Labrador app before running
the CLI, and vice versa. Like the app, every command that opens the board
applies the calibration stored on it, so voltages read the same in both.

## Where it is installed

| Platform | Location |
| --- | --- |
| Windows installer | `C:\Program Files\EspoTek Labrador\labrador-cli.exe`, next to `labrador.exe`. Not on `PATH`; run it with the full path or `cd` there first. The installer also installs the USB driver the CLI needs. |
| macOS DMG | Inside the bundle: `/Applications/Labrador.app/Contents/MacOS/labrador-cli` (signed and notarized with the app). Put it on `PATH` with `sudo mkdir -p /usr/local/bin && sudo ln -s /Applications/Labrador.app/Contents/MacOS/labrador-cli /usr/local/bin/labrador-cli`. |
| Linux `.deb` | `labrador-cli` on `PATH` (`/usr/bin/labrador-cli` → `/usr/lib/labrador/labrador-cli`). |
| Linux AppImage | `./Labrador-for-Linux-x86_64.AppImage cli <command> [options]`. A symlink named `labrador-cli` to the AppImage also works: `ln -s ~/Labrador-for-Linux-x86_64.AppImage ~/.local/bin/labrador-cli`. |
| Android | Not available. |

The Linux packages need the udev rule the GUI installs (the `.deb` installs
it; the AppImage offers to on first run) so the board is reachable without
root.

From a source checkout, every desktop preset builds the `labrador_cli` target
into `build/<preset>/cli/labrador-cli` (`.exe` on Windows), with a copy of the
firmware beside it:

```sh
cmake --preset macos && cmake --build --preset macos --target labrador_cli
./build/macos/cli/labrador-cli --help
```

## `info`: what is plugged in

Reports whether a board is attached and in which state (running its firmware,
waiting in bootloader mode, or misconfigured), its firmware against the one
this build needs, the USB transport in use and the calibration stored on the
board. It only reads; nothing on the board changes.

```
labrador-cli info [--json] [--verbose]
```

```
board: firmware 0x000c variant 3 (current)
USB transport: bulk
calibration: none stored on the board
```

With `--json` the same facts come as `state` (`application`, `bootloader` or
`gobindar`), `firmware` and `expected_firmware` (`version`, `variant`),
`firmware_current`, `transport` and `calibration` (`stored`, `vref_ch1`,
`gain_scale_ch1`, `vref_ch2`, `gain_scale_ch2`, `psu_offset_v`; null unless the
board runs the current firmware). Exit status is 0 whenever a board was found.

## Signal specs (`--sg1`, `--sg2`)

`siggen`, `measure`, `capture` and `stream-test` drive the two signal
generators with the same syntax, matching the app's Signals page:

| Spec | Output |
| --- | --- |
| `off` | the generator parked at the DAC's floor, about 0.06 V (what the app's off switch does) |
| `dc:VOLTS` | a constant level, 0 to 9.6 V |
| `WAVE:FREQ:VPP[:BASE]` | `sine`, `square`, `triangle` or `sawtooth` at FREQ Hz (1 Hz to 100 kHz), swinging VPP volts (0.15 to 9) up from BASE volts (0 to 9, default 0), the app's Vpeak-peak and Vbase |

The generators' output stage runs from the power supply rail, so a signal's
peak (BASE + VPP) must stay 1.18 V below the PSU voltage, set with
`--psu VOLTS` (4.5 to 11, default 4.5, the supply's minimum and the app's
default). A request the PSU cannot support is refused before anything is sent:

```
labrador-cli siggen: SG1 'sine:1000:4:0' peaks at 4 V, but with the PSU at 4.5 V the
generators reach 3.32 V; raise '--psu' to at least 5.18
```

The board rounds every request: the frequency to what the generator's timer
can make (within 1 %), the levels to its 8-bit DAC (12.5 mV steps up to
3.2 V, 37.6 mV through the x3 output stage above that), and it cannot drive
below about 0.06 V, so a lower BASE is raised and the swing shrinks to match.
Each command reports what the generator actually outputs.

## `siggen`: set the signal generators

Sets the PSU, then both generators (a generator not given is switched off),
and prints what each now outputs. The board keeps generating after the
command exits, until another command or the Labrador app changes the
generators (the app switches them off when it connects) or the board loses
power.

```
labrador-cli siggen [--sg1 SPEC] [--sg2 SPEC] [--psu VOLTS] [--json] [--verbose]
```

```sh
labrador-cli siggen --sg1 square:2000:2:0.5
```

```
PSU 4.5 V
SG1 square:2000:2:0.5: 2000 Hz, 0.489 to 2.497 V
SG2 off (parked at 0.063 V)
```

A 6 V peak needs the PSU at 7.18 V or more:

```sh
labrador-cli siggen --psu 8 --sg1 triangle:200:6:0.5 --sg2 dc:1.2
```

## `measure`: drive the generators and measure what the scope sees

Sets the PSU and generators, configures the scope, waits `--settle` seconds,
records the next `--seconds` of samples and reports, per channel, the minimum,
maximum, mean, peak-to-peak and AC RMS voltage and, for a periodic signal, the
frequency (from rising crossings of the mid level, with hysteresis), the
shortest and longest single period, and the duty cycle. Everything happens in
one session, so the result never depends on what the board was left doing.
With SG1 wired to CH1 it is a quick end-to-end check of the board.

```
labrador-cli measure [options]

  --seconds S    length of the window, up to 9.5 (4.5 in scope1-750k); default 0.1
  --sg1 SPEC     drive signal generator 1; default off
  --sg2 SPEC     drive signal generator 2; default off
  --psu VOLTS    power supply voltage, 4.5 to 11; default 4.5
  --mode MODE    scope1 (CH1 only), scope2 (CH1 and CH2, 375 kS/s each) or
                 scope1-750k (CH1 only at 750 kS/s); default scope2
  --gain G       hardware gain 0.5, 1, 2, 4, 8, 16, 32 or 64; default 4
  --settle S     pause after configuring the board; default 0.5
  --transport T  auto, iso6, iso1 or bulk; default auto
  --csv FILE     also save the window's samples (same format as capture)
  --json         print the results as JSON instead of text
  --verbose      also show librador's debug messages on stderr
```

```sh
labrador-cli measure --sg1 sine:1000:2:0.5
```

```
SG1 sine:1000:2:0.5 (actual 997.34 Hz, 0.489 to 2.497 V), SG2 off (0.063 V), PSU 4.5 V
scope scope2 at gain 4, 375000 S/s over bulk, board holds no calibration
CH1: 37500 samples, min 0.3566 V, max 2.4814 V, mean 1.4202 V, Vpp 2.1248 V, RMS(ac) 0.7378 V, 997.34 Hz over 99 cycles, duty 49.2%
CH2: 37500 samples, min -0.1515 V, max -0.0129 V, mean -0.0662 V, Vpp 0.1386 V, RMS(ac) 0.0201 V, not periodic
USB frames: 59 ok, 0 bad checksum, 59 dropped, 0 unvalidated
warning: 0 bad-checksum and 59 dropped USB frames during the window; the samples may contain corrupt or repeated stretches
```

That run was on a USB link that loses every other frame (a board behind a
USB-C hub); a healthy link reports 0 dropped and no warning. The measurement
still looks right here because a 997 Hz period is almost exactly one 1 ms USB
frame long, so a repeated frame hides in the waveform; `stream-test` is the
command that catches a lossy link.

The `--json` document holds `transport`, `firmware`, `calibration`, `mode`,
`gain`, `lsb_v` (one ADC step in volts at that gain), `sample_rate_hz`,
`stimulus` (`psu_v`, and per generator its `spec`, `wave`, `frequency_hz`,
`vpp`, `base_v` and the `actual` output: `frequency_hz`, `v_min`, `v_max`),
`ch1` and `ch2` (null in the one-channel modes: `samples`, `min_v`, `max_v`,
`mean_v`, `rms_ac_v`, `vpp`, `periodic`, and when periodic `cycles`,
`frequency_hz`, `period_min_s`, `period_max_s`, `duty`), and `frames` (`ok`,
`bad_checksum`, `dropped`, `unvalidated`, counted while the window arrived).
A window counts as periodic when it swings at least six ADC steps and rises
through its mid level at least twice.

## `capture`: triggered scope capture to CSV

Arms an edge trigger on one scope channel, waits for it to fire, and writes
the samples from `--pre` seconds before to `--post` seconds after the trigger
at the board's full rate, 375 kS/s per channel (750 kS/s in `scope1-750k`).
`armed: ...` on stderr means the trigger is waiting; Ctrl-C aborts. The
generators and PSU are left as they are unless `--sg1`, `--sg2` or `--psu` is
given; then all three are set first, in the same session, so a circuit's
response to a known stimulus can be captured in one go.

```
labrador-cli capture --out FILE [options]

  --out FILE               CSV file to write (required); FILE.json gets the metadata
  --trigger CH:EDGE:VOLTS  channel (ch1|ch2), edge (rising|falling), level; default ch1:rising:3
  --hysteresis VOLTS       signal must leave the level by this much to re-arm; default 0.25
  --pre SECONDS            keep this long before the trigger; default 0.2
  --post SECONDS           keep this long after the trigger; default 0.3
  --timeout SECONDS        give up if no trigger; 0 waits forever (default)
  --mode MODE              scope1 (CH1 only), scope2 (CH1 and CH2) or scope1-750k
                           (CH1 only at 750 kS/s); default scope2
  --gain G                 hardware gain 0.5, 1, 2, 4, 8, 16, 32 or 64; default 4
  --settle SECONDS         pause after configuring the scope; default 0.5
  --transport T            USB transport: auto (the platform default: iso6 on Windows
                           and Linux, bulk on macOS), iso6, iso1 or bulk; default auto
  --verbose                also show librador's debug messages on stderr
  --sg1 SPEC               first set signal generator 1 (see Signal specs)
  --sg2 SPEC               first set signal generator 2; switched off if not given
  --psu VOLTS              power supply voltage for the generators; default 4.5
```

`--pre` plus `--post` must fit in the board's 10 s sample buffer with 0.5 s
to spare. Full scale is about 1.65 V ± 23.65 V / gain, so pick the lowest gain
that keeps the signal in range; larger signals clip.

### Examples

Both channels, 50 ms before and 100 ms after CH1 rising through 1.5 V:

```sh
labrador-cli capture --trigger ch1:rising:1.5 --pre 0.05 --post 0.1 --out edge.csv
```

```
armed: ch1 rising through 1.5 V, pre 0.05 s, post 0.1 s
wrote edge.csv: 56250 samples at 375000 Hz (CH1+CH2), trigger at t=0 (sample 18750)
```

CH1 only at gain 16 (about ±1.5 V full scale), falling edge through 0 V:

```sh
labrador-cli capture --mode scope1 --gain 16 --trigger ch1:falling:0 --out pulse.csv
```

Trigger on CH2 (needs `--mode scope2`, the default) and give up after a
minute, so a script can tell "no event" from "captured":

```sh
if labrador-cli capture --trigger ch2:rising:2 --timeout 60 --out event.csv; then
    echo captured
else
    echo "no trigger in 60 s (exit $?)"
fi
```

Ten captures in a row, one file each:

```sh
for i in $(seq 1 10); do
    labrador-cli capture --trigger ch1:rising:1.5 --pre 0.01 --post 0.01 \
        --out "burst_$i.csv" || break
done
```

Force the bulk transport (what macOS uses by default) on Linux for an A/B
check, with librador's own log on stderr:

```sh
labrador-cli capture --transport bulk --verbose --out bulk.csv
```

Drive SG1 with a 2 kHz sine (SG1 wired to CH1) and capture 2 ms before and
4 ms after it rises through 1.5 V, all in one session:

```sh
labrador-cli capture --sg1 sine:2000:2:0.5 --trigger ch1:rising:1.5 --pre 0.002 --post 0.004 --out sine.csv
```

```
armed: ch1 rising through 1.5 V, pre 0.002 s, post 0.004 s
wrote sine.csv: 2250 samples at 375000 Hz (CH1+CH2), trigger at t=0 (sample 750)
warning: 0 bad-checksum and 19 dropped USB frames while waiting; the window may contain corrupt or repeated samples
```

(This run was on a link that loses frames, hence the warning; see `stream-test`.)

### Output

`FILE` is CSV with the header `t,CH1,CH2` (`t,CH1` in the one-channel modes). `t` is
seconds with 0 at the trigger sample; the channels are volts at the input
pins. The trigger sample is the first one at or past the level:

```
t,CH1,CH2
-0.0500000,2.9896,-0.0591
...
-0.0000027,0.2643,0.2643
0.0000000,1.5576,0.2181
0.0000027,2.9896,-0.0129
```

`FILE.json` records how the capture was taken (the stimulus is null unless
`--sg1`, `--sg2` or `--psu` was given) and whether the USB stream was clean
while waiting. From the example above, on a link that was losing frames:

```json
{
  "sample_rate_hz": 375000,
  "samples": 2250,
  "pre_samples": 750,
  "trigger": {
    "channel": 1,
    "edge": "rising",
    "level_v": 1.5,
    "hysteresis_v": 0.25
  },
  "gain": 4,
  "mode": "scope2",
  "frames_bad_checksum": 0,
  "frames_dropped": 19,
  "transport": "bulk",
  "firmware": {
    "version": 12,
    "variant": 3
  },
  "calibration": {
    "stored": false,
    "vref_ch1": 1.65,
    "gain_scale_ch1": 1,
    "vref_ch2": 1.65,
    "gain_scale_ch2": 1,
    "psu_offset_v": 0
  },
  "stimulus": {
    "psu_v": 4.5,
    "sg1": {
      "spec": "sine:2000:2:0.5",
      "wave": "sine",
      "frequency_hz": 2000,
      "vpp": 2,
      "base_v": 0.5,
      "actual": {
        "frequency_hz": 2000,
        "v_min": 0.4894117647,
        "v_max": 2.497254902
      }
    },
    "sg2": {
      "spec": "off",
      "wave": "off",
      "actual": {
        "v_min": 0.06274509804,
        "v_max": 0.06274509804
      }
    }
  }
}
```

Non-zero `frames_bad_checksum` or `frames_dropped` also prints a warning:
the window may contain corrupt or repeated samples.

## `stream-test`: is the USB link losing data?

Streams the scope for `--seconds` (default 60) and reports the USB frame
counters, the sample rate actually delivered and the longest pause in the
stream. With `--check ch1|ch2|both` it also follows a periodic signal on that
input (a generator wired to it and driven with `--sg1`/`--sg2`) and counts
periods more than 5 % off the median: a lost or repeated 1 ms frame shifts the
waveform by 375 samples and shows up there even when no counter notices.
Progress goes to stderr every five seconds.

```
labrador-cli stream-test [options]

  --seconds S    how long to stream; default 60
  --check CH     ch1, ch2 or both: inputs carrying a periodic signal to follow
  --sg1/--sg2/--psu, --mode/--gain/--settle/--transport/--verbose as for measure
  --json         print the results as JSON instead of text
```

Pick a frequency whose period is not a whole fraction of 375 samples (for
example 354.1 Hz), so every frame carries a different part of the waveform:

```sh
labrador-cli stream-test --sg1 sine:354.1:2:0.5 --check ch1 --seconds 12
```

A healthy link reports zero bad, dropped and off-period counts. This run is
from a macOS laptop with the board behind a USB-C hub, where only every other
bulk frame arrived:

```
stream-test: streaming for 12 s over bulk
stream-test: 5 of 12 s, 1875750 samples, frames 2501 ok, 0 bad checksum, 2501 dropped, 0 unvalidated, CH1 period anomalies 1533
stream-test: 10 of 12 s, 3751500 samples, frames 5002 ok, 0 bad checksum, 5002 dropped, 0 unvalidated, CH1 period anomalies 3067
streamed 12.0 s over bulk: 4489500 samples (375077.6 S/s delivered, 375000 expected), longest pause 0.039 s
USB frames: 5986 ok, 0 bad checksum, 5986 dropped, 0 unvalidated
CH1: 8070 periods of 375.43 samples (998.855 Hz), largest deviation 183.67%, 3671 more than 5% off
```

The sample count still adds up because librador fills each lost frame with a
copy of the last good one; the period check is what exposes it. The JSON
document holds `transport`, `samples`, `delivered_sample_rate_hz`,
`max_stall_s`, `frames` (`ok`, `bad_checksum`, `dropped`, `unvalidated`),
`anomaly_fraction` and per checked channel `continuity` entries (`channel`,
`periods`, `median_period_samples`, `frequency_hz`, `max_period_deviation`,
`anomalies`, and the first 20 `anomaly_times_s`). Exit status is 0 whenever
the test ran, whatever it found, so scripts must read the counts to judge the
link.

## `update-firmware`: flash the bundled firmware

Checks the attached board's firmware against the version this build streams
from and reflashes it when they differ. A board stuck in bootloader mode is
launched first and reflashed only if that fails. Flashing takes about ten
seconds; do not unplug the board meanwhile.

```
labrador-cli update-firmware [options]

  --hex FILE  Intel hex image to flash; default is the bundled labrafirm_000D_03.hex
  --force     reflash even if the board already runs the expected firmware
  --verbose   also show librador's debug messages on stderr
```

The bundled image is the one the Labrador app flashes, found in the
`assets/firmware` directory the packages install beside the binaries
(`Contents/Resources/assets/firmware` inside the macOS bundle).

```sh
labrador-cli update-firmware
```

```
firmware 0x000c variant 3 is already current; use --force to reflash
```

Reflash regardless, or flash a different image:

```sh
labrador-cli update-firmware --force
labrador-cli update-firmware --hex ~/labrafirm_000D_03.hex
```

`capture` refuses to run on a board with other firmware and points here:

```
labrador-cli capture: error: the board is running firmware 0x0007 variant 2 but
this build needs 0x000c variant 3; run 'labrador-cli update-firmware' first
```

## Exit status

| Code | Meaning |
| --- | --- |
| 0 | Success. For `update-firmware`: the board ends up on the expected firmware, flashed or not. For `info`: a board was found, whatever its state. For `stream-test`: the test ran, whatever its counts show. |
| 1 | The work failed: no board, board open elsewhere, wrong firmware, trigger timeout, write error, flash error. |
| 2 | Bad command line, including a signal the generators cannot make or the PSU cannot support. |

Errors go to stderr as `labrador-cli <command>: error: ...`.
