/**
 * Astronaught's CPU half, ported to JavaScript. No DOM and no GL in this file,
 * so `demo/tools/crosscheck/` can run it under node against the plugin's C++.
 *
 * Everything here is a hand translation, and only a reader and that crosscheck
 * hold it to the plugin:
 *
 *   heads       source/Heads.{h,cpp}      the Mode Selector, transcribed
 *   controls    source/Controls.{h,cpp}   slider position -> physical quantity
 *   transport   source/Transport.{h,cpp}  the capstan, and the position integral
 *   tape        source/Tape.{h,cpp}       the ring of slots, as bookkeeping
 *   spring      source/Spring.{h,cpp}     the tank's per-frame coefficients
 *   presets     source/Presets.h          the ten factory rows, and the override
 *   store       source/Store.cpp          how big the tape is allowed to be
 *   frames      source/Astronaught.cpp    the CPU half of ProcessOpenGL and of
 *                                         SetFloatParameter
 *
 * The C++ works in 32-bit float almost everywhere and in double for the tape
 * position. `f()` (Math.fround) is applied where the C++ rounds to float, so
 * the port carries the same values rather than more precise ones -- the hash
 * behind wow and flutter takes the fractional part of numbers in the
 * thousands, where float and double genuinely disagree. What it cannot
 * reproduce exactly is the platform's libm (sinf, powf, expf) or a compiler
 * that fuses a multiply into an add; the crosscheck measures how far that
 * moves anything.
 *
 * ☠️ The tape is indexed by POSITION, never by time. `AstronaughtFrames` winds
 * a position integral exactly as the plugin does and the slot table records
 * where on it each pass of the record head landed. Do not "simplify" it into a
 * frame queue: the drag, the chorus and the tear all live in that one choice,
 * and a queue renders something plausible without any of them.
 */

const f = Math.fround;
const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

/** std::lround: half away from zero, which Math.round is not for negatives. */
export function lround(x) {
  return x < 0 ? -Math.floor(-x + 0.5) : Math.floor(x + 0.5);
}

//===========================================================================
// heads -- source/Heads.{h,cpp}
//===========================================================================

export const kHeadCount = 3;
export const kModeCount = 12;

/** Head k's delay as a multiple of head 1's. Geometry, not tuning. */
export const kSpacing = [f(1.0), f(2.0), f(3.0)];

/**
 * The Mode Selector, transcribed from the printed table (BOSS RE-20 owner's
 * manual, About the Variation Mode), as Heads.cpp has it. Read down the
 * columns: there is no echo-only 1+2, 1+3 or 1+2+3.
 */
export const kModes = [
  //  label               h1     h2     h3     reverb
  ['1  Head 1',      [true, false, false], false],
  ['2  Head 2',      [false, true, false], false],
  ['3  Head 3',      [false, false, true], false],
  ['4  Heads 2+3',   [false, true, true], false],
  ['5  H1 + Rev',    [true, false, false], true],
  ['6  H2 + Rev',    [false, true, false], true],
  ['7  H3 + Rev',    [false, false, true], true],
  ['8  H1+2 + Rev',  [true, true, false], true],
  ['9  H2+3 + Rev',  [false, true, true], true],
  ['10 H1+3 + Rev',  [true, false, true], true],
  ['11 All + Rev',   [true, true, true], true],
  ['12 Reverb Only', [false, false, false], true],
].map(([label, head, reverb]) => ({ label, head, reverb }));

export function mode(index) {
  return kModes[clamp(index, 0, kModeCount - 1)];
}

export function headCount(index) {
  return mode(index).head.reduce((n, up) => n + (up ? 1 : 0), 0);
}

//===========================================================================
// controls -- source/Controls.{h,cpp}
//===========================================================================

export const kDelaySlow = f(1.000);
export const kDelayFast = f(0.070);
export const kSpreadLo = f(0.25);
export const kSpreadHi = f(2.50);
export const kIntensityMax = f(1.15);
export const kDriveLo = f(0.25);
export const kDriveHi = f(2.50);
export const kToneDb = f(12.0);
export const kHissMax = f(0.10);
export const kDropoutMax = f(0.06);

