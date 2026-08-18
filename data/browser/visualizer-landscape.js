// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

const canvas = document.getElementById('visualizer');
const status = document.getElementById('status');
const tempo = document.getElementById('tempo');
const bpmValue = document.getElementById('bpm-value');
const configuredPort = Number(new URLSearchParams(window.location.search).get('port')) || 8765;

const renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true, powerPreference: 'high-performance' });
renderer.setClearColor(0x000000, 0);
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
renderer.outputColorSpace = THREE.SRGBColorSpace;

const scene = new THREE.Scene();
scene.fog = new THREE.FogExp2(0x030817, 0.052);

const camera = new THREE.PerspectiveCamera(47, 1, 0.1, 80);
camera.position.set(0, 6.8, 10.5);
camera.lookAt(0, 0.2, -7);

const columns = 32;
const rows = 40;
const width = 15;
const depth = 22;
const geometry = new THREE.PlaneGeometry(width, depth, columns - 1, rows - 1);
geometry.rotateX(-Math.PI / 2);
geometry.translate(0, -1.4, -8);
const basePositions = geometry.attributes.position.array.slice();

const landscapeMaterial = new THREE.MeshBasicMaterial({
  color: 0x22d3ee,
  wireframe: true,
  transparent: true,
  opacity: 0.68,
  blending: THREE.AdditiveBlending,
  depthWrite: false
});
const landscape = new THREE.Mesh(geometry, landscapeMaterial);
scene.add(landscape);

const glowMaterial = new THREE.MeshBasicMaterial({
  color: 0x7c3aed,
  transparent: true,
  opacity: 0.08,
  blending: THREE.AdditiveBlending,
  depthWrite: false,
  side: THREE.DoubleSide
});
const glowLandscape = new THREE.Mesh(geometry, glowMaterial);
glowLandscape.position.y = -0.05;
glowLandscape.scale.y = 0.96;
scene.add(glowLandscape);

const horizon = new THREE.Mesh(
  new THREE.PlaneGeometry(19, 0.035),
  new THREE.MeshBasicMaterial({ color: 0xf0abfc, transparent: true, opacity: 0.7, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide })
);
horizon.position.set(0, -1.27, -18.7);
scene.add(horizon);

const shockwave = new THREE.Mesh(
  new THREE.PlaneGeometry(width, 0.055),
  new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide })
);
shockwave.rotation.x = -Math.PI / 2;
shockwave.position.set(0, -1.1, 1.8);
scene.add(shockwave);

const particleCount = 520;
const particlePositions = new Float32Array(particleCount * 3);
for (let index = 0; index < particleCount; index += 1) {
  particlePositions[index * 3] = (Math.random() - 0.5) * 21;
  particlePositions[index * 3 + 1] = -0.8 + Math.random() * 8;
  particlePositions[index * 3 + 2] = 4 - Math.random() * 30;
}
const particleGeometry = new THREE.BufferGeometry();
particleGeometry.setAttribute('position', new THREE.BufferAttribute(particlePositions, 3));
const particles = new THREE.Points(
  particleGeometry,
  new THREE.PointsMaterial({ color: 0xa5f3fc, size: 0.026, transparent: true, opacity: 0.52, blending: THREE.AdditiveBlending, depthWrite: false, sizeAttenuation: true })
);
scene.add(particles);

const history = Array.from({ length: rows }, () => new Float32Array(columns));
const currentSpectrum = new Float32Array(columns);
const target = { rms: 0, peak: 0, bass: 0, mid: 0, high: 0 };
const level = { rms: 0, peak: 0, bass: 0, mid: 0, high: 0 };
let historyAccumulator = 0;
let beatPulse = 0;
let transientPulse = 0;
let shockProgress = 1;
let socket;
let reconnectTimer;

function scaleAudio(value, gain = 4) {
  return Math.min(1, Math.max(0, Number(value) * gain));
}

function smooth(current, next, attack = 0.5, release = 0.11) {
  return current + (next - current) * (next > current ? attack : release);
}

function setStatus(connected) {
  status.textContent = connected ? 'Connected' : 'Disconnected';
  status.className = `status ${connected ? 'connected' : 'disconnected'}`;
}

