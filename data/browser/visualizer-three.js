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
const camera = new THREE.PerspectiveCamera(48, 1, 0.1, 100);
camera.position.set(0, 0.2, 17);

const group = new THREE.Group();
scene.add(group);

const orbUniforms = {
  time: { value: 0 },
  bass: { value: 0 },
  mid: { value: 0 },
  high: { value: 0 },
  beat: { value: 0 }
};

const orb = new THREE.Mesh(
  new THREE.IcosahedronGeometry(2.05, 5),
  new THREE.ShaderMaterial({
    uniforms: orbUniforms,
    transparent: true,
    depthWrite: false,
    blending: THREE.AdditiveBlending,
    wireframe: true,
    vertexShader: `
      uniform float time;
      uniform float bass;
      uniform float mid;
      uniform float high;
      uniform float beat;
      varying float intensity;

      void main() {
        vec3 p = position;
        float waveA = sin(p.x * 3.2 + time * 2.1) * cos(p.y * 3.0 - time * 1.7);
        float waveB = sin((p.z + p.y) * 5.0 + time * 3.4);
        float displacement = waveA * (0.06 + mid * 0.55) + waveB * high * 0.18;
        p += normal * (displacement + bass * 0.4 + beat * 0.24);
        intensity = 0.35 + bass * 1.4 + mid * 0.8 + high * 1.1 + beat;
        gl_Position = projectionMatrix * modelViewMatrix * vec4(p, 1.0);
      }
    `,
    fragmentShader: `
      varying float intensity;
      void main() {
        vec3 cyan = vec3(0.05, 0.82, 1.0);
        vec3 magenta = vec3(1.0, 0.08, 0.62);
        vec3 color = mix(cyan, magenta, clamp(intensity * 0.42, 0.0, 1.0));
        gl_FragColor = vec4(color * intensity, clamp(0.28 + intensity * 0.32, 0.0, 0.95));
      }
    `
  })
);
group.add(orb);

const core = new THREE.Mesh(
  new THREE.IcosahedronGeometry(1.46, 3),
  new THREE.MeshBasicMaterial({ color: 0x35106b, transparent: true, opacity: 0.34, blending: THREE.AdditiveBlending, depthWrite: false })
);
group.add(core);

const barCount = 32;
const barGeometry = new THREE.BoxGeometry(0.12, 1, 0.12);
barGeometry.translate(0, 0.5, 0);
const barMaterial = new THREE.MeshBasicMaterial({ color: 0x35d9ff, transparent: true, opacity: 0.82, blending: THREE.AdditiveBlending, depthWrite: false });
const spectrumRing = new THREE.InstancedMesh(barGeometry, barMaterial, barCount);
spectrumRing.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
group.add(spectrumRing);

const dummy = new THREE.Object3D();
const ringRadius = 3.1;
const spectrum = new Float32Array(barCount);
const targetSpectrum = new Float32Array(barCount);

const particleCount = 900;
const particlePositions = new Float32Array(particleCount * 3);
for (let index = 0; index < particleCount; index += 1) {
  const radius = 4.2 + Math.random() * 5.6;
  const theta = Math.random() * Math.PI * 2;
  const phi = Math.acos(2 * Math.random() - 1);
  particlePositions[index * 3] = radius * Math.sin(phi) * Math.cos(theta);
  particlePositions[index * 3 + 1] = radius * Math.cos(phi);
  particlePositions[index * 3 + 2] = radius * Math.sin(phi) * Math.sin(theta);
}
const particleGeometry = new THREE.BufferGeometry();
particleGeometry.setAttribute('position', new THREE.BufferAttribute(particlePositions, 3));
const particles = new THREE.Points(
  particleGeometry,
  new THREE.PointsMaterial({ color: 0x64e8ff, size: 0.035, transparent: true, opacity: 0.58, blending: THREE.AdditiveBlending, depthWrite: false })
);
scene.add(particles);

const shockwave = new THREE.Mesh(
  new THREE.TorusGeometry(2.35, 0.025, 8, 128),
  new THREE.MeshBasicMaterial({ color: 0xff3ea5, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false })
);
group.add(shockwave);

const target = { rms: 0, peak: 0, bass: 0, mid: 0, high: 0 };
const level = { rms: 0, peak: 0, bass: 0, mid: 0, high: 0 };
let beatPulse = 0;
let transientPulse = 0;
let shockwaveLife = 0;
let socket;
let reconnectTimer;

