import { INSTRUMENTS, uid, clamp, noteName, makeDemo, makeEmpty, createTrack, createClip, moveClip, validateProject } from './model.js';
import { AudioEngine } from './audio.js';

const $ = selector => document.querySelector(selector);
const $$ = selector => [...document.querySelectorAll(selector)];
const esc = value => String(value).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
const paths = {
  play: '<path d="m6 3 12 9-12 9z"/>', pause: '<path d="M7 4h3v16H7zM15 4h3v16h-3z"/>', stop: '<rect x="6" y="6" width="12" height="12" rx="1"/>',
  restart: '<path d="M5 5v14m14-14L8 12l11 7z"/>', loop: '<path d="M4 9V6h14l3 3-3 3M20 15v3H6l-3-3 3-3"/>',
  metronome: '<path d="m12 3-7 17h14L12 3Zm0 7 6-5M8 16h8"/>', save: '<path d="M5 3h12l4 4v14H3V3h2Zm2 0v7h10V3M7 21v-7h10v7"/>',
  export: '<path d="M12 16V3m-4 4 4-4 4 4M4 14v7h16v-7"/>', search: '<circle cx="10" cy="10" r="6"/><path d="m15 15 5 5"/>',
  keys: '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M8 5v8h3V5m3 0v8h3V5M9 13v6m6-6v6"/>',
  drums: '<ellipse cx="12" cy="8" rx="9" ry="4"/><path d="M3 8v8c0 5 18 5 18 0V8M7 12v6m10-6v6M5 3l5 4m9-4-5 4"/>',
  wave: '<path d="M2 12h3l2-7 3 14 4-16 3 14 2-5h3"/>', cloud: '<path d="M7 18a5 5 0 0 1-1-10 6 6 0 0 1 11-1 5.5 5.5 0 1 1 1 11Z"/>',
  mic: '<rect x="9" y="2" width="6" height="13" rx="3"/><path d="M5 10v2a7 7 0 0 0 14 0v-2M12 19v3m-4 0h8"/>',
  plus: '<path d="M12 5v14M5 12h14"/>', minus: '<path d="M5 12h14"/>', arrange: '<path d="M3 4h9v4H3zM8 10h13v4H8zM3 16h13v4H3z"/>',
  undo: '<path d="m8 3-5 5 5 5M3 8h10a7 7 0 0 1 0 14"/>', redo: '<path d="m16 3 5 5-5 5m5-5h-10a7 7 0 0 0 0 14"/>',
  magnet: '<path d="M4 4v10a8 8 0 0 0 16 0V4h-5v10a3 3 0 0 1-6 0V4H4Zm0 5h5m6 0h5"/>', notes: '<path d="M3 5h18M3 12h18M3 19h18M7 4v4m7 3v4m-3 3v4"/>',
  sliders: '<path d="M6 3v8m0 4v6M12 3v3m0 4v11M18 3v11m0 4v3M3 11h6v4H3zM9 6h6v4H9zM15 14h6v4h-6z"/>',
  copy: '<rect x="8" y="8" width="13" height="13" rx="2"/><path d="M16 8V3H3v13h5"/>', trash: '<path d="M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M10 10v7m4-7v7"/>', close: '<path d="m6 6 12 12M6 18 18 6"/>'
};
const icon = name => `<svg viewBox="0 0 24 24" aria-hidden="true">${paths[name] || paths.wave}</svg>`;
$$('[data-icon]').forEach(el => el.innerHTML = icon(el.dataset.icon));

let project = makeDemo();
let selectedTrack = project.tracks[2].id, selectedClip = project.tracks[2].clips[0].id, selectedNote = null;
let barWidth = 120, editorTab = 'clip', libraryTab = 'instruments', drumPage = 0;
let history = [], future = [], dirty = false, revision = 0, recovering = true, saveTimer, toastTimer, busy = false;
let recorder = null, recordStream = null, recordStart = 0, recordLimit;
const engine = new AudioEngine(() => project, updatePosition, () => { updateTransport(); if (recorder?.state === 'recording') stopRecording(); });
const getTrack = () => project.tracks.find(t => t.id === selectedTrack);
const getClip = () => getTrack()?.clips.find(c => c.id === selectedClip);
const db = value => value <= 0 ? '−∞' : (20 * Math.log10(value)).toFixed(1);

function toast(message) {
  $('#toast').textContent = message; $('#toast').classList.add('show');
  clearTimeout(toastTimer); toastTimer = setTimeout(() => $('#toast').classList.remove('show'), 3600);
  $('#status-message').textContent = message;
}
function checkpoint() { history.push(structuredClone(project)); if (history.length > 40) history.shift(); future = []; }
function changed(render = true) {
  dirty = true; revision++; $('#save-state').textContent = 'UNSAVED CHANGES';
  clearTimeout(saveTimer); saveTimer = setTimeout(() => recoverStore('write', project).catch(() => { $('#save-state').textContent = 'SAVE TO FILE'; }), 600);
  if (render) renderAll(); else { $('#undo').disabled = !history.length; $('#redo').disabled = !future.length; engine.updateMix(); }
}
function edit(action, render = true) { if (busy) return; checkpoint(); action(); changed(render); }
function renderAll() {
  $('#project-name').value = project.name; $('#bpm').value = project.bpm; $('#song-bars').value = project.bars;
  $('#track-count').textContent = `${project.tracks.length} tracks`; $('#asset-count').textContent = Object.keys(project.assets).length;
  $('#undo').disabled = !history.length; $('#redo').disabled = !future.length;
  updateTransport(); renderArrangement(); renderDetail(); renderLibrary(); engine.updateMix();
}
function updateTransport() {
  $('#play').innerHTML = icon(engine.playing ? 'pause' : 'play'); $('#play').setAttribute('aria-label', engine.playing ? 'Pause' : 'Play'); $('#play').classList.toggle('playing', engine.playing);
  $('#loop').classList.toggle('active', project.loop); $('#loop').setAttribute('aria-pressed', project.loop);
  $('#engine-status').textContent = recorder?.state === 'recording' ? 'Recording microphone' : engine.playing ? 'Session playing' : 'Ready to create';
  if (engine.context) $('#sample-rate').textContent = `${engine.context.sampleRate / 1000} kHz · Stereo`;
}
function updatePosition(step, level) {
  $('#position').innerHTML = `${Math.floor(step / 16) + 1}<span>.</span>${Math.floor(step % 16 / 4) + 1}<span>.</span>${Math.floor(step % 4) + 1}`;
  const head = $('#playhead'); if (head) head.style.left = `${220 + step * barWidth / 16}px`;
  const clip = getClip(), editorHead = $('.editor-playhead');
  if (editorHead && clip) { const pos = step - clip.start; editorHead.style.display = pos >= 0 && pos < clip.length ? 'block' : 'none'; editorHead.style.left = `${56 + pos * 24}px`; }
  $$('.drum-step').forEach(el => el.classList.toggle('current', engine.playing && clip && Math.floor(step - clip.start) === Number(el.dataset.step)));
  $$('#output-bars i').forEach((el, i) => el.classList.toggle('lit', level * 32 > i));
  $$('.channel-meter i').forEach(el => { const t = project.tracks.find(t => t.id === el.dataset.meter); el.style.height = `${Math.min(100, level * 320 * (t?.mute ? 0 : t?.volume ?? 1))}%`; });
}

