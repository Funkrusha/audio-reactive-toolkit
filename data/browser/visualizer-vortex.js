// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

const art = ARTVisualizer.create({ fov: 52, cameraPosition: [0, 0, 15] });
const { renderer, scene, camera, level, spectrum } = art;
const particleCount = 2400;
const arms = 5;
const positions = new Float32Array(particleCount * 3);
const colors = new Float32Array(particleCount * 3);
const seeds = [];
const cyan = new THREE.Color(0x16e6ff);
const magenta = new THREE.Color(0xff2bd6);

for (let index = 0; index < particleCount; index += 1) {
  const radius = 0.5 + Math.pow(Math.random(), 0.72) * 6.3;
  const arm = index % arms;
  const phase = (arm / arms) * Math.PI * 2 + radius * 1.18 + (Math.random() - 0.5) * 0.48;
  const height = (Math.random() - 0.5) * (0.25 + radius * 0.12);
  seeds.push({ radius, phase, height, speed: 0.75 + Math.random() * 0.5, band: index % 32 });
  const color = cyan.clone().lerp(magenta, radius / 6.8);
  colors.set([color.r, color.g, color.b], index * 3);
}

const geometry = new THREE.BufferGeometry();
geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
const particles = new THREE.Points(geometry, new THREE.PointsMaterial({
  size: 0.065, vertexColors: true, transparent: true, opacity: 0.9,
  blending: THREE.AdditiveBlending, depthWrite: false, sizeAttenuation: true
}));
scene.add(particles);

const core = new THREE.Mesh(
  new THREE.SphereGeometry(0.55, 48, 32),
  new THREE.MeshBasicMaterial({ color: 0xf8fafc, transparent: true, opacity: 0.82, blending: THREE.AdditiveBlending })
);
scene.add(core);

const rings = [1.2, 2.1, 3.2].map((radius, index) => {
  const ring = new THREE.Mesh(
    new THREE.TorusGeometry(radius, 0.018, 6, 160),
    new THREE.MeshBasicMaterial({ color: index % 2 ? 0xff2bd6 : 0x16e6ff, transparent: true, opacity: 0.35, blending: THREE.AdditiveBlending, depthWrite: false })
  );
  ring.rotation.x = Math.PI / 2;
  scene.add(ring);
  return ring;
});

const shockwaves = Array.from({ length: 3 }, (_, index) => {
  const wave = new THREE.Mesh(
    new THREE.RingGeometry(0.95, 1, 96),
    new THREE.MeshBasicMaterial({ color: index === 1 ? 0xff2bd6 : 0x67e8f9, transparent: true, opacity: 0, side: THREE.DoubleSide, blending: THREE.AdditiveBlending, depthWrite: false })
  );
  wave.userData.life = 0;
  scene.add(wave);
  return wave;
});
let previousBeat = 0;

function animate() {
  const { delta, elapsed } = art.tick();
  if (art.beat > previousBeat + 0.25) {
    const wave = shockwaves.find(item => item.userData.life <= 0) || shockwaves[0];
    wave.userData.life = 1;
  }
  previousBeat = art.beat;
  const spin = elapsed * (0.16 + level.mid * 0.34);
  const position = geometry.attributes.position.array;
  seeds.forEach((seed, index) => {
    const fft = spectrum[seed.band];
    const angle = seed.phase + spin * seed.speed + Math.sin(elapsed * 0.7 + seed.radius) * level.mid * 0.16;
    const radius = seed.radius * (1 + level.bass * 0.08 + art.beat * 0.035) + fft * 0.42;
    position[index * 3] = Math.cos(angle) * radius;
    position[index * 3 + 1] = seed.height + Math.sin(angle * 2 + elapsed) * (0.05 + level.high * 0.24);
    position[index * 3 + 2] = Math.sin(angle) * radius * 0.46;
  });
  geometry.attributes.position.needsUpdate = true;
  particles.material.size = 0.045 + level.high * 0.09 + art.transient * 0.045;
  particles.rotation.z = Math.sin(elapsed * 0.13) * 0.08;
  core.scale.setScalar(1 + level.bass * 0.65 + art.beat * 0.5);
  core.material.opacity = 0.45 + level.peak * 0.45;
  rings.forEach((ring, index) => {
    ring.rotation.z = elapsed * (index % 2 ? -0.12 : 0.09) + index;
    ring.scale.setScalar(1 + level.rms * 0.08 + spectrum[index * 7] * 0.12);
    ring.material.opacity = 0.16 + level.mid * 0.34;
  });
  shockwaves.forEach(wave => {
    wave.userData.life = Math.max(0, wave.userData.life - delta * 1.3);
    const progress = 1 - wave.userData.life;
    wave.scale.setScalar(1 + progress * 7);
    wave.material.opacity = wave.userData.life * 0.55;
  });
  camera.position.x = Math.sin(elapsed * 0.1) * 0.8;
  camera.position.y = 4.4 + Math.cos(elapsed * 0.12) * 0.5;
  camera.position.z = 13.5;
  camera.lookAt(0, 0, 0);
  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}

animate();
