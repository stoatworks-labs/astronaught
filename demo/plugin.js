/**
 * Astronaught — browser demo.
 *
 * The four passes are the plugin's own GLSL: `shaders.js` is generated from
 * `source/shaders/*.cpp` by `demo/tools/check_shaders.py --write`, and the same
 * script, run by `tools/verify.sh`, fails if a character of it drifts or if the
 * way the pieces are joined below stops being the way Passes.cpp joins them.
 *
 * `port.js` is the CPU half, ported: the transport, the slot table, the
 * controls, the presets and ProcessOpenGL's bookkeeping. `demo/tools/crosscheck/`
 * holds it to the real plugin class, frame for frame.
 *
 * This file is the GL half of ProcessOpenGL and the page around it. It binds,
 * it draws, and it decides nothing about the tape.
 *
 * ------------------------------------------------------------ the tape
 *
 * ☠️ **The tape is the plugin's tape.** Ninety-six passes of the record head as
 * the layers of ONE `TEXTURE_2D_ARRAY` (Store.h), sized by Store.cpp's own byte
 * budget, written a layer at a time through `framebufferTextureLayer` and read
 * per fragment by the plugin's `tapeRead`, which walks the slot table that
 * port.js keeps. Not a queue of recent frames: a playback head finds whatever
 * was laid on the tape at its POSITION, and that is where the drag, the chorus
 * and the tear come from. A queue would render something plausible and none of
 * the three.
 *
 * ---------------------------------------------------------- decided, not asked
 *
 * **Preset is the plugin's control, and this host honours its events.** The
 * plugin writes a preset straight into the values it renders with and raises a
 * value event per control, asking the host to re-read the knob. Resolume does
 * not, so in Arena the preset renders and the knobs stay where they were. This
 * page's panel is the host here and it does re-read them, so the sliders move to
 * the preset; and when a genuine edit drops the plugin to Custom, the Preset
 * dropdown follows. The decision to drop is the plugin's own SetFloatParameter,
 * ported, with its 1e-3 tolerance. The kit's own presets menu is not used.
 *
 * **Paused, the tape stands still.** The plugin records on every ProcessOpenGL;
 * this page calls one per frame of its clock, and a redraw with no new frame
 * (moving a control while paused) runs the playback heads and the output stage
 * from the tape as it stands, without recording, winding the tape or stepping
 * the tank. Otherwise dragging a slider while paused would lay dozens of
 * zero-length passes on the tape and push the oldest echoes off the ring.
 */

import { mountDemo } from './vendor/demo.js';
import { Program, PassBuffer, bindTexture, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import * as P from './port.js';

//---------------------------------------------------------------------------
// The store: Store.{h,cpp} against WebGL2.
//---------------------------------------------------------------------------

class TapeStore {
  constructor(gl) {
    this.gl = gl;
    this.texture = null;
    this.fbo = null;
    this.width = 0;
    this.height = 0;
    this.layers = 0;
  }

  /**
   * Allocate `layers` passes at width x height. Returns false, leaving nothing
   * allocated, if the GPU refuses: a 96-layer array is hundreds of megabytes
   * and a browser is entitled to say no where Resolume would not.
   */
  ensure(width, height, layers) {
    if (this.texture && this.width === width && this.height === height && this.layers === layers) return true;
    const gl = this.gl;
    this.dispose();

    while (gl.getError() !== gl.NO_ERROR) { /* clear anything stale */ }

    this.texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, this.texture);
    gl.texStorage3D(gl.TEXTURE_2D_ARRAY, 1, gl.RGBA8, width, height, layers);
    // GL_LINEAR across the picture, GL_CLAMP_TO_EDGE at its edges, and no
    // filtering BETWEEN layers: a head landing between two passes reads a tear,
    // not a dissolve. As Store.cpp.
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
    const err = gl.getError();
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, null);

    if (err !== gl.NO_ERROR) {
      gl.deleteTexture(this.texture);
      this.texture = null;
      return false;
    }

    this.fbo = gl.createFramebuffer();
    this.width = width;
    this.height = height;
    this.layers = layers;
    return true;
  }

  /** Store::BindLayer: the framebuffer, with `layer` attached, viewport set. */
  bindLayer(layer) {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fbo);
    gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, this.texture, 0,
      Math.min(Math.max(layer, 0), this.layers - 1));
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
      throw new GLError(`The tape framebuffer is incomplete at layer ${layer}.`);
    }
    gl.viewport(0, 0, this.width, this.height);
  }

  dispose() {
    const gl = this.gl;
    if (this.fbo) gl.deleteFramebuffer(this.fbo);
    if (this.texture) gl.deleteTexture(this.texture);
    this.fbo = null;
    this.texture = null;
    this.width = 0;
    this.height = 0;
    this.layers = 0;
  }
}

