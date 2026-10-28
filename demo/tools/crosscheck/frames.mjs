// The JavaScript half of the cross-check: demo/port.js -- the module the page
// itself imports -- driven through scenario.mjs's frames, printing what
// uniforms.cpp prints from the real plugin: the CPU state after each frame,
// every parameter, and every uniform each of the four programs holds. A program
// keeps a uniform's value until a pass sets it again, so a pass that does not
// run this frame (Record between passes of the record head, Spring in Mode
// positions 1 to 4) prints what it was last given. A uniform the port has never
// set prints nothing here; compare.py requires the plugin's to be GL's initial
// zero.
//
// `host` and `push` are the event-consuming host uniforms.cpp describes, and the
// same host the page's panel is (plugin.js, pushHostValues): every control sent
// in id order, and any parameter the plugin raised a value event on read back
// into the host's knobs at once.
//
//     node demo/tools/crosscheck/frames.mjs < scenario.txt > js-frames.txt

import { readFileSync } from 'node:fs';
import { AstronaughtFrames, PT_PRESET } from '../../port.js';

const view = new DataView(new ArrayBuffer(8));
const h32 = (v) => (v >>> 0).toString(16).padStart(8, '0');
const d = (v) => { view.setFloat64(0, v); return h32(view.getUint32(0)) + h32(view.getUint32(4)); };
const f = (v) => { view.setFloat32(0, v); return h32(view.getUint32(0)); };
const fromBits32 = (hex) => { view.setUint32(0, parseInt(hex, 16)); return view.getFloat32(0); };
const fromBits64 = (hex) => {
  view.setUint32(0, parseInt(hex.slice(0, 8), 16));
  view.setUint32(4, parseInt(hex.slice(8), 16));
  return view.getFloat64(0);
};

const INTS = new Set(['TapeNewest', 'TapeWritten']);
const encode = (name, value) => {
  const list = Array.isArray(value) || ArrayBuffer.isView(value) ? [...value] : [value];
  return INTS.has(name) ? `i:${list.map((v) => String(v | 0)).join(',')}` : `f:${list.map(f).join(',')}`;
};

const st = new AstronaughtFrames();
const belief = Float32Array.from(st.params.subarray(0, PT_PRESET + 1));
const held = { playback: new Map(), record: new Map(), spring: new Map(), output: new Map() };
let width = 0;
let height = 0;
let frame = 0;
const out = [];

for (const line of readFileSync(0, 'utf8').split('\n')) {
  const words = line.trim().split(/\s+/);
  const verb = words[0];
  if (!verb) continue;
  if (verb === 'size') {
    width = Number(words[1]);
    height = Number(words[2]);
    out.push(`R ${width}x${height}`);
  } else if (verb === 'set') {
    st.SetFloatParameter(Number(words[1]), fromBits32(words[2]));
  } else if (verb === 'host') {
    const id = Number(words[1]);
    if (id <= PT_PRESET) belief[id] = fromBits32(words[2]);
  } else if (verb === 'push') {
    for (let id = 0; id <= PT_PRESET; id += 1) {
      for (const r of st.SetFloatParameter(id, belief[id])) belief[r] = st.params[r];
    }
  } else if (verb === 'frame') {
    const plan = st.frame(fromBits64(words[1]), width, height);
    out.push(`C ${frame} pos=${d(st.position)} span=${d(st.span)} stride=${st.writeStride} newest=${st.tape.Newest()} written=${st.tape.Written()} origin=${d(st.tape.Origin())} tape=${plan.tapeW}x${plan.tapeH}`);
    const params = [];
    for (let id = 0; id <= PT_PRESET; id += 1) params.push(f(st.params[id]));
    out.push(`P ${frame} ${params.join(' ')}`);
    out.push(`H ${frame} ${[...belief].map(f).join(' ')}`);
    for (const name of ['playback', 'record', 'spring', 'output']) {
      const uniforms = plan[name];
      if (uniforms) for (const [k, v] of Object.entries(uniforms)) held[name].set(k, encode(k, v));
      const fields = [...held[name]].sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0)).map(([k, v]) => ` ${k}=${v}`);
      out.push(`U ${frame} ${name}${fields.join('')}`);
    }
    frame += 1;
  } else {
    process.stderr.write(`frames.mjs: unknown '${verb}'\n`);
    process.exit(2);
  }
}

process.stdout.write(`${out.join('\n')}\n`);
