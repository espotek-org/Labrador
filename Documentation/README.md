# EspoTek Labrador — Documentation

The EspoTek Labrador is a small USB board that turns your computer, Raspberry Pi,
or Android phone into a five-in-one electronics lab bench:

| Instrument | What it does |
|---|---|
| **Oscilloscope** (2 channels) | Draws a graph of voltage over time, so you can *see* signals |
| **Arbitrary waveform generator** (2 channels) | Produces sine, square, triangle, sawtooth or custom waves |
| **Power supply** | A programmable 4.5–11 V supply for powering your circuits |
| **Logic analyzer** (2 channels) | Captures digital signals and decodes UART and I²C |
| **Multimeter** | Measures voltage, current, resistance and capacitance |

These docs cover the board itself and the **Labrador app** (the unified
interface in `Unified_App/`), and are written assuming you're comfortable with
basic circuit ideas (voltage, current, resistance) but may never have used an
oscilloscope before.

## Where to start

1. **[Getting started](getting-started.md)** — what's in the box, installing
   the software, plugging in for the first time.
2. **[Tutorial — your first measurements](tutorial.md)** — a 20-minute,
   two-jumper-wire tour of the scope and signal generator, then a real circuit.
3. **[User manual](user-manual.md)** — every page, control and shortcut in the
   app, instrument by instrument.
4. **[Pinout reference](pinout.md)** — what every pin on the board does, plus
   the board's electrical limits.
5. **[Troubleshooting](troubleshooting.md)** — device not detected, safety
   mode, firmware recovery.
6. **[LLM guide](llm-guide.md)** — a dense, self-contained reference written
   for AI assistants and automation agents (exact UI strings, limits,
   wiring recipes, misconception traps). Point your AI tools at this file —
   there is also an [`llms.txt`](../llms.txt) index at the repository root.

## Quick specifications

| | |
|---|---|
| Oscilloscope | 2 ch, −20 V to +20 V, 1 MΩ input, 375 kSa/s per channel (750 kSa/s single-channel), ~100 kHz usable bandwidth |
| Waveform generator | 2 ch, 0.15–9 V peak-to-peak, 1 Hz – 1 MHz, 8-bit / 1 MSa/s / 512-sample buffer, 50 Ω output |
| Power supply | 4.5–11 V, up to 0.75 W |
| Logic analyzer | 2 ch, 3 MSa/s per channel, tolerant of 3.3 / 5 / 12 V logic |
| Digital outputs | 4 pins, 3.3 V, 50 Ω source impedance |
| Multimeter | ±20 V; current, resistance and capacitance measured with an external reference resistor |
| Host connection | Micro-USB (USB 1.1 full speed) |

One hardware limit worth knowing from day one: the board has **two data-buffer
resources**. Each running scope channel uses one, the multimeter uses both.
That's why the multimeter and the oscilloscope can't run at the same time —
the app switches modes for you and tells you when it does.

## More resources

* [EspoTek Labrador wiki](https://github.com/espotek-org/Labrador/wiki) — the original wiki these docs draw on
* [Lief Koepsel's tutorial series](https://www.wellys.com/posts/courses_electronics/) — excellent articles and videos for beginners
* [espotek.com](https://espotek.com) — buy a board, contact the developer