function previewSvg(track, clip) {
  if (clip.assetId) return waveform(clip.assetId, 'clip-preview');
  const pitches = clip.notes.map(n => n.pitch), low = Math.min(...pitches) - 2, high = Math.max(...pitches) + 2;
  return `<svg class="clip-preview" viewBox="0 0 200 36" preserveAspectRatio="none">${clip.notes.map(n => `<rect x="${n.step / clip.length * 200}" y="${(high - n.pitch) / (high - low) * 29}" width="${Math.max(2, n.duration / clip.length * 200 - 1)}" height="${track.instrument === 'drums' ? 3 : 2}" rx=".6" fill="currentColor" opacity="${n.velocity}"/>`).join('')}</svg>`;
}
function waveform(assetId, className) {
  const buffer = engine.buffers.get(assetId);
  if (!buffer) return `<svg class="${className}" viewBox="0 0 300 100"><path d="M0 50h300" stroke="currentColor"/></svg>`;
  const samples = buffer.getChannelData(0), count = 200, stride = Math.max(1, Math.floor(samples.length / count));
  let path = '';
  for (let i = 0; i < count; i++) {
    let peak = 0; for (let j = 0; j < stride; j += Math.max(1, Math.floor(stride / 30))) peak = Math.max(peak, Math.abs(samples[Math.min(samples.length - 1, i * stride + j)]));
    const height = Math.max(1, peak * 44); path += `M${i * 1.5} ${50 - height}v${height * 2}`;
  }
  return `<svg class="${className}" viewBox="0 0 300 100" preserveAspectRatio="none"><path d="${path}" stroke="currentColor" stroke-width="1"/></svg>`;
}
function renderArrangement() {
  const scroll = $('#arrangement-scroll'), scrollLeft = scroll.scrollLeft, scrollTop = scroll.scrollTop;
  const width = project.bars * barWidth;
  $('#arrangement-grid').className = 'arrangement-grid';
  $('#arrangement-grid').style.cssText = `--bar-width:${barWidth}px;--timeline-width:${width};width:${width + 220}px;`;
  $('#arrangement-grid').innerHTML = `<div class="ruler-row"><div class="ruler-head"><span>TRACKS</span><span>M &nbsp; S</span></div><div class="ruler" style="width:${width}px">${Array.from({ length: project.bars }, (_, i) => `<div class="ruler-bar">${i + 1}</div>`).join('')}</div></div>
  <div class="loop-row"><div class="loop-head"></div><div class="loop-lane" style="width:${width}px"><div class="loop-region ${project.loop ? '' : 'disabled'}" role="button" tabindex="0" title="Edit loop region" style="left:${project.loopStart / 16 * barWidth}px;width:${(project.loopEnd - project.loopStart) / 16 * barWidth}px"><span>LOOP</span><span>↻</span></div></div></div>
  ${project.tracks.map((track, i) => `<div class="track-row" style="--track-color:${track.color}" data-track="${track.id}"><div class="track-head ${track.id === selectedTrack ? 'selected' : ''}" data-track="${track.id}"><div class="track-title"><span class="track-number">${String(i + 1).padStart(2, '0')}</span>${icon(INSTRUMENTS[track.instrument].icon)}<strong title="Double-click to rename">${esc(track.name)}</strong><div class="track-buttons"><button data-mute="${track.id}" class="${track.mute ? 'on' : ''}" aria-label="Mute ${esc(track.name)}" aria-pressed="${track.mute}">M</button><button data-solo="${track.id}" class="${track.solo ? 'on' : ''}" aria-label="Solo ${esc(track.name)}" aria-pressed="${track.solo}">S</button></div></div><div class="track-bottom"><input type="range" min="0" max="1" step="0.01" value="${track.volume}" data-volume="${track.id}" aria-label="${esc(track.name)} volume"><span>${db(track.volume)} dB</span></div></div>
  <div class="track-lane" data-lane="${track.id}" style="width:${width}px">${track.clips.map(clip => `<div class="clip ${clip.id === selectedClip ? 'selected' : ''}" data-clip="${clip.id}" draggable="true" tabindex="0" aria-label="${esc(clip.name)}, bar ${clip.start / 16 + 1}" style="left:${clip.start / 16 * barWidth + 2}px;width:${clip.length / 16 * barWidth - 4}px"><div class="clip-name">${icon(clip.assetId ? 'wave' : 'notes')}${esc(clip.name)}</div>${previewSvg(track, clip)}<div class="clip-resize" title="Drag to resize clip"></div></div>`).join('')}</div></div>`).join('')}
  <div class="empty-arrangement">A little more room for your next idea.</div><div class="playhead" id="playhead"></div>`;
  scroll.scrollLeft = scrollLeft; scroll.scrollTop = scrollTop;
  $('.ruler').onclick = event => { if (recorder) return; const rect = event.currentTarget.getBoundingClientRect(); void run(() => engine.seek(clamp(Math.floor((event.clientX - rect.left) / barWidth * 16 / 4) * 4, 0, project.bars * 16 - 1))); };
  $('.loop-region').onclick = loopDialog; $('.loop-region').onkeydown = e => { if (e.key === 'Enter') loopDialog(); };
  $$('.track-head').forEach(el => {
    el.onclick = event => { if (event.target.closest('button,input')) return; selectedTrack = el.dataset.track; selectedClip = getTrack().clips[0]?.id; selectedNote = null; renderArrangement(); renderDetail(); };
    el.querySelector('strong').ondblclick = event => { event.stopPropagation(); renameTrack(el.dataset.track); };
    el.oncontextmenu = event => { event.preventDefault(); trackDialog(el.dataset.track); };
  });
  bindMixControls($('#arrangement-grid'));
  $$('.clip').forEach(el => {
    el.onclick = event => { event.stopPropagation(); selectClip(el.dataset.clip); };
    el.onkeydown = event => { if (event.key === 'Enter') selectClip(el.dataset.clip); };
    el.ondragstart = event => { const clip = project.tracks.flatMap(t => t.clips).find(c => c.id === el.dataset.clip); event.dataTransfer.setData('application/theda-clip', JSON.stringify({ id: clip.id, offset: (event.clientX - el.getBoundingClientRect().left) / barWidth * 16, duplicate: event.altKey })); event.dataTransfer.effectAllowed = 'copyMove'; };
    el.querySelector('.clip-resize').onpointerdown = event => resizeClip(event, el);
  });
  $$('.track-lane').forEach(el => {
    el.ondblclick = event => { if (event.target.closest('.clip')) return; const track = project.tracks.find(t => t.id === el.dataset.lane); if (track.instrument === 'audio') { $('#audio-file').click(); return; }
      const start = Math.floor((event.clientX - el.getBoundingClientRect().left) / barWidth) * 16;
      edit(() => { const clip = createClip(track, start, Math.min(16, project.bars * 16 - start)); track.clips.push(clip); selectedTrack = track.id; selectedClip = clip.id; selectedNote = null; drumPage = 0; });
    };
    el.ondragover = event => { event.preventDefault(); el.classList.add('drag-over'); };
    el.ondragleave = () => el.classList.remove('drag-over');
    el.ondrop = event => {
      event.preventDefault(); el.classList.remove('drag-over');
      const payload = event.dataTransfer.getData('application/theda-clip');
      if (payload) { const data = JSON.parse(payload), start = (event.clientX - el.getBoundingClientRect().left) / barWidth * 16 - data.offset;
        const source = project.tracks.find(t => t.clips.some(c => c.id === data.id)); const target = project.tracks.find(t => t.id === el.dataset.lane);
        if (source?.instrument !== target.instrument) return toast('Move clips to a track with the same instrument.');
        edit(() => { const clip = moveClip(project, data.id, target.id, start, data.duplicate || event.altKey); selectedTrack = target.id; selectedClip = clip.id; });
      } else if (event.dataTransfer.files.length) void importFiles(event.dataTransfer.files);
      else { const instrument = event.dataTransfer.getData('application/theda-instrument'); if (Object.hasOwn(INSTRUMENTS, instrument)) addTrack(instrument); }
    };
  });
  updatePosition(engine.position, 0);
}
function selectClip(id) { selectedTrack = project.tracks.find(t => t.clips.some(c => c.id === id)).id; selectedClip = id; selectedNote = null; drumPage = 0; editorTab = 'clip'; renderArrangement(); renderDetail(); }
function resizeClip(event, el) {
  if (event.button !== 0 || busy) return; event.stopPropagation(); event.preventDefault();
  const track = project.tracks.find(t => t.clips.some(c => c.id === el.dataset.clip)), clip = track.clips.find(c => c.id === el.dataset.clip);
  const x = event.clientX, oldLength = clip.length; let nextLength = oldLength;
  const move = e => { nextLength = clamp(Math.round((oldLength + (e.clientX - x) / barWidth * 16) / 4) * 4, 4, project.bars * 16 - clip.start); el.style.width = `${nextLength / 16 * barWidth - 4}px`; };
  const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); if (nextLength !== oldLength) edit(() => { clip.length = nextLength; clip.notes = clip.notes.filter(n => n.step < nextLength); clip.notes.forEach(n => n.duration = Math.min(n.duration, nextLength - n.step)); }); };
  window.addEventListener('pointermove', move); window.addEventListener('pointerup', up, { once: true });
}
function bindMixControls(root) {
  root.querySelectorAll('[data-mute],[data-solo]').forEach(el => el.onclick = event => { event.stopPropagation(); const prop = el.dataset.mute ? 'mute' : 'solo', id = el.dataset[prop]; edit(() => { const track = project.tracks.find(t => t.id === id); track[prop] = !track[prop]; }); });
  root.querySelectorAll('[data-volume],[data-pan]').forEach(el => {
    el.onpointerdown = () => checkpoint(); el.onkeydown = e => { if (e.key.startsWith('Arrow')) checkpoint(); };
    el.oninput = () => { const prop = el.dataset.volume ? 'volume' : 'pan', id = el.dataset[prop]; const t = project.tracks.find(t => t.id === id); t[prop] = Number(el.value); changed(false); if (prop === 'volume') { const label = el.closest('.track-bottom')?.querySelector('span') || el.closest('.mixer-channel')?.querySelector('.mixer-db'); if (label) label.textContent = `${db(t.volume)} dB`; } };
    el.onchange = () => renderArrangement();
  });
}