const kWowRateLo = f(0.15);
const kWowRateHi = f(3.0);
const kFlutterRateLo = f(3.0);
const kFlutterRateHi = f(34.0);
const kFlutterDepth = f(0.06);

/** The constructor's defaults, controls::HostValues, in host 0..1 space. */
export const kHostDefaults = {
  mode: f(3.0),
  repeatRate: f(0.52),
  intensity: f(0.55),
  headSpread: f(0.602),
  inputLevel: f(0.5),
  saturation: f(0.35),
  headWear: f(0.25),
  hiss: f(0.18),
  dropouts: f(0.0),
  wowFlutter: f(0.35),
  wow: f(0.45),
  flutter: f(0.40),
  scrape: f(0.20),
  reverbTime: f(0.45),
  dispersion: f(0.5),
  echoVolume: f(0.7),
  reverbVolume: f(0.5),
  directVolume: f(1.0),
  bass: f(0.5),
  treble: f(0.5),
  mix: f(1.0),
};

function dbToGain(db) {
  return f(Math.pow(10, f(db / f(20.0))));
}

export function expRange(t, lo, hi) {
  const u = clamp(t, 0, 1);
  return f(lo * f(Math.pow(f(hi / lo), u)));
}

export function option(value, elementCount) {
  if (elementCount <= 0) return 0;
  return clamp(lround(value), 0, elementCount - 1);
}

/** controls::render. Everything the frame needs, in physical units. */
export function render(host) {
  const r = {};

  r.mode = option(host.mode, kModeCount);
  r.selector = mode(r.mode);
  r.spread = expRange(clamp(host.headSpread, 0, 1), kSpreadLo, kSpreadHi);

  // A tape speed, converted as a delay range and inverted: turning Repeat Rate
  // up makes the delay SHORTER, which is the hardware's direction.
  r.baseDelay = expRange(clamp(host.repeatRate, 0, 1), kDelaySlow, kDelayFast);

  const t = {};
  t.speed = f(kSpacing[0] / Math.max(f(1e-3), r.baseDelay));
  const trim = clamp(host.wowFlutter, 0, 1);
  t.amount = trim;
  t.depth = kFlutterDepth;
  t.wow = clamp(host.wow, 0, 1);
  t.flutter = clamp(host.flutter, 0, 1);
  t.scrape = clamp(host.scrape, 0, 1);
  t.wowRate = expRange(f(0.5), kWowRateLo, kWowRateHi);
  t.flutterRate = expRange(f(0.5), kFlutterRateLo, kFlutterRateHi);
  r.transport = t;

  r.reach = reachOf(r.selector, r.spread);

  const m = {};
  m.drive = expRange(clamp(host.inputLevel, 0, 1), kDriveLo, kDriveHi);
  m.saturation = clamp(host.saturation, 0, 1);
  m.headWear = clamp(host.headWear, 0, 1);
  const hiss = clamp(host.hiss, 0, 1);
  m.hiss = f(f(kHissMax * hiss) * hiss);
  const drop = clamp(host.dropouts, 0, 1);
  m.dropouts = f(f(kDropoutMax * drop) * drop);
  m.intensity = f(clamp(host.intensity, 0, 1) * kIntensityMax);
  r.tape = m;

  r.tank = {
    active: r.selector.reverb,
    decay: clamp(host.reverbTime, 0, 1),
    dispersion: clamp(host.dispersion, 0, 1),
    level: clamp(host.reverbVolume, 0, 1),
  };

  r.echoVolume = clamp(host.echoVolume, 0, 1);
  r.reverbVolume = r.tank.active ? clamp(host.reverbVolume, 0, 1) : 0;
  r.directVolume = clamp(host.directVolume, 0, 1);

  r.bass = dbToGain(f(f(f(clamp(host.bass, 0, 1) - f(0.5)) * f(2.0)) * kToneDb));
  r.treble = dbToGain(f(f(f(clamp(host.treble, 0, 1) - f(0.5)) * f(2.0)) * kToneDb));

  r.mix = clamp(host.mix, 0, 1);
  return r;
}

//===========================================================================
// transport -- source/Transport.{h,cpp}
//===========================================================================

const kSubsteps = 8;
const kWowWeight = f(1.00);
const kFlutterWeight = f(0.55);
const kScrapeWeight = f(0.22);
const kScrapeRate = f(47.0);
const kTwoPi = f(6.28318531);

