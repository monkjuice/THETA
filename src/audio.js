import { activeTracks, eventsAtStep, encodeWav } from './model.js';

function noise(context) {
  const buffer = context.createBuffer(1, context.sampleRate, context.sampleRate);
  const data = buffer.getChannelData(0);
  let seed = 42;
  for (let i = 0; i < data.length; i++) { seed = (seed * 16807) % 2147483647; data[i] = seed / 1073741824 - 1; }
  return buffer;
}
function graph(context, project) {
  const master = context.createGain(); master.gain.value = project.master;
  const limiter = context.createDynamicsCompressor(); limiter.threshold.value = -5; limiter.knee.value = 4; limiter.ratio.value = 16; limiter.attack.value = 0.003; limiter.release.value = 0.12;
  const analyser = context.createAnalyser(); analyser.fftSize = 256;
  master.connect(limiter).connect(analyser).connect(context.destination);
  const tracks = new Map();
  for (const track of project.tracks) {
    const gain = context.createGain(); gain.gain.value = activeTracks(project).includes(track) ? track.volume : 0;
    const pan = context.createStereoPanner(); pan.pan.value = track.pan;
    const filter = context.createBiquadFilter(); filter.type = 'lowpass'; filter.frequency.value = track.instrument === 'audio' || track.instrument === 'drums' ? 20000 : track.cutoff; filter.Q.value = 0.7;
    filter.connect(gain).connect(pan).connect(master);
    let delay, feedback, wet;
    if (['keys', 'pad', 'lead'].includes(track.instrument)) {
      delay = context.createDelay(2); delay.delayTime.value = 60 / project.bpm * 0.75;
      feedback = context.createGain(); feedback.gain.value = 0.24;
      wet = context.createGain(); wet.gain.value = track.instrument === 'pad' ? 0.3 : 0.13;
      filter.connect(delay).connect(feedback).connect(delay); delay.connect(wet).connect(gain);
    }
    tracks.set(track.id, { input: filter, gain, pan, filter, delay, feedback, wet });
  }
  return { master, limiter, analyser, tracks, noise: noise(context) };
}

function voice(context, destination, instrument, pitch, velocity, time, duration, noiseBuffer, sources) {
  const gain = context.createGain(); gain.connect(destination);
  const add = (source, end) => {
    source.start(time); source.stop(end); sources?.add(source);
    source.onended = () => { sources?.delete(source); source.disconnect(); };
  };
  if (instrument === 'drums') {
    if (pitch === 36) {
      const osc = context.createOscillator(); osc.frequency.setValueAtTime(145, time); osc.frequency.exponentialRampToValueAtTime(43, time + 0.11);
      gain.gain.setValueAtTime(0.001, time); gain.gain.linearRampToValueAtTime(velocity * 0.85, time + 0.004); gain.gain.exponentialRampToValueAtTime(0.001, time + 0.36);
      osc.connect(gain); add(osc, time + 0.38);
    } else {
      const source = context.createBufferSource(); source.buffer = noiseBuffer;
      const filter = context.createBiquadFilter(); filter.type = pitch === 38 || pitch === 39 ? 'bandpass' : 'highpass'; filter.frequency.value = pitch === 38 || pitch === 39 ? 1900 : 7500;
      const length = pitch === 46 ? 0.28 : pitch === 38 || pitch === 39 ? 0.16 : 0.055;
      gain.gain.setValueAtTime(velocity * (pitch === 38 ? 0.52 : 0.32), time); gain.gain.exponentialRampToValueAtTime(0.001, time + length);
      source.connect(filter).connect(gain); add(source, time + length + 0.02);
      if (pitch === 38) {
        const tone = context.createOscillator(); tone.frequency.value = 175; tone.type = 'triangle'; tone.connect(gain); add(tone, time + 0.075);
      }
    }
    return;
  }
  const frequency = 440 * 2 ** ((pitch - 69) / 12);
  const attack = instrument === 'pad' ? 0.22 : instrument === 'keys' ? 0.008 : 0.012;
  const release = instrument === 'pad' ? 0.65 : instrument === 'keys' ? 0.3 : 0.1;
  const level = velocity * (instrument === 'pad' ? 0.16 : instrument === 'bass' ? 0.29 : 0.2);
  const sustainTime = time + Math.max(duration, attack + 0.01);
  gain.gain.setValueAtTime(0, time); gain.gain.linearRampToValueAtTime(level, time + attack);
  gain.gain.exponentialRampToValueAtTime(Math.max(0.001, level * (instrument === 'keys' ? 0.3 : 0.72)), sustainTime);
  gain.gain.exponentialRampToValueAtTime(0.0001, sustainTime + release);
  const osc = context.createOscillator(); osc.type = instrument === 'keys' ? 'sine' : instrument === 'lead' ? 'triangle' : 'sawtooth'; osc.frequency.value = frequency; osc.connect(gain); add(osc, sustainTime + release + 0.02);
  if (instrument === 'keys' || instrument === 'pad') {
    const second = context.createOscillator(); second.type = 'sine'; second.frequency.value = frequency * (instrument === 'keys' ? 2 : 1); second.detune.value = instrument === 'pad' ? 9 : 0;
    const blend = context.createGain(); blend.gain.value = 0.22; second.connect(blend).connect(gain); add(second, sustainTime + release + 0.02);
  }
}

