// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

const canvas = document.getElementById('visualizer');
const status = document.getElementById('status');
const tempo = document.getElementById('tempo');
const bpmValue = document.getElementById('bpm-value');

const renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true, powerPreference: 'high-performance' });
renderer.setClearColor(0x000000, 0);
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
renderer.outputColorSpace = THREE.SRGBColorSpace;

const scene = new THREE.Scene();
scene.fog = new THREE.FogExp2(0x090012, 0.047);

const camera = new THREE.PerspectiveCamera(58, 1, 0.1, 80);
camera.position.set(0, 0, 8.5);

const tunnelGroup = new THREE.Group();
scene.add(tunnelGroup);

const ringCount = 26;
const tunnelLength = 38;
const ringSpacing = tunnelLength / ringCount;
const ringGeometry = new THREE.TorusGeometry(1, 0.012, 4, 80);
const ringMaterial = new THREE.MeshBasicMaterial({
  color: 0xff3d81,
  transparent: true,
  opacity: 0.58,
  blending: THREE.AdditiveBlending,
  depthWrite: false
});
const rings = new THREE.InstancedMesh(ringGeometry, ringMaterial, ringCount);
rings.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
tunnelGroup.add(rings);

const radialLinePositions = [];
const radialSpokes = 16;
for (let spoke = 0; spoke < radialSpokes; spoke += 1) {
  const angle = (spoke / radialSpokes) * Math.PI * 2;
  radialLinePositions.push(
    Math.cos(angle) * 3.4, Math.sin(angle) * 3.4, 7,
    Math.cos(angle) * 3.4, Math.sin(angle) * 3.4, -tunnelLength
  );
}
const radialGeometry = new THREE.BufferGeometry();
radialGeometry.setAttribute('position', new THREE.Float32BufferAttribute(radialLinePositions, 3));
const radialLines = new THREE.LineSegments(
  radialGeometry,
  new THREE.LineBasicMaterial({ color: 0x7c3aed, transparent: true, opacity: 0.18, blending: THREE.AdditiveBlending, depthWrite: false })
);
tunnelGroup.add(radialLines);

const barCount = 32;
const barGeometry = new THREE.BoxGeometry(0.09, 1, 0.07);
barGeometry.translate(0, 0.5, 0);
const barMaterial = new THREE.MeshBasicMaterial({ color: 0xffb347, transparent: true, opacity: 0.9, blending: THREE.AdditiveBlending, depthWrite: false });
const spectrumHalo = new THREE.InstancedMesh(barGeometry, barMaterial, barCount);
spectrumHalo.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
scene.add(spectrumHalo);

const particleCount = 750;
const particlePositions = new Float32Array(particleCount * 3);
for (let index = 0; index < particleCount; index += 1) {
  const angle = Math.random() * Math.PI * 2;
  const radius = 1.5 + Math.random() * 5.4;
  particlePositions[index * 3] = Math.cos(angle) * radius;
  particlePositions[index * 3 + 1] = Math.sin(angle) * radius;
  particlePositions[index * 3 + 2] = 7 - Math.random() * (tunnelLength + 10);
}
const particleGeometry = new THREE.BufferGeometry();
particleGeometry.setAttribute('position', new THREE.BufferAttribute(particlePositions, 3));
const particles = new THREE.Points(
  particleGeometry,
  new THREE.PointsMaterial({ color: 0xffc4dd, size: 0.035, transparent: true, opacity: 0.68, blending: THREE.AdditiveBlending, depthWrite: false, sizeAttenuation: true })
);
scene.add(particles);

const flash = new THREE.Mesh(
  new THREE.RingGeometry(0.65, 0.7, 96),
  new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide })
);
flash.position.z = 1.4;
scene.add(flash);

const target = { rms: 0, peak: 0, bass: 0, mid: 0, high: 0 };
const level = { rms: 0, peak: 0, bass: 0, mid: 0, high: 0 };
const spectrum = new Float32Array(barCount);
let beatPulse = 0;
let transientPulse = 0;
let travel = 0;

function scaleAudio(value, gain = 4) {
  return Math.min(1, Math.max(0, Number(value) * gain));
}

function smooth(current, next, attack = 0.5, release = 0.12) {
  return current + (next - current) * (next > current ? attack : release);
}

function setStatus(connected) {
  status.textContent = connected ? `Connected · ${ART.transportLabel}` : 'Disconnected';
  status.className = `status ${connected ? 'connected' : 'disconnected'}`;
}