function hash11(p) {
  p = f(Math.abs(p) * f(0.1031));
  p = f(p - Math.floor(p));
  p = f(p * f(p + f(33.33)));
  p = f(p * f(p + p));
  return f(p - Math.floor(p));
}

function noise(x) {
  const i = Math.floor(x);
  const fr = f(x - i);
  const u = f(f(fr * fr) * f(f(3.0) - f(f(2.0) * fr)));
  return f(f(hash11(i) * f(1 - u)) + f(hash11(f(i + 1)) * u));
}

function wobble(x) {
  return f(
    f(f(noise(x) - f(0.5)) * f(1.3333))
    + f(f(noise(f(f(x * f(2.17)) + f(11.3))) - f(0.5)) * f(0.6667)),
  );
}

/** The fractional speed error at one instant. Deterministic in `seconds`. */
export function speedError(s, seconds) {
  const t = f(seconds);
  const trim = Math.max(0, s.amount);
  const depth = Math.max(0, s.depth);

  const wowPhase = f(t * s.wowRate);
  const wow = f(f(f(
    f(f(Math.sin(f(wowPhase * kTwoPi))) * f(0.75))
    + f(wobble(f(wowPhase * f(0.31))) * f(0.5)),
  ) * s.wow) * kWowWeight);

  const flutPhase = f(t * s.flutterRate);
  const flutter = f(f(f(
    f(f(Math.sin(f(f(flutPhase * kTwoPi) + f(1.7)))) * f(0.5))
    + wobble(f(f(flutPhase * f(0.73)) + f(5.1))),
  ) * s.flutter) * kFlutterWeight);

  const scrape = f(f(wobble(f(t * kScrapeRate)) * s.scrape) * kScrapeWeight);

  return f(f(f(f(wow + flutter) + scrape) * depth) * trim);
}

export function speedAt(s, seconds) {
  const nominal = Math.max(f(1e-3), s.speed);
  const v = f(nominal * f(1 + speedError(s, seconds)));
  return Math.max(f(nominal * f(0.05)), v);
}

/** Advance the position integral across one frame, in double, by substeps. */
export function advance(s, position, seconds, dt) {
  if (!(dt > 0)) return position;
  const step = dt / kSubsteps;
  let p = position;
  for (let i = 0; i < kSubsteps; i += 1) p += speedAt(s, seconds + step * i) * step;
  return p;
}

/** Peak-to-peak speed error over two seconds, as a percentage. For the readout. */
export function flutterPercent(s) {
  const kSamples = 4096;
  const kSpan = 2.0;
  let lo = 0;
  let hi = 0;
  for (let i = 0; i < kSamples; i += 1) {
    const e = speedError(s, (kSpan * i) / kSamples);
    lo = Math.min(lo, e);
    hi = Math.max(hi, e);
  }
  return f(f(hi - lo) * 100);
}

//===========================================================================
// tape -- source/Tape.{h,cpp}
//===========================================================================

/** Passes of the record head the ring holds. A byte budget, not a Roland figure. */
export const kSlots = 96;

const kRebaseAt = 256.0;

export class Loop {
  constructor() {
    this.starts = new Float32Array(kSlots);
    this.spans = new Float32Array(kSlots);
    this.head = 0;
    this.origin = 0;
    this.newest = -1;
    this.written = 0;
  }

  Erase() {
    this.starts.fill(0);
    this.spans.fill(0);
    this.head = 0;
    this.origin = 0;
    this.newest = -1;
    this.written = 0;
  }

  Rebase() {
    // `head` stays absolute. See Tape.cpp for the bug zeroing it caused.
    const shift = this.head - this.origin;
    for (let i = 0; i < kSlots; i += 1) {
      if (this.spans[i] > 0) this.starts[i] = this.starts[i] - shift;
    }
    this.origin = this.head;
  }

  Record(position) {
    const previous = this.head;
    this.head = Math.max(position, previous);

    const slot = (this.newest + 1) % kSlots;
    const span = this.newest < 0 ? Math.max(1e-6, this.head - previous) : this.head - previous;

    this.starts[slot] = previous - this.origin;
    this.spans[slot] = Math.max(1e-6, span);

    this.newest = slot;
    this.written = Math.min(this.written + 1, kSlots);

    if (this.head - this.origin > kRebaseAt) this.Rebase();
    return slot;
  }

