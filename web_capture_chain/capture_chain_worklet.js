// Runs capture_chain.wasm (Strong, the input gate and the dynamics from
// common/cpp) on the microphone inside an AudioWorklet, so a browser call
// sends what a desktop one does. The chain takes 10 ms frames on the int16
// scale; the worklet hands it 128-sample render quanta on the -1..1 scale.
const FULL_SCALE = 32768;
const QUANTUM = 128;

function gcd(a, b) {
  return b === 0 ? a : gcd(b, a % b);
}

class SlimmCaptureChain extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const module = new WebAssembly.Module(options.processorOptions.wasm);
    this.chain = new WebAssembly.Instance(module, {}).exports;
    this.chain._initialize();
    this.frame = Math.round(sampleRate / 100);
    this.chain.slimm_chain_initialize(sampleRate, 1);
    this.input = new Float32Array(this.frame);
    this.filled = 0;
    // A frame leaves only once it is whole, so the output starts this far behind to never run dry.
    this.out = new Float32Array(this.frame * 2 + QUANTUM);
    this.outRead = 0;
    this.outCount = this.frame - gcd(this.frame, QUANTUM);
    this.port.onmessage = (event) => this.onMessage(event.data);
  }

  onMessage(message) {
    const chain = this.chain;
    if (message.type === 'configure') {
      chain.slimm_chain_configure(
        message.enabled ? 1 : 0,
        message.thresholdDb,
        message.dynamics ? 1 : 0,
        message.levelAfterSuppression ? 1 : 0,
        message.strongNoiseSuppression ? 1 : 0,
      );
    } else if (message.type === 'read') {
      this.port.postMessage({
        type: 'reading',
        id: message.id,
        levelDb: chain.slimm_chain_take_peak_db(),
        voice: chain.slimm_chain_voice() === 1,
        dynamics: chain.slimm_chain_dynamics() === 1,
        levelerGainDb: chain.slimm_chain_leveler_gain_db(),
        strongNoiseSuppression: chain.slimm_chain_strong_active() === 1,
      });
    }
  }

  runFrame() {
    const chain = this.chain;
    const pointer = chain.slimm_chain_frame_buffer();
    const heap = new Float32Array(chain.memory.buffer, pointer, this.frame);
    for (let i = 0; i < this.frame; i++) heap[i] = this.input[i] * FULL_SCALE;
    chain.slimm_chain_process(pointer, this.frame);
    const size = this.out.length;
    let write = (this.outRead + this.outCount) % size;
    for (let i = 0; i < this.frame; i++) {
      this.out[write] = heap[i] / FULL_SCALE;
      write = (write + 1) % size;
    }
    this.outCount += this.frame;
    this.filled = 0;
  }

  process(inputs, outputs) {
    const source = inputs[0] && inputs[0][0];
    const target = outputs[0][0];
    const n = target.length;
    for (let i = 0; i < n; i++) {
      this.input[this.filled++] = source ? source[i] : 0;
      if (this.filled === this.frame) this.runFrame();
    }
    const size = this.out.length;
    const take = Math.min(n, this.outCount);
    for (let i = 0; i < take; i++) {
      target[i] = this.out[this.outRead];
      this.outRead = (this.outRead + 1) % size;
    }
    target.fill(0, take);
    this.outCount -= take;
    for (let c = 1; c < outputs[0].length; c++) outputs[0][c].set(target);
    return true;
  }
}

registerProcessor('slimm-capture-chain', SlimmCaptureChain);
