// The AudioWorklet wrapper around capture_chain.wasm, driven the way a browser
// drives it: 128-sample quanta in, the same out. Run with `node --test`.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { test } from 'node:test';

const dir = new URL('../../web_capture_chain/', import.meta.url);
const wasm = readFileSync(new URL('capture_chain.wasm', dir));

function processorAt(rate) {
  let Processor;
  globalThis.sampleRate = rate;
  globalThis.AudioWorkletProcessor = class {
    constructor() {
      this.sent = [];
      this.port = { postMessage: (m) => this.sent.push(m) };
    }
  };
  globalThis.registerProcessor = (_, cls) => { Processor = cls; };
  const require = createRequire(import.meta.url);
  delete require.cache[require.resolve(new URL('capture_chain_worklet.js', dir).pathname)];
  require(new URL('capture_chain_worklet.js', dir).pathname);
  return new Processor({ processorOptions: { wasm } });
}

function configure(p, { gate, strong = false, thresholdDb = -50 }) {
  p.onMessage({
    type: 'configure', enabled: gate, thresholdDb, dynamics: false,
    levelAfterSuppression: false, strongNoiseSuppression: strong,
  });
}

// Runs `quanta` render quanta of gen(t) through and returns every output sample.
function run(p, quanta, gen, start = 0) {
  const out = new Float32Array(quanta * 128);
  const input = new Float32Array(128);
  const target = new Float32Array(128);
  for (let q = 0; q < quanta; q++) {
    for (let i = 0; i < 128; i++) input[i] = gen(start + q * 128 + i);
    assert.equal(p.process([[input]], [[target]]), true);
    out.set(target, q * 128);
  }
  return out;
}

function read(p) {
  p.onMessage({ type: 'read', id: 7 });
  return p.sent.at(-1);
}

const rms = (a) => Math.sqrt(a.reduce((s, v) => s + v * v, 0) / a.length);

test('an open gate passes a tone through at its level', () => {
  const p = processorAt(48000);
  configure(p, { gate: false });
  const out = run(p, 400, (t) => 0.1 * Math.sin(2 * Math.PI * 300 * t / 48000));
  assert.ok(Math.abs(rms(out.subarray(48000)) - 0.1 / Math.SQRT2) < 0.005);
});

test('the output trails the input by a fixed delay and never runs dry', () => {
  const p = processorAt(48000);
  configure(p, { gate: false });
  const impulseAt = 10000;
  const out = run(p, 200, (t) => (t === impulseAt ? 0.5 : 0));
  const peak = out.reduce((best, v, i) => (Math.abs(v) > Math.abs(out[best]) ? i : best), 0);
  // 448 samples of worklet framing plus the gate's 960 sample (20 ms) lookahead.
  assert.equal(peak - impulseAt, 448 + 960);
  assert.ok(Math.abs(out[peak] - 0.5) < 1e-4);
});

test('a closed gate sends silence below the threshold and reports the level', () => {
  const p = processorAt(48000);
  configure(p, { gate: true });
  const out = run(p, 400, (t) => 0.0003 * Math.sin(2 * Math.PI * 300 * t / 48000));
  assert.equal(rms(out.subarray(24000)), 0);
  const reading = read(p);
  assert.equal(reading.type, 'reading');
  assert.equal(reading.id, 7);
  assert.equal(reading.voice, false);
  assert.ok(reading.levelDb < -65 && reading.levelDb > -80, `${reading.levelDb}`);
});

test('speech-loud sound opens the gate', () => {
  const p = processorAt(48000);
  configure(p, { gate: true });
  const out = run(p, 400, (t) => 0.1 * Math.sin(2 * Math.PI * 300 * t / 48000));
  assert.ok(rms(out.subarray(24000)) > 0.05);
  assert.equal(read(p).voice, true);
});

test('Strong reports rnnoise running, and stops when switched off', () => {
  const p = processorAt(48000);
  configure(p, { gate: false, strong: true });
  run(p, 50, (t) => 0.1 * Math.sin(t / 7));
  assert.equal(read(p).strongNoiseSuppression, true);
  configure(p, { gate: false, strong: false });
  run(p, 10, (t) => 0.1 * Math.sin(t / 7));
  assert.equal(read(p).strongNoiseSuppression, false);
});

test('a 44.1 kHz context is framed in 441-sample frames', () => {
  const p = processorAt(44100);
  configure(p, { gate: false, strong: true });
  const out = run(p, 400, (t) => 0.1 * Math.sin(2 * Math.PI * 300 * t / 44100));
  assert.ok(rms(out.subarray(44100)) > 0.05);
  assert.equal(read(p).strongNoiseSuppression, true);
});