  Origin() { return this.origin; }
  Newest() { return this.newest; }
  Written() { return this.written; }
}

export function strideFor(needed, perFrame) {
  if (!(perFrame > 0) || !(needed > 0)) return 1;
  const framesNeeded = needed / perFrame;
  const perSlot = framesNeeded / Math.max(1, kSlots - 2);
  const wanted = Math.ceil(perSlot);
  return clamp(Number.isFinite(wanted) ? wanted : kSlots, 1, kSlots);
}

export function reachOf(m, spread) {
  let reach = 0;
  for (let i = 0; i < kHeadCount; i += 1) {
    if (m.head[i]) reach = Math.max(reach, f(kSpacing[i] * spread));
  }
  return reach;
}

//===========================================================================
// spring -- source/Spring.{h,cpp}
//===========================================================================

export const kLegs = 3;
export const kLegLength = [f(1.0), f(0.7573), f(0.5761)];
export const kCoupling = f(0.28);
export const kLegOffsetX = [f(-0.0075), f(0.0000), f(0.0075)];
export const kLegOffsetY = [f(0.0042), f(-0.0060), f(0.0031)];
export const kDecayLo = f(0.35);
export const kDecayHi = f(6.0);
const kSpreadPerSecond = f(0.35);
const kDrive = f(0.55);

export function decaySeconds(decay) {
  return f(kDecayLo * f(Math.pow(f(kDecayHi / kDecayLo), clamp(decay, 0, 1))));
}

/** spring::coeffs, for a frame of `dt` seconds (a float, as the plugin passes it). */
export function coeffs(s, dt) {
  const c = { feedback: 0, spread: 0, drive: 0, legGain: [0, 0, 0] };
  if (!s.active || !(dt > 0)) return c;

  const seconds = decaySeconds(s.decay);
  c.feedback = f(Math.pow(f(0.001), f(dt / Math.max(f(1e-3), seconds))));
  c.spread = f(f(kSpreadPerSecond * clamp(s.dispersion, 0, 1)) * dt);
  c.drive = kDrive;

  let total = 0;
  for (let i = 0; i < kLegs; i += 1) {
    c.legGain[i] = f(kLegLength[i] + f(kCoupling * f(1 - kLegLength[i])));
    total = f(total + c.legGain[i]);
  }
  for (let i = 0; i < kLegs; i += 1) c.legGain[i] = f(c.legGain[i] / Math.max(f(1e-6), total));
  return c;
}

//===========================================================================
// presets -- source/Presets.h
//===========================================================================

/** The ten factory rows, in presets::Param column order (Mode is an element index). */
export const kPresets = [
  //                     mode  rate  int  sprd  in   sat  wear hiss drop  w&f  wow  flut scrp rvbT disp echo rvbV dirV bass treb
  ['Slapback',       [0, .78, .18, .602, .50, .30, .20, .12, .00, .22, .40, .40, .15, .40, .50, .65, .00, 1.0, .50, .50]],
  ['Three Heads',    [10, .50, .45, .602, .50, .35, .25, .18, .00, .35, .45, .40, .20, .45, .50, .70, .40, 1.0, .50, .50]],
  ['Long Throw',     [2, .10, .55, .602, .45, .30, .35, .22, .05, .30, .50, .35, .18, .55, .55, .75, .00, 1.0, .55, .42]],
  ['Dub Siren',      [3, .62, .82, .602, .62, .55, .30, .25, .00, .40, .45, .45, .25, .45, .50, .80, .00, .85, .60, .55]],
  ['Runaway',        [3, .70, .95, .602, .55, .70, .40, .30, .08, .45, .50, .50, .30, .50, .50, .85, .00, .70, .55, .60]],
  ['Chorus Tape',    [0, .88, .35, .450, .50, .28, .22, .15, .00, .70, .60, .75, .40, .40, .50, .60, .00, 1.0, .50, .52]],
  ['Roll Tear',      [3, .30, .60, .850, .55, .40, .30, .20, .12, .55, .70, .45, .25, .45, .50, .78, .00, .90, .50, .48]],
  ['Tape Ambience',  [10, .45, .40, .602, .45, .30, .45, .20, .00, .30, .45, .35, .15, .70, .70, .55, .70, 1.0, .58, .40]],
  ['Sick Transport', [3, .40, .58, .680, .58, .60, .70, .45, .55, .85, .75, .70, .55, .45, .50, .78, .00, .80, .55, .35]],
  ['Spring Only',    [11, .50, .00, .602, .50, .25, .20, .10, .00, .25, .45, .40, .15, .60, .65, .00, .85, 1.0, .50, .50]],
].map(([name, values]) => ({ name, values: values.map(f) }));

