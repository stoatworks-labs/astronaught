# Working in astronaught

A model of the Roland RE-201 Space Echo, as an FFGL effect for Resolume. This
file is the *why*; `CLAUDE.md` is the short command reference. Read this before
changing the tape, the heads or the loop.

---

## The one idea

**The tape is indexed by position, never by time.**

Everything else here is bookkeeping around that. A frame is laid on the tape
wherever the tape had reached when it was recorded; a playback head is a fixed
distance down the path; the delay is the time the tape takes to cover that
distance, and is stored nowhere.

Three behaviours follow with no code of their own, and all three disappear
silently if anyone replaces the position integral with a timestamp and a
subtraction:

| behaviour | why it falls out | the check |
|---|---|---|
| Changing Repeat Rate **drags** the echoes already recorded | the material does not move; only the rate at which heads reach it does | `--drag` |
| The three echoes wobble **independently** | each head reads a different point, so each sees the speed error from a different moment | `--chorus` |
| The picture comes back **torn and stretched** | a frame occupies a *span* of tape, and a head almost never lands on a boundary | `--read`, and looking at a frame |

A timestamped version renders something plausible. It passes a smoke test. It is
not a tape echo. If you are about to make the tape simpler, read
`Transport.h` first, and run `--drag`.

## Load-bearing invariants

- **The three delays are ONE number.** The heads are bolted at equal intervals,
  so 1 : 2 : 3 is geometry rather than tuning. `Head Spread` is declared an
  extension in the README precisely because it breaks something the hardware
  could only break with a screwdriver.
- **The Mode Selector is transcribed, not derived.** `astest --modes` holds a
  second, independent transcription so that editing it takes two deliberate
  changes in two files. Its negative assertions matter as much as its positive
  ones: there is no echo-only 1+2, 1+3 or 1+2+3.
- **Playback runs before Record.** That ordering *is* the feedback loop.
- **The tank is fed from the input, in parallel with the tape.** Roland is
  explicit about this and it is one line to get wrong.
- **A parameter change must not erase the tape.** The opposite of the fleet's
  usual GPU habit — the tape is the instrument's memory, and clearing it on a
  slider move makes every control a reset button. Only `Store::ConsumeResized`
  may erase, and only on a resolution change.
- **Everything filters along the scan and only along the scan.** Tape has one
  frequency axis. ferric argues this at more length and is right.

## Where a picture is unipolar and a Space Echo's signal is not

This is the only place the model deliberately departs from the circuit, it does
so in three places, and all three have the same cause. Audio repeats are as often
negative as positive and partly cancel. A picture has no negative half, so
anything that sums accumulates, every time, with nothing to cancel it.

Modelled literally, the plugin had **four usable selector positions out of
twelve** and `--presets` failed six of ten.

1. **The head bus is normalised** by the number of heads up. `Shaders.h` states
   at length why this is not escapement's averaging bug — the short version is
   that escapement's `/= tapCount` was *inside a feedback loop*, where 1/N is a
   loop gain, and this one is on the bus leaving the machine while the loop gain
   is set separately and explicitly by Intensity.
2. **The record amplifier's headroom is shared** between input and regeneration.
3. **The tank is a normalised one-pole**, so a driven tank settles at the input
   level rather than at ten times it.

Any future change that "restores fidelity" by removing one of these should run
`--presets` first and look at what it renders.

## Traps that have already bitten, here

- ☠️ **A soft clip is not a limiter.** `tanh(x*k)/tanh(k)` maps 1 to 1, which
  looks like the property you want, and has a small-signal gain of `k/tanh(k)` —
  2.8 at Saturation 0.35. Inside a feedback loop that is an amplifier, and the
  loop was white in three transits at any Intensity. The knee is transparent
  below itself now and compresses above, which means heavy Saturation folds the
  peaks down. That trade is the right way round; the other way is unstable.
- ☠️ **A blur that compounds accumulates in QUADRATURE.** `Dispersion` was sized
  by eye at "a small blur per frame" and was 0.0003 of picture height — a third
  of a pixel, and after a two-second tail still under half a pixel at 1080. It
  was completely dead and **only `tools/sweep.py` noticed**, because a dead
  control renders a perfectly good picture. Size a compounding effect from
  `sqrt(n) * r`, not from `r`.
- ☠️ **`set -o pipefail` plus `grep -q` fails when the match is EARLY.** grep
  exits on the first hit, the producer dies of SIGPIPE, and the pipeline's status
  is that signal. `strings | grep -q` reported two strings missing that were
  plainly in the binary, while `nm | grep -q` on the line above passed — because
  nm's output fits the pipe buffer. That difference is what made it look like a
  linker problem. `verify.sh` captures into variables first.
