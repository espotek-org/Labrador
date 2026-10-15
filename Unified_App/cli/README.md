# labrador-cli

Headless command-line companion to the Labrador app. It drives the board
through the same `librador` the GUI uses and ships with the same firmware
image, so it can run where no window is wanted: shell scripts, cron jobs, a
headless Raspberry Pi, or a quick capture from a terminal.

```
labrador-cli <command> [options]
labrador-cli help [command]

Commands:
  capture          Save full-rate scope samples around a trigger edge to a CSV file
  update-firmware  Flash the board with the firmware this build expects
```

Only one program can hold the board: close the Labrador app before running
the CLI, and vice versa.

## Where it is installed

| Platform | Location |
| --- | --- |
| Windows installer | `C:\Program Files\EspoTek Labrador\labrador-cli.exe`, next to `labrador.exe`. Not on `PATH`; run it with the full path or `cd` there first. The installer also installs the USB driver the CLI needs. |
| macOS DMG | Inside the bundle: `/Applications/Labrador.app/Contents/MacOS/labrador-cli` (signed and notarized with the app). Put it on `PATH` with `sudo mkdir -p /usr/local/bin && sudo ln -s /Applications/Labrador.app/Contents/MacOS/labrador-cli /usr/local/bin/labrador-cli`. |
| Linux `.deb` | `labrador-cli` on `PATH` (`/usr/bin/labrador-cli` → `/usr/lib/labrador/labrador-cli`). |
| Linux AppImage | `./Labrador_x86_64.AppImage cli <command> [options]`. A symlink named `labrador-cli` to the AppImage also works: `ln -s ~/Labrador_x86_64.AppImage ~/.local/bin/labrador-cli`. |
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

## `capture`: triggered scope capture to CSV

Arms an edge trigger on one scope channel, waits for it to fire, and writes
the samples from `--pre` seconds before to `--post` seconds after the trigger
at the board's full rate, 375 kS/s per channel. `armed: ...` on stderr means
the trigger is waiting; Ctrl-C aborts.

```
labrador-cli capture --out FILE [options]

  --out FILE               CSV file to write (required); FILE.json gets the metadata
  --mode MODE              scope1 (CH1 only) or scope2 (CH1 and CH2); default scope2
  --gain G                 hardware gain 0.5, 1, 2, 4, 8, 16, 32 or 64; default 4
  --trigger CH:EDGE:VOLTS  channel (ch1|ch2), edge (rising|falling), level; default ch1:rising:3
  --hysteresis VOLTS       signal must leave the level by this much to re-arm; default 0.25
  --pre SECONDS            keep this long before the trigger; default 0.2
  --post SECONDS           keep this long after the trigger; default 0.3
  --timeout SECONDS        give up if no trigger; 0 waits forever (default)
  --settle SECONDS         pause after configuring the scope; default 0.5
  --transport T            USB transport: auto (the platform default: iso6 on Windows
                           and Linux, bulk on macOS), iso6, iso1 or bulk; default auto
  --verbose                also show librador's debug messages on stderr
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

### Output

`FILE` is CSV with the header `t,CH1,CH2` (`t,CH1` in `scope1` mode). `t` is
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

`FILE.json` records how the capture was taken and whether the USB stream was
clean while waiting:

```json
{
  "sample_rate_hz": 375000,
  "samples": 56250,
  "pre_samples": 18750,
  "trigger": {"channel": 1, "edge": "rising", "level_v": 1.5, "hysteresis_v": 0.25},
  "gain": 4,
  "mode": "scope2",
  "frames_bad_checksum": 0,
  "frames_dropped": 0
}
```

Non-zero `frames_bad_checksum` or `frames_dropped` also prints a warning:
the window may contain corrupt or repeated samples.

## `update-firmware`: flash the bundled firmware

Checks the attached board's firmware against the version this build streams
from and reflashes it when they differ. A board stuck in bootloader mode is
launched first and reflashed only if that fails. Flashing takes about ten
seconds; do not unplug the board meanwhile.

```
labrador-cli update-firmware [options]

  --hex FILE  Intel hex image to flash; default is the bundled labrafirm_000C_03.hex
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
labrador-cli update-firmware --hex ~/labrafirm_000C_03.hex
```

`capture` refuses to run on a board with other firmware and points here:

```
labrador-cli capture: error: the board is running firmware 0x0007 variant 2 but
this build needs 0x000c variant 3; run 'labrador-cli update-firmware' first
```

## Exit status

| Code | Meaning |
| --- | --- |
| 0 | Success. For `update-firmware`: the board ends up on the expected firmware, flashed or not. |
| 1 | The work failed: no board, board open elsewhere, wrong firmware, trigger timeout, write error, flash error. |
| 2 | Bad command line. |

Errors go to stderr as `labrador-cli <command>: error: ...`.