function updateAudio(data) {
  if (data.version !== 1 || !data.bands) return;
  target.rms = scaleAudio(data.rms, 3.4);
  target.peak = scaleAudio(data.peak, 2.3);
  target.bass = scaleAudio(data.bands.bass, 4.6);
  target.mid = scaleAudio(data.bands.mid, 6.2);
  target.high = scaleAudio(data.bands.high, 10.5);
  if (Array.isArray(data.fft32)) {
    data.fft32.slice(0, columns).forEach((value, index) => {
      currentSpectrum[index] = scaleSpectrum(value, 15);
    });
  }
  if (data.beat?.detected) {
    beatPulse = Math.max(beatPulse, 0.5 + Number(data.beat.strength) * 0.65);
    shockProgress = 0;
  }
  if (data.transient?.detected) transientPulse = Math.max(transientPulse, 0.35 + Number(data.transient.strength) * 0.65);
  if (data.tempo) {
    const bpm = Number(data.tempo.bpm);
    bpmValue.textContent = Number.isFinite(bpm) && bpm > 0 ? bpm.toFixed(1) : '—';
    tempo.classList.toggle('locked', Boolean(data.tempo.locked));
  }
}

function connect() {
  clearTimeout(reconnectTimer);
  socket = new WebSocket(`ws://127.0.0.1:${configuredPort}`);
  socket.addEventListener('open', () => setStatus(true));
  socket.addEventListener('message', event => {
    try { updateAudio(JSON.parse(event.data)); } catch { /* Ignore malformed or future messages. */ }
  });
  socket.addEventListener('close', () => {
    setStatus(false);
    reconnectTimer = setTimeout(connect, 1500);
  });
  socket.addEventListener('error', () => socket.close());
}

function resize() {
  const widthPx = Math.max(1, window.innerWidth);
  const heightPx = Math.max(1, window.innerHeight);
  renderer.setSize(widthPx, heightPx, false);
  camera.aspect = widthPx / heightPx;
  camera.updateProjectionMatrix();
}

function pushSpectrumHistory() {
  const oldest = history.pop();
  oldest.set(currentSpectrum);
  history.unshift(oldest);
}

const clock = new THREE.Clock();
function animate() {
  const delta = Math.min(clock.getDelta(), 0.05);
  const elapsed = clock.elapsedTime;
  for (const name of Object.keys(level)) level[name] = smooth(level[name], target[name]);

  beatPulse *= Math.pow(0.045, delta);
  transientPulse *= Math.pow(0.018, delta);
  historyAccumulator += delta;
  const historyStep = 0.045;
  while (historyAccumulator >= historyStep) {
    pushSpectrumHistory();
    historyAccumulator -= historyStep;
  }

  const positions = geometry.attributes.position.array;
  for (let row = 0; row < rows; row += 1) {
    const depthFade = 1 - row / rows;
    for (let column = 0; column < columns; column += 1) {
      const vertex = row * columns + column;
      const base = vertex * 3;
      const band = history[row][column];
      const ambientWave = Math.sin(elapsed * 1.1 + column * 0.34 + row * 0.22) * (0.035 + level.mid * 0.055);
      const bassLift = level.bass * 0.22 * Math.sin(column * 0.22 + elapsed * 0.8) * depthFade;
      positions[base] = basePositions[base];
      positions[base + 1] = basePositions[base + 1] + band * (2.4 + depthFade * 1.8) + ambientWave + bassLift;
      positions[base + 2] = basePositions[base + 2];
    }
  }
  geometry.attributes.position.needsUpdate = true;

  landscapeMaterial.opacity = 0.42 + level.rms * 0.24 + transientPulse * 0.2;
  landscapeMaterial.color.setHSL(0.51 + level.mid * 0.18, 0.92, 0.58 + level.high * 0.12);
  glowMaterial.opacity = 0.045 + level.bass * 0.11 + beatPulse * 0.12;
  horizon.material.opacity = 0.35 + level.high * 0.4 + beatPulse * 0.25;

  shockProgress = Math.min(1, shockProgress + delta * 1.25);
  shockwave.position.z = 2 - shockProgress * depth;
  shockwave.position.y = -1.08 + Math.sin(shockProgress * Math.PI) * 0.28;
  shockwave.material.opacity = (1 - shockProgress) * Math.min(0.85, 0.35 + beatPulse);

  particles.rotation.y = Math.sin(elapsed * 0.09) * 0.035;
  particles.position.z = (particles.position.z + delta * (0.18 + level.rms * 0.25)) % 2;
  particles.material.size = 0.018 + level.high * 0.05 + transientPulse * 0.025;
  particles.material.opacity = 0.28 + level.high * 0.42;

  camera.position.x = Math.sin(elapsed * 0.16) * 0.32;
  camera.position.y = 6.8 + Math.cos(elapsed * 0.2) * 0.18 + level.bass * 0.2;
  camera.lookAt(0, -0.1 + level.rms * 0.16, -7.5);

  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}

window.addEventListener('resize', resize);
resize();
connect();
animate();
