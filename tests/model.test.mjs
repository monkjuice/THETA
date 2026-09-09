import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeDemo, makeEmpty, validateProject, activeTracks, eventsAtStep, moveClip, createTrack, encodeWav } from '../src/model.js';

test('demo and empty sessions are valid and survive a JSON round trip', () => {
  for (const project of [makeDemo(), makeEmpty()]) assert.deepEqual(validateProject(JSON.parse(JSON.stringify(project))), project);
});
test('demo has scheduled voices at the beginning and no events beyond its clips', () => {
  const p = makeDemo(); assert.ok(eventsAtStep(p, 0).length >= 9); assert.equal(eventsAtStep(p, 128).length, 0);
});
test('solo and mute affect every event in the song', () => {
  const p = makeDemo(); p.tracks[1].solo = true;
  assert.deepEqual(activeTracks(p).map(t => t.id), [p.tracks[1].id]);
  assert.ok(eventsAtStep(p, 0).every(e => e.track.id === p.tracks[1].id));
  p.tracks[1].mute = true; assert.equal(eventsAtStep(p, 0).length, 0);
});
test('moving clips snaps to beats and clamps to the arrangement', () => {
  const p = makeDemo(), track = p.tracks[0], clip = track.clips[0];
  moveClip(p, clip.id, track.id, 19); assert.equal(clip.start, 20);
  moveClip(p, clip.id, track.id, 9999); assert.equal(clip.start, p.bars * 16 - clip.length);
  moveClip(p, clip.id, track.id, -100); assert.equal(clip.start, 0);
  validateProject(p);
});
test('duplication creates independent note IDs and preserves the source', () => {
  const p = makeDemo(), track = p.tracks[0], source = track.clips[0];
  const result = moveClip(p, source.id, track.id, 16, true);
  assert.notEqual(result.id, source.id); assert.notEqual(result.notes[0].id, source.notes[0].id); assert.equal(source.start, 0);
  result.notes[0].pitch = 60; assert.equal(source.notes[0].pitch, 36); validateProject(p);
});
test('clips cannot be moved onto an incompatible instrument', () => {
  const p = makeDemo(), clip = p.tracks[0].clips[0];
  assert.equal(moveClip(p, clip.id, p.tracks[1].id, 0), null); assert.ok(p.tracks[0].clips.includes(clip));
});
test('validation rejects malformed, out-of-bounds, and unsafe imported projects', () => {
  const mutations = [p => p.bpm = 0, p => p.loopEnd = p.loopStart, p => p.tracks[0].color = 'red;position:fixed', p => p.tracks[0].instrument = '__proto__', p => p.tracks[0].clips[0].start = -1, p => p.tracks[0].clips[0].notes[0].duration = 10000, p => p.tracks[0].clips[0].notes[0].velocity = NaN, p => p.tracks[1].id = p.tracks[0].id, p => p.assets.bad = { name: 'bad', duration: 1, data: 'https://example.com/track.wav' }];
  for (const change of mutations) { const p = makeDemo(); change(p); assert.throws(() => validateProject(p)); }
});
test('imported audio events appear at their clip start', () => {
  const p = makeEmpty(), track = createTrack('audio'); p.tracks.push(track);
  p.assets.sample = { name: 'sample', duration: 1, data: 'data:audio/wav;base64,AAAA' };
  track.clips.push({ id: 'audio-clip', name: 'Audio', start: 16, length: 16, notes: [], assetId: 'sample' });
  assert.equal(eventsAtStep(p, 16)[0].audio, true); assert.equal(eventsAtStep(p, 17).length, 0); validateProject(p);
});
test('WAV output has correct stereo PCM headers and clamps out-of-range samples', () => {
  const channels = [new Float32Array([-2, 0, 2]), new Float32Array([1, 0.5, -1])];
  const buffer = encodeWav({ numberOfChannels: 2, sampleRate: 44100, length: 3, getChannelData: c => channels[c] });
  const bytes = new DataView(buffer); assert.equal(buffer.byteLength, 56); assert.equal(bytes.getUint16(22, true), 2); assert.equal(bytes.getUint32(24, true), 44100); assert.equal(bytes.getUint32(40, true), 12);
  assert.equal(bytes.getInt16(44, true), -32768); assert.equal(bytes.getInt16(46, true), 32767); assert.equal(bytes.getInt16(52, true), 32767);
});
