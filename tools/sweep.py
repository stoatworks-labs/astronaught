"""Every parameter must actually change the picture.

A GLSL uniform name that does not match the C++ is silently ignored:
glGetUniformLocation returns -1, glUniform on -1 is a documented no-op, and
nothing in the build says a word. A control can therefore be completely dead
while everything compiles, links, loads and renders. Nothing else in this repo
catches that.

So: render each parameter at both ends of its range against a baseline where
every stage is switched on, and report any that made no difference.

    python3 tools/sweep.py

Run it after adding a parameter, renaming a uniform, or moving anything between
the C++ and the GLSL. Exit code 1 means something is dead.

--------------------------------------------------- sweeping a plugin with a tape

Two things this sweep needs that most of the fleet's do not, both for the same
reason: **the tape is state**.

  * ☠️ **A fresh instance per render.** Every render is its own subprocess, so
    the tape starts empty each time. Two settings compared inside one instance
    differ because of what was on the tape when the second one started, not
    because of the settings, and every row below would be measuring the order
    the rows ran in.

  * ☠️ **A pinned frame count, and a LONG one.** Ninety frames at sixty, so a
    second and a half of tape has gone by. Two frames -- which is what the rest
    of the fleet sweeps with -- is a quarter of the shortest delay this machine
    has: no echo has reached any head yet, every render is the direct signal,
    and the sweep reports the entire Echo group dead.

-------------------------------------------------------------------- the traps

Several controls are SUPPOSED to do nothing in the default configuration, and a
sweep that ignores that reports them dead and is right to.

  * **The reverb needs a selector position that has the tank in.** Reverb Time,
    Dispersion and Reverb Volume are all inert in positions 1-4, and not by
    omission: `controls::render` forces the reverb level to zero when the mode
    has no tank, because on the hardware there is no reverb switch -- there are
    eight positions of the selector that have the springs in circuit. They are
    swept in position 11.

  * **Wow, Flutter and Scrape are inert with the Wow & Flutter trim at zero.**
    They are components of one error signal and the trim is its master.

  * **Saturation needs something above the knee to work on.** It is exactly
    transparent below it -- that is the whole point of the curve, see
    Medium.cpp -- so a baseline with the input low reports it dead. Swept with
    Input Level up.

  * **Head Wear, Hiss and Dropouts are on the TAPE**, so they need the echo
    audible at the output. With Echo Volume at zero the tape is still running
    and nothing is listening to it.

  * **Bass and Treble act on the wet bus only.** They do nothing to the direct
    signal, deliberately -- see Passes.cpp -- so they need an echo to shape.

If a control ever reads dead, work out what is masking it before assuming the
test is wrong.
"""
import argparse
import os
import struct
import subprocess
import sys
import zlib

SIZE = "256x144"

# Long enough for every head to have arrived. See the module docstring.
FRAMES = 90

SCRATCH = os.environ.get("TMPDIR", "/tmp")

# A baseline with every stage switched on, so that a control is not reported
# dead because something upstream of it is.
BASE = {
    "Mode": 3,              # position 4: heads 2+3, no reverb
    "Repeat Rate": 0.55,
    "Intensity": 0.50,
    "Head Spread": 0.602,   # the hardware's own spacing
    "Input Level": 0.55,
    "Saturation": 0.35,
    "Head Wear": 0.30,
    "Hiss": 0.25,
    "Dropouts": 0.20,
    "Wow & Flutter": 0.45,
    "Wow": 0.45,
    "Flutter": 0.40,
    "Scrape": 0.25,
    "Reverb Time": 0.45,
    "Dispersion": 0.50,
    "Echo Volume": 0.75,
    "Reverb Volume": 0.50,
    "Direct Volume": 0.80,
    "Bass": 0.5,
    "Treble": 0.5,
    "Mix": 1.0,
    # Last, so that sweeping it overrides everything above rather than being
    # overridden. Custom, so it changes nothing while other controls are swept.
    "Preset": 0,
}

# Parameters that only do anything in one configuration, and the baseline change
# that switches it on.
CONTEXT = {
    # Position 11: all three heads and the tank. There is no reverb switch on
    # this machine and there is not one here either.
    "Reverb Time": {"Mode": 10, "Reverb Volume": 0.85},
    "Dispersion": {"Mode": 10, "Reverb Volume": 0.85},
    "Reverb Volume": {"Mode": 10},
    # The three transport components are inert with their master at zero.
    "Wow": {"Wow & Flutter": 0.85},
    "Flutter": {"Wow & Flutter": 0.85},
    "Scrape": {"Wow & Flutter": 0.85},
    # The knee is exactly transparent below itself, so there has to be
    # something above it.
    "Saturation": {"Input Level": 0.85},
    # These are on the tape, so the tape has to be reaching the output.
    "Head Wear": {"Echo Volume": 1.0, "Direct Volume": 0.2},
    "Hiss": {"Echo Volume": 1.0, "Direct Volume": 0.2},
    "Dropouts": {"Echo Volume": 1.0, "Direct Volume": 0.2},
    # The tone controls shape the wet bus and nothing else.
    "Bass": {"Echo Volume": 1.0, "Direct Volume": 0.2},
    "Treble": {"Echo Volume": 1.0, "Direct Volume": 0.2},
}