//---------------------------------------------------------------------------
// The renderer: ProcessOpenGL's GL half.
//---------------------------------------------------------------------------

function createRenderer(gl, quad) {
  // compileShaders(): every stage is kVertex and a fragment assembled as
  // Passes.cpp assembles it. check_shaders.py holds these four lines to it.
  const programs = {
    playback: new Program(gl, S.VERTEX, S.VERSION + S.PREAMBLE + S.TAPE_FUNCTIONS + S.PLAYBACK, 'playback'),
    record: new Program(gl, S.VERTEX, S.VERSION + S.PREAMBLE + S.MEDIUM_FUNCTIONS + S.RECORD, 'record'),
    spring: new Program(gl, S.VERTEX, S.VERSION + S.PREAMBLE + S.SPRING, 'spring'),
    output: new Program(gl, S.VERTEX, S.VERSION + S.PREAMBLE + S.OUTPUT, 'output'),
  };

  const store = new TapeStore(gl);
  const playbackBuffer = new PassBuffer(gl, { filter: 'linear' });
  const tankBuffer = [new PassBuffer(gl, { filter: 'linear' }), new PassBuffer(gl, { filter: 'linear' })];

  let instance = null;
  let tankFront = 0;
  let lastTime = null;
  let lastFrameIndex = null;
  let stats = null;
  let allocationNote = '';

  const unbind = (count) => {
    for (let unit = count - 1; unit >= 0; unit -= 1) bindTexture(gl, unit, null);
    gl.activeTexture(gl.TEXTURE0);
  };

  /** Push a pass's uniforms the way FFGLShader::Set and glUniform*v would. */
  const INT_UNIFORMS = new Set(['TapeNewest', 'TapeWritten']);
  const ARRAY_UNIFORMS = new Set(['SlotStart', 'SlotSpan', 'HeadOffset', 'HeadUp', 'LegGain']);
  function apply(program, uniforms) {
    for (const [name, value] of Object.entries(uniforms)) {
      if (INT_UNIFORMS.has(name)) program.setInt(name, value);
      else if (ARRAY_UNIFORMS.has(name)) program.setArray(name, value, 1);
      else if (name === 'LegOffset') {
        // vec2[3]: the kit's setArray has no two-component case.
        const loc = program.location('LegOffset[0]') ?? program.location('LegOffset');
        if (loc === null) program.missing.add(name);
        else gl.uniform2fv(loc, value);
      } else if (Array.isArray(value)) program.set(name, value[0], value[1]);
      else program.set(name, value);
    }
  }

  /**
   * The store, allocated before anything is bound, at Store.cpp's size -- or,
   * if the browser refuses that much, at half the budget and then half again,
   * which the status line reports.
   */
  function ensureStore(st, width, height) {
    for (;;) {
      const size = P.storeSize(width, height, P.kSlots, st.budget);
      if (store.ensure(size.width, size.height, P.kSlots)) return;
      if (st.budget <= 8 * 1024 * 1024) {
        throw new GLError(`This browser would not allocate the tape: ${P.kSlots} passes at ${size.width} × ${size.height} (${mb(size.bytes)}), even after halving the plugin's budget several times. The plugin keeps every pass of the record head on the GPU, and this page has nowhere smaller to put it.`);
      }
      st.budget /= 2;
      st.compW = 0; // so the port's Store::Ensure reallocates too
      allocationNote = `This browser would not allocate the plugin's ${mb(P.kByteBudget)} tape, so it is running on a ${mb(st.budget)} budget and the echoes are softer than the plugin's at this size.`;
    }
  }

  /** One ProcessOpenGL. */
  function processFrame(st, input, width, height, time, target = null) {
    gl.disable(gl.BLEND);

    ensureStore(st, width, height);
    const plan = st.frame(time, width, height);
    playbackBuffer.ensure(plan.tapeW, plan.tapeH, gl.RGBA16F);
    tankBuffer[0].ensure(plan.tapeW, plan.tapeH, gl.RGBA16F);
    tankBuffer[1].ensure(plan.tapeW, plan.tapeH, gl.RGBA16F);

    // 1. Playback. Every head that is up, off the tape as it stands.
    playbackPass(plan.playback);

    // 2. Record. Only on the frames the record head passes.
    if (plan.record) {
      store.bindLayer(plan.slot);
      const p = programs.record.use();
      bindTexture(gl, 0, input.texture);
      bindTexture(gl, 1, playbackBuffer.texture);
      p.setSampler('InputTexture', 0);
      p.setSampler('PlaybackTexture', 1);
      apply(p, plan.record);
      quad.draw();
      unbind(2);
    }

    // 3. The tank. Fed from the INPUT, in parallel with the tape.
    if (plan.spring) {
      const back = 1 - tankFront;
      tankBuffer[back].bind();
      const p = programs.spring.use();
      bindTexture(gl, 0, tankBuffer[tankFront].texture);
      bindTexture(gl, 1, input.texture);
      p.setSampler('TankTexture', 0);
      p.setSampler('SourceTexture', 1);
      apply(p, plan.spring);
      quad.draw();
      unbind(2);
      tankFront = back;
    }

    // 4. The output stage, into the host's framebuffer.
    outputPass(plan.output, input, width, height, target);
    return plan;
  }

  function playbackPass(uniforms) {
    playbackBuffer.bind();
    const p = programs.playback.use();
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, store.texture);
    p.setSampler('TapeTex', 0);
    apply(p, uniforms);
    quad.draw();
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, null);
  }

  function outputPass(uniforms, input, width, height, target) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, target);
    gl.viewport(0, 0, width, height);
    const p = programs.output.use();
    bindTexture(gl, 0, input.texture);
    bindTexture(gl, 1, playbackBuffer.texture);
    bindTexture(gl, 2, tankBuffer[tankFront].texture);
    p.setSampler('InputTexture', 0);
    p.setSampler('PlaybackTexture', 1);
    p.setSampler('TankTexture', 2);
    apply(p, uniforms);
    quad.draw();
    unbind(3);
  }

  /** A redraw with no new frame: the heads and the output stage, nothing wound. */
  function redraw(st, input, width, height, target = null) {
    if (!store.texture || !st.renderOut) return processFrame(st, input, width, height, lastTime ?? 0, target);
    gl.disable(gl.BLEND);
    const r = P.render(st.hostValues());
    playbackPass(st.tapeUniforms(r));
    outputPass(st.outputUniforms(r, Math.max(1, width)), input, width, height, target);
    return null;
  }

  /**
   * The host's side of the parameters: every knob pushed to the plugin each
   * frame, in id order, as Resolume pushes them -- but, unlike Resolume, a host
   * that consumes the plugin's value events. After each push, any parameter the
   * plugin raised an event on is read back into the knobs at once, so a drop to
   * Custom moves the Preset knob before the push reaches it (otherwise the same
   * push would send the stale preset back and re-apply it over the edit).
   * crosscheck/ drives the real plugin through this same host ("push").
   */
  const HOST_KEYS = [...P.PARAM_KEYS, 'preset'];
  function pushHostValues(st, params) {
    const knobs = HOST_KEYS.map((key) => params.get(key));
    const raised = new Set();
    for (let id = 0; id <= P.PT_PRESET; id += 1) {
      for (const r of st.SetFloatParameter(id, knobs[id])) {
        knobs[r] = st.params[r];
        raised.add(r);
      }
    }
    return { knobs, raised };
  }

  /** The value events, honoured: the panel's knobs re-read from the plugin. */
  function rereadKnobs(params, { knobs, raised }) {
    for (const id of raised) params.set(HOST_KEYS[id], knobs[id]);
  }

  return {
    get stats() { return stats; },
    get allocationNote() { return allocationNote; },
    get instance() { return instance; },
    programs,
    store,
    processFrame,
    pushHostValues,
    newInstance: () => new P.AstronaughtFrames(),
    reset() { instance = null; },

    render({ input, params, width, height, time, frameIndex }) {
      if (!instance) {
        instance = new P.AstronaughtFrames();
        tankFront = 0;
      }
      const st = instance;
      const pushed = pushHostValues(st, params);

      const isFrame = lastTime === null || time !== lastTime || frameIndex !== lastFrameIndex;
      lastTime = time;
      lastFrameIndex = frameIndex;
      const plan = isFrame ? processFrame(st, input, width, height, time) : redraw(st, input, width, height);

      if (pushed.raised.size) rereadKnobs(params, pushed);

      const r = st.renderOut;
      stats = {
        mode: r.mode,
        selector: r.selector,
        baseDelay: r.baseDelay,
        spread: r.spread,
        transport: r.transport,
        stride: st.writeStride,
        tapeW: store.width,
        tapeH: store.height,
        bytes: store.width * store.height * 4 * P.kSlots,
        written: st.tape.Written(),
        recorded: plan ? plan.slot >= 0 : false,
        position: st.position,
        tank: r.tank.active,
      };
    },
  };
}

