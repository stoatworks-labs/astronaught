// The frames the cross-check drives both sides through, printed once so the
// C++ and the JavaScript read exactly the same thing:
//
//     size W H              the host's raster (a change is a resolution change)
//     set ID BITS           SetFloatParameter(ID, the float with these bits)
//     host ID BITS          the event-consuming host's knob ID moves
//     push                  that host sends every knob, reading back events
//     frame BITS            SetTime(the double with these bits), ProcessOpenGL
//
//     node demo/tools/crosscheck/scenario.mjs > scenario.txt
//
// Chosen to reach every branch of the CPU half that decides something: every
// Mode position (with and without the tank, none up at 12), Repeat Rate swept
// while echoes are on the tape (the drag), Head Spread both ways, the medium and
// the transport at their extremes, a preset applied and held through a host that
// restates its old values and then echoes the preset's, and dropped by a genuine
// edit, three frame rates, a stalled frame and a clock jump backwards, two
// resolution changes (one past Store.cpp's budget), and a run at the fastest
// tape long enough for the slot table to rebase several times.

import { kHostDefaults, PARAM_KEYS, PT_PRESET, kPresets } from '../../port.js';

const view = new DataView(new ArrayBuffer(8));
const f32 = (v) => { view.setFloat32(0, v); return view.getUint32(0).toString(16).padStart(8, '0'); };
const f64 = (v) => {
  view.setFloat64(0, v);
  return view.getUint32(0).toString(16).padStart(8, '0') + view.getUint32(4).toString(16).padStart(8, '0');
};

const out = [];
const id = (key) => (key === 'preset' ? PT_PRESET : PARAM_KEYS.indexOf(key));
const host = { ...kHostDefaults, preset: 0 };
let t = 0;

const set = (key, value) => {
  host[key] = value;
  out.push(`set ${id(key)} ${f32(value)}`);
};
const frames = (n, fps) => {
  for (let i = 0; i < n; i += 1) {
    out.push(`frame ${f64(t)}`);
    t += 1 / fps;
  }
};
/** Resolume's habit: every control restated every frame, whatever it last sent. */
const restating = (n, fps, values = host) => {
  for (let i = 0; i < n; i += 1) {
    for (const key of [...PARAM_KEYS, 'preset']) out.push(`set ${id(key)} ${f32(values[key])}`);
    out.push(`frame ${f64(t)}`);
    t += 1 / fps;
  }
};

/**
 * A host that restates its controls every frame but sends Preset only when it
 * changes. `restating`, which restates Preset too, re-applies the preset over
 * anything that dropped it, and would hide a broken restatement rule.
 */
const controlsOnly = (n, fps, values) => {
  for (let i = 0; i < n; i += 1) {
    for (const key of PARAM_KEYS) out.push(`set ${id(key)} ${f32(values[key])}`);
    out.push(`frame ${f64(t)}`);
    t += 1 / fps;
  }
};

out.push('size 320 180');

// A fresh instance whose first instruction is a preset, followed by the host
// restating defaults it has never sent: they must read as restatements, which
// only holds if seedHostSent() ran before the preset (Astronaught.h).
set('preset', 3); // Long Throw
controlsOnly(20, 60, kHostDefaults);
set('preset', 0);
for (const key of PARAM_KEYS) set(key, kHostDefaults[key]);

frames(240, 60);

// The drag: speed changes with echoes on the tape.
set('repeatRate', 0.95);
frames(180, 60);
set('repeatRate', 0.15);
frames(240, 60);
set('repeatRate', 0.5);

// Every position of the selector.
for (let m = 0; m < 12; m += 1) {
  set('mode', m);
  frames(30, 60);
}
set('mode', 3);

// Head Spread both ways, then back to the hardware's spacing.
set('headSpread', 0.9);
frames(60, 60);
set('headSpread', 0.2);
frames(60, 60);
set('headSpread', 0.602);

