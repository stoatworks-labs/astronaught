# astronaught

A video signal put through a Space Echo — a model of the Roland RE-201, as an
FFGL effect for Resolume Arena/Avenue. C++/GLSL, CMake MODULE → universal
`.bundle` (macOS) + Windows `.dll`. Public MIT repo.

Read `AGENTS.md` before changing the tape, the heads or the loop.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build`
- Install to Resolume: `cmake --install build`
- Render a frame offline: `./build/astest --out /tmp/f.png --frames 150`
- A factory preset: `./build/astest --out /tmp/f.png --preset 2`
- Measure a setting without judging it: `./build/astest --stats --set "Intensity=0.8"`
- List parameters, with types and any at the 16-character limit: `./build/astest --list`
- Set anything by its host-facing name: `--set "Repeat Rate=0.35" --set "Mode=10"`

## Verify
- **Everything (21 checks, clean universal build): `tools/verify.sh`**
- The whole machine: `./build/astest --all`
- The Mode Selector against Roland's table: `./build/astest --modes`
- The heads are at 1 : 2 : 3: `./build/astest --ratios`
- Repeat Rate's range, and which way it runs: `./build/astest --delay`
- The transport is frame-rate independent: `./build/astest --rate`
- **Changing Repeat Rate drags the echoes: `./build/astest --drag`**
- The three heads wobble independently: `./build/astest --chorus`
- The GLSL tape read against `Tape.cpp`: `./build/astest --read`
- Mix at zero is transparent: `./build/astest --identity`
- Every preset renders structure: `./build/astest --presets`
- A hostile machine leaves no NaN: `./build/astest --guard`
- No dead controls: `python3 tools/sweep.py`

## Notes
- ☠️ **The tape is indexed by POSITION, never by time.** Every recognisable
  Space Echo behaviour — the drag, the chorus, the tear — is a consequence of
  that one decision. A version that stores a frame with a timestamp and
  subtracts a delay renders something plausible, passes a smoke test, and has
  silently deleted all three. `--drag` and `--chorus` are what notice.
- **The three delays are ONE number.** The heads are bolted at equal intervals,
  so 1 : 2 : 3 is geometry. `Head Spread` is declared an extension for exactly
  that reason.
- **The Mode Selector's table is transcribed, not derived.** There is no
  echo-only 1+2, 1+3 or 1+2+3 position. `--modes` holds a second transcription.
- ☠️ **The head bus IS normalised, and that is not escapement's averaging bug.**
  A video signal is unipolar, so three heads over a static area sum to 3× with
  nothing to cancel them. The loop gain is set separately by Intensity, on the
  already-normalised bus. `Shaders.h` states the distinction; read it before
  reaching for the "fix".
- ☠️ **The record amplifier's knee must have gain ≤ 1 everywhere.** The
  normalised soft clip `tanh(x*k)/tanh(k)` has a small-signal gain of `k/tanh(k)`
  — 2.8 at Saturation 0.35 — which is an amplifier inside a feedback loop. It is
  a limiter now: transparent below the knee, compressing above.
- **The tank is fed from the INPUT, in parallel with the tape.** Feeding it from
  the head bus puts every repeat through the springs again.
- **Playback runs BEFORE record.** That is the feedback loop. Reversing them
  closes it within one frame and the delay stops being a delay.
- **A parameter change must NOT erase the tape** — the opposite of the fleet's
  GPU habit. Only a resolution change may, and `Store::ConsumeResized` is the
  only thing that does.
- **Everything filters along the scan and only along the scan.** Tape has one
  frequency axis. A symmetrical blur makes this a lens effect.
- **Sweeping needs a fresh instance per render and ~90 frames.** The tape is
  state, and two frames is a quarter of the shortest delay — every Echo control
  reads dead.
- ☠️ **`set -o pipefail` plus `grep -q` reports a failure when the string IS
  present**: grep exits early, the producer dies of SIGPIPE. `verify.sh`
  captures into variables first.
- FFGL truncates every parameter name at 16 characters. `astest --names`.
- `SetParamInfo` clamps a STANDARD default into 0..1 before `SetParamRange` can
  widen it, so every numeric parameter is 0..1 and mapped in `Controls.cpp`.
- Override `SetTextParameter` to return FF_SUCCESS for the About block, or no
  host can instantiate the plugin at all.
- `astronaught_core` is an OBJECT library, not STATIC — the plugin registers
  itself from a file-scope constructor nothing references by name.
- macOS build must be universal. Verify with `lipo`, never the build log.
- `sample`, `input`, `output`, `filter`, `common`, `active`, `layout` are GLSL
  reserved words. Shader errors surface only at runtime, in the diag log.
- Public repo. "Commit" = commit **and** push.

## Not done yet

- ☠️ **Never loaded into Resolume, or any host.** No packaged build, nothing
  installed, no show. The README's Status section says so plainly and should be
  replaced rather than added to after the first real session.
- **CI and release workflows exist and have NEVER RUN.** Both are adapted from
  ferric's, which are proven there; the adaptation is not. `ci.yml` builds arm64
  and runs the no-GPU half of the machine (`--modes --ratios --delay --rate
  --drag --chorus --names`). `release.yml` is entirely untested here.
- **No Windows build, no browser demo, no plugin-bench expectation.**
- **`StoatworksAbout.h` is a hand-written placeholder.** astronaught has no
  entry in the website's `projects.json`, so `sync-about.py` cannot generate it
  and the four About buttons point at pages that do not exist.
- **No OpenFX target, and not "not yet".** A tape loop cannot answer an OFX
  host's request for an arbitrary frame in arbitrary order. The reasoning is in
  `CMakeLists.txt`.
- **No trace overlay.** ferric's `Show Trace` has no equivalent here, and a plot
  of where the heads are sitting on the tape would be the single most useful
  diagnostic this plugin could have.

## Diagnostics

`source/Diag.{h,cpp}` — log file only, no crash handler (this runs inside
Resolume), no bundle command.

    ~/Library/Logs/astronaught/astronaught.YYYY-MM-DD.log
