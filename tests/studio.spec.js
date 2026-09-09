import { test, expect, _electron as electron } from '@playwright/test';
import { mkdir, mkdtemp, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { encodeWav, makeEmpty } from '../src/model.js';

let app, page, errors, dataPath;
test.beforeEach(async () => {
  await mkdir('artifacts', { recursive: true });
  dataPath = await mkdtemp(resolve('artifacts/session-'));
  const env = { ...process.env, THEDA_TEST: '1', THEDA_TEST_DATA: dataPath }; delete env.ELECTRON_RUN_AS_NODE;
  app = await electron.launch({ args: ['.', '--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream'], env });
  await app.evaluate(({ dialog }) => { dialog.showMessageBoxSync = () => 1; });
  page = await app.firstWindow(); errors = []; page.on('pageerror', error => errors.push(error.message));
  await page.waitForSelector('.clip'); await page.waitForTimeout(200);
});
test.afterEach(async () => { await app?.close(); expect(errors).toEqual([]); });

test('desktop starts with a playable demo, transport, loop and mixer', async () => {
  await expect(page.locator('.track-row')).toHaveCount(5);
  await expect(page.locator('.piano-note')).toHaveCount(8);
  await page.screenshot({ path: 'artifacts/theda-studio.png' });
  await page.getByRole('button', { name: 'Play', exact: true }).click();
  await expect(page.locator('#engine-status')).toHaveText('Session playing');
  await expect.poll(() => page.locator('#output-bars i.lit').count()).toBeGreaterThan(0);
  await page.waitForTimeout(800); await expect(page.locator('#position')).not.toHaveText('1.1.1');
  await page.getByRole('button', { name: 'Stop', exact: true }).click();
  await expect(page.locator('#position')).toHaveText('1.1.1');
  await page.locator('#mixer-tab').click(); await expect(page.locator('.mixer-channel')).toHaveCount(6);
  await page.getByRole('button', { name: 'Mute Drum machine', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Mute Drum machine', exact: true })).toHaveAttribute('aria-pressed', 'true');
  await page.locator('#undo').click(); await expect(page.getByRole('button', { name: 'Mute Drum machine', exact: true })).toHaveAttribute('aria-pressed', 'false');
});

test('drum and piano edits, clip creation, duplication and undo persist to disk', async () => {
  await page.locator('.track-row').first().locator('.clip').first().click();
  const step = page.getByRole('button', { name: 'Kick step 2', exact: true });
  await expect(step).toHaveAttribute('aria-pressed', 'false'); await step.click(); await expect(step).toHaveAttribute('aria-pressed', 'true');
  await page.locator('#undo').click(); await expect(step).toHaveAttribute('aria-pressed', 'false');
  await page.locator('#redo').click(); await expect(step).toHaveAttribute('aria-pressed', 'true');
  await page.locator('.track-row').nth(2).locator('.clip').first().click();
  const notes = await page.locator('.piano-note').count();
  await page.locator('.note-lane[data-pitch="74"]').click({ position: { x: 330, y: 8 } });
  await expect(page.locator('.piano-note')).toHaveCount(notes + 1);
  await page.locator('.piano-note.selected').click({ button: 'right' }); await expect(page.locator('.piano-note')).toHaveCount(notes);
  await page.locator('#duplicate-clip').click(); await expect(page.locator('.track-row').nth(2).locator('.clip')).toHaveCount(5);
  const savePath = resolve(dataPath, 'test.theda');
  await app.evaluate(({ dialog }, savePath) => { dialog.showSaveDialog = async () => ({ canceled: false, filePath: savePath }); }, savePath);
  await page.locator('#save-project').click(); await expect(page.locator('#save-state')).toHaveText('SAVED TO FILE');
  const saved = JSON.parse(await readFile(savePath, 'utf8')); expect(saved.tracks[2].clips).toHaveLength(5); expect(saved.tracks[0].clips[0].notes.some(n => n.step === 1 && n.pitch === 36)).toBeTruthy();
  await page.locator('#new-project').click(); await expect(page.locator('.track-row')).toHaveCount(2);
  await app.evaluate(({ dialog }, file) => { dialog.showOpenDialog = async () => ({ canceled: false, filePaths: [file] }); }, savePath);
  await page.locator('#open-project').click(); await expect(page.locator('.track-row')).toHaveCount(5);
  await expect(page.locator('.track-row').nth(2).locator('.clip')).toHaveCount(5);
});

test('audio import creates a waveform and export renders non-silent stereo PCM', async () => {
  const samples = new Float32Array(44100); for (let i = 0; i < samples.length; i++) samples[i] = Math.sin(i / 44100 * 440 * 2 * Math.PI) * 0.25;
  const file = resolve(dataPath, 'tone.wav'); await writeFile(file, Buffer.from(encodeWav({ numberOfChannels: 1, sampleRate: 44100, length: samples.length, getChannelData: () => samples })));
  await page.locator('#audio-file').setInputFiles(file); await expect(page.locator('.track-row')).toHaveCount(6);
  await expect(page.locator('.audio-waveform path')).toHaveAttribute('d', /M/);
  const output = resolve(dataPath, 'mix.wav'); await app.evaluate(({ dialog }, output) => { dialog.showSaveDialog = async () => ({ canceled: false, filePath: output }); }, output);
  await page.locator('#export').click(); await expect(page.locator('#toast')).toContainText('Export complete', { timeout: 40000 });
  const wave = await readFile(output); expect(wave.toString('ascii', 0, 4)).toBe('RIFF'); expect(wave.readUInt16LE(22)).toBe(2); expect(wave.readUInt32LE(24)).toBe(44100);
  let peak = 0, sum = 0; for (let i = 44; i < wave.length; i += 2) { const v = wave.readInt16LE(i) / 32768; peak = Math.max(peak, Math.abs(v)); sum += v * v; }
  expect(peak).toBeGreaterThan(0.05); expect(Math.sqrt(sum / ((wave.length - 44) / 2))).toBeGreaterThan(0.01);
  expect(peak).toBeLessThan(1);
});

test('microphone recording produces an audio clip', async () => {
  await page.locator('#record').click(); await page.locator('#begin-record').click();
  await expect(page.locator('#record')).toHaveClass(/recording/);
  await page.waitForTimeout(1600); await page.locator('#record').click();
  await expect(page.locator('.track-row')).toHaveCount(6);
  await expect(page.locator('.audio-waveform')).toBeVisible();
  await expect(page.locator('#toast')).toContainText('Recording added');
});

test('empty projects allow new instruments and clips, and reject invalid imports', async () => {
  await page.locator('#new-project').click(); await expect(page.locator('.clip')).toHaveCount(0);
  await page.locator('.track-lane').first().dblclick({ position: { x: 40, y: 35 } }); await expect(page.locator('.clip')).toHaveCount(1);
  await page.locator('#add-track').click(); await page.locator('[data-add="pad"]').click(); await expect(page.locator('.track-row')).toHaveCount(3);
  const file = resolve(dataPath, 'invalid.theda'); await writeFile(file, JSON.stringify({ ...makeEmpty(), bpm: -20 }));
  await app.evaluate(({ dialog }, file) => { dialog.showOpenDialog = async () => ({ canceled: false, filePaths: [file] }); }, file);
  await page.locator('#open-project').click(); await page.locator('#confirm-replace').click();
  await expect(page.locator('#toast')).toContainText('not a valid Theda project'); await expect(page.locator('.track-row')).toHaveCount(3);
});