// The tank, hard.
set('mode', 10);
set('reverbTime', 0.9);
set('dispersion', 1.0);
frames(60, 60);

// The medium and the transport at their extremes.
for (const [key, value] of [['inputLevel', 0.8], ['saturation', 0.9], ['headWear', 0.8], ['hiss', 0.7], ['dropouts', 0.5]]) set(key, value);
frames(60, 60);
for (const key of ['wowFlutter', 'wow', 'flutter', 'scrape']) set(key, 1.0);
frames(120, 60);
for (const [key, value] of [['echoVolume', 0.3], ['reverbVolume', 0.9], ['directVolume', 0.5], ['bass', 0.1], ['treble', 0.9], ['mix', 0.6]]) set(key, value);
frames(30, 60);

// A preset, through a host that restates everything every frame.
restating(10, 60);
const before = { ...host };
set('preset', 4); // Dub Siren
restating(30, 60, { ...before, preset: 4 }); // the host's old values: must hold
const dub = Object.fromEntries(PARAM_KEYS.map((key, i) => [key, i < 20 ? kPresets[3].values[i] : host[key]]));
restating(30, 60, { ...dub, preset: 4 }); // the preset echoed back: must hold
restating(30, 60, { ...dub, intensity: 0.3, preset: 4 }); // a genuine edit: drops to Custom
Object.assign(host, dub, { intensity: 0.3, preset: 0 });
set('preset', 0);
restating(10, 60);

// The same host, saying Preset only when it changes.
const stale = { ...host };
set('preset', 6); // Chorus Tape over the host's stale values: must hold
controlsOnly(30, 60, stale);
controlsOnly(20, 60, { ...stale, scrape: stale.scrape + 0.3 }); // a genuine edit: Custom
Object.assign(host, kHostDefaults, { preset: 0 });
for (const key of PARAM_KEYS) set(key, kHostDefaults[key]);
set('preset', 0);
frames(10, 60);

// The same, through a host that consumes the plugin's value events -- the
// page's panel. `host` is an operator moving a knob; `push` restates every
// knob and reads back whatever the plugin raised an event on.
const knob = (key, value) => out.push(`host ${id(key)} ${f32(value)}`);
const pushing = (n, fps) => {
  for (let i = 0; i < n; i += 1) {
    out.push('push');
    out.push(`frame ${f64(t)}`);
    t += 1 / fps;
  }
};
pushing(10, 60);
knob('preset', 7); // Roll Tear: the knobs move to it, and it holds
pushing(30, 60);
knob('repeatRate', 0.2); // a genuine edit of a covered control: Custom, edit kept
pushing(30, 60);
knob('mix', 0.4); // not covered by any preset
knob('preset', 2); // Three Heads
pushing(20, 60);
knob('intensity', kPresets[1].values[2] + 0.0005); // inside the 1e-3 allowance: holds
pushing(20, 60);
knob('intensity', kPresets[1].values[2] + 0.01); // outside it: Custom
pushing(20, 60);
knob('preset', 9); // Sick Transport, then straight back to Custom by hand
pushing(10, 60);
knob('preset', 0);
pushing(10, 60);

// Other frame rates.
frames(120, 30);
frames(200, 144);

// A stalled frame (clamped to a tenth of a second), then the clock jumping back.
t += 0.5;
frames(30, 60);
t = 0;
frames(60, 60);

// Resolution changes: the only thing allowed to erase the tape. 1920x1080 is
// past Store.cpp's budget, so the tape is scaled down.
out.push('size 640 360');
frames(120, 60);
out.push('size 1920 1080');
frames(30, 60);
out.push('size 320 180');

// The fastest tape at ten frames a second: ~1.4 units a frame, past the
// rebase threshold of 256 several times.
set('repeatRate', 1.0);
set('mode', 2);
frames(700, 10);

process.stdout.write(`${out.join('\n')}\n`);
