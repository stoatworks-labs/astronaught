# astronaught — notes

Repo-local facts. Cross-cutting ones live in
[fleet-notes](https://github.com/stoatworks-labs/fleet-notes).

---

## Where this came from (2026-08-27)

The brief was "new resolume plugin, astronaught, a video signal put through a
space echo delay", and the useful part of the first ten minutes was working out
what it should **not** be, because three plugins in this fleet already stand next
to it:

- **ferric** already treats the video signal as an analogue tape signal — wow,
  flutter, scrape, hiss, dropouts, companding.
- **afterglow** is a decaying queue of recent frames.
- **escapement** is a video feedback rig with a frame store.

A Space Echo's distinctive claim is none of those. It is **one continuously
moving tape loop with three fixed playback heads**, which locks the delays in a
fixed ratio, makes the Repeat Rate knob drag every echo already recorded, and
turns Intensity into a generation-loss loop. Answering "full standalone RE-201,
RE-201-faithful then extended" settled the rest.

## The research, and the document that would have been wrong

The mode table is the plugin's one genuinely factual asset, and the obvious
sources are wrong or absent:

- Wikipedia's *Roland Space Echo* article does not give the twelve positions at
  all.
- The **RE-202** reference manual *does* print a twelve-position table, it is
  easy to find, and it is **wrong for the RE-201** — that machine has a fourth
  playback head and its own manual says so ("Modes 8–12 feature the sound of
  playback head 4, which was not on the Roland Space Echo RE-201"). Five of the
  twelve positions differ.
- The right document is the **BOSS RE-20** owner's manual, whose selector
  "carries on the tradition of the RE-201" and whose *About the Variation Mode*
  (p.18) prints the table this plugin uses.

The RE-20 manual is worth more than the table. Pages 18–19 are Roland describing
the *mechanisms* — the pitch drag when Repeat Rate changes, written out in both
directions; the speed oscillation and the chorus it produces on each of the three
echoes; the three springs in a Z, each influencing the others. Those paragraphs
are the design document, and each is quoted in the source where it is
implemented.

⚠️ The UA copy of the RE-201 manual 403s to a fetch, and `WebFetch` on the
RE-202 PDF returns unparsed binary — it saves the file to disk and names the
path, which `pdftotext -layout` then handles. Worth remembering: a manual that
"cannot be read" often just needs the local file the fetch already wrote.

## Where a picture is not an audio signal, and what it cost

The single biggest finding of the session, and the only place the model
deliberately departs from the circuit.

**A video signal is unipolar.** Audio repeats are as often negative as positive
and partly cancel; a picture has no negative half. So anything that sums
accumulates, every time, with nothing to cancel it.

Modelled literally, the machine had **four usable selector positions out of
twelve** and `--presets` failed six of ten as flat white. Three separate fixes,
all the same cause:

1. The head bus is normalised by the number of heads up. Three heads over a part
   of the picture that has not moved are three copies of the same value.
2. The record amplifier's headroom is shared between input and regeneration.
3. The tank is a normalised one-pole, not an accumulator — an unnormalised tank
   settles at `drive/(1-feedback)`, which at a two-second decay and 60 fps is
   about ten times the input.

Only (1) is uncomfortable, because it looks exactly like the averaging that
rendered five of escapement's rigs black. The distinction is real and is written
out in `Shaders.h`: escapement's `/= tapCount` was **inside a feedback loop**,
where 1/N is a loop gain and it deleted the attractor; this one is on the bus
leaving the machine, and the loop gain is set separately and explicitly by
Intensity on the already-normalised bus.

## The bugs, and which check found each

Four, and the interesting part is that **each was found by a different check** —
no single one of them would have caught more than one.

**The knee was an amplifier.** `tanh(x*k)/tanh(k)` is the standard normalised
soft clip and maps 1 to 1, which is the property that looks worth having. Its
small-signal gain is `k/tanh(k)` — 2.8 at Saturation 0.35 — and inside a feedback
loop that whites the tape in three transits whatever Intensity is. `--presets`
failed and **`--stats` diagnosed it**: sweeping Intensity moved nothing and
sweeping Saturation moved the cliff, which is not how a feedback problem behaves
and is exactly how a gain in the wrong place does. It is a limiter now —
transparent below the knee, compressing above — which means heavy Saturation
folds the peaks down. That trade is the right way round; the other way is
unstable.

**`Dispersion` was completely dead.** Sized by eye as "a small blur per frame" at
0.020/s, which is 0.0003 of picture height at 60 fps — a third of a pixel — and
after a two-second tail still under half a pixel at 1080. **Only `tools/sweep.py`
found it**, because a dead control renders a perfectly good picture and every
other check passed. A blur that compounds accumulates in **quadrature**: size it
from `sqrt(n) * r`, not from `r`.

**The harness was feeding the plugin upside-down frames.** `glTexImage2D` treats
the first row of data as t = 0, which is the *bottom*, so a top-down test card
uploaded unflipped puts its first scanline where the last one belongs. The plugin
computes `scan = 1.0 - uv.y` from the assumption a real host gives it, so the
tear rolled the opposite way from the way it will in Resolume — meaning every
judgement made by looking at a rendered frame was about a picture no host would
produce. **`--identity` found it**, as 124800 differing bytes with the top-left
pixel holding the grating that belongs in the bottom third.

**The stride exploded to 31,910,486 on the first frame.** No time has passed, so
`perFrame` is degenerate and `strideFor` divides by nearly nothing; after that the
record head never passes again and the tape runs on with nothing written to it,
which presents as the effect switching itself off. **Found by reading `--out`'s
own status line**, which prints the stride. Clamped at `kSlots` now, and the
stride follows a smoothed span so one stalled frame cannot step the recording
density.

## The check that was measuring itself

`--chorus` asserts that the three heads decorrelate. Its first version reported
**6.45% decorrelation on a perfectly steady transport**, which should be exactly
zero, and the model was correct.

The lookup that answers "how old is the material under this head" snapped to the
nearest recorded frame. At 120 fps that is 8.3 ms of quantisation on a 500 ms
delay — 1.7% — and because the check compares head 3 against *three times* head 1
the errors add to about 6.6%. The measurement error and the effect being measured
were the same size.

Interpolating between samples took it to 0.0000% steady and 2.01% at the default
trim. Worth generalising: **when a measurement and the thing it measures are the
same order, the measurement is the bug.**

The threshold then had to be re-derived rather than tuned. 2% is not a number
that was found by lowering a constant until the test passed — the default trim is
0.35 of a 6% peak speed error, so about 2% relative wander between head 1 and
head 3 is what the transport asks for, and 1% is comfortably below it and three
orders of magnitude above the steady case.

## `pipefail` + `grep -q` reports a failure when the string IS there

☠️ `verify.sh` reported that the binary carried neither its `CFFGLPluginInfo`
string nor its build stamp. Both were plainly in it — `strings | grep` found them
by hand, immediately.

`grep -q` exits on the **first match**. The producer then dies of SIGPIPE, and
under `set -o pipefail` the pipeline's status is that signal. So the check fails
precisely when the string is present *and near the top of the output*.

What made it look like a linker problem rather than a shell one: the
`nm -gU | grep -q "_plugMain"` check on the line immediately above **passed** —
because nm's output is small enough to fit the pipe buffer before grep can exit.
Two adjacent checks, same idiom, opposite results, and the difference was the
size of the producer's output.

Capture into a variable first. This belongs in fleet-notes; every `verify.sh` in
the fleet uses this idiom.

## What the store costs, and how honest the justification is

96 passes of the record head, as one `GL_TEXTURE_2D_ARRAY`, scaled to fit a
384 MB budget. At 1080p that lands at 1365×768 — 0.711 of the composition —
and 384 MB exactly.

Two claims are made for this and only one of them is physics:

- **True:** the direct path never touches the store, so what loses resolution is
  the echoes, and an echo off a tape genuinely has less bandwidth than the signal
  that made it. Likewise, reaching further back than 96 passes means recording
  less often, and a tape running slowly does carry less signal per second.
- **Not physics:** the number 96, and the 384 MB. Those come from a byte budget.

The README says which is which rather than letting the first argument cover the
second.

`GL_RGBA8` rather than a float format is the same shape of argument and the
honest version is: tape has a noise floor around eight bits, the quantisation
*would* compound in a feedback loop, and it does not here because the **hiss
dithers it** — the hiss being inside the loop and on by default. Set Hiss to
zero, put Intensity near unity, and the banding is visible. That is correct
behaviour for a tape with no noise floor and it is also the honest limit of the
choice.

## First contact with a real host (2026-08-27)

Both platforms, Arena 7.27.1 rev 15990 — the same host build on each, so a
difference between them is the plugin.

The order that made it cheap, and is worth repeating:

1. **`oxbow selftest <bundle>` first.** It goes through the real `plugMain` and
   `FF_INSTANTIATE_GL`, which is the only thing that catches the About-block bug
   that has silently killed four fleet plugins in hosts while every in-repo
   harness passed. Ten seconds, and it passed.
2. **macOS Arena**, driven over REST.
3. **Windows**, because that is where the fleet has actually shipped a plugin
   that killed Arena at startup.

Nothing was wrong. That is worth recording as plainly as a bug would be: the
things that usually break at first contact — a shader that only compiles on one
vendor, a truncated parameter name, a host clock in the wrong unit, a preset that
snaps back to Custom — were all already handled, because they are all in
fleet-notes and were designed around rather than discovered.

**Three things only a real host could establish:**

- **The clock really is milliseconds.** Both hosts log `scale=0.001000`. The code
  has handled this from the first commit and the harness declares seconds, so the
  detection had never actually had to *decide* anything until now.
- **The factory preset holds.** Applied `Three Heads`, watched Arena report
  `Mode='11 All + Rev'` and `Intensity=0.450` — the preset table exactly — and
  hold through 10 s of live rendering. Then a hand on the Repeat Rate knob
  produced `preset dropped to Custom: parameter 1 moved to 0.310000`. That is the
  vertigo bug's fix working, and it had only been seen live once before, on
  compander.
- **Nothing is truncated**, including all twelve Mode Selector elements. Two
  parameter names are exactly 16 characters and both display complete.

**Windows is the half that pays.** llvmpipe is a completely different GLSL
compiler from Apple's, and all four programs compiled under it — `initialised`
only logs after `compileShaders()` succeeds. Arena survived with **0 crash
dumps**. The box has no GPU, so this says the plugin is correct and says nothing
whatever about speed.

### Two traps that cost time here, neither of them the plugin

**win-lab has no build tooling at all** — no cmake, no git, no `cl`, no vcpkg. The
Windows DLL has to come from CI. `release.yml` carries `workflow_dispatch`, so a
test binary costs one manual run and no tag; the artefact's version string reads
`0.0.0`, which is the tell that it is not a release build.

⚠️ **Checking a Windows log by line offset is a trap.** The first survival check
read `Get-Content $log | Select-Object -Skip 1883` from a count taken before the
launch, and reported **no registration line** — which fleet-notes says *is* the
crash signature. Arena was fine. The log had **shrunk** to 1880 lines between the
two reads, so the skip consumed everything. Find the last `Log started` and slice
from there; never trust a line count taken at another moment.

### A REST fact the fleet did not have

☠️ **A `ChoiceParameter` is written by its VALUE STRING, not by its index.**

    PUT /parameter/by-id/{id}   {"value": "Three Heads"}    -> 204
    PUT /parameter/by-id/{id}   {"index": 2}                -> 400

`{"index": N}` is what the GET returns alongside the value, so it is the obvious
thing to write back, and it is a 400 every time. Also: the write endpoint is
`/parameter/by-id/{id}` — there is no `/composition/params/by-id/{id}`, and
guessing that one returns 404.

## Not done

- **No NVIDIA or AMD driver has run it.** Two GLSL compilers, no discrete-GPU
  driver. Nothing has been used on a show.
- **The macOS artefacts are not signed or notarised**, and no tagged release
  exists yet.
- **No user guide, no video, no screenshots, no `projects.json` entry.** The last
  of those is why `StoatworksAbout.h` is a hand-written placeholder whose four
  buttons point at pages that do not exist.
- **No browser demo, no plugin-bench expectation.**
- **No OpenFX target**, and structurally not "not yet": an OFX host wants an
  arbitrary frame in arbitrary order, and what is on a tape at frame 900 depends
  on frames 1-899 including everything Intensity fed back. Afterglow escapes this
  because its queue is a pure function of time; feedback ends that trick.
- **No trace overlay.** ferric has `Show Trace`; a plot of where the three heads
  are currently sitting on the tape, and how much material is behind them, would
  be the single most useful diagnostic this plugin could have — and would have
  made three of the four early bugs visible at a glance.
- **Bass and Treble act on the wet bus only.** That is a reading of the hardware
  — they sit in the echo path on the RE-201 — and not a measurement of one. It is
  also the more useful behaviour, since a tone control on the direct signal is a
  colour correction and Resolume has several. Flagged because it is the one panel
  decision here taken on judgement rather than on a cited figure.
- **The tank's three legs share one field**, so their coupling is total where a
  real Z tank's is partial. What it costs is that the tank cannot ring on a mode
  of one spring alone.