export const kEchoTolerance = f(1e-3);

export function presetLabel(element) {
  return element <= 0 || element > kPresets.length ? 'Custom' : kPresets[element - 1].name;
}

//===========================================================================
// The parameter ids -- Astronaught::ParamID, in the constructor's order.
//===========================================================================

/** host-values key, in ParamID order. PT_PRESET follows; the About block is not here. */
export const PARAM_KEYS = [
  'mode', 'repeatRate', 'intensity', 'headSpread',
  'inputLevel', 'saturation', 'headWear', 'hiss', 'dropouts',
  'wowFlutter', 'wow', 'flutter', 'scrape',
  'reverbTime', 'dispersion',
  'echoVolume', 'reverbVolume', 'directVolume', 'bass', 'treble', 'mix',
];
export const PT_PRESET = PARAM_KEYS.length; // 21
export const PT_COUNT = PT_PRESET + 1; // without the About block

/** The ParamID each presets::Param drives: every real control but Mix. */
export const kPresetParamIDs = PARAM_KEYS.slice(0, 20).map((_, i) => i);

//===========================================================================
// store -- the sizing half of source/Store.cpp
//===========================================================================

export const kByteBudget = 384 * 1024 * 1024;
export const kMinScale = 0.25;

/** The tape's raster for a composition: scaled down to fit the budget, never up. */
export function storeSize(compWidth, compHeight, slots = kSlots, budget = kByteBudget) {
  compWidth = Math.max(1, compWidth);
  compHeight = Math.max(1, compHeight);
  slots = Math.max(2, slots);
  const perPixel = slots * 4.0;
  const area = compWidth * compHeight;
  let scale = 1.0;
  if (perPixel * area > budget) scale = Math.sqrt(budget / (perPixel * area));
  scale = clamp(scale, kMinScale, 1.0);
  const w = Math.max(16, lround(compWidth * scale));
  const h = Math.max(16, lround(compHeight * scale));
  return { width: w, height: h, bytes: perPixel * w * h };
}

//===========================================================================
// frames -- the CPU half of source/Astronaught.cpp
//===========================================================================

const kMaxFrameDelta = 0.1;
const kGrainWrap = 65536.0;
const kDropBlocks = f(20.0);
const kHissWidthNew = f(1.5);
const kHissWidthOld = f(5.0);

function wrapGrain(v) {
  return v - kGrainWrap * Math.floor(v / kGrainWrap);
}

/**
 * One Astronaught instance, without the GL. `frame()` is ProcessOpenGL's CPU
 * half: it winds the tape, decides the stride, records, and returns what each
 * pass is handed -- the GL side in plugin.js only binds and draws.
 *
 * The clock is DECLARED seconds (clockScale 1), as astest declares it. The
 * plugin's vote on whether a host sends seconds or milliseconds is not ported:
 * this page's clock is seconds by construction.
 */
export class AstronaughtFrames {
  constructor() {
    this.params = new Float32Array(PT_COUNT);
    PARAM_KEYS.forEach((key, id) => { this.params[id] = kHostDefaults[key]; });
    this.params[PT_PRESET] = 0; // Custom: the sliders are the truth
    this.hostSent = new Float32Array(PT_COUNT);
    this.hostSeeded = false;

    this.tape = new Loop();
    this.position = 0;
    this.span = 0;
    this.spanAverage = 0;
    this.framesSinceWrite = 0;
    this.writeStride = 1;
    this.grainPhase = 0;
    this.lastHostTime = -1;

    // Store::Ensure's bookkeeping. `resized` is ConsumeResized().
    this.compW = 0;
    this.compH = 0;
    this.tapeW = 0;
    this.tapeH = 0;
    this.resized = false;
    this.budget = kByteBudget;

    this.renderOut = null;
    this.dt = 0;
    /** Which way the last SetFloatParameter on a covered control went. */
    this.events = [];
  }