function scaleAudio(value, gain = 4) {
  return Math.min(1, Math.max(0, Number(value) * gain));
}

function setStatus(connected) {
  status.textContent = connected ? 'Connected' : 'Disconnected';
  status.className = `status ${connected ? 'connected' : 'disconnected'}`;
}

function updateAudio(data) {
  if (data.version !== 1 || !data.bands) return;
  target.rms = scaleAudio(data.rms, 3.5);
  target.peak = scaleAudio(data.peak, 2.4);
  target.bass = scaleAudio(data.bands.bass, 4.5);
  target.mid = scaleAudio(data.bands.mid, 6);
  target.high = scaleAudio(data.bands.high, 10);
  if (Array.isArray(data.fft32)) {
    data.fft32.slice(0, barCount).forEach((value, index) => {
      targetSpectrum[index] = scaleSpectrum(value, 14);
    });
  }
  if (data.beat?.detected) {
    beatPulse = Math.max(beatPulse, 0.45 + Number(data.beat.strength) * 0.75);
    shockwaveLife = 1;
  }
  if (data.transient?.detected)
    transientPulse = Math.max(transientPulse, 0.35 + Number(data.transient.strength) * 0.65);
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
  const width = Math.max(1, window.innerWidth);
  const height = Math.max(1, window.innerHeight);
  renderer.setSize(width, height, false);
  camera.aspect = width / height;
  camera.updateProjectionMatrix();
}

function smooth(current, next, attack = 0.48, release = 0.1) {
  return current + (next - current) * (next > current ? attack : release);
}

const clock = new THREE.Clock();
function animate() {
	const delta = Math.min(clock.getDelta(), 0.05);
	const elapsed = clock.elapsedTime;

  for (const name of Object.keys(level))
    level[name] = smooth(level[name], target[name]);

  beatPulse *= Math.pow(0.05, delta);
  transientPulse *= Math.pow(0.025, delta);
  shockwaveLife = Math.max(0, shockwaveLife - delta * 1.8);

  orbUniforms.time.value = elapsed;
  orbUniforms.bass.value = level.bass;
  orbUniforms.mid.value = level.mid;
  orbUniforms.high.value = level.high;
  orbUniforms.beat.value = beatPulse;

  const orbScale = 1 + level.rms * 0.12 + beatPulse * 0.09;
  orb.scale.setScalar(orbScale);
  orb.rotation.y = elapsed * (0.08 + level.mid * 0.16);
  orb.rotation.x = Math.sin(elapsed * 0.3) * 0.12;
  core.scale.setScalar(1 + level.bass * 0.22 + beatPulse * 0.16);
  core.material.opacity = 0.18 + level.bass * 0.28 + transientPulse * 0.14;

  for (let index = 0; index < barCount; index += 1) {
    // FFT values are already asymmetrically smoothed by the native analyzer.
    // Applying another interpolator here makes the ring visibly trail the music.
    spectrum[index] = targetSpectrum[index];
    const angle = (index / barCount) * Math.PI * 2;
    const height = 0.08 + spectrum[index] * 1.55;
    dummy.position.set(Math.sin(angle) * ringRadius, Math.cos(angle) * ringRadius, 0);
    dummy.rotation.set(0, 0, -angle);
    dummy.scale.set(1, height, 1);
    dummy.updateMatrix();
    spectrumRing.setMatrixAt(index, dummy.matrix);
  }
  spectrumRing.instanceMatrix.needsUpdate = true;
  spectrumRing.rotation.z = elapsed * 0.035;
  barMaterial.opacity = 0.46 + level.high * 0.45 + transientPulse * 0.18;

  particles.rotation.y = elapsed * (0.012 + level.mid * 0.04);
  particles.rotation.x = Math.sin(elapsed * 0.11) * 0.08;
  particles.material.size = 0.025 + level.high * 0.055 + transientPulse * 0.025;
  particles.material.opacity = 0.32 + level.high * 0.42;

  const shockProgress = 1 - shockwaveLife;
  shockwave.scale.setScalar(1 + shockProgress * 2.8);
  shockwave.material.opacity = shockwaveLife * 0.75;

  camera.position.x = Math.sin(elapsed * 0.13) * 0.35;
  camera.position.y = 0.2 + Math.cos(elapsed * 0.17) * 0.24;
  camera.lookAt(0, 0, 0);

  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}

window.addEventListener('resize', resize);
resize();
connect();
animate();
