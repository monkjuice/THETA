export const STEPS_PER_BAR = 16;
export const COLORS = ['#b6cc84', '#dfaa72', '#9faff0', '#c497cb', '#76bdc0', '#d78f94'];
export const INSTRUMENTS = {
  drums: { name: 'Studio Drum Kit', family: 'Drums', description: 'A warm kick, snappy snare, and crisp hats.', icon: 'drums' },
  bass: { name: 'Analog Bass', family: 'Bass', description: 'A rounded, low-pass filtered sawtooth.', icon: 'wave' },
  keys: { name: 'Velvet Keys', family: 'Keys', description: 'Soft electric keys with a little shimmer.', icon: 'keys' },
  pad: { name: 'Cloud Pad', family: 'Pads', description: 'Slow, spacious chords and a soft release.', icon: 'cloud' },
  lead: { name: 'Prism Lead', family: 'Synths', description: 'A bright, focused melodic voice.', icon: 'wave' },
  audio: { name: 'Audio', family: 'Audio', description: 'Import a sample or record a microphone.', icon: 'mic' }
};
export const uid = () => globalThis.crypto.randomUUID();
export const clamp = (v, min, max) => Math.max(min, Math.min(max, v));
export const noteName = midi => ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'][midi % 12] + (Math.floor(midi / 12) - 1);
export function createTrack(instrument, index = 0) {
  return { id: uid(), name: INSTRUMENTS[instrument].name, instrument, color: COLORS[index % COLORS.length], volume: 0.72, pan: 0, mute: false, solo: false, cutoff: instrument === 'bass' ? 1100 : 6000, clips: [] };
}
export function createClip(track, start = 0, length = 16) {
  return { id: uid(), name: track.instrument === 'drums' ? 'New beat' : 'New pattern', start, length, notes: [] };
}
const note = (step, pitch, duration = 1, velocity = 0.8) => ({ id: uid(), step, pitch, duration, velocity });
export function makeDemo() {
  const drums = createTrack('drums', 0); drums.name = 'Drum machine';
  const bass = createTrack('bass', 1); bass.name = 'Sub / bass'; bass.volume = 0.62;
  const keys = createTrack('keys', 2); keys.name = 'Velvet chords'; keys.volume = 0.56;
  const lead = createTrack('lead', 3); lead.name = 'Night melody'; lead.volume = 0.35; lead.cutoff = 2400;
  const pad = createTrack('pad', 4); pad.name = 'Atmosphere'; pad.volume = 0.34;
  for (let bar = 0; bar < 8; bar++) {
    const beat = createClip(drums, bar * 16); beat.name = bar >= 6 ? 'Late-night groove' : 'Pocket groove';
    [0, 6, 8, 14].forEach(s => beat.notes.push(note(s, 36, 1, s % 8 === 0 ? 0.95 : 0.7)));
    [4, 12].forEach(s => beat.notes.push(note(s, 38, 1, 0.75)));
    for (let s = 0; s < 16; s += 2) beat.notes.push(note(s, 42, 1, s % 4 === 0 ? 0.42 : 0.63));
    if (bar >= 6) beat.notes.push(note(15, 46, 1, 0.48));
    drums.clips.push(beat);
    const root = [45, 41, 48, 43][Math.floor(bar / 2)];
    const low = createClip(bass, bar * 16); low.name = ['A minor', 'F major', 'C major', 'G major'][Math.floor(bar / 2)];
    [0, 3, 6, 8, 11, 14].forEach((s, i) => low.notes.push(note(s, root - 12 + (i === 5 ? 12 : 0), i % 3 === 0 ? 2 : 1.5, 0.72)));
    bass.clips.push(low);
    if (bar % 2 === 0) {
      const chord = createClip(keys, bar * 16, 32); chord.name = ['Am9 · velvet', 'Fmaj7 · velvet', 'Cmaj7 · velvet', 'G6 · velvet'][bar / 2];
      [0, root === 45 ? 3 : 4, 7, 14].forEach((offset, i) => { chord.notes.push(note(i * 0.25, root + 12 + offset, 12, 0.62)); chord.notes.push(note(16 + i * 0.25, root + 12 + offset, 10, 0.5)); });
      keys.clips.push(chord);
      const air = createClip(pad, bar * 16, 32); air.name = 'Soft horizon';
      [0, 7, 12].forEach(offset => air.notes.push(note(0, root + 12 + offset, 30, 0.45)));
      pad.clips.push(air);
    }
    if (bar >= 2 && bar % 2 === 0) {
      const melody = createClip(lead, bar * 16, 32); melody.name = 'After hours';
      [76, 72, 71, 67, 69, 72, 76, 74].forEach((pitch, i) => melody.notes.push(note(i * 4 + (i % 2 ? 1 : 0), pitch, i === 7 ? 3 : 2, 0.6)));
      lead.clips.push(melody);
    }
  }
  return { version: 1, name: 'After hours', bpm: 112, bars: 16, loop: true, loopStart: 0, loopEnd: 128, master: 0.78, tracks: [drums, bass, keys, lead, pad], assets: {} };
}
export function makeEmpty() {
  return { version: 1, name: 'Untitled session', bpm: 120, bars: 16, loop: true, loopStart: 0, loopEnd: 64, master: 0.78, tracks: [createTrack('drums', 0), createTrack('keys', 1)], assets: {} };
}
export function activeTracks(project) {
  const solo = project.tracks.some(t => t.solo);
  return project.tracks.filter(t => !t.mute && (!solo || t.solo));
}
export function eventsAtStep(project, step) {
  const events = [];
  for (const track of activeTracks(project)) for (const clip of track.clips) {
    if (clip.assetId) {
      if (clip.start === step) events.push({ track, clip, audio: true });
    } else for (const note of clip.notes) {
      if (Math.floor(clip.start + note.step) === step && note.step < clip.length) events.push({ track, clip, note });
    }
  }
  return events;
}
export function moveClip(project, clipId, trackId, start, duplicate = false) {
  const source = project.tracks.find(t => t.clips.some(c => c.id === clipId));
  const target = project.tracks.find(t => t.id === trackId);
  if (!source || !target || source.instrument !== target.instrument) return null;
  let clip = source.clips.find(c => c.id === clipId);
  if (duplicate) { clip = structuredClone(clip); clip.id = uid(); clip.notes.forEach(n => n.id = uid()); }
  else source.clips = source.clips.filter(c => c.id !== clipId);
  clip.start = clamp(Math.round(start / 4) * 4, 0, project.bars * 16 - clip.length);
  target.clips.push(clip);
  return clip;
}
export function validateProject(value) {
  const fail = () => { throw new Error('This is not a valid Theta project.'); };
  const num = (v, min, max) => Number.isFinite(v) && v >= min && v <= max;
  if (!value || value.version !== 1 || typeof value.name !== 'string' || value.name.length > 120 || !num(value.bpm, 40, 240) || ![8, 16, 32, 64].includes(value.bars) || !num(value.master, 0, 1) || typeof value.loop !== 'boolean') fail();
  const end = value.bars * 16;
  if (!Number.isInteger(value.loopStart) || !Number.isInteger(value.loopEnd) || !num(value.loopStart, 0, end - 1) || !num(value.loopEnd, value.loopStart + 1, end)) fail();
  if (!Array.isArray(value.tracks) || value.tracks.length > 64 || !value.assets || typeof value.assets !== 'object' || Array.isArray(value.assets)) fail();
  const ids = new Set();
  const checkId = id => { if (typeof id !== 'string' || ids.has(id) || id.length > 100) fail(); ids.add(id); };
  for (const asset of Object.values(value.assets)) {
    if (!asset || typeof asset.name !== 'string' || typeof asset.data !== 'string' || !/^data:audio\/[a-zA-Z0-9.+-]+;base64,[A-Za-z0-9+/=]+$/.test(asset.data) || !num(asset.duration, 0.001, 3600)) fail();
  }
  for (const t of value.tracks) {
    checkId(t.id);
    if (!Object.hasOwn(INSTRUMENTS, t.instrument) || typeof t.name !== 'string' || t.name.length > 120 || !/^#[0-9a-f]{6}$/i.test(t.color) || !num(t.volume, 0, 1) || !num(t.pan, -1, 1) || !num(t.cutoff, 80, 16000) || typeof t.mute !== 'boolean' || typeof t.solo !== 'boolean' || !Array.isArray(t.clips) || t.clips.length > 2048) fail();
    for (const c of t.clips) {
      checkId(c.id);
      if (typeof c.name !== 'string' || c.name.length > 120 || !Number.isInteger(c.start) || !num(c.start, 0, end - 1) || !Number.isInteger(c.length) || !num(c.length, 1, end - c.start) || !Array.isArray(c.notes) || c.notes.length > 8192) fail();
      if (c.assetId && (t.instrument !== 'audio' || !Object.hasOwn(value.assets, c.assetId))) fail();
      for (const n of c.notes) {
        checkId(n.id);
        if (!num(n.step, 0, c.length - 0.001) || !Number.isInteger(n.pitch) || !num(n.pitch, 0, 127) || !num(n.duration, 0.125, c.length - n.step) || !num(n.velocity, 0.01, 1)) fail();
      }
    }
  }
  return value;
}
export function encodeWav(buffer) {
  const channels = buffer.numberOfChannels;
  const length = buffer.length * channels * 2;
  const bytes = new ArrayBuffer(44 + length);
  const view = new DataView(bytes);
  const str = (offset, text) => [...text].forEach((c, i) => view.setUint8(offset + i, c.charCodeAt(0)));
  str(0, 'RIFF'); view.setUint32(4, 36 + length, true); str(8, 'WAVE'); str(12, 'fmt ');
  view.setUint32(16, 16, true); view.setUint16(20, 1, true); view.setUint16(22, channels, true);
  view.setUint32(24, buffer.sampleRate, true); view.setUint32(28, buffer.sampleRate * channels * 2, true);
  view.setUint16(32, channels * 2, true); view.setUint16(34, 16, true); str(36, 'data'); view.setUint32(40, length, true);
  const data = Array.from({ length: channels }, (_, c) => buffer.getChannelData(c));
  for (let i = 0, offset = 44; i < buffer.length; i++) for (let c = 0; c < channels; c++, offset += 2) {
    const value = clamp(data[c][i], -1, 1); view.setInt16(offset, value < 0 ? value * 32768 : value * 32767, true);
  }
  return bytes;
}
