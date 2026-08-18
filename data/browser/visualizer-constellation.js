// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

const art = ARTVisualizer.create({ fov: 48, cameraPosition: [0, 0, 15] });
const { renderer, scene, camera, level, spectrum } = art;
const nodeCount = 128;
const base = [];
const positions = new Float32Array(nodeCount * 3);
const colors = new Float32Array(nodeCount * 3);
const cyan = new THREE.Color(0x43f4ff);
const pink = new THREE.Color(0xff45c8);

for (let index = 0; index < nodeCount; index += 1) {
  const radius = 2.2 + Math.random() * 4.4;
  const angle = Math.random() * Math.PI * 2;
  const z = (Math.random() - 0.5) * 5.2;
  base.push({ radius, angle, z, phase: Math.random() * Math.PI * 2, band: index % 32 });
  const color = cyan.clone().lerp(pink, index / nodeCount);
  colors.set([color.r, color.g, color.b], index * 3);
}
const nodeGeometry = new THREE.BufferGeometry();
nodeGeometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
nodeGeometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
const nodes = new THREE.Points(nodeGeometry, new THREE.PointsMaterial({ size: 0.13, vertexColors: true, transparent: true, opacity: 0.95, blending: THREE.AdditiveBlending, depthWrite: false }));
scene.add(nodes);

const edges = [];
for (let index = 0; index < nodeCount; index += 1) {
  edges.push([index, (index + 1) % nodeCount]);
  if (index % 2 === 0) edges.push([index, (index + 11) % nodeCount]);
  if (index % 5 === 0) edges.push([index, (index + 37) % nodeCount]);
}
const linePositions = new Float32Array(edges.length * 6);
const lineGeometry = new THREE.BufferGeometry();
lineGeometry.setAttribute('position', new THREE.BufferAttribute(linePositions, 3));
const lines = new THREE.LineSegments(lineGeometry, new THREE.LineBasicMaterial({ color: 0x8be9ff, transparent: true, opacity: 0.22, blending: THREE.AdditiveBlending, depthWrite: false }));
scene.add(lines);

const pulseGeometry = new THREE.RingGeometry(0.7, 0.73, 96);
const pulses = Array.from({ length: 4 }, (_, index) => {
  const pulse = new THREE.Mesh(pulseGeometry, new THREE.MeshBasicMaterial({ color: index % 2 ? 0xff45c8 : 0x43f4ff, transparent: true, opacity: 0, side: THREE.DoubleSide, blending: THREE.AdditiveBlending, depthWrite: false }));
  pulse.userData.life = 0;
  scene.add(pulse);
  return pulse;
});
let previousBeat = 0;

function animate() {
  const { delta, elapsed } = art.tick();
  if (art.beat > previousBeat + 0.25) {
    const pulse = pulses.find(item => item.userData.life <= 0) || pulses[0];
    pulse.userData.life = 1;
    pulse.rotation.set(Math.random() * 0.5, Math.random() * 0.5, Math.random() * Math.PI);
  }
  previousBeat = art.beat;
  const output = nodeGeometry.attributes.position.array;
  base.forEach((node, index) => {
    const fft = spectrum[node.band];
    const angle = node.angle + elapsed * (0.025 + level.mid * 0.075);
    const radius = node.radius * (1 + level.bass * 0.06 + art.beat * 0.025) + fft * 0.7;
    output[index * 3] = Math.cos(angle) * radius;
    output[index * 3 + 1] = Math.sin(angle) * radius * 0.68 + Math.sin(elapsed * 0.8 + node.phase) * (0.12 + level.high * 0.42);
    output[index * 3 + 2] = node.z + Math.cos(elapsed * 0.55 + node.phase) * (0.18 + level.mid * 0.35);
  });
  nodeGeometry.attributes.position.needsUpdate = true;
  edges.forEach((edge, index) => {
    for (let end = 0; end < 2; end += 1) {
      const source = edge[end] * 3;
      const target = index * 6 + end * 3;
      linePositions[target] = output[source];
      linePositions[target + 1] = output[source + 1];
      linePositions[target + 2] = output[source + 2];
    }
  });
  lineGeometry.attributes.position.needsUpdate = true;
  nodes.material.size = 0.08 + level.high * 0.12 + art.transient * 0.08;
  lines.material.opacity = 0.08 + level.rms * 0.22 + art.beat * 0.16;
  pulses.forEach(pulse => {
    pulse.userData.life = Math.max(0, pulse.userData.life - delta * 1.15);
    const progress = 1 - pulse.userData.life;
    pulse.scale.setScalar(1 + progress * 8);
    pulse.material.opacity = pulse.userData.life * 0.5;
  });
  const rotation = elapsed * 0.018;
  nodes.rotation.y = rotation;
  lines.rotation.y = rotation;
  camera.position.x = Math.sin(elapsed * 0.12) * 1.1;
  camera.position.y = Math.cos(elapsed * 0.1) * 0.7;
  camera.lookAt(0, 0, 0);
  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}

animate();