function renderLibrary() {
  const search = $('#library-search').value.toLowerCase();
  $$('.browser-nav button').forEach(el => el.classList.toggle('selected', el.dataset.library === libraryTab));
  $('#library-label').textContent = libraryTab === 'instruments' ? 'BUILT-IN COLLECTION' : 'PROJECT AUDIO';
  if (libraryTab === 'instruments') {
    $('#library-list').innerHTML = Object.entries(INSTRUMENTS).filter(([id, p]) => id !== 'audio' && `${p.name} ${p.family}`.toLowerCase().includes(search)).map(([id, preset]) => `<div class="instrument-card" data-instrument="${id}" draggable="true" tabindex="0" title="${esc(preset.description)} Double-click to add a track."><div class="instrument-icon">${icon(preset.icon)}</div><div><strong>${preset.name}</strong><small>${preset.family} · Instrument</small></div><button class="preview-sound" aria-label="Preview ${preset.name}">▷</button></div>`).join('');
    $$('.instrument-card').forEach(el => { el.ondblclick = () => addTrack(el.dataset.instrument); el.onkeydown = e => { if (e.key === 'Enter') addTrack(el.dataset.instrument); }; el.ondragstart = e => e.dataTransfer.setData('application/theda-instrument', el.dataset.instrument); el.querySelector('.preview-sound').onclick = () => void run(() => engine.preview(createTrack(el.dataset.instrument), el.dataset.instrument === 'drums' ? 36 : el.dataset.instrument === 'bass' ? 33 : 60)); });
  } else {
    $('#library-list').innerHTML = Object.entries(project.assets).filter(([, a]) => a.name.toLowerCase().includes(search)).map(([id, a]) => `<div class="instrument-card" data-asset="${id}"><div class="instrument-icon">${icon('wave')}</div><div><strong>${esc(a.name.slice(0, 22))}</strong><small>${a.duration.toFixed(1)} sec · Audio</small></div></div>`).join('') || '<p class="drum-note">Your imported and recorded audio will live here.</p>';
    $$('[data-asset]').forEach(el => el.ondblclick = () => insertAsset(el.dataset.asset));
  }
}
function renderDetail() {
  $('#clip-tab').classList.toggle('selected', editorTab === 'clip'); $('#mixer-tab').classList.toggle('selected', editorTab === 'mixer');
  $('#clip-actions').style.display = editorTab === 'clip' && getClip() ? 'flex' : 'none';
  $('#detail-hint').textContent = editorTab === 'mixer' ? 'VOLUME · PAN · MUTE · SOLO' : 'CLICK TO DRAW · DRAG TO MOVE · RIGHT-CLICK TO ERASE';
  if (editorTab === 'mixer') return renderMixer();
  const track = getTrack(), clip = getClip();
  if (!track || !clip) { $('#detail-content').innerHTML = `<div class="empty-editor">${icon('notes')}<div>Every song starts with an idea.</div><p>Double-click an empty track lane to make your first clip.</p></div>`; return; }
  const currentNote = clip.notes.find(n => n.id === selectedNote);
  $('#detail-content').innerHTML = `<aside class="clip-inspector" style="--track-color:${track.color}"><div class="eyebrow">${clip.assetId ? 'AUDIO CLIP' : track.instrument === 'drums' ? 'DRUM PATTERN' : 'MIDI CLIP'}</div><input class="clip-title-input" id="clip-name" value="${esc(clip.name)}" maxlength="120" aria-label="Clip name"><div class="inspector-row"><span>Start</span><span class="readout">${clip.start / 16 + 1} bar</span></div><div class="inspector-row"><label for="clip-length">Length</label><input id="clip-length" type="number" min="0.25" max="${(project.bars * 16 - clip.start) / 16}" step="0.25" value="${clip.length / 16}" aria-label="Clip length in bars"></div>${track.instrument === 'drums' ? `<div class="inspector-row"><label for="drum-page">Edit bar</label><select id="drum-page">${Array.from({ length: Math.ceil(clip.length / 16) }, (_, i) => `<option value="${i}" ${i === drumPage ? 'selected' : ''}>${i + 1}</option>`).join('')}</select></div>` : ''}
  ${!clip.assetId ? `<div class="inspector-row"><label for="note-duration">Note length</label><select id="note-duration"><option value="1">1/16</option><option value="2">1/8</option><option value="4" selected>1/4</option><option value="8">1/2</option><option value="16">1 bar</option></select></div><div class="inspector-row"><label for="velocity">Velocity</label><input id="velocity" type="range" min="0.05" max="1" step="0.05" value="${currentNote?.velocity ?? 0.75}" title="${currentNote ? 'Selected note velocity' : 'New note velocity'}"></div>` : ''}
  <div class="inspector-divider"></div><div class="device-name">${icon(INSTRUMENTS[track.instrument].icon)}${INSTRUMENTS[track.instrument].name}</div>${!['audio', 'drums'].includes(track.instrument) ? `<div class="inspector-row"><label for="cutoff">Tone</label><input id="cutoff" type="range" min="80" max="16000" step="20" value="${track.cutoff}" aria-label="Filter cutoff"></div>` : '<div class="drum-note">Built in. Ready to play.</div>'}</aside><div id="note-editor" style="display:contents;--track-color:${track.color}"></div>`;
  $('#clip-name').onchange = event => edit(() => clip.name = event.target.value.trim() || 'Untitled clip');
  $('#clip-length').onchange = event => { const value = Number(event.target.value); if (!Number.isFinite(value)) return renderDetail(); edit(() => { clip.length = clamp(Math.round(value * 4) * 4, 4, project.bars * 16 - clip.start); clip.notes = clip.notes.filter(n => n.step < clip.length); clip.notes.forEach(n => n.duration = Math.min(n.duration, clip.length - n.step)); drumPage = 0; }); };
  if ($('#velocity')) { $('#velocity').onpointerdown = () => { if (currentNote) checkpoint(); }; $('#velocity').onchange = event => { if (currentNote) { currentNote.velocity = Number(event.target.value); changed(false); renderArrangement(); } }; }
  if ($('#cutoff')) { $('#cutoff').onpointerdown = () => checkpoint(); $('#cutoff').oninput = event => { track.cutoff = Number(event.target.value); changed(false); }; }
  if (clip.assetId) {
    $('#note-editor').innerHTML = `<div class="audio-editor" style="--track-color:${track.color}"><h3>${esc(project.assets[clip.assetId].name)}</h3>${waveform(clip.assetId, 'audio-waveform')}<p>${project.assets[clip.assetId].duration.toFixed(2)} seconds · Original speed · Resize the clip to trim its end.</p></div>`;
  } else if (track.instrument === 'drums') { $('#drum-page').onchange = event => { drumPage = Number(event.target.value); renderDrums(track, clip); }; renderDrums(track, clip); }
  else renderPiano(track, clip);
}
function renderDrums(track, clip) {
  const sounds = [[46, 'Open hat'], [42, 'Closed hat'], [39, 'Clap'], [38, 'Snare'], [36, 'Kick']];
  const steps = Array.from({ length: Math.min(16, clip.length - drumPage * 16) }, (_, i) => i + drumPage * 16);
  $('#note-editor').innerHTML = `<div class="drum-wrap"><div class="drum-grid"><div class="drum-ruler">${steps.map(s => `<span>${s % 4 === 0 ? s / 4 + 1 : '·'}</span>`).join('')}</div>${sounds.map(([pitch, name]) => `<div class="drum-row"><button class="drum-label" data-pitch="${pitch}">${name}<span>▷</span></button>${steps.map(step => `<button class="drum-step ${clip.notes.some(n => n.step === step && n.pitch === pitch) ? 'on' : ''}" data-step="${step}" data-pitch="${pitch}" aria-label="${name} step ${step + 1}" aria-pressed="${clip.notes.some(n => n.step === step && n.pitch === pitch)}"></button>`).join('')}</div>`).join('')}<div class="drum-note">Make a little rhythm. Click a step to turn it on or off.</div></div></div>`;
  $$('.drum-label').forEach(el => el.onclick = () => void run(() => engine.preview(track, Number(el.dataset.pitch))));
  $$('.drum-step').forEach(el => el.onclick = () => { const step = Number(el.dataset.step), pitch = Number(el.dataset.pitch), existing = clip.notes.find(n => n.step === step && n.pitch === pitch); const velocity = Number($('#velocity').value);
    edit(() => { if (existing) clip.notes = clip.notes.filter(n => n.id !== existing.id); else clip.notes.push({ id: uid(), step, pitch, duration: 1, velocity }); }, false);
    renderDrums(track, clip); renderArrangement(); if (!existing && !engine.playing) void run(() => engine.preview(track, pitch, velocity));
  });
}
function renderPiano(track, clip, keepScroll) {
  const pitches = clip.notes.map(n => n.pitch), defaultLow = track.instrument === 'bass' ? 24 : 48;
  const low = Math.max(0, Math.min(defaultLow, ...pitches)), high = Math.min(127, Math.max(defaultLow + 35, ...pitches));
  const width = Math.max(clip.length * 24, 480), rows = Array.from({ length: high - low + 1 }, (_, i) => high - i);
  $('#note-editor').innerHTML = `<div class="piano-wrap" style="--step-width:24px"><div class="piano-inner" style="width:${width + 56}px"><div class="piano-ruler">${Array.from({ length: Math.ceil(clip.length / 4) }, (_, i) => `<span style="width:96px">${Math.floor(i / 4) + 1}.${i % 4 + 1}</span>`).join('')}</div>${rows.map(pitch => `<div class="piano-row"><button class="piano-key ${[1, 3, 6, 8, 10].includes(pitch % 12) ? 'black' : ''}" data-key="${pitch}">${noteName(pitch)}</button><div class="note-lane" data-pitch="${pitch}" style="width:${width}px">${clip.notes.filter(n => n.pitch === pitch).map(n => `<div class="piano-note ${selectedNote === n.id ? 'selected' : ''}" data-note="${n.id}" title="${noteName(n.pitch)} · velocity ${Math.round(n.velocity * 100)}%" style="left:${n.step * 24 + 1}px;width:${Math.max(5, n.duration * 24 - 2)}px"><span class="note-resize"></span></div>`).join('')}</div></div>`).join('')}<div class="editor-playhead" style="display:none"></div></div></div>`;
  const wrap = $('.piano-wrap'); wrap.scrollTop = keepScroll?.top ?? Math.max(0, (high - (pitches.length ? Math.max(...pitches) : 72) - 2) * 17); wrap.scrollLeft = keepScroll?.left ?? 0;
  $$('[data-key]').forEach(el => el.onclick = () => void run(() => engine.preview(track, Number(el.dataset.key))));
  $$('.note-lane').forEach(el => el.onclick = event => {
    if (event.target.closest('.piano-note')) return;
    const step = Math.floor((event.clientX - el.getBoundingClientRect().left) / 24); if (step >= clip.length) return;
    const pitch = Number(el.dataset.pitch), duration = Math.min(Number($('#note-duration').value), clip.length - step), velocity = Number($('#velocity').value);
    const scroll = { top: wrap.scrollTop, left: wrap.scrollLeft };
    edit(() => { const n = { id: uid(), step, pitch, duration, velocity }; clip.notes.push(n); selectedNote = n.id; }, false); renderPiano(track, clip, scroll); renderArrangement();
    if (!engine.playing) void run(() => engine.preview(track, pitch, velocity));
  });
  $$('.piano-note').forEach(el => {
    el.oncontextmenu = event => { event.preventDefault(); event.stopPropagation(); const scroll = { top: wrap.scrollTop, left: wrap.scrollLeft }; edit(() => clip.notes = clip.notes.filter(n => n.id !== el.dataset.note), false); renderPiano(track, clip, scroll); renderArrangement(); };
    el.onpointerdown = event => {
      if (event.button !== 0 || busy) return; event.stopPropagation(); event.preventDefault();
      const n = clip.notes.find(n => n.id === el.dataset.note); selectedNote = n.id;
      $$('.piano-note').forEach(other => other.classList.toggle('selected', other === el)); $('#velocity').value = n.velocity;
      $('#velocity').onchange = e => edit(() => n.velocity = Number(e.target.value), false);
      const x = event.clientX, y = event.clientY, resizing = event.target.classList.contains('note-resize'); let step = n.step, pitch = n.pitch, duration = n.duration;
      const move = e => { if (resizing) { duration = clamp(n.duration + Math.round((e.clientX - x) / 24), 0.25, clip.length - n.step); el.style.width = `${duration * 24 - 2}px`; }
        else { step = clamp(n.step + Math.round((e.clientX - x) / 24), 0, clip.length - n.duration); pitch = clamp(n.pitch - Math.round((e.clientY - y) / 17), low, high); el.style.transform = `translate(${(step - n.step) * 24}px,${(n.pitch - pitch) * 17}px)`; } };
      const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); if (step !== n.step || pitch !== n.pitch || duration !== n.duration) edit(() => Object.assign(n, { step, pitch, duration }), false); renderPiano(track, clip, { top: wrap.scrollTop, left: wrap.scrollLeft }); renderArrangement(); };
      window.addEventListener('pointermove', move); window.addEventListener('pointerup', up, { once: true });
    };
  });
  updatePosition(engine.position, 0);
}
function renderMixer() {
  $('#detail-content').innerHTML = `<div class="mixer">${project.tracks.map(track => `<div class="mixer-channel" style="--track-color:${track.color}"><h3>${esc(track.name)}</h3><small>${INSTRUMENTS[track.instrument].name}</small><div class="fader-row"><input class="volume-fader" type="range" min="0" max="1" step="0.01" value="${track.volume}" data-volume="${track.id}" aria-label="${esc(track.name)} mixer volume"><div class="channel-meter"><i data-meter="${track.id}"></i></div></div><div class="mixer-db">${db(track.volume)} dB</div><div class="mixer-pan">L<input type="range" min="-1" max="1" step="0.05" value="${track.pan}" data-pan="${track.id}" aria-label="${esc(track.name)} pan">R</div><div class="track-buttons"><button data-mute="${track.id}" class="${track.mute ? 'on' : ''}">M</button><button data-solo="${track.id}" class="${track.solo ? 'on' : ''}">S</button></div></div>`).join('')}<div class="mixer-channel master-channel" style="--track-color:#c1d69a"><h3>Master</h3><small>Stereo output</small><div class="fader-row"><input id="master-volume" class="volume-fader" type="range" min="0" max="1" step="0.01" value="${project.master}" aria-label="Master volume"><div class="channel-meter"><i></i></div></div><div class="mixer-db" id="master-db">${db(project.master)} dB</div><div class="drum-note">Output limiter active</div></div></div>`;
  bindMixControls($('#detail-content')); $('#master-volume').onpointerdown = checkpoint; $('#master-volume').oninput = e => { project.master = Number(e.target.value); $('#master-db').textContent = `${db(project.master)} dB`; changed(false); };
}

