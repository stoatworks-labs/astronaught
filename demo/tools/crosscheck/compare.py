"""Compare uniforms.cpp's output (the real plugin) with frames.mjs's (the port).

    python3 compare.py cpp-frames.txt js-frames.txt

What has to be identical, and what may differ by how much:

  - Every integer: the stride, the newest slot, how many slots are written,
    the tape's raster, TapeNewest and TapeWritten. One of these differing means
    the port recorded on a different frame or into a different slot, and
    everything after it is a different tape.
  - Every parameter (GetFloatParameter, including Preset), and every knob of
    the event-consuming host after it has read back the plugin's events: bit
    for bit. These
    are the host's floats and the preset table's, and the preset logic decides
    which reaches params[]; there is no arithmetic in them to round.
  - Every float uniform and every double of the transport: within REL_TOL of the
    plugin's, relative to max(|value|, ABS_FLOOR). The port rounds to float where
    the C++ does, but it cannot use the same sinf, powf and expf, and the C++
    compiler may fuse a multiply into an add where JavaScript cannot; the tape
    position integrates those last-bit differences over thousands of frames.
    The report says how many values were bit-identical and what the worst was.

A uniform the port never set must be GL's initial zero in the plugin.
"""
import struct
import sys

REL_TOL = 1e-5
ABS_FLOOR = 1e-3


def f32(hexbits):
    return struct.unpack(">f", bytes.fromhex(hexbits))[0]


def f64(hexbits):
    return struct.unpack(">d", bytes.fromhex(hexbits))[0]


def load(path):
    data = {}
    with open(path) as handle:
        for line in handle:
            words = line.split()
            if not words:
                continue
            kind = words[0]
            if kind == "R":
                data.setdefault("R", []).append(words[1])
            elif kind == "C":
                data[("C", int(words[1]))] = dict(w.split("=", 1) for w in words[2:])
            elif kind in ("P", "H"):
                data[(kind, int(words[1]))] = words[2:]
            elif kind == "U":
                data[("U", int(words[1]), words[2])] = dict(w.split("=", 1) for w in words[3:])
    return data


class Tally:
    def __init__(self):
        self.values = 0
        self.identical = 0
        self.worst = (0.0, None)
        self.failures = []

    def float(self, where, a, b):
        self.values += 1
        if a == b:
            self.identical += 1
            return
        err = abs(a - b) / max(abs(a), ABS_FLOOR)
        if err > self.worst[0]:
            self.worst = (err, f"{where}: plugin {a!r}, port {b!r}")
        if err > REL_TOL:
            self.failures.append(f"{where}: plugin {a!r}, port {b!r} (relative {err:.2e})")

    def exact(self, where, a, b):
        self.values += 1
        if a == b:
            self.identical += 1
        else:
            self.failures.append(f"{where}: plugin {a}, port {b}")


def main(cpp_path, js_path):
    cpp, js = load(cpp_path), load(js_path)
    t = Tally()

    t.exact("rasters", cpp.get("R"), js.get("R"))
    frames = sorted(k[1] for k in cpp if isinstance(k, tuple) and k[0] == "C")
    if frames != sorted(k[1] for k in js if isinstance(k, tuple) and k[0] == "C"):
        print("FAIL  the two sides ran different frames")
        return 1

    for n in frames:
        a, b = cpp[("C", n)], js[("C", n)]
        for key in ("stride", "newest", "written", "tape"):
            t.exact(f"frame {n} {key}", a[key], b[key])
        for key in ("pos", "span", "origin"):
            t.float(f"frame {n} {key}", f64(a[key]), f64(b[key]))

        for kind, label in (("P", "param"), ("H", "host knob")):
            pa, pb = cpp[(kind, n)], js[(kind, n)]
            t.exact(f"frame {n} {label} count", len(pa), len(pb))
            for i, (x, y) in enumerate(zip(pa, pb)):
                t.exact(f"frame {n} {label} {i}", x, y)

        for program in ("playback", "record", "spring", "output"):
            ua, ub = cpp[("U", n, program)], js[("U", n, program)]
            for name, value in ua.items():
                kind, raw = value.split(":", 1)
                where = f"frame {n} {program}.{name}"
                if name not in ub:
                    zero = all(v in ("0", "00000000", "80000000") for v in raw.split(","))
                    t.exact(f"{where} (never set by the port)", "zero" if zero else raw, "zero")
                    continue
                kb, rb = ub[name].split(":", 1)
                if kb != kind:
                    t.exact(f"{where} type", kind, kb)
                    continue
                xs, ys = raw.split(","), rb.split(",")
                if len(xs) != len(ys):
                    t.exact(f"{where} length", len(xs), len(ys))
                    continue
                for i, (x, y) in enumerate(zip(xs, ys)):
                    if kind == "i":
                        t.exact(f"{where}[{i}]", x, y)
                    else:
                        t.float(f"{where}[{i}]", f32(x), f32(y))
            for name in ub:
                if name not in ua:
                    t.failures.append(f"frame {n} {program}.{name}: the port sets a uniform the plugin's program does not have")

    print(f"{len(frames)} frames, {t.values} values compared, {t.identical} bit-identical "
          f"({100.0 * t.identical / max(1, t.values):.3f}%)")
    if t.worst[1]:
        print(f"worst float: relative {t.worst[0]:.2e} at {t.worst[1]}")
    if t.failures:
        for line in t.failures[:20]:
            print(f"FAIL  {line}")
        if len(t.failures) > 20:
            print(f"... and {len(t.failures) - 20} more")
        return 1
    print(f"ok    every integer and parameter identical; every float within {REL_TOL:g} relative")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
