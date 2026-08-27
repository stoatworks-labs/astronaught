# Attributions

## Third-party code

**Resolume FFGL SDK** — `external/ffgl`, a git submodule pinned to `b1afaf9`.
Copyright Resolume, MIT. https://github.com/resolume/ffgl

**zlib** — linked from the OS for the harness's PNG writer. Not vendored.

Nothing else is vendored. `astronaught` itself is MIT; see `LICENSE`.

## The machine this models

Astronaught is an independent model of the Roland RE-201 Space Echo, written from
**published documentation only**. It contains no Roland code, no samples, no
impulse responses, and no measurements taken from a unit.

Roland, RE-201, RE-20, RE-202 and Space Echo are trademarks of Roland
Corporation. Astronaught is not affiliated with, endorsed by, or derived from any
Roland product.

### The published figures it is checked against

Two documents, both Roland's own, and the distinction between them matters:

**BOSS RE-20 Space Echo, Owner's Manual** (Roland Corporation, 2007). Its Mode
Selector "carries on the tradition of the RE-201", and *About the Variation Mode*
(p.18) prints the twelve-position table this plugin transcribes — which heads and
whether the reverb is in, for every position. *About the Playback Heads* (p.18)
gives the 1 : 2 : 3 delay ratio, and *About the Configuration of the Tape
Echo/Reverb* (p.19) states that the three playback heads are "arranged
sequentially at equal intervals", which is where that ratio comes from.

The same pages carry Roland's own descriptions of three mechanisms this plugin
models rather than approximates, and they are quoted in the source where each is
implemented:

- *Changing of the Echo Sound's Pitch When REPEAT RATE is Changed* (p.19) — both
  halves of the drag, which is `Transport.h`'s reason for existing.
- *Oscillation in the Tape Speed* (p.19) — the speed error and the chorus it
  produces on each of the three echoes.
- *About the Reverb* (p.19) — three springs in a "Z" formation, each influencing
  the others.

⚠️ **BOSS RE-202 Space Echo, Reference Manual** (Roland Corporation) is the wrong
document for the mode table and is cited here so that nobody reaches for it. The
RE-202 has a **fourth** playback head, its manual says so explicitly ("Modes 8–12
feature the sound of playback head 4, which was not on the Roland Space Echo
RE-201"), and its table differs from the RE-201's in five of the twelve
positions. It is useful for the delay-time ranges and for confirming the equal
spacing; it is not useful for the selector.

`astest --modes` holds a second, independent transcription of the RE-201 table,
so that changing it has to be done twice, deliberately, by somebody who has
looked at a source.

## Sibling projects in this fleet

**ferric** models the video signal as an analogue tape signal — wow, flutter,
scrape, hiss, dropouts, and consumer noise reduction. Astronaught has its own
transport because it needs the position integral rather than a per-pixel error
field, but ferric got there first on the central idea that **tape has one
frequency axis and it lands along the scan**, which is why nothing here filters
vertically.

**afterglow** is a decaying queue of recent frames, and **escapement** is a video
feedback rig. Both are adjacent and neither is this: a queue indexes by frame
count and a feedback rig has no heads. Escapement also supplied the hard-won
warning about averaging inside a loop, which `Shaders.h` addresses directly
because the head bus here *is* normalised and the distinction needed stating.