- **The harness must upload the test card flipped.** `glTexImage2D` treats the
  first row of data as t = 0, which is the *bottom*. Uploading a top-down card
  unflipped hands the plugin an upside-down frame, `scan` runs backwards, and the
  tear rolls the opposite way from the way it will in Resolume — so every
  judgement made by looking at a rendered frame is about a picture no host will
  produce. `--identity` caught it.
- **A stride longer than the ring is never the right answer.** On the first
  frame no time has passed, `perFrame` is degenerate, and `strideFor` returned
  31,910,486 — after which the record head never passes again and the tape runs
  on with nothing written to it. Clamped at `kSlots`, and the stride follows a
  smoothed span so one stalled frame cannot step the recording density.
- **The playback head sweeps from the START of the frame.** `position` has
  already been advanced by the time the pass runs, and handing that over puts
  every head one frame of tape further on than it is.

## Fleet traps that apply here

These are documented in
[fleet-notes](https://github.com/stoatworks-labs/fleet-notes) and are repeated
only where this repo depends on them.

- **FFGL truncates parameter names at 16 characters** and the SDK does not
  enforce it, so every offline harness passes and only the host is wrong. Six
  fleet plugins shipped `Background Opaci`. `astest --names` is the gate, and it
  reports names that are *exactly* 16 rather than passing them silently — the
  host cannot tell "fits exactly" from "truncated" and neither can the test.
- **A factory preset is an OVERRIDE, not a write.** Resolume does not consume
  value events; it goes on pushing the values it still believes in, so a naive
  "a covered control changed, so drop to Custom" rule fires on the host's own
  echo. `Presets.h` has the full story. `seedHostSent()` must run *before* any
  preset can be applied.
- **Resolume matches a saved composition's parameters BY NAME.** Reordering ids
  is safe; renaming a released control silently loses everybody's saved value,
  and since only non-default values are written there is nothing left in the file
  to notice it by. Option *elements* are stored as numbers — append, never
  insert.
- **`SetParamInfo` clamps a STANDARD default into 0..1** before `SetParamRange`
  can widen it, so every numeric control is 0..1 and mapped in `Controls.cpp`.
- **A TEXT parameter with no `SetTextParameter` override makes the whole plugin
  un-instantiable** in any real host, while every class-level harness passes.
- **`astronaught_core` is an OBJECT library.** In a STATIC archive the linker
  may drop the translation unit whose file-scope constructor registers the
  plugin, giving a bundle that loads, exports `plugMain`, and reports that it
  contains no plugins.
- **Verify universality with `lipo`, never the build log.**

## Verified vs assumed

**Verified**, by `tools/verify.sh` against a clean universal build:

- the mode table, the head ratios and the delay range, against Roland's figures
- the drag and the chorus, as measurements rather than assertions
- the GLSL tape read against `Tape.cpp`, to 1.6e-6 on every row
- transparency at Mix 0, byte for byte
- all ten presets render structure; four hostile machines leave no NaN
- all 22 controls reach the picture
- the bundle is universal, exports `plugMain`, and its plist names the binary
  that is on disk

**Verified in Resolume Arena 7.27.1 rev 15990**, on macOS (Apple M4 Max, GL 4.1
Metal) and on Windows (win-lab, Mesa llvmpipe 26.2.0, GL 4.5, no GPU):

- registers as `Astronaught` uid `AN01` category 1 on both
- all four shader programs compile under **two different GLSL compilers**
- 27 parameters, none truncated, all 12 Mode elements complete
- the host clock is milliseconds, and the detection says so on both
- a factory preset applies, holds through 10 s of live rendering, and drops to
  Custom only on a genuine operator edit
- renders correctly on real footage on both
- Arena survives; 0 crash dumps on Windows

**Assumed, and not small:**

- **No NVIDIA or AMD driver has ever run it.** Apple's compiler and Mesa's are
  two data points and neither is a discrete GPU driver.
- **Nothing has been used on a show.**
- **No performance figure on any GPU but one.** 0.93 ms at 1080p on an M4 Max;
  llvmpipe is a CPU rasteriser and its numbers mean nothing here.
- **The macOS artefacts are not yet signed or notarised.**
- Windows was tested with a `workflow_dispatch` build whose version string reads
  `0.0.0`, not with a tagged release artefact.

The 4K figure deserves one caution: the store is capped at 1365×768 by its byte
budget, so 4K is cheap *because the echoes are at a quarter of the composition's
linear resolution*. That is defensible — see Store.h — but it is not the same
statement as "it is fast at 4K".