  hostValues() {
    const out = {};
    PARAM_KEYS.forEach((key, id) => { out[key] = this.params[id]; });
    return out;
  }

  seedHostSent() {
    if (this.hostSeeded) return;
    this.hostSent.set(this.params);
    this.hostSeeded = true;
  }

  static isPresetParam(index) {
    return kPresetParamIDs.includes(index);
  }

  /**
   * Astronaught::SetFloatParameter. Returns the ids whose value event the
   * plugin raised (RaiseParamEvent): a preset's writes, or PT_PRESET when a
   * genuine edit drops it to Custom.
   */
  SetFloatParameter(index, value) {
    const raised = [];
    if (index < 0 || index >= PT_COUNT) return raised;
    value = f(value);

    this.seedHostSent();

    if (index === PT_PRESET) {
      this.hostSent[index] = value;
      const chosen = lround(value);
      if (chosen !== lround(this.params[PT_PRESET])) raised.push(...this.applyPreset(chosen));
      return raised;
    }

    const lastFromHost = this.hostSent[index];
    this.hostSent[index] = value;

    const active = lround(this.params[PT_PRESET]);
    if (active > 0 && AstronaughtFrames.isPresetParam(index)) {
      // Judged on what the value IS, never on the fact that it changed:
      // Resolume goes on pushing the values it still believes in.
      const restatement = Math.abs(value - lastFromHost) <= kEchoTolerance;
      const echoOfPreset = Math.abs(value - this.params[index]) <= kEchoTolerance;
      if (restatement || echoOfPreset) return raised;

      this.params[index] = value;
      this.params[PT_PRESET] = 0;
      raised.push(PT_PRESET);
      return raised;
    }

    this.params[index] = value;
    return raised;
  }

  applyPreset(presetIndex) {
    const raised = [];
    this.params[PT_PRESET] = presetIndex;
    if (presetIndex <= 0 || presetIndex > kPresets.length) return raised;
    const values = kPresets[presetIndex - 1].values;
    for (let j = 0; j < kPresetParamIDs.length; j += 1) {
      const id = kPresetParamIDs[j];
      if (Math.abs(this.params[id] - values[j]) <= f(1e-6)) continue;
      this.params[id] = values[j];
      raised.push(id);
    }
    return raised;
  }

  /** Store::Ensure, for the sizing and the resize flag. The pixels are plugin.js's. */
  ensureStore(frameW, frameH) {
    if (this.tapeW > 0 && this.compW === frameW && this.compH === frameH) return;
    const size = storeSize(frameW, frameH, kSlots, this.budget);
    this.compW = frameW;
    this.compH = frameH;
    this.tapeW = size.width;
    this.tapeH = size.height;
    this.resized = true;
  }

