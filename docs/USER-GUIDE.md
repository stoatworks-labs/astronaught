# Astronaught user guide

Astronaught puts a video signal through a Space Echo. It is an FFGL plugin for
[Resolume](https://resolume.com) Arena and Avenue that models the **Roland RE-201**: a loop of
tape pulled past an erase head, a record head and three playback heads at equal intervals; a
twelve-position Mode Selector deciding which of them are listening; an Intensity control feeding
the head mix back onto the tape; and a three-spring reverb tank hanging off the input in parallel.

One idea holds the whole thing up, and it is worth twenty seconds because every control follows
from it.

**The tape is indexed by position, not by time.** A frame is laid on the tape wherever the tape
had reached when it was recorded, and it stays there. A playback head is a fixed distance down the
path, so it reads whatever material has arrived under it. The delay is not a setting — it is the
time the tape takes to cover the head spacing.

Three things follow, and they are what makes this a tape echo rather than a delay:

- **Turning Repeat Rate drags every echo already on the tape.** The material does not move; only
  the rate at which the heads reach it does. Turn it while echoes are running and they slide and
  re-pitch on the way to the new setting, which is the sound — and the look — the machine is
  famous for.
- **The picture comes back torn and rolled.** A frame is written *down* the tape while the tape is
  moving, so it occupies a stretch of it, and a head almost never lands on a boundary. The top of
  an echo is the bottom of one pass and the bottom is the top of the next.
- **The three echoes wobble independently.** The transport is never quite steady, and each head is
  reading a different point on the tape, so each one sees the speed error from a different moment.
  That is the chorus.

> **Before you rely on this:** 21 automated checks pass from a clean universal build. The GLSL tape
> read agrees with an independent C++ implementation on every row to 1.6e-06; the twelve-position
> Mode Selector is checked against Roland's own published table, including the three head
> combinations the machine does **not** offer; a bypassed Astronaught returns the picture **byte for
> byte**; and the drag is measured, not asserted — after a 500 ms to 125 ms speed change, head 1 is
> still reading 450 ms-old material.
>
> **It has been loaded and rendered in Resolume Arena 7.27.1 on both macOS and Windows** — the
> Windows run on Mesa llvmpipe, a completely different GLSL compiler, which is what makes it worth
> doing. Both hosts read all 27 parameters correctly, with nothing truncated, and a factory preset
> applies and holds on both.
>
> Still open: **no operator has dragged a slider.** Every control was driven over Resolume's REST
> API, so the inspector's feel is unjudged. **No NVIDIA or AMD driver has run it.** There is no
> OpenFX build. None of it has been through a show.
>
> This codebase was created with AI assistance, directed and reviewed by a human author.

---

## Start here

Drop Astronaught on a clip or a layer. Out of the box it is selector position **4** — heads 2 and
3, no reverb — with the transport unsteady and Intensity up far enough to give you several
repeats. You should see the picture echoed twice at a 2:3 rhythm, drifting.

Then, in order:

1. **Repeat Rate.** This is a tape speed, not a delay time. Turn it up and the echoes get *shorter*
   — and watch what happens to the ones already running while you turn it.
2. **Mode.** Twelve positions. Try 1, then 4, then 11.
3. **Intensity.** How much of what the heads hear goes back onto the tape. Past about three
   quarters it starts to build rather than decay.

## The Mode Selector

Twelve positions, exactly the ones the hardware has. Note what is **not** there: positions 1 to 4
are echo only and the only combination among them is heads 2+3. There is no dry 1+2 and no dry
1+2+3. Everything from 5 to 11 has the spring tank in, and 12 is the tank on its own.

| Position | Heads | Reverb |
|---|---|---|
| 1 | 1 | |
| 2 | 2 | |
| 3 | 3 | |
| 4 | 2 + 3 | |
| 5 | 1 | ● |
| 6 | 2 | ● |
| 7 | 3 | ● |
| 8 | 1 + 2 | ● |
| 9 | 2 + 3 | ● |
| 10 | 1 + 3 | ● |
| 11 | 1 + 2 + 3 | ● |
| 12 | — | ● |

The heads are bolted at equal intervals, so their delays are locked at **1 : 2 : 3**. There is one
delay control, not three. Head 1 runs from 70 ms to 1 second, which puts head 3 at up to three
seconds.

## The controls

### Echo

| Control | What it does |
|---|---|
| **Mode** | The twelve-position selector above. |
| **Repeat Rate** | Tape speed. **Up is faster, so up is shorter.** Turning it drags the echoes already recorded. |
| **Intensity** | How much of the head mix is re-recorded. Each trip is another generation — more saturation, more softening, more noise. Past about 0.8 it builds. |
| **Head Spread** | *Not on the hardware.* Moves the playback heads along the path. 0.602 is where the RE-201 bolts them; below that the taps bunch up, above it they spread out and the tears get wilder. |

### Tape

| Control | What it does |
|---|---|
| **Input Level** | Drive into the record amplifier. More level means more saturation and a hotter loop. |
| **Saturation** | *Not on the RE-201 — it is on BOSS's own RE-20 and RE-202.* Transparent below the knee, folding the highlights above it. |
| **Head Wear** | High-frequency loss along the scan, and wider tape noise with it. |
| **Hiss** | The oxide's noise floor. **Leave a little in.** It is inside the feedback loop, where it dithers the store — turn it to zero with Intensity near unity and long echoes will band. |
| **Dropouts** | The tape lifting off the head. Blocks of a scanline, not speckle. |

### Transport

| Control | What it does |
|---|---|
| **Wow & Flutter** | Master trim on the whole speed error. At zero the transport is perfect and the echoes lock to exactly 1 : 2 : 3. |
| **Wow** | Slow, sub-hertz. Leans the picture. |
| **Flutter** | A few hertz to tens. Draws a wave down the frame. |
| **Scrape** | Stick-slip against the heads. Fine horizontal roughness. |

### Reverb

Only live in positions 5 to 12 — on the hardware there is no reverb switch, there are eight
positions of the selector that have the springs in.

| Control | What it does |
|---|---|
| **Reverb Time** | How long the tank rings, 0.35 s to 6 s. |
| **Dispersion** | *Not on the hardware.* How much the tank blooms as it decays. A spring is a dispersive delay line — fine detail lags behind coarse — and this is how much of that there is. |

### Output

| Control | What it does |
|---|---|
| **Echo Volume** | The head bus into the output. |
| **Reverb Volume** | The tank into the output. |
| **Direct Volume** | The dry signal. It never goes near the tape, so it stays sharp. |
| **Bass** / **Treble** | Shelves on the **wet** bus only — the direct signal comes through flat. Centred at 0.5. |
| **Mix** | Wet/dry against the untouched input. At 0 the plugin is byte-for-byte transparent. |

## Presets

Ten, in the Preset dropdown. Choosing one overrides the sliders; moving any covered slider drops
back to Custom.

| Preset | What it is |
|---|---|
| **Slapback** | One head, fast tape, low feedback. A single tight repeat. |
| **Three Heads** | Position 11 — all three heads and the tank. The full machine. |
| **Long Throw** | Head 3 at a slow tape. Three-second echoes. |
| **Dub Siren** | High Intensity, hot input. On the edge. |
| **Runaway** | Past the edge. It builds. |
| **Chorus Tape** | Short delay and a badly serviced transport — the wobble is the point. |
| **Roll Tear** | Wide head spread and a slow tape, so the tears are large and obvious. |
| **Tape Ambience** | Position 11, long reverb, echoes sitting behind the picture. |
| **Sick Transport** | Everything wrong at once: worn heads, dropouts, a lurching capstan. |
| **Spring Only** | Position 12. The tank, and no tape at all. |

## Things worth knowing

**Turn Repeat Rate slowly, on purpose.** Almost every other delay plugin re-times its taps when you
change the delay. This one drags them, because the tape is a real object with material already on
it. Sweeping the knob is a performance move.

**Intensity means the same thing in every position.** The head bus is levelled before the feedback,
so position 11 does not run away at a setting that position 1 survives.

**A long delay is temporally coarser.** The tape holds a fixed number of passes, so reaching
further back means recording less often. That is also what a real tape does at low speed — less
signal per second — but the number here comes from a memory budget, and the README says so.

**Memory.** The loop is 96 passes of the record head. At 1080p that is about 384 MB of GPU memory,
and it is allocated whether or not the echoes are audible. If you need that memory back, take the
plugin off the layer rather than turning Echo Volume down.

**Cost.** About 0.93 ms a frame at 1080p and 1.42 ms at 4K on an Apple M4 Max. 4K is not much
dearer than 1080p because the tape itself is capped — which also means the echoes at 4K are softer
than the direct signal. That is deliberate, and it is what a tape echo does anyway.

## If something looks wrong

**The effect does nothing.** Check `~/Library/Logs/astronaught/astronaught.YYYY-MM-DD.log` (on
Windows, `%LOCALAPPDATA%\astronaught\logs`). A shader that will not compile is silent in Resolume
and loud in there.

**No echoes at all.** Position 12 is Reverb Only — the tape is running and nothing is listening to
it. Also check Echo Volume, and that Mix is not at 0.

**Everything goes white.** Intensity is above unity and the loop is building. That is the machine
working; bring Intensity or Input Level down.

**Long echoes band or posterise.** Hiss is at zero. It is inside the loop and it dithers the store;
put a little back.

## Licence and credits

MIT. Roland, RE-201, RE-20, RE-202 and Space Echo are trademarks of Roland Corporation, used only
to say what this models. Astronaught is not affiliated with, endorsed by, or derived from any
Roland product; it contains no Roland code, samples, impulse responses or measurements. The
published figures it is checked against are cited in
[ATTRIBUTIONS.md](https://github.com/stoatworks-labs/astronaught/blob/main/ATTRIBUTIONS.md).