//---------------------------------------------------------------------------
// The parameters: the constructor's, in ParamID order and groups, with the
// plugin's names, defaults and element lists. Displays are the plugin's own
// conversions from port.js.
//---------------------------------------------------------------------------

const D = P.kHostDefaults;
const ms = (s) => (s >= 1 ? `${s.toFixed(2)} s` : `${Math.round(s * 1000)} ms`);
const mb = (bytes) => `${Math.round(bytes / (1024 * 1024))} MB`;
const fix = (n, d = 2) => n.toFixed(d);
const db = (v) => {
  const x = (Math.min(Math.max(v, 0), 1) - 0.5) * 2 * P.kToneDb;
  return `${x >= 0 ? '+' : '−'}${Math.abs(x).toFixed(1)} dB`;
};

const std = (id, name, group, display, hint) => ({ id, name, group, type: 'standard', default: D[id], display, hint });

const PARAMS = [
  {
    id: 'mode', name: 'Mode', group: 'Echo', type: 'option', default: D.mode,
    elements: P.kModes.map((m) => m.label),
    hint: 'The Mode Selector, transcribed from Roland’s printed table. 1–4 are echo only, and the only combination among them is heads 2+3; 5–11 add the spring tank; 12 is the tank alone. There is no dry 1+2, 1+3 or 1+2+3, because the machine has none.',
  },
  std('repeatRate', 'Repeat Rate', 'Echo', (v) => `h1 ${ms(P.expRange(v, P.kDelaySlow, P.kDelayFast))}`,
    'A tape speed, not a delay time: up is faster tape and SHORTER echoes. Head 1 spans 1 s to 70 ms; heads 2 and 3 are twice and three times it. Turning it drags every echo already on the tape.'),
  std('intensity', 'Intensity', 'Echo', (v) => `×${fix(Math.min(Math.max(v, 0), 1) * P.kIntensityMax)}`,
    'The head mix fed back onto the tape: the loop gain, 0 to 1.15. Past 1 it runs away, as the hardware does.'),
  std('headSpread', 'Head Spread', 'Echo', (v) => `×${fix(P.expRange(v, P.kSpreadLo, P.kSpreadHi))}`,
    'An extension: moves the playback heads, ×0.25 to ×2.5 of where the hardware bolts them. Nominal (×1) at its default. Changes the delay without changing the tape speed.'),

  std('inputLevel', 'Input Level', 'Tape', (v) => `×${fix(P.expRange(v, P.kDriveLo, P.kDriveHi))}`,
    'Drive into the record amplifier, ×0.25 to ×2.5.'),
  std('saturation', 'Saturation', 'Tape', null,
    'An extension (on BOSS’s RE-20, not the RE-201): the record amplifier’s knee. Transparent below it, compressing above, so heavy Saturation folds the peaks down.'),
  std('headWear', 'Head Wear', 'Tape', null,
    'Head-gap loss: horizontal detail lost along the scan, every generation. Tape has one frequency axis.'),
  std('hiss', 'Hiss', 'Tape', (v) => fix(P.kHissMax * v * v, 4),
    'The oxide’s noise floor, as horizontal grain that moves with the tape. Squared, to a peak of 0.1 of full scale.'),
  std('dropouts', 'Dropouts', 'Tape', (v) => `${fix(100 * P.kDropoutMax * v * v, 1)}%`,
    'The tape lifting off the head: whole blocks of a scanline lost, up to 6% of them.'),

  std('wowFlutter', 'Wow & Flutter', 'Transport', null,
    'The master trim on the transport’s instability, which multiplies the three below. Not zero by default: a Space Echo that runs dead steady is a digital delay.'),
  std('wow', 'Wow', 'Transport', null, 'Slow speed error, as from an out-of-round capstan.'),
  std('flutter', 'Flutter', 'Transport', null, 'Faster, rougher speed error, from the pinch roller and the guides.'),
  std('scrape', 'Scrape', 'Transport', null, 'Stick-slip of the tape against the heads, at about 47 Hz. Rough on purpose.'),

  std('reverbTime', 'Reverb Time', 'Reverb', (v) => ms(P.decaySeconds(v)),
    'The tank’s decay to −60 dB, 0.35 to 6 seconds. Only in Mode positions 5 to 12.'),
  std('dispersion', 'Dispersion', 'Reverb', null,
    'How fast the tank’s bloom grows per trip: a spring is a dispersive delay line, so its fine detail lags its coarse.'),

  std('echoVolume', 'Echo Volume', 'Output', null, 'The head bus into the output.'),
  std('reverbVolume', 'Reverb Volume', 'Output', null, 'The tank into the output. Silent in positions 1 to 4.'),
  std('directVolume', 'Direct Volume', 'Output', null, 'The dry input, straight off the input and never near the tape.'),
  std('bass', 'Bass', 'Output', db, 'A low shelf on the WET bus only, ±12 dB, along the scan. Flat at the centre.'),
  std('treble', 'Treble', 'Output', db, 'A high shelf on the WET bus only, ±12 dB, along the scan. Flat at the centre.'),
  std('mix', 'Mix', 'Output', null, 'The fleet’s wet/dry. At 0 the picture comes back exactly as it went in.'),

  {
    id: 'preset', name: 'Preset', group: 'Preset', type: 'option', default: 0,
    elements: ['Custom', ...P.kPresets.map((p) => p.name)],
    hint: 'The plugin’s ten factory presets. Every control but Mix is covered; a genuine edit of a covered control drops it back to Custom.',
  },
];

