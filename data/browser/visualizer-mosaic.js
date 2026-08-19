// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

// Splits an image into a grid of tiles and animates each tile slightly in 3D,
// driven by its own band of the 32-band spectrum: gentle position drift,
// depth ("zoom"), and a beat-triggered shatter/settle impulse.
//
// Plain CSS 3D transforms on background-image tiles, not three.js/WebGL: a
// texture built from a file:// image always taints a WebGL canvas in
// Chromium, including OBS's embedded browser, with no client-side workaround.
// Displaying the same image as a CSS background is unaffected by that rule.

const stage = document.getElementById('stage');
const grid = document.getElementById('grid');
const status = document.getElementById('status');
const tempo = document.getElementById('tempo');
const bpmValue = document.getElementById('bpm-value');

const parameters = new URLSearchParams(window.location.search);

function clampInt(value, fallback, min, max) {
  const parsed = parseInt(value, 10);
  return Number.isFinite(parsed) ? Math.min(max, Math.max(min, parsed)) : fallback;
}

const cols = clampInt(parameters.get('cols'), 8, 2, 16);
const rows = clampInt(parameters.get('rows'), 5, 2, 16);
const imagePath = parameters.get('image');

function setStatus(connected) {
  status.textContent = connected ? `Connected · ${ART.transportLabel}` : 'Disconnected';
  status.className = `status ${connected ? 'connected' : 'disconnected'}`;
}

// --- Audio state, smoothed the same way visualizer-runtime.js does for the
// three.js visualizers, kept local here since this page has no WebGL renderer. ---
const target = { rms: 0, peak: 0, bass: 0, mid: 0, high: 0 };
const level = { ...target };
const spectrum = new Float32Array(32);
let beat = 0;
let transient = 0;

const scaleAudio = (value, gain) => Math.min(1, Math.max(0, Number(value) * gain));
const smooth = (current, next, attack = 0.48, release = 0.1) =>
  current + (next - current) * (next > current ? attack : release);

function updateAudio(data) {
  if (data.version !== 1 || !data.bands) return;
  setStatus(true);
  target.rms = scaleAudio(data.rms, 3.5);
  target.peak = scaleAudio(data.peak, 2.4);
  target.bass = scaleAudio(data.bands.bass, 4.8);
  target.mid = scaleAudio(data.bands.mid, 6.5);
  target.high = scaleAudio(data.bands.high, 11);
  if (Array.isArray(data.fft32)) {
    data.fft32.slice(0, spectrum.length).forEach((value, index) => {
      spectrum[index] = scaleSpectrum(value, 14);
    });
  }
  if (data.beat?.detected) beat = Math.max(beat, 0.5 + Number(data.beat.strength) * 0.7);
  if (data.transient?.detected)
    transient = Math.max(transient, 0.35 + Number(data.transient.strength) * 0.65);
  if (data.tempo) {
    const bpm = Number(data.tempo.bpm);
    bpmValue.textContent = Number.isFinite(bpm) && bpm > 0 ? bpm.toFixed(1) : '—';
    tempo.classList.toggle('locked', Boolean(data.tempo.locked));
  }
}

// --- Source image -------------------------------------------------------
function makePlaceholderImage() {
  const canvas = document.createElement('canvas');
  canvas.width = 640;
  canvas.height = 400;
  const context = canvas.getContext('2d');
  const gradient = context.createLinearGradient(0, 0, canvas.width, canvas.height);
  gradient.addColorStop(0, '#0f172a');
  gradient.addColorStop(1, '#1e1b4b');
  context.fillStyle = gradient;
  context.fillRect(0, 0, canvas.width, canvas.height);
  context.textAlign = 'center';
  context.fillStyle = '#a78bfa';
  context.font = '600 26px Inter, "Segoe UI", sans-serif';
  context.fillText('Add ?image=yourphoto.jpg', canvas.width / 2, canvas.height / 2 - 12);
  context.fillStyle = '#67e8f9';
  context.font = '400 17px Inter, "Segoe UI", sans-serif';
  context.fillText('place the file next to this page in the browser folder', canvas.width / 2, canvas.height / 2 + 20);
  // A canvas drawn locally is never tainted, unlike one built from a loaded file.
  return { src: canvas.toDataURL('image/png'), aspect: canvas.width / canvas.height };
}

function loadSourceImage(path) {
  return new Promise(resolve => {
    if (!path) {
      resolve(makePlaceholderImage());
      return;
    }
    const image = new Image();
    image.onload = () => resolve({ src: path, aspect: image.naturalWidth / image.naturalHeight });
    image.onerror = error => {
      console.error(`ART mosaic: failed to load "${path}"`, error);
      resolve(makePlaceholderImage());
    };
    image.src = path;
  });
}