function updateAudio(data) {
  if (data.version !== 1 || !data.bands) return;
  setStatus(true);
  target.rms = scaleAudio(data.rms, 3.4);
  target.peak = scaleAudio(data.peak, 2.2);
  target.bass = scaleAudio(data.bands.bass, 4.8);
  target.mid = scaleAudio(data.bands.mid, 6.5);
  target.high = scaleAudio(data.bands.high, 11);
  if (Array.isArray(data.fft32)) {
    data.fft32.slice(0, barCount).forEach((value, index) => {
      spectrum[index] = scaleSpectrum(value, 14);
    });
  }
  if (data.beat?.detected) beatPulse = Math.max(beatPulse, 0.5 + Number(data.beat.strength) * 0.7);
  if (data.transient?.detected) transientPulse = Math.max(transientPulse, 0.35 + Number(data.transient.strength) * 0.65);
  if (data.tempo) {
    const bpm = Number(data.tempo.bpm);
    bpmValue.textContent = Number.isFinite(bpm) && bpm > 0 ? bpm.toFixed(1) : '—';
    tempo.classList.toggle('locked', Boolean(data.tempo.locked));
  }
}

function resize() {
  const width = Math.max(1, window.innerWidth);
  const height = Math.max(1, window.innerHeight);
  renderer.setSize(width, height, false);
  camera.aspect = width / height;
  camera.updateProjectionMatrix();
}

const ringDummy = new THREE.Object3D();
const barDummy = new THREE.Object3D();
const clock = new THREE.Clock();

function animate() {
  const delta = Math.min(clock.getDelta(), 0.05);
  const elapsed = clock.elapsedTime;
  for (const name of Object.keys(level)) level[name] = smooth(level[name], target[name]);

  beatPulse *= Math.pow(0.045, delta);
  transientPulse *= Math.pow(0.02, delta);
  travel = (travel + delta * (1.7 + level.rms * 2.2 + beatPulse * 1.4)) % ringSpacing;

  const tunnelRadius = 2.8 + level.bass * 0.42 + beatPulse * 0.14;
  for (let index = 0; index < ringCount; index += 1) {
    const z = 6 - index * ringSpacing + travel;
    const depthFade = Math.max(0.18, 1 - Math.abs(z) / (tunnelLength + 4));
    const wobble = Math.sin(elapsed * 0.7 + index * 0.42) * (0.08 + level.mid * 0.16);
    ringDummy.position.set(Math.sin(index * 0.31 + elapsed * 0.18) * wobble, Math.cos(index * 0.27 + elapsed * 0.16) * wobble, z);
    ringDummy.rotation.set(0, 0, index * 0.075 + elapsed * (0.08 + level.mid * 0.18));
    ringDummy.scale.setScalar(tunnelRadius * (0.96 + depthFade * 0.04));
    ringDummy.updateMatrix();
    rings.setMatrixAt(index, ringDummy.matrix);
  }
  rings.instanceMatrix.needsUpdate = true;
  ringMaterial.opacity = 0.34 + level.rms * 0.28 + transientPulse * 0.25;
  ringMaterial.color.setHSL(0.94 - level.mid * 0.12, 0.95, 0.58 + level.high * 0.12);

  const haloRadius = 3.35 + level.bass * 0.22;
  for (let index = 0; index < barCount; index += 1) {
    const angle = (index / barCount) * Math.PI * 2 + elapsed * 0.055;
    const height = 0.09 + spectrum[index] * 1.35;
    barDummy.position.set(Math.cos(angle) * haloRadius, Math.sin(angle) * haloRadius, 0.7);
    barDummy.rotation.set(0, 0, angle - Math.PI / 2);
    barDummy.scale.set(1, height, 1);
    barDummy.updateMatrix();
    spectrumHalo.setMatrixAt(index, barDummy.matrix);
  }
  spectrumHalo.instanceMatrix.needsUpdate = true;
  barMaterial.opacity = 0.52 + level.high * 0.38 + transientPulse * 0.2;

  particles.position.z = travel;
  particles.rotation.z = elapsed * (0.018 + level.mid * 0.045);
  particles.material.size = 0.025 + level.high * 0.055 + transientPulse * 0.025;
  particles.material.opacity = 0.38 + level.high * 0.4;

  flash.scale.setScalar(1 + (1 - beatPulse) * 5.5);
  flash.material.opacity = Math.min(0.72, beatPulse * 0.65);
  camera.position.x = Math.sin(elapsed * 0.22) * (0.12 + level.mid * 0.16);
  camera.position.y = Math.cos(elapsed * 0.19) * (0.1 + level.high * 0.12);
  camera.lookAt(0, 0, -7);

  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}

window.addEventListener('resize', resize);
resize();
ART.on('frame', updateAudio);
ART.connect();
animate();