export class AudioEngine {
  constructor(getProject, onPosition, onStop) {
    this.getProject = getProject; this.onPosition = onPosition; this.onStop = onStop;
    this.context = null; this.playing = false; this.position = 0; this.sources = new Set(); this.buffers = new Map(); this.metronome = false;
  }
  async init() {
    if (!this.context) this.context = new AudioContext({ latencyHint: 'interactive' });
    if (this.context.state !== 'running') await this.context.resume();
    return this.context;
  }
  async loadAssets(project = this.getProject()) {
    await this.init();
    for (const [id, asset] of Object.entries(project.assets)) if (!this.buffers.has(id)) {
      const binary = atob(asset.data.split(',')[1]);
      const bytes = Uint8Array.from(binary, c => c.charCodeAt(0));
      this.buffers.set(id, await this.context.decodeAudioData(bytes.buffer));
    }
  }
  async play() {
    if (this.playing || this.starting) return;
    this.starting = true;
    try {
      await this.loadAssets();
      const project = this.getProject();
      if (this.position >= (project.loop ? project.loopEnd : project.bars * 16)) this.position = project.loop ? project.loopStart : 0;
      this.disconnectGraph();
      this.graph = graph(this.context, project);
      this.step = Math.floor(this.position); this.nextTime = this.context.currentTime + 0.06;
      this.originTime = this.nextTime; this.originStep = this.step; this.playing = true;
      // Resume audio clips that began before the playhead.
      this.resumeAudio(this.step, this.nextTime);
      this.schedule(); this.timer = setInterval(() => this.schedule(), 25);
      this.animate();
    } finally { this.starting = false; }
  }
  resumeAudio(step, time) {
    for (const track of activeTracks(this.getProject())) for (const clip of track.clips) {
      if (clip.assetId && clip.start < step && clip.start + clip.length > step) this.scheduleAudio(this.context, this.graph, track, clip, time, (step - clip.start) * 60 / this.getProject().bpm / 4);
    }
  }
  scheduleAudio(context, routing, track, clip, time, offset = 0, sources = this.sources) {
    const buffer = this.buffers.get(clip.assetId); if (!buffer || offset >= buffer.duration) return;
    const source = context.createBufferSource(); source.buffer = buffer; source.connect(routing.tracks.get(track.id).input);
    const available = clip.length * 60 / this.getProject().bpm / 4 - offset;
    if (available <= 0) return;
    source.start(time, offset, Math.min(buffer.duration - offset, available)); sources?.add(source);
    source.onended = () => { sources?.delete(source); source.disconnect(); };
  }
  schedule() {
    if (!this.playing) return;
    const project = this.getProject(); const seconds = 60 / project.bpm / 4;
    while (this.nextTime < this.context.currentTime + 0.12) {
      const end = project.loop ? project.loopEnd : project.bars * 16;
      if (this.step >= end) {
        if (!project.loop) {
          if (this.context.currentTime >= this.nextTime) { this.stop(); this.onStop?.(); }
          break;
        }
        this.step = project.loopStart;
        // Audio regions must not spill across the loop boundary.
        for (const source of this.sources) { try { source.stop(this.nextTime); } catch {} }
        this.resumeAudio(this.step, this.nextTime);
      }
      for (const event of eventsAtStep(project, this.step)) {
        if (event.audio) this.scheduleAudio(this.context, this.graph, event.track, event.clip, this.nextTime);
        else {
          const offset = event.note.step % 1;
          const duration = Math.min(event.note.duration, event.clip.length - event.note.step, end - (event.clip.start + event.note.step));
          voice(this.context, this.graph.tracks.get(event.track.id).input, event.track.instrument, event.note.pitch, event.note.velocity, this.nextTime + offset * seconds, duration * seconds, this.graph.noise, this.sources);
        }
      }
      if (this.metronome && this.step % 4 === 0) {
        const osc = this.context.createOscillator(), gain = this.context.createGain();
        osc.frequency.value = this.step % 16 === 0 ? 1300 : 900; gain.gain.setValueAtTime(0.13, this.nextTime); gain.gain.exponentialRampToValueAtTime(0.001, this.nextTime + 0.035);
        osc.connect(gain).connect(this.graph.master); osc.start(this.nextTime); osc.stop(this.nextTime + 0.04); this.sources.add(osc); osc.onended = () => this.sources.delete(osc);
      }
      this.step++; this.nextTime += seconds;
    }
  }
  animate() {
    if (!this.playing) return;
    const p = this.getProject();
    let step = Math.max(this.originStep, this.originStep + (this.context.currentTime - this.originTime) / (60 / p.bpm / 4));
    if (p.loop && step >= p.loopEnd) step = p.loopStart + (step - p.loopEnd) % (p.loopEnd - p.loopStart);
    this.position = step; this.onPosition(step, this.level());
    this.frame = requestAnimationFrame(() => this.animate());
  }
  level() {
    if (!this.graph) return 0;
    const samples = new Float32Array(256); this.graph.analyser.getFloatTimeDomainData(samples);
    return Math.sqrt(samples.reduce((sum, sample) => sum + sample * sample, 0) / samples.length);
  }
  disconnectGraph() {
    if (!this.graph) return;
    for (const track of this.graph.tracks.values()) for (const node of Object.values(track)) node?.disconnect();
    this.graph.master.disconnect(); this.graph.limiter.disconnect(); this.graph.analyser.disconnect(); this.graph = null;
  }
  stop(reset = true) {
    this.playing = false; clearInterval(this.timer); cancelAnimationFrame(this.frame);
    for (const source of this.sources) { try { source.stop(); } catch {} }
    this.sources.clear(); this.disconnectGraph();
    if (reset) this.position = 0;
    this.onPosition(this.position, 0);
  }
  async seek(step) { const wasPlaying = this.playing; this.stop(false); this.position = step; this.onPosition(step, 0); if (wasPlaying) await this.play(); }
  updateMix() {
    if (!this.graph) return;
    const p = this.getProject(), active = activeTracks(p);
    this.graph.master.gain.setTargetAtTime(p.master, this.context.currentTime, 0.015);
    for (const t of p.tracks) {
      const channel = this.graph.tracks.get(t.id); if (!channel) continue;
      channel.gain.gain.setTargetAtTime(active.includes(t) ? t.volume : 0, this.context.currentTime, 0.015);
      channel.pan.pan.setTargetAtTime(t.pan, this.context.currentTime, 0.015);
      if (!['audio', 'drums'].includes(t.instrument)) channel.filter.frequency.setTargetAtTime(t.cutoff, this.context.currentTime, 0.015);
    }
  }
  async preview(track, pitch, velocity = 0.7) {
    await this.init();
    const gain = this.context.createGain(); gain.gain.value = track.volume * this.getProject().master; gain.connect(this.context.destination);
    voice(this.context, gain, track.instrument, pitch, velocity, this.context.currentTime, 0.18, noise(this.context), this.sources);
    setTimeout(() => gain.disconnect(), 1200);
  }
  async export() {
    const p = structuredClone(this.getProject()); await this.loadAssets(p);
    const seconds = 60 / p.bpm / 4;
    const end = Math.max(16, ...p.tracks.flatMap(t => t.clips.map(c => c.start + c.length)));
    const context = new OfflineAudioContext(2, Math.ceil((end * seconds + 1.5) * 44100), 44100);
    const routing = graph(context, p);
    for (let step = 0; step < end; step++) for (const event of eventsAtStep(p, step)) {
      if (event.audio) this.scheduleAudio(context, routing, event.track, event.clip, step * seconds, 0, null);
      else voice(context, routing.tracks.get(event.track.id).input, event.track.instrument, event.note.pitch, event.note.velocity, (step + event.note.step % 1) * seconds, Math.min(event.note.duration, event.clip.length - event.note.step) * seconds, routing.noise);
    }
    return encodeWav(await context.startRendering());
  }
}