//---------------------------------------------------------------------------
// The page.
//---------------------------------------------------------------------------

let renderer = null;

const mounted = mountDemo({
  name: 'Astronaught',
  pluginId: 'AN01',
  kind: 'effect',
  tagline:
    'A video signal put through a Space Echo: a model of the Roland RE-201, with a loop of tape pulled past a record head and three playback heads at equal intervals, a twelve-position Mode Selector, an Intensity control that feeds the heads back onto the tape, and a three-spring tank hanging off the input. The tape is indexed by position, not by time, so turning Repeat Rate drags the echoes already recorded, the three repeats wobble independently, and the picture comes back torn.',
  repo: 'https://github.com/stoatworks-labs/astronaught',
  page: 'https://stoatworks-labs.com/software/astronaught/',
  video: 'https://www.youtube.com/watch?v=gKclEJn55sk',

  blurb:
    'It is Astronaught’s own four GLSL passes, ported from the repository to WebGL2 unedited, recording onto the plugin’s own tape (96 passes of the record head in one texture array, at the plugin’s own byte budget) and driven by a JavaScript port of its CPU half (the transport, the slot table, the controls and the presets), which a check in the repository holds to the plugin’s C++. It runs on generated clips or your own, and the plugin has no audio input, so nothing is left out for want of one.',

  // The output carries alpha: an echo of a keyed source is keyed too.
  showBackdrop: true,
  // The head bus and the tank are half-float targets in the plugin.
  needFloat: true,

  // Moving, high-contrast things first: an echo of a still picture is the same
  // picture. Lights on black shows the 1 : 2 : 3 heads as separate repeats; the
  // geometry card makes the tear and the vertical stretch readable; fine detail
  // is what Head Wear and the tone shelves take away.
  sources: ['spot', 'scene', 'grid', 'detail', 'ramp', 'bars', 'alpha'],

  params: PARAMS,

  differences: [
    'The CPU half is a PORT. The plugin winds its tape on the CPU (Transport.cpp: the capstan’s wow, flutter and scrape, integrated across each frame in eight substeps), keeps the slot table there (Tape.cpp: where on the tape each pass of the record head landed, rebased as it runs), maps its controls there (Controls.cpp), applies its presets there (Presets.h and SetFloatParameter) and sizes its tape there (Store.cpp). Here all of that is demo/port.js, rounded to 32-bit float wherever the C++ is. A check in the repository (demo/tools/crosscheck/) drives the real plugin class, in an OpenGL context, and the port through the same 3,010 frames: every Mode position, Repeat Rate swept while echoes are on the tape, Head Spread both ways, the tank, presets applied, held and dropped through three kinds of host, four frame rates, a stalled clock and one that jumps back, three resolutions, and a fast tape run long enough to rebase the slot table. After every frame it compares the tape position, the stride, the slot table, every parameter and every uniform the plugin handed to GL: 898,803 values. Every integer and every parameter is identical; against the plugin built without fused multiply-add (the x86_64 slice’s arithmetic) 99.6% of the values are bit-identical and the worst is one unit in the last place of a float, from a libm that is not the plugin’s. Against the Apple Silicon build, which fuses, 80% are bit-identical and the worst is 5 parts in a million.',
    'Frames drawn here were compared with the plugin’s own harness (astest, the real class in Apple’s OpenGL) on its own test card at the same settings, 640 × 360, 30 to 600 frames, with this page’s renderer running in headless Chrome on SwiftShader. Mix 0 came out identical to the byte. At the defaults, on a 600-frame run with a slow tape and the tank in, and on seven of the ten factory presets, every pixel was within two levels of 255 and all but 0.02% within one. The three presets with Dropouts up (Long Throw, Runaway, Sick Transport) also differed in a handful of whole dropout blocks, 0.06% to 0.65% of the pixels, where the hash that decides whether a block drops lands on the other side of its threshold on a different GPU. Your browser’s GPU is not SwiftShader, and nothing was compared on it.',
    'The GPU half is not a port: the playback, record, spring and output passes are the plugin’s own GLSL, reading the slot table with the plugin’s own tapeRead and writing one layer of a 96-layer RGBA8 texture array per pass of the record head, with the head bus and the tank in half-float buffers as in the plugin (WebGL2 needs EXT_color_buffer_float for those, and the page stops and says so without it). demo/tools/check_shaders.py fails the repository’s verify script if a character of them, or the way they are joined, drifts.',
    'The tape is the plugin’s size: 96 passes at the composition’s resolution, scaled down to fit Store.cpp’s 384 MB budget (at 1920 × 1080 it is 1365 × 768, as in Resolume). That is a lot of memory for a web page. If the browser refuses it the page halves the budget until it is allowed, and the line under the picture says so; if the browser drops the GL context instead, the page says that.',
    'Preset is the plugin’s own control and its own logic: a preset writes its values over the controls it covers, and a genuine edit of one of them (more than the plugin’s 1e-3 tolerance from what the host last sent) drops it to Custom. The plugin also asks the host to re-read the knobs. Resolume 7.27.1 does not, so there the preset renders and the knobs stay where they were; this page’s panel does, so the sliders move to the preset and the dropdown goes back to Custom when the plugin does.',
    'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame (the plugin’s own cap is the same tenth). The plugin’s vote on whether a host sends seconds or milliseconds is not ported, because this host’s unit is known, as the plugin’s harness declares it. Paused, no frames are processed: moving a control redraws the playback heads and the output stage from the tape as it stands, and the tape, the record head and the tank wait for Play or Step. Step is one frame of a sixtieth of a second. Restart is a clock jump backwards, which the plugin takes as a frame with no time in it: the tape stops for that frame and the spring tank, whose coefficients are zero for no time, empties.',
    'Changing Composition is a resolution change, and that is the one thing the plugin allows to erase its tape: the echoes start again. A parameter change never does. Dropping in your own file changes the resolution to the file’s, so it erases the tape too.',
    'Nothing audio: the plugin has no audio input and no beat sync, so nothing is missing.',
    'The About block (the plugin’s name, version and links, which FFGL hosts draw as parameters) is not on the panel, as on every page in this suite.',
    'What the plugin’s harness establishes, and this page cannot: it holds the Mode Selector to Roland’s table cell by cell, the heads to 1 : 2 : 3, the drag (a second and a half after a 500 ms → 125 ms speed change, head 1 still reads 450 ms-old material), the chorus, the GLSL tape read to Tape.cpp to 1.6e-6, and Mix 0 to byte-for-byte transparency — and that harness, not this page, is the reason to believe the machine. The plugin has run in Resolume Arena 7.27.1 on macOS and Windows; no NVIDIA or AMD driver has run it, and nothing has been used on a show.',
  ],

  createRenderer: (gl, quad) => {
    renderer = createRenderer(gl, quad);
    return renderer;
  },
});