# Endpoints to sweep between, where 0 and 1 are the wrong pair.
ENDS = {
    # Position 1 (head 1 alone) against position 11 (all three and the tank).
    "Mode": (0, 10),
    "Preset": (0, 10),
    # ⚠️ NOT 0 to 1. At 0 head 3 is three seconds back and has not reached the
    # head in ninety frames, so the slow end renders no third echo at all and
    # the row measures the frame count rather than the control.
    "Repeat Rate": (0.35, 0.85),
    "Head Spread": (0.30, 0.90),
    # At 1 the loop is past unity and both ends saturate towards the same
    # picture, which reads as a smaller change than the control really makes.
    "Intensity": (0.15, 0.80),
}

# Not controls: the About block is a text line and four buttons that open a
# browser.
SKIP_TYPES = {"text", "event", "buffer"}
KNOWN_TYPES = {"standard", "boolean", "option", "text", "event", "buffer"}


def render(binary, path, overrides):
    args = [binary, "--out", path, "--size", SIZE, "--frames", str(FRAMES)]
    merged = dict(BASE)
    merged.update(overrides)
    for key, value in merged.items():
        args += ["--set", f"{key}={value}"]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        print("render failed:", result.stdout, result.stderr)
        sys.exit(1)
    with open(path, "rb") as handle:
        return handle.read()


def pixels(png):
    i = 8
    idat = b""
    width = height = 0
    while i < len(png):
        length = struct.unpack(">I", png[i:i + 4])[0]
        kind = png[i + 4:i + 8]
        data = png[i + 8:i + 8 + length]
        if kind == b"IHDR":
            width, height = struct.unpack(">II", data[:8])
        if kind == b"IDAT":
            idat += data
        i += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    return b"".join(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)] for y in range(height))


def difference(a, b):
    pa, pb = pixels(a), pixels(b)
    changed = 0
    total = 0
    count = len(pa) // 4
    for i in range(0, len(pa), 4):
        d = max(abs(pa[i] - pb[i]), abs(pa[i + 1] - pb[i + 1]), abs(pa[i + 2] - pb[i + 2]))
        if d > 2:
            changed += 1
        total += d
    return changed / count * 100.0, total / count


def parameters(binary):
    """Names and types, read out of the plugin itself.

    `--list` prints `id  name  type  default  notes`, and the NAME contains
    spaces. So the type is found by looking for the one field that is a known
    type word, and everything between the id and it is the name -- rather than
    counting fields, which breaks the first time a name gains or loses a word.
    """
    listing = subprocess.run([binary, "--list"], capture_output=True, text=True)
    if listing.returncode != 0:
        print("could not list parameters:", listing.stderr)
        sys.exit(1)

    out = []
    for line in listing.stdout.strip().splitlines():
        fields = line.split()
        if len(fields) < 3 or not fields[0].isdigit():
            continue

        kind_at = next((i for i, f in enumerate(fields) if f in KNOWN_TYPES), None)
        if kind_at is None:
            continue

        kind = fields[kind_at]
        name = " ".join(fields[1:kind_at])
        if kind in SKIP_TYPES or not name:
            continue
        out.append(name)
    return out


def main():
    parser = argparse.ArgumentParser()
    # verify.sh builds into its own directory with the shipping architectures
    # and runs this against THAT binary -- hardcoding a path would silently
    # sweep a different build from the one being verified.
    parser.add_argument("--binary", default=os.environ.get("ASTEST", "./build/astest"))
    args = parser.parse_args()

    if not os.path.exists(args.binary):
        print(f"{args.binary} not found -- build first")
        return 1

    names = parameters(args.binary)
    print(f"{'parameter':<16} {'pixels changed':>15} {'mean delta':>11}   verdict")

    dead = []
    for name in names:
        low, high = ENDS.get(name, (0.0, 1.0))
        context = CONTEXT.get(name, {})

        a = render(args.binary, os.path.join(SCRATCH, "sweep_a.png"), {**context, name: low})
        b = render(args.binary, os.path.join(SCRATCH, "sweep_b.png"), {**context, name: high})

        percent, mean = difference(a, b)
        # A tenth of a per cent of the frame is a real change; anything below is
        # dithering and rounding between two renders of the same picture.
        alive = percent > 0.1
        if not alive:
            dead.append(name)

        note = "  (" + ", ".join(f"{k}={v}" for k, v in context.items()) + ")" if context else ""
        print(f"{name:<16} {percent:>14.2f}% {mean:>11.3f}   {'ok' if alive else 'DEAD'}{note}")

    print()
    if dead:
        print("DEAD CONTROLS:", ", ".join(dead))
        print("Check the uniform name matches the GLSL, and that nothing in BASE masks it.")
        return 1

    print(f"all {len(names)} controls reach the picture")
    return 0


if __name__ == "__main__":
    sys.exit(main())