// --- Tile grid ------------------------------------------------------------
let tiles = [];
let imageSrc = null;
let imageAspect = 1;

function buildGrid() {
  grid.innerHTML = '';
  tiles = [];

  const containerWidth = stage.clientWidth;
  const containerHeight = stage.clientHeight;
  const containerAspect = containerWidth / containerHeight;

  // Cover-fit size of the full image if it were scaled to fill the grid area,
  // shared by every tile so their background-position slices line up exactly.
  let bgWidth, bgHeight;
  if (imageAspect > containerAspect) {
    bgHeight = containerHeight;
    bgWidth = containerHeight * imageAspect;
  } else {
    bgWidth = containerWidth;
    bgHeight = containerWidth / imageAspect;
  }
  const offsetX = (containerWidth - bgWidth) / 2;
  const offsetY = (containerHeight - bgHeight) / 2;

  const tileWidth = containerWidth / cols;
  const tileHeight = containerHeight / rows;
  const insetX = tileWidth * 0.035;
  const insetY = tileHeight * 0.035;

  for (let row = 0; row < rows; row += 1) {
    for (let col = 0; col < cols; col += 1) {
      const left = col * tileWidth;
      const top = row * tileHeight;
      const tile = document.createElement('div');
      tile.className = 'tile';
      tile.style.left = `${left + insetX}px`;
      tile.style.top = `${top + insetY}px`;
      tile.style.width = `${tileWidth - insetX * 2}px`;
      tile.style.height = `${tileHeight - insetY * 2}px`;
      tile.style.backgroundImage = `url("${imageSrc}")`;
      tile.style.backgroundSize = `${bgWidth}px ${bgHeight}px`;
      tile.style.backgroundPosition = `${offsetX - left}px ${offsetY - top}px`;
      grid.appendChild(tile);

      tiles.push({
        el: tile,
        band: (row * cols + col) % 32,
        phase: Math.random() * Math.PI * 2,
        speed: 0.45 + Math.random() * 0.55,
        impulse: 0
      });
    }
  }
}

loadSourceImage(imagePath).then(({ src, aspect }) => {
  imageSrc = src;
  imageAspect = aspect;
  buildGrid();

  let resizeTimer = null;
  window.addEventListener('resize', () => {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(buildGrid, 120);
  });

  requestAnimationFrame(animate);
});

// --- Animation --------------------------------------------------------
let previousBeat = 0;
let lastTime = performance.now();

function animate(now) {
  const delta = Math.min((now - lastTime) / 1000, 0.05);
  lastTime = now;
  const elapsed = now / 1000;

  for (const name of Object.keys(level)) level[name] = smooth(level[name], target[name]);
  beat *= Math.pow(0.045, delta);
  transient *= Math.pow(0.02, delta);

  if (beat > previousBeat + 0.25) {
    for (const tile of tiles) tile.impulse = Math.max(tile.impulse, 0.6 + Math.random() * 0.4);
  }
  previousBeat = beat;

  for (const tile of tiles) {
    tile.impulse *= Math.pow(0.05, delta);
    const fft = spectrum[tile.band];
    const bob = Math.sin(elapsed * tile.speed + tile.phase) * (1.5 + level.mid * 3);
    const drift = Math.cos(elapsed * tile.speed * 0.7 + tile.phase) * (1 + level.high * 2.2);
    const zoom = 1 + fft * 0.22 + level.rms * 0.06 + tile.impulse * 0.14;
    const depth = fft * 40 + level.bass * 14 + tile.impulse * 120;
    const spin = Math.sin(elapsed * 0.3 + tile.phase) * 0.8 * (1 + tile.impulse);
    const kickX = Math.cos(tile.phase) * tile.impulse * 9;
    const kickY = Math.sin(tile.phase) * tile.impulse * 9;

    tile.el.style.transform =
      `translate3d(${(drift + kickX).toFixed(2)}px, ${(bob + kickY).toFixed(2)}px, ${depth.toFixed(2)}px) scale(${zoom.toFixed(3)}) rotateZ(${spin.toFixed(2)}deg)`;
    tile.el.style.opacity = ((0.88 + level.rms * 0.12 + transient * 0.06) * 0.5).toFixed(3);
  }

  requestAnimationFrame(animate);
}

ART.on('frame', updateAudio);
ART.connect();