function dialog(content) { $('#modal-content').innerHTML = `<button class="dialog-close" aria-label="Close dialog">${icon('close')}</button>${content}`; $('.dialog-close').onclick = () => $('#modal').close(); if (!$('#modal').open) $('#modal').showModal(); }
async function confirmReplace(action) {
  if (recorder || busy) return toast('Finish recording or exporting first.');
  if (!dirty) return action();
  dialog('<h2>Start a different session?</h2><p>Save your current session first if you want to keep these changes.</p><div class="dialog-buttons"><button id="cancel-replace">Keep working</button><button id="save-replace">Save first</button><button id="confirm-replace" class="primary">Continue without saving</button></div>');
  $('#cancel-replace').onclick = () => $('#modal').close();
  $('#save-replace').onclick = async () => { if (await saveProject()) { $('#modal').close(); action(); } };
  $('#confirm-replace').onclick = () => { $('#modal').close(); action(); };
}
function loadProject(next) {
  engine.stop(); engine.buffers.clear(); project = next; selectedTrack = project.tracks[0]?.id; selectedClip = project.tracks[0]?.clips[0]?.id; selectedNote = null;
  history = []; future = []; dirty = false; revision++; $('#save-state').textContent = 'LOCAL SESSION'; renderAll();
  void recoverStore('write', project).catch(() => {});
  void engine.loadAssets().then(() => { renderArrangement(); renderDetail(); }).catch(error => toast(`Some audio could not be decoded: ${error.message}`));
}
function addTrack(instrument) {
  if (project.tracks.length >= 64) return toast('This session supports up to 64 tracks.');
  if (recorder) return toast('Finish recording before adding a track.');
  engine.stop(false);
  edit(() => { const track = createTrack(instrument, project.tracks.length); project.tracks.push(track); selectedTrack = track.id; selectedClip = null; selectedNote = null; });
  $('#modal').close(); toast(`${INSTRUMENTS[instrument].name} added. Double-click its lane to create a clip.`);
}
function addTrackDialog() { dialog(`<h2>Make room for a new sound.</h2><p>Choose an instrument or bring in your own audio.</p><div class="track-options">${Object.entries(INSTRUMENTS).map(([id, preset]) => `<button class="track-option" data-add="${id}">${icon(preset.icon)}${preset.name}<small>${preset.family}</small></button>`).join('')}</div>`); $$('[data-add]').forEach(el => el.onclick = () => addTrack(el.dataset.add)); }
function renameTrack(id) {
  const track = project.tracks.find(t => t.id === id);
  dialog(`<h2>Name your track</h2><div class="loop-inputs"><label>Track name<input id="track-name" style="width:300px" maxlength="120" value="${esc(track.name)}"></label></div><div class="dialog-buttons"><button id="rename-confirm" class="primary">Rename</button></div>`);
  $('#rename-confirm').onclick = () => { edit(() => track.name = $('#track-name').value.trim() || 'Untitled track'); $('#modal').close(); }; $('#track-name').focus(); $('#track-name').select();
}
function trackDialog(id) {
  const track = project.tracks.find(t => t.id === id);
  dialog(`<h2>${esc(track.name)}</h2><p>${track.clips.length} clips · ${INSTRUMENTS[track.instrument].name}</p><div class="dialog-buttons"><button id="rename-track">Rename</button><button id="remove-track">Delete track</button></div>`);
  $('#rename-track').onclick = () => renameTrack(id); $('#remove-track').onclick = () => { if (recorder) return toast('Finish recording first.'); engine.stop(false); edit(() => { project.tracks = project.tracks.filter(t => t.id !== id); if (selectedTrack === id) { selectedTrack = project.tracks[0]?.id; selectedClip = null; } }); $('#modal').close(); };
}
function loopDialog() {
  if (recorder) return;
  dialog(`<h2>A little repetition goes a long way.</h2><p>Set the bars you want to loop. The end bar is included.</p><div class="loop-inputs"><label>First bar<input id="loop-start" type="number" min="1" max="${project.bars}" value="${Math.floor(project.loopStart / 16) + 1}"></label><label>Last bar<input id="loop-end" type="number" min="1" max="${project.bars}" value="${Math.ceil(project.loopEnd / 16)}"></label></div><div class="dialog-buttons"><button id="set-loop" class="primary">Set loop</button></div>`);
  $('#set-loop').onclick = () => { const start = Number($('#loop-start').value), end = Number($('#loop-end').value); if (!Number.isInteger(start) || !Number.isInteger(end) || start < 1 || end < start || end > project.bars) return toast('Choose a valid range within your arrangement.'); engine.stop(false); edit(() => { project.loopStart = (start - 1) * 16; project.loopEnd = end * 16; project.loop = true; }); $('#modal').close(); };
}
function duplicateClip() {
  const track = getTrack(), clip = getClip(); if (!clip) return;
  if (clip.start + clip.length * 2 > project.bars * 16) return toast('Increase the song length to make room for a duplicate.');
  edit(() => { const next = moveClip(project, clip.id, track.id, clip.start + clip.length, true); selectedClip = next.id; selectedNote = null; });
}
function deleteSelection(clipOnly = false) {
  const clip = getClip(); if (!clip) return;
  edit(() => { if (selectedNote && !clipOnly) { clip.notes = clip.notes.filter(n => n.id !== selectedNote); selectedNote = null; } else { getTrack().clips = getTrack().clips.filter(c => c.id !== selectedClip); selectedClip = null; selectedNote = null; } });
}
function undo(redo = false) {
  if (recorder || busy) return; const stack = redo ? future : history; if (!stack.length) return;
  engine.stop(false); (redo ? history : future).push(structuredClone(project)); project = stack.pop(); selectedNote = null; changed();
  if (!getTrack()) { selectedTrack = project.tracks[0]?.id; selectedClip = null; renderAll(); }
}
async function run(action) { try { return await action(); } catch (error) { console.error(error); toast(error.message || 'Something went wrong. Please try again.'); } }
function download(name, data, type) { const url = URL.createObjectURL(new Blob([data], { type })), link = document.createElement('a'); link.href = url; link.download = name; link.click(); setTimeout(() => URL.revokeObjectURL(url), 10000); }
async function saveProject() {
  try {
    const atRevision = revision, content = JSON.stringify(project);
    if (window.desktop) { if (!await window.desktop.saveProject(project.name, content)) return false; }
    else download(`${project.name}.theda`, content, 'application/json');
    if (revision === atRevision) { dirty = false; $('#save-state').textContent = 'SAVED TO FILE'; }
    toast('Project saved. Your sounds and audio are included.'); return true;
  } catch (error) { toast(`Could not save: ${error.message}`); return false; }
}
async function openProject() {
  await confirmReplace(async () => {
    if (window.desktop) await run(async () => { const content = await window.desktop.openProject(); if (content) { const next = validateProject(JSON.parse(content)); loadProject(next); toast('Project opened.'); } });
    else $('#project-file').click();
  });
}
async function exportAudio() {
  if (busy || recorder) return toast('Finish the current recording or export first.');
  busy = true; $('#export').disabled = true; $('#export').textContent = 'Rendering…'; toast('Rendering your full arrangement to a stereo WAV…');
  try { const name = project.name, bytes = await engine.export();
    if (window.desktop) { const result = await window.desktop.exportAudio(name, new Uint8Array(bytes)); if (!result) return; }
    else download(`${name}.wav`, bytes, 'audio/wav');
    toast('Export complete. 44.1 kHz · 16-bit stereo WAV.');
  } catch (error) { toast(`Export failed: ${error.message}`); }
  finally { busy = false; $('#export').disabled = false; $('#export').innerHTML = `${icon('export')}Export audio`; }
}
function blobData(blob) { return new Promise((resolve, reject) => { const reader = new FileReader(); reader.onload = () => resolve(reader.result); reader.onerror = reject; reader.readAsDataURL(blob); }); }
async function importFiles(files) {
  if (recorder || busy) return toast('Finish recording or exporting first.');
  for (const file of files) {
    if (project.tracks.length >= 64) { toast('This session supports up to 64 tracks.'); break; }
    await run(async () => {
      if (file.size > 40_000_000) throw new Error('Choose an audio file smaller than 40 MB.');
      toast(`Importing ${file.name}…`);
      await engine.init(); const bytes = await file.arrayBuffer(); const buffer = await engine.context.decodeAudioData(bytes);
      const type = /^audio\/[a-zA-Z0-9.+-]+$/.test(file.type) ? file.type : 'audio/wav';
      const asset = { name: file.name.slice(0, 120), data: await blobData(new Blob([await file.arrayBuffer()], { type })), duration: buffer.duration };
      const id = uid(); engine.buffers.set(id, buffer); engine.stop(false);
      edit(() => { project.assets[id] = asset; insertAssetInternal(id); }); libraryTab = 'samples'; renderLibrary(); toast(`${file.name} added to the arrangement.`);
    });
  }
  $('#audio-file').value = '';
}
function insertAssetInternal(id, start = Math.floor(engine.position / 16) * 16) {
  start = clamp(start, 0, project.bars * 16 - 1);
  const asset = project.assets[id], track = createTrack('audio', project.tracks.length); track.name = asset.name.replace(/\.[^.]+$/, '').slice(0, 120);
  const needed = start + Math.ceil(asset.duration / (60 / project.bpm / 4));
  if (needed > project.bars * 16) project.bars = [8, 16, 32, 64].find(b => b * 16 >= needed) || 64;
  const clip = createClip(track, start, Math.min(project.bars * 16 - start, Math.max(1, needed - start))); clip.name = track.name; clip.assetId = id;
  track.clips.push(clip); project.tracks.push(track); selectedTrack = track.id; selectedClip = clip.id; editorTab = 'clip';
  if (needed > 1024) toast('Audio exceeds 64 bars; the clip has been trimmed to the arrangement.');
}
function insertAsset(id) { if (project.tracks.length >= 64 || recorder) return; engine.stop(false); edit(() => insertAssetInternal(id)); }
async function recordAudio() {
  if (recorder) return stopRecording();
  if (busy || project.tracks.length >= 64) return toast('Finish exporting or remove a track before recording.');
  if (!navigator.mediaDevices?.getUserMedia) return toast('Microphone recording requires the desktop app or localhost.');
  dialog('<h2>Capture something real.</h2><p>Record your microphone onto a new audio track, starting at the playhead. Use headphones to keep the backing track out of your recording.</p><p>The loop will turn off while recording. Press Record or Stop to finish.</p><div class="dialog-buttons"><button id="begin-record" class="primary">Start recording</button></div>');
  $('#begin-record').onclick = () => { $('#modal').close(); void run(beginRecording); };
}
async function beginRecording() {
  try {
    recordStream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false }, video: false });
    engine.stop(false); recordStart = Math.floor(engine.position); if (recordStart >= project.bars * 16) recordStart = 0;
    edit(() => { project.loop = false; });
    engine.position = recordStart;
    const chunks = []; recorder = new MediaRecorder(recordStream);
    recorder.ondataavailable = e => { if (e.data.size) chunks.push(e.data); };
    recorder.onstop = () => {
      const mimeType = recorder.mimeType.split(';')[0] || 'audio/webm'; recorder = null; recordStream.getTracks().forEach(t => t.stop()); recordStream = null; clearTimeout(recordLimit);
      $('#record').classList.remove('recording'); updateTransport();
      void run(async () => {
        const blob = new Blob(chunks, { type: mimeType }); if (blob.size < 100) return toast('Recording was too short. Try again.');
        const data = await blobData(blob), buffer = await engine.context.decodeAudioData(await blob.arrayBuffer());
        const id = uid(); engine.buffers.set(id, buffer);
        edit(() => { project.assets[id] = { name: `Recording ${Object.keys(project.assets).length + 1}`, duration: buffer.duration, data }; insertAssetInternal(id, recordStart); });
        libraryTab = 'samples'; renderLibrary(); toast('Recording added. Save your project to keep it.');
      });
    };
    recorder.start(100); $('#record').classList.add('recording'); await engine.play(); updateTransport();
    recordLimit = setTimeout(stopRecording, (project.bars * 16 - recordStart) * 60 / project.bpm / 4 * 1000);
  } catch (error) { recordStream?.getTracks().forEach(t => t.stop()); recordStream = null; recorder = null; $('#record').classList.remove('recording'); throw new Error(`Could not record: ${error.message}`); }
}
function stopRecording() { if (recorder?.state === 'recording') { recorder.stop(); engine.stop(false); } }
async function recoverStore(mode, value) {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open('theda-recovery', 1);
    request.onupgradeneeded = () => request.result.createObjectStore('session');
    request.onerror = () => reject(request.error);
    request.onsuccess = () => { const database = request.result, transaction = database.transaction('session', mode === 'write' ? 'readwrite' : 'readonly'); const store = transaction.objectStore('session'); const operation = mode === 'write' ? store.put(value, 'current') : store.get('current'); let result;
      operation.onsuccess = () => result = operation.result; transaction.oncomplete = () => { database.close(); resolve(result); }; transaction.onerror = () => { database.close(); reject(transaction.error); };
    };
  });
}
async function togglePlayback() { if (recorder) return stopRecording(); if (engine.playing) engine.stop(false); else await engine.play(); updateTransport(); }
$('#play').onclick = () => void run(togglePlayback);
$('#stop').onclick = () => { if (recorder) stopRecording(); engine.stop(); updateTransport(); };
$('#restart').onclick = () => { if (!recorder) void run(() => engine.seek(0)); };
$('#record').onclick = () => void recordAudio();
$('#loop').onclick = () => { if (recorder) return; edit(() => project.loop = !project.loop); };
$('#metronome').onclick = () => { engine.metronome = !engine.metronome; $('#metronome').classList.toggle('active', engine.metronome); $('#metronome').setAttribute('aria-pressed', engine.metronome); };
$('#project-name').onchange = e => edit(() => project.name = e.target.value.trim() || 'Untitled session');
$('#bpm').onchange = e => { if (recorder || busy) { e.target.value = project.bpm; return; } const value = Number(e.target.value); if (!Number.isFinite(value)) return renderAll(); const wasPlaying = engine.playing; engine.stop(false); edit(() => project.bpm = clamp(Math.round(value), 40, 240)); if (wasPlaying) void run(async () => { await engine.play(); updateTransport(); }); };
$('#song-bars').onchange = e => {
  if (recorder || busy) return renderAll(); const bars = Number(e.target.value); const lastClip = Math.max(0, ...project.tracks.flatMap(t => t.clips.map(c => c.start + c.length)));
  if (bars * 16 < lastClip) { e.target.value = project.bars; return toast('Move or shorten clips beyond that length first.'); }
  engine.stop(false); edit(() => { project.bars = bars; project.loopEnd = Math.min(project.loopEnd, bars * 16); project.loopStart = Math.min(project.loopStart, project.loopEnd - 16); });
};
$('#zoom-in').onclick = () => { barWidth = Math.min(240, barWidth + 24); $('#zoom-label').textContent = `${Math.round(barWidth / 120 * 100)}%`; renderArrangement(); };
$('#zoom-out').onclick = () => { barWidth = Math.max(48, barWidth - 24); $('#zoom-label').textContent = `${Math.round(barWidth / 120 * 100)}%`; renderArrangement(); };
$('#clip-tab').onclick = () => { editorTab = 'clip'; renderDetail(); }; $('#mixer-tab').onclick = () => { editorTab = 'mixer'; renderDetail(); };
$('#duplicate-clip').onclick = duplicateClip; $('#delete-clip').onclick = () => deleteSelection(true);
$('#undo').onclick = () => undo(); $('#redo').onclick = () => undo(true);
$('#add-track').onclick = addTrackDialog; $('#library-search').oninput = renderLibrary;
$$('[data-library]').forEach(el => el.onclick = () => { libraryTab = el.dataset.library; renderLibrary(); });
$('#import-audio').onclick = () => $('#audio-file').click(); $('#audio-file').onchange = e => void importFiles(e.target.files);
$('#save-project').onclick = () => void saveProject(); $('#open-project').onclick = () => void openProject(); $('#export').onclick = () => void exportAudio();
$('#new-project').onclick = () => void confirmReplace(() => { loadProject(makeEmpty()); toast('A fresh session. Make it yours.'); });
$('#load-demo').onclick = () => void confirmReplace(() => { loadProject(makeDemo()); toast('After hours · demo session loaded. Press Space to play.'); });
$('#project-file').onchange = event => void run(async () => { const file = event.target.files[0]; if (!file) return; if (file.size > 150_000_000) throw new Error('Project exceeds the 150 MB limit.'); loadProject(validateProject(JSON.parse(await file.text()))); event.target.value = ''; toast('Project opened.'); });
$('#help').onclick = () => dialog('<h2>Stay in the flow.</h2><div class="shortcuts"><span>Play / pause</span><kbd>Space</kbd><span>Return to start</span><kbd>Home</kbd><span>Save / open / new</span><kbd>Ctrl + S / O / N</kbd><span>Undo / redo</span><kbd>Ctrl + Z / Shift + Z</kbd><span>Duplicate selected clip</span><kbd>Ctrl + D</kbd><span>Delete selected note or clip</span><kbd>Delete</kbd><span>Toggle looping</span><kbd>L</kbd><span>Search instruments</span><kbd>/</kbd><span>Copy a clip while dragging</span><kbd>Alt + drag</kbd><span>Erase a piano roll note</span><kbd>Right-click</kbd><span>Rename / delete a track</span><kbd>Right-click track</kbd></div>');
document.addEventListener('keydown', event => {
  if ($('#modal').open || event.target.closest('input,select,textarea') || recovering) return;
  const mod = event.ctrlKey || event.metaKey;
  if (event.code === 'Space') { event.preventDefault(); if (!event.repeat) void run(togglePlayback); }
  else if (mod && event.key.toLowerCase() === 's') { event.preventDefault(); void saveProject(); }
  else if (mod && event.key.toLowerCase() === 'o') { event.preventDefault(); void openProject(); }
  else if (mod && event.key.toLowerCase() === 'n') { event.preventDefault(); $('#new-project').click(); }
  else if (mod && event.key.toLowerCase() === 'z') { event.preventDefault(); undo(event.shiftKey); }
  else if (mod && event.key.toLowerCase() === 'd') { event.preventDefault(); duplicateClip(); }
  else if (event.key === 'Delete' || event.key === 'Backspace') { event.preventDefault(); deleteSelection(); }
  else if (event.key === 'Home') { event.preventDefault(); $('#restart').click(); }
  else if (event.key.toLowerCase() === 'l') $('#loop').click();
  else if (event.key === '/') { event.preventDefault(); $('#library-search').focus(); }
  else if (event.key === '?') $('#help').click();
});
window.addEventListener('beforeunload', event => { if (dirty || recorder) { event.preventDefault(); event.returnValue = ''; } });
document.addEventListener('dragover', event => { if (event.dataTransfer.types.includes('Files')) event.preventDefault(); });
document.addEventListener('drop', event => { if (event.dataTransfer.files.length && !event.target.closest('.track-lane')) { event.preventDefault(); void importFiles(event.dataTransfer.files); } });
renderAll();
try {
  const recovery = await recoverStore('read');
  if (recovery) { project = validateProject(recovery); selectedTrack = project.tracks[0]?.id; selectedClip = project.tracks[0]?.clips[0]?.id; renderAll(); toast('Your last session is restored.'); }
} catch { toast('Recovery was unavailable. A fresh demo is ready.'); }
finally { recovering = false; }