//---------------------------------------------------------------------------
// A status line: reports what the port computed, measures nothing.
//---------------------------------------------------------------------------
if (mounted) {
  const query = new URLSearchParams(window.location.search);
  const embed = query.has('embed') && query.get('embed') !== '0';
  const canvas = document.querySelector('canvas');
  const stage = document.querySelector('.stage');

  let lost = null;
  if (canvas) {
    canvas.addEventListener('webglcontextlost', (event) => {
      event.preventDefault();
      if (lost) lost.hidden = false;
    });
  }

  if (!embed && stage) {
    const line = document.createElement('p');
    line.className = 'stage__status';
    line.id = 'astronaught-stats';
    line.setAttribute('aria-live', 'off');
    stage.append(line);

    const note = document.createElement('p');
    note.className = 'stage__status';
    note.id = 'astronaught-note';
    stage.append(note);

    lost = document.createElement('p');
    lost.className = 'stage__status is-error';
    lost.hidden = true;
    lost.textContent = 'The browser dropped the GL context, most likely because the GPU ran out of memory for the tape. Reload to try again, or pick a smaller Composition before the tape fills.';
    stage.append(lost);

    let flutterKey = '';
    let flutter = 0;
    setInterval(() => {
      const s = renderer?.stats;
      if (!s) return;
      const key = JSON.stringify(s.transport);
      if (key !== flutterKey) {
        flutterKey = key;
        flutter = P.flutterPercent(s.transport);
      }
      const heads = [0, 1, 2]
        .map((i) => `${s.selector.head[i] ? '' : '('}${i + 1} at ${ms(s.baseDelay * s.spread * P.kSpacing[i])}${s.selector.head[i] ? '' : ')'}`)
        .join(', ');
      line.textContent =
        `Mode ${s.mode + 1}: heads ${heads}${s.tank ? ' + tank' : ''}`
        + ` · record head every ${s.stride} frame${s.stride === 1 ? '' : 's'}`
        + ` · ${s.written} of ${P.kSlots} passes on the tape`
        + ` · wow & flutter ${flutter.toFixed(2)}% p-p`;
      note.textContent =
        `Tape: ${P.kSlots} passes of ${s.tapeW} × ${s.tapeH}, ${mb(s.bytes)}. Delays are at the nominal speed; the transport wobbles round them. A bracketed head is not up in this position.`
        + (renderer.allocationNote ? ` ${renderer.allocationNote}` : '');
    }, 250);
  }

  // For the headless checks in AGENTS.md: the page's pieces, read-only by convention.
  window.astronaughtDemo = { mounted, get renderer() { return renderer; }, PARAMS, P, createRenderer };
}