  /**
   * One ProcessOpenGL, CPU side. `hostTime` is seconds, `frameW`/`frameH` the
   * host's viewport. Returns the uniforms for each pass in the order they run,
   * and `record` / `spring` as null on the frames those passes do not run.
   */
  frame(hostTime, frameW, frameH) {
    frameW = Math.max(1, frameW);
    frameH = Math.max(1, frameH);

    const now = hostTime;
    const dt = this.lastHostTime >= 0 ? clamp(now - this.lastHostTime, 0, kMaxFrameDelta) : 0;
    this.dt = dt;

    const r = render(this.hostValues());
    this.renderOut = r;

    // Wind the tape on. Every frame, whether or not the record head passes.
    const previousPosition = this.position;
    this.position = advance(r.transport, this.position, this.lastHostTime >= 0 ? this.lastHostTime : now, dt);
    this.span = Math.max(1e-9, this.position - previousPosition);
    this.lastHostTime = now;

    // The grain advances with the TAPE, not with the clock.
    this.grainPhase = wrapGrain(this.grainPhase + this.span * 40.0);

    this.ensureStore(frameW, frameH);
    const tapeW = this.tapeW;
    const tapeH = this.tapeH;

    // ☠️ The ONLY thing allowed to erase the loop.
    let erased = false;
    if (this.resized) {
      this.resized = false;
      this.tape.Erase();
      this.position = 0;
      this.span = Math.max(1e-9, this.span);
      this.framesSinceWrite = 0;
      erased = true;
    }

    {
      const reach = r.reach > 0 ? r.reach : f(kSpacing[kHeadCount - 1] * r.spread);
      this.spanAverage = this.spanAverage > 0 ? this.spanAverage * 0.9 + this.span * 0.1 : this.span;
      this.writeStride = strideFor(reach, this.spanAverage);
    }

    const tapeWf = f(tapeW);
    const tapeHf = f(tapeH);

    // 1. Playback: setTapeUniforms, against the tape AS IT STANDS -- before
    // this frame's Record, which may rebase the origin.
    const playback = this.tapeUniforms(r);

    // 2. Record, on the frames the record head passes.
    let record = null;
    let slot = -1;
    this.framesSinceWrite += 1;
    if (this.framesSinceWrite >= this.writeStride) {
      this.framesSinceWrite = 0;
      slot = this.tape.Record(this.position);
      record = {
        MaxUV: [1, 1],
        InputMaxUV: [1, 1],
        Intensity: r.tape.intensity,
        TapeDrive: r.tape.drive,
        TapeSat: r.tape.saturation,
        TapeWear: r.tape.headWear,
        TapeHiss: r.tape.hiss,
        TapeHissW: f(kHissWidthNew + f(f(kHissWidthOld - kHissWidthNew) * r.tape.headWear)),
        TapeDrop: r.tape.dropouts,
        TapeDropBlk: kDropBlocks,
        TapeGrain: f(this.grainPhase),
        TapeTexelX: f(1 / tapeWf),
        TapeLines: tapeHf,
      };
    }

    // 3. The tank. Fed from the INPUT, in parallel with the tape.
    const tank = coeffs(r.tank, f(dt));
    let spring = null;
    if (r.tank.active) {
      const legOffsets = [];
      for (let i = 0; i < kLegs; i += 1) legOffsets.push(kLegOffsetX[i], kLegOffsetY[i]);
      spring = {
        MaxUV: [1, 1],
        SourceMaxUV: [1, 1],
        TankFeedback: tank.feedback,
        TankSpread: tank.spread,
        TankDrive: tank.drive,
        TankAspectWH: f(tapeWf / tapeHf),
        LegGain: tank.legGain.slice(),
        LegOffset: legOffsets,
      };
    }

    // 4. The output stage.
    const output = this.outputUniforms(r, frameW);

    return { tapeW, tapeH, erased, slot, playback, record, spring, output, tank };
  }

  /** Astronaught::setTapeUniforms, plus MaxUV. Snapshots the slot table. */
  tapeUniforms(r) {
    const offset = [];
    const up = [];
    for (let i = 0; i < kHeadCount; i += 1) {
      offset.push(f(kSpacing[i] * r.spread));
      up.push(r.selector.head[i] ? 1 : 0);
    }
    const upHeads = headCount(r.mode);
    return {
      MaxUV: [1, 1],
      SlotStart: Float32Array.from(this.tape.starts),
      SlotSpan: Float32Array.from(this.tape.spans),
      TapeNewest: Math.max(0, this.tape.Newest()),
      TapeWritten: this.tape.Written(),
      HeadOffset: offset,
      HeadUp: up,
      HeadNorm: upHeads > 0 ? f(1 / upHeads) : 1,
      // The START of this frame's sweep, not the end.
      HeadPos: f(this.position - this.span - this.tape.Origin()),
      HeadSpan: f(this.span),
    };
  }

  outputUniforms(r, frameW) {
    return {
      MaxUV: [1, 1],
      InputMaxUV: [1, 1],
      EchoVolume: r.echoVolume,
      ReverbVolume: r.reverbVolume,
      DirectVolume: r.directVolume,
      BassGain: r.bass,
      TrebleGain: r.treble,
      MixAmount: r.mix,
      OutTexelX: f(1 / f(frameW)),
    };
  }
}
